#include "chrononmotion/motion/MotionScene.hpp"
#include "MotionScene_internal.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

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

    MotionScene& MotionScene::setFps(float fps) {

        ensureMutable("change fps");
        if (!std::isfinite(fps) || fps <= 0.f)
            throw std::invalid_argument("MotionScene::setFps: fps must be finite and positive");
        fps_ = fps;
        return *this;
    }

    bool MotionScene::orderStale() const {

        if (orderDirty_ || order_.size() != layers_.size() ||
            orderSwitchParentIds_.size() != layers_.size()) {
            return true;
        }

        // parentId is a public field, so a re-parent cannot be intercepted: the
        // cache instead remembers the parent *id* each slot was built from, and
        // an integer compare per layer exposes any direct write.
        for (std::size_t position = 0; position < order_.size(); ++position) {
            const Layer& layer = layers_[order_[position]];
            if (layer.parentId != orderParentIds_[position]) return true;
            const auto& dependencies = orderSwitchParentIds_[position];
            if (layer.spaceSwitches.size() + layer.constraints.size() + layer.drivers.size() !=
                dependencies.size()) return true;
            std::size_t dependency = 0;
            for (const auto& point : layer.spaceSwitches)
                if (point.parentId != dependencies[dependency++]) return true;
            for (const auto& constraint : layer.constraints)
                if (constraint.targetId != dependencies[dependency++]) return true;
            for (const auto& driver : layer.drivers)
                if (driver.targetId != dependencies[dependency++]) return true;
        }
        return false;
    }

    void MotionScene::syncGraph() {

        // The Object3D graph is the one hierarchy. `parentId` is its id-addressed
        // authoring form, so an unevaluated layer is reconciled to its authored
        // parent. A switched layer's graph parent, however, is time-dependent and
        // is written by evaluate(); rebuilding the order must preserve the last
        // evaluated parent rather than jumping ahead to a future switch target.
        for (std::size_t index = 0; index < layers_.size(); ++index) {
            auto& layer = layers_[index];
            if (!layer.spaceSwitches.empty() && states_[index].evaluated) continue;
            const Layer::Id graphParentId = layer.parentId;
            Layer* parent = (graphParentId == Layer::kRoot) ? nullptr : findLayer(graphParentId);
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
        const auto authoredIndexOf = [&](Layer::Id id) {
            const auto found = index_.find(id);
            return found == index_.end() ? npos : found->second;
        };
        order_.clear();
        orderParents_.clear();
        orderParentIds_.clear();
        orderSwitchParentIds_.clear();
        orderValid_ = true;
        order_.reserve(count);
        orderParents_.reserve(count);
        orderParentIds_.reserve(count);
        orderSwitchParentIds_.reserve(count);

        // A switched-to space is also an evaluation dependency: its world
        // matrix must exist before the switching layer can be composed. Validate
        // that the dependency schedule does not contain a space-switch cycle.
        std::vector<std::vector<std::size_t>> dependencies(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto addDependency = [&](Layer::Id id) {
                if (id == Layer::kRoot) return;
                const std::size_t dependency = authoredIndexOf(id);
                if (dependency == npos || dependency == index) {
                    orderValid_ = false;
                    return;
                }
                if (std::find(dependencies[index].begin(), dependencies[index].end(), dependency) ==
                    dependencies[index].end()) dependencies[index].push_back(dependency);
            };
            addDependency(layers_[index].parentId);
            for (const auto& point : layers_[index].spaceSwitches) addDependency(point.parentId);
            for (const auto& constraint : layers_[index].constraints) addDependency(constraint.targetId);
            for (const auto& driver : layers_[index].drivers) addDependency(driver.targetId);
        }

        // 0 = unvisited, 1 = on the current path, 2 = emitted.
        std::vector<char> color(count, 0);
        struct Frame {
            std::size_t index;
            std::size_t nextDependency;
        };

        std::vector<Frame> stack;
        for (std::size_t root = 0; root < count; ++root) {
            if (color[root] != 0) continue;
            color[root] = 1;
            stack.push_back({root, 0});
            while (!stack.empty()) {
                Frame& top = stack.back();
                const auto& links = dependencies[top.index];
                if (top.nextDependency < links.size()) {
                    const std::size_t dependency = links[top.nextDependency++];
                    if (color[dependency] == 0) {
                        color[dependency] = 1;
                        stack.push_back({dependency, 0});
                        continue;
                    }
                    if (color[dependency] == 1) orderValid_ = false;
                    continue;
                }
                color[top.index] = 2;
                order_.push_back(top.index);
                orderParents_.push_back(npos);
                orderParentIds_.push_back(Layer::kRoot);
                orderSwitchParentIds_.emplace_back();
                stack.pop_back();
            }
        }

        // Fill the cached parent indices now that every layer has a slot.
        for (std::size_t position = 0; position < order_.size(); ++position) {
            const std::size_t index = order_[position];
            const std::size_t parent = authoredIndexOf(layers_[index].parentId);
            orderParents_[position] = (parent != npos && parent != index) ? parent : npos;
            orderParentIds_[position] = layers_[index].parentId;
            auto& switchParents = orderSwitchParentIds_[position];
            switchParents.clear();
            switchParents.reserve(layers_[index].spaceSwitches.size());
            for (const auto& point : layers_[index].spaceSwitches)
                switchParents.push_back(point.parentId);
            for (const auto& constraint : layers_[index].constraints)
                switchParents.push_back(constraint.targetId);
            for (const auto& driver : layers_[index].drivers)
                switchParents.push_back(driver.targetId);
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
        (void)time.pts(); // validate aggregate-constructed samples too
        if (time.frame == std::numeric_limits<int>::min())
            throw std::invalid_argument("MotionScene::evaluate: previous frame is not representable");
        // Use the exact same solver for both samples; drivers, constraints and
        // space switches must never be omitted from the previous pose.
        evaluatePose(MotionTime{time.frame - 1, time.fps, time.subFrame});
        for (auto& state : states_) {
            state.previousWorld = state.world;
            state.previousWorldOpacity = state.worldOpacity;
            state.previousVisible = state.visible;
        }
        evaluatePose(time);
    }

    void MotionScene::evaluatePose(const MotionTime& time) {

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

        using EvaluatedSpace = std::pair<Layer::Id, Matrix4>;
        std::function<EvaluatedSpace(Layer::Id, float, std::vector<Layer::Id>&)> spaceAt;
        std::function<Matrix4(Layer::Id, float, std::vector<Layer::Id>&)> worldAt;
        spaceAt = [&](Layer::Id id, float atSeconds,
                      std::vector<Layer::Id>& recursion) -> EvaluatedSpace {
            const Layer* layer = findLayer(id);
            if (!layer) throw std::invalid_argument("MotionScene::evaluate: space switch references an unknown layer");
            Layer::Id parentId = layer->parentId;
            Matrix4 offset;
            float previousSwitchTime = -1.f;
            for (const SpaceSwitchPoint& point : layer->spaceSwitches) {
                if (!std::isfinite(point.seconds) || point.seconds < 0.f || point.seconds <= previousSwitchTime)
                    throw std::invalid_argument("MotionScene::evaluate: space switches must have strictly sorted finite non-negative times");
                previousSwitchTime = point.seconds;
                if (point.seconds > atSeconds) break;
                if (point.parentId == parentId) continue;
                Matrix4 oldWorld;
                if (parentId != Layer::kRoot)
                    oldWorld = worldAt(parentId, point.seconds, recursion);
                Matrix4 newWorld;
                if (point.parentId != Layer::kRoot) {
                    if (!findLayer(point.parentId))
                        throw std::invalid_argument("MotionScene::evaluate: space switch references an unknown parent");
                    newWorld = worldAt(point.parentId, point.seconds, recursion);
                    if (std::fabs(newWorld.determinant()) < 1e-10f)
                        throw std::invalid_argument("MotionScene::evaluate: space switch parent world matrix is singular");
                    newWorld.invert();
                }
                Matrix4 handoff;
                handoff.multiplyMatrices(newWorld, oldWorld);
                offset.premultiply(handoff);
                parentId = point.parentId;
            }
            return {parentId, offset};
        };
        worldAt = [&](Layer::Id id, float atSeconds,
                      std::vector<Layer::Id>& recursion) -> Matrix4 {
            if (std::find(recursion.begin(), recursion.end(), id) != recursion.end())
                throw std::invalid_argument("MotionScene::evaluate: cyclic space-switch dependency");
            recursion.push_back(id);
            const Layer* layer = findLayer(id);
            if (!layer) throw std::invalid_argument("MotionScene::evaluate: space switch references an unknown layer");
            const EvaluatedSpace evaluated = spaceAt(id, atSeconds, recursion);
            Matrix4 local = layer->sample(atSeconds, resolve).toMatrix();
            local.premultiply(evaluated.second);
            if (evaluated.first != Layer::kRoot)
                local.premultiply(worldAt(evaluated.first, atSeconds, recursion));
            recursion.pop_back();
            return local;
        };

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
            std::vector<Layer::Id> visitingSpaces;
            const EvaluatedSpace activeSpace = spaceAt(layers_[index].id, layerSeconds, visitingSpaces);
            const Layer::Id activeParentId = activeSpace.first;
            const Matrix4 localOffset = activeSpace.second;

            for (const auto& constraint : layers_[index].constraints) {
                ConstraintSpec evaluatedConstraint = constraint;
                if (constraint.amountTrack) {
                    float sampledAmount = constraint.amount;
                    constraint.amountTrack->sample(layerSeconds, sampledAmount, resolve);
                    evaluatedConstraint.amount = std::clamp(sampledAmount, 0.f, 1.f);
                }
                const std::size_t target = indexOf(constraint.targetId);
                if (constraint.targetId == Layer::kRoot &&
                    (constraint.kind == ConstraintKind::FollowPath || constraint.kind == ConstraintKind::LimitPosition ||
                     constraint.kind == ConstraintKind::LimitRotation || constraint.kind == ConstraintKind::LimitScale)) {
                    // Root-target FollowPath has no target layer to index. The
                    // constraint only consumes seconds and its path, so pass
                    // neutral target values and never dereference npos.
                    const Vector3 rootPosition{};
                    const Quaternion rootRotation{};
                    const Vector3 rootScale{1.f, 1.f, 1.f};
                    if (!applyConstraint(evaluatedConstraint, state.local, rootPosition,
                                          rootRotation, rootScale, layerSeconds))
                        throw std::invalid_argument("MotionScene::evaluate: constraint failed");
                    continue;
                }
                if (target == npos || !states_[target].evaluated) {
                    if (constraint.targetId != Layer::kRoot) {
                        throw std::invalid_argument("MotionScene::evaluate: constraint has an unknown or unsolved target");
                    }
                    continue;
                }
                { // Every target-dependent constraint consumes target state in local space.
                    Matrix4 relative = states_[target].world;
                    if (activeParentId != Layer::kRoot) {
                        const auto parent = indexOf(activeParentId);
                        if (parent == npos || !states_[parent].evaluated)
                            throw std::invalid_argument("MotionScene::evaluate: unsolved constraint parent");
                        Matrix4 inverse = states_[parent].world;
                        if (inverse.determinant() == 0.f)
                            throw std::invalid_argument("MotionScene::evaluate: singular constraint parent");
                        relative.premultiply(inverse.invert());
                    }
                    Matrix4 inverseOffset = localOffset;
                    if (inverseOffset.determinant() == 0.f)
                        throw std::invalid_argument("MotionScene::evaluate: singular space offset");
                    relative.premultiply(inverseOffset.invert());
                    Vector3 position, scale;
                    Quaternion rotation;
                    relative.decompose(position, rotation, scale);
                    if (!applyConstraint(evaluatedConstraint, state.local, position, rotation, scale, layerSeconds))
                        throw std::invalid_argument("MotionScene::evaluate: constraint failed");
                }
            }

            for (const auto& driver : layers_[index].drivers) {
                const std::size_t target = indexOf(driver.targetId);
                Vector3 targetPosition;
                if (target != npos && states_[target].evaluated) {
                    Matrix4 relative = states_[target].world;
                    if (activeParentId != Layer::kRoot) {
                        Matrix4 inverse = states_[indexOf(activeParentId)].world;
                        if (std::fabs(inverse.determinant()) < 1e-10f)
                            throw std::invalid_argument("MotionScene::evaluate: singular driver parent");
                        relative.premultiply(inverse.invert());
                    }
                    Matrix4 inverseOffset = localOffset;
                    if (std::fabs(inverseOffset.determinant()) < 1e-10f)
                        throw std::invalid_argument("MotionScene::evaluate: singular driver space");
                    relative.premultiply(inverseOffset.invert());
                    targetPosition.setFromMatrixPosition(relative);
                } else if (driver.targetId != Layer::kRoot) {
                    throw std::invalid_argument("MotionScene::evaluate: unknown or unsolved driver target");
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
            if (!layers_[index].spaceSwitches.empty())
                localMatrix.premultiply(localOffset);
            state.world.copy(localMatrix);

            // Lifetime is a whole-frame contract: a layer's in/out point is an
            // authoring decision, not a continuous one, so visibility stays
            // keyed to the integer frame and is unaffected by the fraction.
            const bool alive = layers_[index].visibleAt(time.frame);
            const std::size_t parent = activeParentId == Layer::kRoot ? npos : indexOf(activeParentId);
            if (parent != npos && !states_[parent].evaluated)
                throw std::invalid_argument("MotionScene::evaluate: space-switch parent was not evaluated first");

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
            } else {
                state.visible = alive;
                state.worldOpacity = state.local.opacity;
            }

            // `Layer` is the Object3D graph node, so the evaluated pose is written
            // back onto it: `matrix`/`matrixWorld` are the authority and
            // `matrixAutoUpdate` is off, so no consumer can recompose them from a
            // transform that does not know about the anchor. The PQS fields mirror
            // the layer's local TRS for generic Object3D readers; `rotation` is the
            // composed orientation (`orientation * euler`) as an Euler.
            Layer& node = layers_[index];
            Object3D* evaluatedParent = parent == npos ? nullptr : &layers_[parent];
            if (node.parent != evaluatedParent) {
                if (node.parent) node.removeFromParent();
                if (evaluatedParent) evaluatedParent->addRef(node);
            }
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
        const float dt = 1.f / lastTime_.fps;
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
            state.direction.copy(state.sample.direction);
            if (state.sample.parentId != Layer::kRoot) {
                state.direction.transformDirection(states_[indexOf(state.sample.parentId)].world);
            } else {
                state.direction.normalize();
            }

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

} // namespace chrononmotion::motion
