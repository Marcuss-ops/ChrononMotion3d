#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace chrononmotion::motion {

    namespace {

        const LayerState kMissingState{};

    }// namespace

    MotionScene::MotionScene(std::string name, float fps)
        : name_(std::move(name)), fps_(fps > 0.f ? fps : 24.f) {}

    Layer& MotionScene::addLayer(Layer layer) {

        const Layer::Id id = layer.id;
        const auto existing = index_.find(id);

        if (existing != index_.end()) {
            layers_[existing->second] = std::move(layer);
            states_[existing->second] = LayerState{};
            return layers_[existing->second];
        }

        index_.emplace(id, layers_.size());
        layers_.push_back(std::move(layer));
        states_.emplace_back();
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

    std::size_t MotionScene::indexOf(Layer::Id id) const {

        const auto it = index_.find(id);
        return it == index_.end() ? npos : it->second;
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

        if (fps > 0.f) fps_ = fps;
        return *this;
    }

    std::vector<Layer::Id> MotionScene::evaluationOrder() const {

        const std::size_t count = layers_.size();
        std::vector<Layer::Id> order;
        order.reserve(count);

        // 0 = unvisited, 1 = on the current path, 2 = emitted.
        std::vector<char> color(count, 0);

        std::function<void(std::size_t)> visit = [&](std::size_t index) {
            if (color[index] != 0) return;
            color[index] = 1;

            const std::size_t parent = indexOf(layers_[index].parentId);
            if (parent != npos && parent != index) {
                visit(parent);
            }

            color[index] = 2;
            order.push_back(layers_[index].id);
        };

        for (std::size_t index = 0; index < count; ++index) {
            visit(index);
        }

        return order;
    }

    void MotionScene::evaluate(const MotionTime& time) {

        const float seconds = time.pts();

        // Reset so a layer whose parent could not be solved still reads as a
        // deterministic root rather than as last frame's residue.
        for (auto& state : states_) {
            state.visible = true;
            state.worldOpacity = 1.f;
            state.evaluated = false;
        }

        for (const Layer::Id id : evaluationOrder()) {
            const std::size_t index = indexOf(id);
            if (index == npos) continue;

            LayerState& state = states_[index];
            layers_[index].sample(seconds, state.local);
            state.local.compose(state.world);

            const bool alive = layers_[index].visibleAt(time.frame);
            const std::size_t parent = indexOf(layers_[index].parentId);

            const bool hasSolvedParent = parent != npos && parent != index && states_[parent].evaluated;

            if (!hasSolvedParent) {
                state.visible = alive;
                state.worldOpacity = state.local.opacity;
            } else {
                Matrix4 composed;
                composed.multiplyMatrices(states_[parent].world, state.world);
                state.world = composed;
                // Lifetime hides the subtree: a hidden null hides its children.
                state.visible = alive && states_[parent].visible;
                state.worldOpacity = states_[parent].worldOpacity * state.local.opacity;
            }

            state.evaluated = true;
        }

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
        return index == npos ? kMissingState : states_[index];
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
