#include "chrononmotion/motion/MotionScene.hpp"

#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/SceneExport.hpp"

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

    namespace {

        /// The value type a channel resolves to. A link only makes sense between
        /// channels of the same type — following position with an opacity is an
        /// authoring error, not a cast.
        enum class LinkType { Vector3Type, QuaternionType, ScalarType };

        [[nodiscard]] LinkType linkTypeOf(TrackChannel channel) {

            switch (channel) {
                case TrackChannel::Position:
                case TrackChannel::Anchor:
                case TrackChannel::Scale:
                case TrackChannel::Rotation: return LinkType::Vector3Type;
                case TrackChannel::Orientation: return LinkType::QuaternionType;
                case TrackChannel::Opacity:
                case TrackChannel::TimeRemap: return LinkType::ScalarType;
            }
            return LinkType::ScalarType;
        }

    }// namespace

    LinkValue MotionScene::resolveChannel(Layer::Id layerId, TrackChannel channel, float time,
                                          std::vector<std::pair<Layer::Id, TrackChannel>>& visiting,
                                          bool rate) const {

        const std::pair<Layer::Id, TrackChannel> node{layerId, channel};
        if (std::find(visiting.begin(), visiting.end(), node) != visiting.end()) {
            throw std::invalid_argument("MotionScene: link cycle through layer " + std::to_string(layerId));
        }

        const Layer* layer = findLayer(layerId);
        if (!layer) {
            throw std::invalid_argument("MotionScene: link to unknown layer " + std::to_string(layerId));
        }

        // Push before the nested lookup and pop after: the guard is stack-scoped,
        // so the same channel may legitimately be resolved twice in sequence
        // (once for the link's origin) while a true cycle cannot recurse.
        visiting.push_back(node);
        const LinkResolver nested = linkResolver(visiting, rate);

        LinkValue out;
        switch (channel) {
            case TrackChannel::Position:
            case TrackChannel::Anchor:
            case TrackChannel::Scale:
            case TrackChannel::Rotation: {
                const Track<Vector3>& track =
                        channel == TrackChannel::Position ? layer->tracks.position
                        : channel == TrackChannel::Anchor ? layer->tracks.anchor
                        : channel == TrackChannel::Scale ? layer->tracks.scale
                                                          : layer->tracks.rotation;
                Vector3 value;
                if (rate) {
                    value = track.velocity(time, nested);
                } else {
                    track.sample(time, value, nested);
                }
                out = value;
                break;
            }
            case TrackChannel::Orientation: {
                Quaternion value;
                if (rate) {
                    value = layer->tracks.orientation.velocity(time, nested);
                } else {
                    layer->tracks.orientation.sample(time, value, nested);
                }
                out = value;
                break;
            }
            case TrackChannel::Opacity: {
                float value = 0.f;
                if (rate) {
                    value = layer->tracks.opacity.velocity(time, nested);
                } else {
                    layer->tracks.opacity.sample(time, value, nested);
                }
                out = value;
                break;
            }
            case TrackChannel::TimeRemap: {
                float value = 0.f;
                if (rate) {
                    value = layer->timeRemapTrack.velocity(time, nested);
                } else {
                    layer->timeRemapTrack.sample(time, value, nested);
                }
                out = value;
                break;
            }
        }

        visiting.pop_back();
        return out;
    }

    LinkResolver MotionScene::linkResolver(std::vector<std::pair<Layer::Id, TrackChannel>>& visiting,
                                           bool rate) const {

        return [this, &visiting, rate](std::uint32_t layerId, TrackChannel channel, float time) -> LinkValue {
            return resolveChannel(layerId, channel, time, visiting, rate);
        };
    }

    MotionScene::MotionScene(std::string name, float fps)
        : name_(std::move(name)), fps_(fps) {

        if (fps <= 0.f) throw std::invalid_argument("MotionScene: fps must be positive");
    }

    Layer& MotionScene::addLayer(Layer layer) {

        const Layer::Id id = layer.id;
        const auto existing = index_.find(id);

        if (existing != index_.end()) {
            layers_[existing->second].replaceAuthoring(std::move(layer));
            states_[existing->second] = LayerState{};
            orderDirty_ = true;
            return layers_[existing->second];
        }

        index_.emplace(id, layers_.size());
        layers_.emplace_back();
        layers_.back().replaceAuthoring(std::move(layer));
        states_.emplace_back();
        orderDirty_ = true;
        return layers_.back();
    }

    Layer& MotionScene::addLayer(Layer::Id id, std::string name) {

        return addLayer(Layer(id, std::move(name)));
    }

    Layer& MotionScene::addNull(Layer::Id id, std::string name, Layer::Id parentId) {

        Layer layer(id, std::move(name));
        layer.parentId = parentId;
        return addLayer(std::move(layer));
    }

    Layer& MotionScene::addContent(Layer::Id id, std::string name, ContentRef content, Layer::Id parentId) {

        Layer layer(id, std::move(name));
        layer.parentId = parentId;
        layer.content = std::move(content);
        return addLayer(std::move(layer));
    }

    Layer& MotionScene::addMesh(Layer::Id id, std::string name, MeshAttachment mesh, Layer::Id parentId) {

        Layer layer(id, std::move(name));
        layer.parentId = parentId;
        layer.mesh = std::move(mesh);
        return addLayer(std::move(layer));
    }

    Light& MotionScene::addLight(std::unique_ptr<Light> light) {
        if (!light) throw std::invalid_argument("MotionScene::addLight: light is required");
        if (lightIndex_.contains(light->id)) {
            throw std::invalid_argument("MotionScene::addLight: duplicate light id");
        }
        const Light::Id id = light->id;
        lightIndex_.emplace(id, lights_.size());
        lights_.push_back(std::move(light));
        lightStates_.emplace_back();
        return *lights_.back();
    }

    AmbientLight& MotionScene::addAmbientLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<AmbientLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<AmbientLight&>(addLight(std::move(light)));
    }

    DirectionalLight& MotionScene::addDirectionalLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<DirectionalLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<DirectionalLight&>(addLight(std::move(light)));
    }

    PointLight& MotionScene::addPointLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<PointLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<PointLight&>(addLight(std::move(light)));
    }

    SpotLight& MotionScene::addSpotLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<SpotLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<SpotLight&>(addLight(std::move(light)));
    }

    std::size_t MotionScene::indexOf(Layer::Id id) const {

        const auto it = index_.find(id);
        return it == index_.end() ? npos : it->second;
    }

    std::size_t MotionScene::lightIndexOf(Light::Id id) const {

        const auto it = lightIndex_.find(id);
        return it == lightIndex_.end() ? npos : it->second;
    }

    Layer* MotionScene::findLayer(Layer::Id id) {

        const std::size_t index = indexOf(id);
        return index == npos ? nullptr : &layers_[index];
    }

    const Layer* MotionScene::findLayer(Layer::Id id) const {

        const std::size_t index = indexOf(id);
        return index == npos ? nullptr : &layers_[index];
    }

    Layer* MotionScene::findLayer(const std::string& name) {

        for (auto& layer : layers_) {
            if (layer.name == name) return &layer;
        }
        return nullptr;
    }

    const Layer* MotionScene::findLayer(const std::string& name) const {

        for (const auto& layer : layers_) {
            if (layer.name == name) return &layer;
        }
        return nullptr;
    }

    bool MotionScene::hasLayer(Layer::Id id) const {

        return index_.find(id) != index_.end();
    }

    MotionScene& MotionScene::setFps(float fps) {

        if (fps <= 0.f) throw std::invalid_argument("MotionScene::setFps: fps must be positive");
        fps_ = fps;
        return *this;
    }

    bool MotionScene::orderStale() const {

        if (orderDirty_ || order_.size() != layers_.size()) {
            return true;
        }

        // parentId is a public field, so a re-parent cannot be intercepted: the
        // cache instead remembers the parent *id* each slot was built from, and
        // an integer compare per layer exposes any direct write.
        for (std::size_t position = 0; position < order_.size(); ++position) {
            if (layers_[order_[position]].parentId != orderParentIds_[position]) {
                return true;
            }
        }
        return false;
    }

    void MotionScene::syncGraph() {

        // The Object3D graph is the one hierarchy. `parentId` is its id-addressed
        // authoring form, so every rebuild reconciles the two: a layer becomes a
        // child of its authored parent's node, and is detached when it has none.
        // A link the object graph cannot express (unknown parent, self-parenting,
        // a cycle) is left unmaterialized — `validate()` reports why, and the
        // evaluation order still derives from the authored ids, so a broken
        // authoring link is an error rather than a silently different graph.
        for (auto& layer : layers_) {
            Layer* parent = (layer.parentId == Layer::kRoot) ? nullptr : findLayer(layer.parentId);
            if (parent == &layer) parent = nullptr;

            if (parent == nullptr) {
                if (layer.parent) layer.removeFromParent();
                continue;
            }

            if (layer.parent == parent) continue;

            bool cyclic = false;
            for (Object3D* node = parent; node; node = node->parent) {
                if (node == &layer) {
                    cyclic = true;
                    break;
                }
            }
            if (cyclic) continue;

            if (layer.parent) layer.removeFromParent();
            parent->addRef(layer);
        }
    }

    void MotionScene::rebuildOrder() {

        syncGraph();

        const std::size_t count = layers_.size();
        order_.clear();
        orderParents_.clear();
        orderParentIds_.clear();
        orderValid_ = true;
        order_.reserve(count);
        orderParents_.reserve(count);
        orderParentIds_.reserve(count);

        // 0 = unvisited, 1 = on the current path, 2 = emitted.
        std::vector<char> color(count, 0);

        // Iterative parents-first DFS over indices: emit a layer only after its
        // parent chain, so a cycle is broken by the visiting mark instead of
        // recursing forever. Resolved parent indices are cached alongside, so
        // the evaluate loop does no id→index lookups at all.
        struct Frame {
            std::size_t index;
            bool parentVisited;
        };

        std::vector<Frame> stack;
        for (std::size_t root = 0; root < count; ++root) {
            if (color[root] != 0) continue;
            color[root] = 1; // on the current path
            stack.push_back({root, false});
            while (!stack.empty()) {
                Frame& top = stack.back();
                if (!top.parentVisited) {
                    top.parentVisited = true;
                    const std::size_t parent = indexOf(layers_[top.index].parentId);
                    if (parent == npos && layers_[top.index].parentId != Layer::kRoot) {
                        orderValid_ = false;
                    } else if (parent == top.index) {
                        orderValid_ = false;
                    } else if (parent != npos && color[parent] == 0) {
                        color[parent] = 1; // on the current path
                        stack.push_back({parent, false});
                        continue;
                    } else if (parent != npos && color[parent] == 1) {
                        orderValid_ = false;
                    }
                }
                color[top.index] = 2;
                order_.push_back(top.index);
                orderParents_.push_back(npos); // placeholder, filled below
                orderParentIds_.push_back(Layer::kRoot); // placeholder, filled below
                stack.pop_back();
            }
        }

        // Fill the cached parent indices now that every layer has a slot.
        for (std::size_t position = 0; position < order_.size(); ++position) {
            const std::size_t index = order_[position];
            const std::size_t parent = indexOf(layers_[index].parentId);
            orderParents_[position] = (parent != npos && parent != index) ? parent : npos;
            orderParentIds_[position] = layers_[index].parentId;
        }

        orderDirty_ = false;
    }

    std::vector<Layer::Id> MotionScene::evaluationOrder() {

        if (orderStale()) rebuildOrder();

        std::vector<Layer::Id> order;
        order.reserve(order_.size());
        for (const std::size_t index : order_) {
            order.push_back(layers_[index].id);
        }
        return order;
    }

    void MotionScene::evaluate(const MotionTime& time) {

        const float seconds = time.pts();

        if (orderStale()) rebuildOrder();
        if (!orderValid_) {
            throw std::invalid_argument("MotionScene::evaluate: invalid hierarchy; call validate() for details");
        }

        // Reset state before evaluating a validated hierarchy.
        for (auto& state : states_) {
            state.visible = true;
            state.worldOpacity = 1.f;
            state.evaluated = false;
        }

        // One resolver for the whole pass: `visiting` is the stack of channels
        // currently being resolved, so a link that reaches back into one already
        // in progress is caught as a cycle instead of recursing forever.
        std::vector<std::pair<Layer::Id, TrackChannel>> visiting;
        const LinkResolver resolve = linkResolver(visiting, false);

        for (std::size_t position = 0; position < order_.size(); ++position) {
            const std::size_t index = order_[position];

            LayerState& state = states_[index];
            // The sub-frame fraction has to travel through the time remap with the
            // integer frame, or `evaluatePtsExact` would silently snap back to
            // the frame and be identical to `evaluatePts` — which is the whole
            // reason the fraction exists.
            const float exactFrame = static_cast<float>(time.frame) + time.subFrame;
            const float sceneSeconds = exactFrame / time.fps;
            const float layerSeconds =
                    layers_[index].mapFrame(exactFrame, sceneSeconds, resolve) / time.fps;
            layers_[index].sample(layerSeconds, state.local, resolve);

            for (const auto& constraint : layers_[index].constraints) {
                const std::size_t target = indexOf(constraint.targetId);
                Vector3 targetPosition;
                if (constraint.targetId == Layer::kRoot &&
                    constraint.kind == ConstraintKind::FollowPath) {
                    targetPosition.set(0.f, 0.f, 0.f);
                } else {
                    if (target == npos || !states_[target].evaluated) {
                        if (constraint.targetId != Layer::kRoot) {
                            throw std::invalid_argument("MotionScene::evaluate: constraint has an unknown or unsolved target");
                        }
                        continue;
                    }
                    targetPosition.setFromMatrixPosition(states_[target].world);
                }
                (void)applyConstraint(constraint, state.local, targetPosition,
                                layers_[target].transform.orientation,
                                layers_[target].transform.scale, layerSeconds);
            }
            for (const auto& driver : layers_[index].drivers) {
                const std::size_t target = indexOf(driver.targetId);
                Vector3 targetPosition;
                if (target != npos && states_[target].evaluated) {
                    targetPosition.setFromMatrixPosition(states_[target].world);
                }
                const Vector3 offset = evaluateDriverOffset(
                        driver, layerSeconds, state.local.position, targetPosition);
                switch (driver.property) {
                    case DriverProperty::Position: state.local.position.add(offset); break;
                    case DriverProperty::Rotation: state.local.rotation.add(offset); break;
                    case DriverProperty::Scale: state.local.scale.add(offset); break;
                    case DriverProperty::Opacity: state.local.opacity += offset.x; break;
                }
            }
            // The composed local matrix is kept whole: it carries the anchor
            // offset, which has no Object3D field, so it cannot be recomposed
            // from position/quaternion/scale.
            Matrix4 localMatrix;
            state.local.compose(localMatrix);
            state.world.copy(localMatrix);

            Transform previousLocal;
            // The previous sample is exactly one frame before THIS sample, so on a
            // sub-frame instant it is (frame - 1) + subFrame, not frame - 1. Using
            // the bare integer here would make the finite difference span shrink
            // to less than a frame and overstate the velocity by exactly the
            // fraction.
            const float previousExactFrame = static_cast<float>(time.frame - 1) + time.subFrame;
            const float previousSceneSeconds = previousExactFrame / time.fps;
            const float previousSeconds =
                    layers_[index].mapFrame(previousExactFrame, previousSceneSeconds, resolve) / time.fps;
            layers_[index].sample(previousSeconds, previousLocal, resolve);
            previousLocal.compose(state.previousWorld);

            // Lifetime is a whole-frame contract: a layer's in/out point is an
            // authoring decision, not a continuous one, so visibility stays
            // keyed to the integer frame and is unaffected by the fraction.
            const bool alive = layers_[index].visibleAt(time.frame);
            const bool previousAlive = layers_[index].visibleAt(time.frame - 1);
            const std::size_t parent = orderParents_[position];

            if (parent != npos && states_[parent].evaluated) {
                // parents-first order guarantees the parent is solved; compose in
                // place. `multiplyMatrices` reads both operands before writing the
                // target, so folding the parent straight into the child's world
                // matrix needs no temporary. (No translation fast-path here: a
                // parent with scale or rotation must multiply the child's 3x3 too,
                // and the branch would have to check nine elements to be safe.)
                state.world.multiplyMatrices(states_[parent].world, state.world);

                // Lifetime hides the subtree: a hidden null hides its children.
                state.visible = alive && states_[parent].visible;
                state.worldOpacity = states_[parent].worldOpacity * state.local.opacity;
                state.previousWorld.multiplyMatrices(states_[parent].previousWorld, state.previousWorld);
                state.previousVisible = previousAlive && states_[parent].previousVisible;
                state.previousWorldOpacity = states_[parent].previousWorldOpacity * previousLocal.opacity;
            } else {
                state.visible = alive;
                state.worldOpacity = state.local.opacity;
                state.previousVisible = previousAlive;
                state.previousWorldOpacity = previousLocal.opacity;
            }

            // `Layer` is the Object3D graph node, so the evaluated pose is written
            // back onto it: `matrix`/`matrixWorld` are the authority and
            // `matrixAutoUpdate` is off, so no consumer can recompose them from a
            // transform that does not know about the anchor. The PQS fields mirror
            // the layer's local TRS for generic Object3D readers; `rotation` is the
            // composed orientation (`orientation * euler`) as an Euler.
            Layer& node = layers_[index];
            node.position.copy(state.local.position);
            node.scale.copy(state.local.scale);
            node.quaternion.copy(state.local.localQuaternion());
            node.matrix->copy(localMatrix);
            node.matrixWorld->copy(state.world);
            node.matrixAutoUpdate = false;
            node.matrixWorldNeedsUpdate = true;
            node.visible = state.visible;

            state.evaluated = true;
        }

        evaluateLights(time);

        lastFrame_ = time.frame;
        lastPts_ = seconds;
        lastTime_ = time;
        hasEvaluated_ = true;
    }

    void MotionScene::evaluate(int frame) {

        evaluate(MotionTime{frame, fps_});
    }

    void MotionScene::evaluatePts(float seconds) {

        evaluate(MotionTime::atPts(seconds, fps_));
    }

    void MotionScene::evaluatePtsExact(float seconds) {

        evaluate(MotionTime::atPtsExact(seconds, fps_));
    }

    LayerMotion MotionScene::motion(Layer::Id id) const {

        const std::size_t index = indexOf(id);
        if (index == npos) {
            throw std::out_of_range("MotionScene::motion: unknown layer id " + std::to_string(id));
        }

        const LayerState& current = states_[index];
        LayerMotion out{};
        if (!current.evaluated) return out;

        // `previousWorld` is the same layer evaluated exactly one frame back (the
        // evaluate loop composes it parents-first alongside `world`), so the span
        // between the two samples is one frame: 1/fps seconds, NOT the distance
        // the caller happened to jump. Dividing by the true span is what keeps the
        // velocity correct after a seek.
        // One frame is the spacing between `world` and `previousWorld`, whatever
        // the sub-frame fraction is: the previous sample is (frame-1)+subFrame,
        // so the span is still exactly 1 frame.
        const float dt = 1.f / fps_;
        if (!(dt > 0.f)) return out;

        Vector3 here;
        Vector3 before;
        here.setFromMatrixPosition(current.world);
        before.setFromMatrixPosition(current.previousWorld);

        out.positionVelocity = here.sub(before).multiplyScalar(1.f / dt);
        out.linearVelocity = out.positionVelocity;
        out.opacityVelocity = (current.worldOpacity - current.previousWorldOpacity) / dt;
        out.finiteDifference = true;

        // Angular velocity: the shortest-arc rotation that took the layer from its
        // previous world orientation to this one, as an axis/rate. Derived in world
        // space from the two orientation factors, so a parent that spins is
        // reflected in the child's reported rate — which is what a motion-blur
        // consumer wants (it needs the rate the pixels move at, not a local rate).
        Quaternion qNow;
        Quaternion qBefore;
        Vector3 scratchScale;
        current.world.decompose(scratchScale, qNow, scratchScale);
        current.previousWorld.decompose(scratchScale, qBefore, scratchScale);

        Quaternion delta;
        delta.copy(qBefore);
        delta.invert();
        Quaternion step = delta.multiply(qNow);

        // A quaternion double-covers: q and -q are the same rotation. Flipping to
        // the representative with w >= 0 stops a near-identity step from reading
        // as a full 2*pi turn backwards, which is what atan2 would do with a
        // negative w.
        if (step.w < 0.f) {
            step.x = -step.x;
            step.y = -step.y;
            step.z = -step.z;
            step.w = -step.w;
        }

        const float sinHalf = std::sqrt(step.x * step.x + step.y * step.y + step.z * step.z);
        const float angle = 2.f * std::atan2(sinHalf, step.w);
        if (angle > 1e-8f && sinHalf > 1e-8f) {
            Vector3 axis{step.x / sinHalf, step.y / sinHalf, step.z / sinHalf};
            out.angularVelocity = axis.multiplyScalar(angle / dt);
        }

        return out;
    }

    LayerMotion MotionScene::keyedMotion(Layer::Id id, float seconds) const {

        const Layer* layer = findLayer(id);
        if (!layer) {
            throw std::out_of_range("MotionScene::keyedMotion: unknown layer id " + std::to_string(id));
        }

        std::vector<std::pair<Layer::Id, TrackChannel>> visiting;
        return layer->keyedMotion(seconds, linkResolver(visiting, true));
    }

    const LayerState& MotionScene::state(Layer::Id id) const {

        const std::size_t index = indexOf(id);
        if (index == npos) {
            throw std::out_of_range("MotionScene::state: unknown layer id " + std::to_string(id));
        }
        return states_[index];
    }

    const LightState& MotionScene::lightState(Light::Id id) const {
        const std::size_t index = lightIndexOf(id);
        if (index == npos) {
            throw std::out_of_range("MotionScene::lightState: unknown light id " + std::to_string(id));
        }
        return lightStates_[index];
    }

    void MotionScene::evaluateLights(const MotionTime& time) {
        for (std::size_t index = 0; index < lights_.size(); ++index) {
            const Light& light = *lights_[index];
            LightState& state = lightStates_[index];
            state.sample = light.sample(time.pts());
            state.world = state.sample.local.toMatrix();
            state.position.setFromMatrixPosition(state.world);
            state.visible = true;
            state.evaluated = false;

            if (state.sample.parentId != Layer::kRoot) {
                const std::size_t parent = indexOf(state.sample.parentId);
                if (parent == npos || !states_[parent].evaluated) {
                    throw std::invalid_argument("MotionScene::evaluate: light has an unknown or unsolved parent");
                }
                state.world.multiplyMatrices(states_[parent].world, state.world);
                state.visible = states_[parent].visible;
            }

            state.position.setFromMatrixPosition(state.world);
            state.direction.set(0.f, 0.f, -1.f).transformDirection(state.world);

            if (state.sample.targetId != Layer::kRoot) {
                const Layer* target = findLayer(state.sample.targetId);
                if (!target) {
                    throw std::invalid_argument("MotionScene::evaluate: light has an unknown target");
                }
                Vector3 targetPosition;
                targetPosition.setFromMatrixPosition(states_[indexOf(target->id)].world);
                state.direction.subVectors(targetPosition, state.position).normalize();
            }
            state.evaluated = true;
        }
    }

    std::vector<std::string> MotionScene::validate() const {

        std::vector<std::string> problems;

        // The link graph over (layer, channel) nodes. A link is an edge; an edge
        // that leads back into one already on the resolution path is a cycle and
        // cannot be sampled, so it is an authoring problem here rather than a
        // stack overflow at frame time.
        struct LinkNode {
            Layer::Id layer;
            TrackChannel channel;
        };
        std::vector<LinkNode> linkNodes;
        std::vector<std::pair<std::size_t, std::size_t>> linkEdges;

        for (const auto& layer : layers_) {
            if (!layer.precomp.empty() && !layer.precomp.valid()) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): invalid nested composition");
            }
            if (!layer.timeRemap.valid()) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): invalid time remap");
            }
            const auto reportTrack = [&](const char* trackName, const std::string& problem) {
                if (!problem.empty()) {
                    problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                       "'): " + trackName + ": " + problem);
                }
            };
            reportTrack("position track", layer.tracks.position.modifierProblem());
            reportTrack("anchor track", layer.tracks.anchor.modifierProblem());
            reportTrack("scale track", layer.tracks.scale.modifierProblem());
            reportTrack("rotation track", layer.tracks.rotation.modifierProblem());
            reportTrack("orientation track", layer.tracks.orientation.modifierProblem());
            reportTrack("opacity track", layer.tracks.opacity.modifierProblem());
            reportTrack("time remap track", layer.timeRemapTrack.modifierProblem());

            const auto checkLinks = [&](const char* trackName, TrackChannel requesting, const auto& track) {
                for (const auto& modifier : track.modifiers()) {
                    const auto* link = std::get_if<LinkModifier>(&modifier);
                    if (!link) continue;

                    const std::string where = "layer " + std::to_string(layer.id) + " ('" + layer.name +
                                              "'): " + trackName;
                    if (!hasLayer(link->layerId)) {
                        problems.push_back(where + " links to unknown layer " +
                                           std::to_string(link->layerId));
                        continue;
                    }
                    if (linkTypeOf(requesting) != linkTypeOf(link->channel)) {
                        problems.push_back(where + " links to a channel of a different type");
                        continue;
                    }

                    const auto indexOfNode = [&](Layer::Id id, TrackChannel channel) {
                        for (std::size_t i = 0; i < linkNodes.size(); ++i) {
                            if (linkNodes[i].layer == id && linkNodes[i].channel == channel) return i;
                        }
                        linkNodes.push_back(LinkNode{id, channel});
                        return linkNodes.size() - 1;
                    };
                    linkEdges.push_back({indexOfNode(layer.id, requesting),
                                         indexOfNode(link->layerId, link->channel)});
                }
            };

            checkLinks("position track", TrackChannel::Position, layer.tracks.position);
            checkLinks("anchor track", TrackChannel::Anchor, layer.tracks.anchor);
            checkLinks("scale track", TrackChannel::Scale, layer.tracks.scale);
            checkLinks("rotation track", TrackChannel::Rotation, layer.tracks.rotation);
            checkLinks("orientation track", TrackChannel::Orientation, layer.tracks.orientation);
            checkLinks("opacity track", TrackChannel::Opacity, layer.tracks.opacity);
            checkLinks("time remap track", TrackChannel::TimeRemap, layer.timeRemapTrack);

            if (layer.parentId == Layer::kRoot) continue;

            if (!hasLayer(layer.parentId)) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): unknown parent " + std::to_string(layer.parentId));
            } else if (layer.parentId == layer.id) {
                problems.push_back("layer " + std::to_string(layer.id) + " ('" + layer.name +
                                   "'): parented to itself");
            }
        }

        for (const auto& light : lights_) {
            if (light->parentId != Layer::kRoot && !hasLayer(light->parentId)) {
                problems.push_back("light " + std::to_string(light->id) + " ('" + light->name +
                                   "'): unknown parent " + std::to_string(light->parentId));
            }
            if ((light->kind() == LightKind::Directional || light->kind() == LightKind::Spot) &&
                light->sample(0.f).targetId != Layer::kRoot &&
                !hasLayer(light->sample(0.f).targetId)) {
                problems.push_back("light " + std::to_string(light->id) + " ('" + light->name +
                                   "'): unknown target");
            }
        }

        if (!linkEdges.empty()) {
            std::vector<char> linkColour(linkNodes.size(), 0);
            std::function<bool(std::size_t)> visitLink = [&](std::size_t index) -> bool {
                linkColour[index] = 1;
                for (const auto& edge : linkEdges) {
                    if (edge.first != index) continue;
                    if (linkColour[edge.second] == 1) return true;
                    if (linkColour[edge.second] == 0 && visitLink(edge.second)) return true;
                }
                linkColour[index] = 2;
                return false;
            };

            bool linkCycle = false;
            for (std::size_t i = 0; i < linkNodes.size() && !linkCycle; ++i) {
                if (linkColour[i] == 0) linkCycle = visitLink(i);
            }
            if (linkCycle) {
                problems.emplace_back("hierarchy contains a link cycle");
            }
        }

        const std::size_t count = layers_.size();
        std::vector<char> color(count, 0);
        bool cyclic = false;

        std::function<void(std::size_t)> visit = [&](std::size_t index) {
            color[index] = 1;

            const std::size_t parent = indexOf(layers_[index].parentId);
            if (parent != npos && parent != index) {
                if (color[parent] == 1) {
                    cyclic = true;
                } else if (color[parent] == 0) {
                    visit(parent);
                }
            } else if (parent == index) {
                cyclic = true;
            }

            color[index] = 2;
        };

        for (std::size_t index = 0; index < count; ++index) {
            if (color[index] == 0) visit(index);
        }

        if (cyclic) {
            problems.emplace_back("hierarchy contains a parent cycle");
        }

        return problems;
    }

    CompiledScene MotionScene::compile() const {
        CompiledScene result;
        result.name = name_;
        result.frame = lastFrame_;
        result.fps = fps_;
        result.transforms.reserve(layers_.size());
        result.precomps.reserve(layers_.size());
        for (std::size_t index = 0; index < layers_.size(); ++index) {
            if (!states_[index].evaluated) {
                throw std::logic_error("MotionScene::compile: evaluate the scene before compiling");
            }
            if (layers_[index].precomp.valid()) {
                const int nestedFrame = static_cast<int>(std::lround(
                        layers_[index].mapFrame(static_cast<float>(lastFrame_), lastPts_)));
                layers_[index].precomp.scene->evaluate(nestedFrame);
                auto nested = std::make_shared<CompiledScene>(layers_[index].precomp.scene->compile());
                result.precomps.push_back(CompiledPrecomp{
                    layers_[index].id, states_[index].world, states_[index].worldOpacity,
                    states_[index].visible, std::move(nested)});
            }
            result.transforms.push_back(CompiledTransform{
                    layers_[index].id,
                    states_[index].world,
                    states_[index].previousWorld,
                    states_[index].worldOpacity,
                    states_[index].previousWorldOpacity,
                    states_[index].visible,
                    states_[index].previousVisible,
                    layers_[index].castsShadow,
                    layers_[index].acceptsShadow,
                    layers_[index].shadowOpacity,
                    layers_[index].shadowBlur});
        }        for (std::size_t index = 0; index < layers_.size(); ++index) {
            const MeshAttachment& mesh = layers_[index].mesh;
            if (mesh.empty()) continue;

            MeshDescriptor descriptor;
            descriptor.layerId = layers_[index].id;
            descriptor.name = layers_[index].name;
            // The asset identity is the opaque one from the mesh attachment, or
            // the layer's content id when the attachment only carries handles.
            descriptor.assetId = mesh.assetId.empty() ? layers_[index].content.id : mesh.assetId;
            descriptor.geometry = mesh.geometry;
            descriptor.material = mesh.material;
            result.addMesh(std::move(descriptor));
        }

        result.lights.reserve(lightStates_.size());
        for (const auto& state : lightStates_) {
            if (!state.evaluated) throw std::logic_error("MotionScene::compile: light state is not evaluated");
            result.lights.push_back(state);
        }
        return result;
    }

    int MotionScene::contentEndFrame() const {

        int last = 0;

        for (const auto& layer : layers_) {
            if (!layer.lifetime.openEnded()) {
                last = std::max(last, layer.lifetime.outFrame);
            }
            const float endTime = layer.endTime();
            if (endTime > 0.f) {
                last = std::max(last, static_cast<int>(std::ceil(endTime * fps_)));
            }
        }

        return last;
    }

}// namespace chrononmotion::motion
