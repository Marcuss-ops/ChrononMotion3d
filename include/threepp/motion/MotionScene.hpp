// ChrononMotion — the evaluated layer hierarchy.
//
// Layers are authored in a flat list with `parentId` links (a null/controller
// layer is just a layer with no content), and `evaluate()` composes world
// matrices parents-first. Evaluation is a pure function of (scene, frame, fps):
// the same frame always produces the same state, regardless of what was rendered
// before it.

#ifndef THREEPP_MOTION_MOTIONSCENE_HPP
#define THREEPP_MOTION_MOTIONSCENE_HPP

#include "threepp/motion/Layer.hpp"

#include "threepp/math/Matrix4.hpp"

#include <cmath>
#include <cstddef>
#include <deque>
#include <string>
#include <unordered_map>
#include <vector>

namespace threepp::motion {

    /// Deterministic time base.
    ///
    /// The integer frame is the authority; PTS (seconds) is derived from it, so
    /// "the value at frame N" depends on N and fps alone.
    struct MotionTime {
        int frame{0};
        float fps{24.f};

        [[nodiscard]] float pts() const {

            return fps > 0.f ? static_cast<float>(frame) / fps : 0.f;
        }

        [[nodiscard]] static MotionTime atFrame(int frame, float fps) {

            return MotionTime{frame, fps};
        }

        /// Nearest frame for a presentation timestamp. Rounding (not truncating)
        /// keeps a PTS that is one ulp below an exact frame on that frame.
        [[nodiscard]] static MotionTime atPts(float seconds, float fps) {

            return MotionTime{static_cast<int>(std::lround(seconds * fps)), fps};
        }
    };

    /// The solved state of one layer at a frame.
    struct LayerState {
        Transform local{};
        Matrix4 world{};
        float worldOpacity{1.f};
        bool visible{true};
        bool evaluated{false};
    };

    /// A hierarchy of layers with deterministic frame evaluation.
    ///
    /// Layers are held in a deque so a `Layer&` returned by `addLayer()` stays
    /// valid as the scene grows; re-fetch by id with `findLayer()` when in doubt.
    class MotionScene {
    public:
        MotionScene() = default;
        explicit MotionScene(std::string name, float fps = 24.f);

        // --- authoring
        Layer& addLayer(Layer layer);
        Layer& addLayer(Layer::Id id, std::string name);
        Layer& addNull(Layer::Id id, std::string name, Layer::Id parentId = Layer::kRoot);
        Layer& addContent(Layer::Id id, std::string name, ContentRef content, Layer::Id parentId = Layer::kRoot);

        [[nodiscard]] Layer* findLayer(Layer::Id id);
        [[nodiscard]] const Layer* findLayer(Layer::Id id) const;
        [[nodiscard]] Layer* findLayer(const std::string& name);
        [[nodiscard]] const Layer* findLayer(const std::string& name) const;
        [[nodiscard]] bool hasLayer(Layer::Id id) const;

        [[nodiscard]] std::size_t layerCount() const { return layers_.size(); }
        [[nodiscard]] const std::deque<Layer>& layers() const { return layers_; }
        [[nodiscard]] std::deque<Layer>& layers() { return layers_; }

        [[nodiscard]] const std::string& name() const { return name_; }
        [[nodiscard]] float fps() const { return fps_; }
        MotionScene& setFps(float fps);

        // --- evaluation
        void evaluate(int frame);
        void evaluate(const MotionTime& time);
        void evaluatePts(float seconds);

        [[nodiscard]] int lastEvaluatedFrame() const { return lastFrame_; }
        [[nodiscard]] float lastEvaluatedPts() const { return lastPts_; }

        [[nodiscard]] const LayerState& state(Layer::Id id) const;
        [[nodiscard]] bool visible(Layer::Id id) const { return state(id).visible; }
        [[nodiscard]] float worldOpacity(Layer::Id id) const { return state(id).worldOpacity; }
        [[nodiscard]] const Matrix4& worldMatrix(Layer::Id id) const { return state(id).world; }
        [[nodiscard]] Matrix4 worldMatrixOf(const Layer& layer) const { return worldMatrix(layer.id); }

        /// Parents-first evaluation order. Deterministic for any hierarchy; a
        /// parent cycle is broken by the visiting mark (see `validate()`).
        [[nodiscard]] std::vector<Layer::Id> evaluationOrder() const;

        /// Authoring problems: unknown parents, self-parenting, cycles.
        [[nodiscard]] std::vector<std::string> validate() const;

        /// The last frame the scene still has something to show: the maximum of
        /// the closed lifetimes and the key ends, in frames.
        [[nodiscard]] int contentEndFrame() const;

    private:
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        [[nodiscard]] std::size_t indexOf(Layer::Id id) const;

        std::string name_{};
        float fps_{24.f};
        std::deque<Layer> layers_{};
        std::unordered_map<Layer::Id, std::size_t> index_{};
        std::deque<LayerState> states_{};
        int lastFrame_{0};
        float lastPts_{0.f};
    };

}// namespace threepp::motion

#endif//THREEPP_MOTION_MOTIONSCENE_HPP
