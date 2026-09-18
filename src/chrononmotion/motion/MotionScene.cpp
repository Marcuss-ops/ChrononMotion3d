#include "chrononmotion/motion/MotionScene.hpp"

#include "chrononmotion/motion/CompiledScene.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

    MotionScene::MotionScene(std::string name, float fps)
        : name_(std::move(name)), fps_(fps) {

        if (fps <= 0.f) throw std::invalid_argument("MotionScene: fps must be positive");
    }

    Layer& MotionScene::addLayer(Layer layer) {

        const Layer::Id id = layer.id;
        const auto existing = index_.find(id);

        if (existing != index_.end()) {
            layers_[existing->second] = std::move(layer);
            states_[existing->second] = LayerState{};
            orderDirty_ = true; // a replacement can re-parent
            return layers_[existing->second];
        }

        index_.emplace(id, layers_.size());
        layers_.push_back(std::move(layer));
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

    void MotionScene::rebuildOrder() const {

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

    std::vector<Layer::Id> MotionScene::evaluationOrder() const {

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

        for (std::size_t position = 0; position < order_.size(); ++position) {
            const std::size_t index = order_[position];

            LayerState& state = states_[index];
            layers_[index].sample(seconds, state.local);
            state.local.compose(state.world);

            const bool alive = layers_[index].visibleAt(time.frame);
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
            } else {
                state.visible = alive;
                state.worldOpacity = state.local.opacity;
            }

            state.evaluated = true;
        }

        evaluateLights(time);

        lastFrame_ = time.frame;
        lastPts_ = seconds;
    }

    void MotionScene::evaluate(int frame) {

        evaluate(MotionTime{frame, fps_});
    }

    void MotionScene::evaluatePts(float seconds) {

        evaluate(MotionTime::atPts(seconds, fps_));
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

        for (const auto& layer : layers_) {
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
        for (std::size_t index = 0; index < layers_.size(); ++index) {
            if (!states_[index].evaluated) {
                throw std::logic_error("MotionScene::compile: evaluate the scene before compiling");
            }
            result.transforms.push_back(CompiledTransform{
                    layers_[index].id,
                    states_[index].world,
                    states_[index].worldOpacity,
                    states_[index].visible});
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
