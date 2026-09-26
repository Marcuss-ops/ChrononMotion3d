// ChrononMotion — the evaluated layer hierarchy.
//
// Layers are authored in a flat list with `parentId` links (a null/controller
// layer is just a layer with no content), and `evaluate()` composes world
// matrices parents-first. Evaluation is a pure function of (scene, frame, fps):
// the same frame always produces the same state, regardless of what was rendered
// before it.

#ifndef CHRONONMOTION_MOTION_MOTIONSCENE_HPP
#define CHRONONMOTION_MOTION_MOTIONSCENE_HPP

#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/Lighting.hpp"

#include "chrononmotion/math/Matrix4.hpp"

#include <cmath>
#include <cstddef>
#include <deque>
#include <stdexcept>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace chrononmotion::motion {

    struct CompiledScene;

    /// Deterministic time base.
    ///
    /// The integer frame is the authority; PTS (seconds) is derived from it, so
    /// "the value at frame N" depends on N and fps alone.
    ///
    /// `subFrame` carries the fractional position inside the frame, in [0,1).
    /// Whole-frame sampling (the default, subFrame == 0) reproduces the original
    /// behaviour exactly. Sub-frame sampling exists so two consumers that ask for
    /// the *same instant* land on the same value: motion blur, sub-frame
    /// sampling and audio sync all need "t", not "the nearest frame to t".
    /// Storing the fraction explicitly rather than in a float PTS keeps a time
    /// sample exactly representable: a whole frame is frame.0, never
    /// frame - epsilon, so a frame-aligned render cannot drift by a frame.
    struct MotionTime {
        int frame{0};
        float fps{24.f};
        float subFrame{0.f};

        [[nodiscard]] static MotionTime atFrame(int frame, float fps) {

            if (fps <= 0.f) throw std::invalid_argument("MotionTime: fps must be positive");
            return MotionTime{frame, fps, 0.f};
        }

        [[nodiscard]] float pts() const {

            if (fps <= 0.f) throw std::invalid_argument("MotionTime: fps must be positive");
            return (static_cast<float>(frame) + subFrame) / fps;
        }

        /// True when this sample sits exactly on a frame boundary.
        [[nodiscard]] bool frameAligned() const { return subFrame == 0.f; }

        /// Nearest frame for a presentation timestamp. Rounding (not truncating)
        /// keeps a PTS that is one ulp below an exact frame on that frame. The
        /// fraction is dropped, so this stays frame-quantised by construction.
        [[nodiscard]] static MotionTime atPts(float seconds, float fps) {

            if (fps <= 0.f) throw std::invalid_argument("MotionTime: fps must be positive");
            return MotionTime{static_cast<int>(std::lround(seconds * fps)), fps, 0.f};
        }

        /// Exact sample: no rounding, no frame snapping. The frame is the floor
        /// and the fraction is the remainder, so `exactFrame()` recovers the
        /// requested position without a second rounding error.
        [[nodiscard]] static MotionTime atPtsExact(float seconds, float fps) {

            if (fps <= 0.f) throw std::invalid_argument("MotionTime: fps must be positive");
            const float exact = seconds * fps;
            const float frame = std::floor(exact);
            const int whole = static_cast<int>(frame);
            // Clamp so a PTS that rounds to the very end of a frame (0.9999999
            // of the way there) reports the next frame with no fraction, rather
            // than a frame carrying a subFrame of ~1.
            const float fraction = exact - frame;
            if (fraction >= 1.f - 1e-6f) {
                return MotionTime{whole + 1, fps, 0.f};
            }
            if (fraction < 0.f) {
                return MotionTime{whole, fps, 0.f};
            }
            return MotionTime{whole, fps, fraction};
        }
    };

    /// The solved state of one layer at a frame.
    struct LayerState {
        Transform local{};
        Matrix4 world{};
        Matrix4 previousWorld{};
        float worldOpacity{1.f};
        float previousWorldOpacity{1.f};
        bool previousVisible{true};
        bool visible{true};
        bool evaluated{false};
    };

    // LayerMotion lives in Track.hpp: Layer.hpp needs to name it and already
    // includes Track.hpp, so declaring it here would be an include cycle.

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

        /// A layer that transforms a 3D mesh. The model's bytes stay on the
        /// content side; only the asset id and the data-only handles travel.
        Layer& addMesh(Layer::Id id, std::string name, MeshAttachment mesh,
                       Layer::Id parentId = Layer::kRoot);

        Light& addLight(std::unique_ptr<Light> light);
        AmbientLight& addAmbientLight(Layer::Id id, std::string name = "AmbientLight",
                                      Layer::Id parentId = Layer::kRoot);
        DirectionalLight& addDirectionalLight(Layer::Id id, std::string name = "DirectionalLight",
                                              Layer::Id parentId = Layer::kRoot);
        PointLight& addPointLight(Layer::Id id, std::string name = "PointLight",
                                  Layer::Id parentId = Layer::kRoot);
        SpotLight& addSpotLight(Layer::Id id, std::string name = "SpotLight",
                                Layer::Id parentId = Layer::kRoot);

        [[nodiscard]] Layer* findLayer(Layer::Id id);
        [[nodiscard]] const Layer* findLayer(Layer::Id id) const;
        [[nodiscard]] Layer* findLayer(const std::string& name);
        [[nodiscard]] const Layer* findLayer(const std::string& name) const;
        [[nodiscard]] bool hasLayer(Layer::Id id) const;
        [[nodiscard]] const std::deque<std::unique_ptr<Light>>& lights() const { return lights_; }
        [[nodiscard]] const LightState& lightState(Light::Id id) const;

        [[nodiscard]] std::size_t layerCount() const { return layers_.size(); }
        [[nodiscard]] std::size_t lightCount() const { return lights_.size(); }
        [[nodiscard]] const std::deque<Layer>& layers() const { return layers_; }
        [[nodiscard]] std::deque<Layer>& layers() { return layers_; }

        [[nodiscard]] const std::string& name() const { return name_; }
        [[nodiscard]] float fps() const { return fps_; }
        MotionScene& setFps(float fps);

        // --- evaluation
        void evaluate(int frame);
        void evaluate(const MotionTime& time);
        void evaluatePts(float seconds);

        /// Exact sub-frame evaluation. Unlike `evaluatePts`, which snaps to the
        /// nearest frame, this samples at the requested instant.
        void evaluatePtsExact(float seconds);

        [[nodiscard]] int lastEvaluatedFrame() const { return lastFrame_; }
        [[nodiscard]] float lastEvaluatedPts() const { return lastPts_; }
        [[nodiscard]] const MotionTime& lastEvaluatedTime() const { return lastTime_; }
        [[nodiscard]] bool hasEvaluated() const { return hasEvaluated_; }

        [[nodiscard]] const LayerState& state(Layer::Id id) const;
        [[nodiscard]] bool visible(Layer::Id id) const { return state(id).visible; }
        [[nodiscard]] float worldOpacity(Layer::Id id) const { return state(id).worldOpacity; }
        [[nodiscard]] const Matrix4& worldMatrix(Layer::Id id) const { return state(id).world; }
        [[nodiscard]] Matrix4 worldMatrixOf(const Layer& layer) const { return worldMatrix(layer.id); }

        // --- motion (velocity/acceleration)
        ///
        /// Finite-difference motion, from the layer's own evaluated world state at
        /// this frame and the previous one. Costs nothing extra: the evaluate loop
        /// already keeps `previousWorld` composed, so this is a subtraction of two
        /// states the scene already holds. Call it after `evaluate()`.
        [[nodiscard]] LayerMotion motion(Layer::Id id) const;

        /// Analytic motion from the layer's keys alone, at a continuous instant
        /// in seconds. No evaluation and no sampling error, and usable at a
        /// sub-frame instant where there is no "previous frame" to difference
        /// against. It does not see drivers, constraints, time remap or the parent
        /// chain — for a parented or driven layer it is the timeline's share of the
        /// motion, not the layer's world rate.
        [[nodiscard]] LayerMotion keyedMotion(Layer::Id id, float seconds) const;

        /// Parents-first evaluation order. Deterministic for any hierarchy; a
        /// parent cycle is broken by the visiting mark (see `validate()`).
        [[nodiscard]] std::vector<Layer::Id> evaluationOrder();

        /// Authoring problems: unknown parents, self-parenting, cycles.
        [[nodiscard]] std::vector<std::string> validate() const;

        /// The last frame the scene still has something to show: the maximum of
        /// the closed lifetimes and the key ends, in frames.
        [[nodiscard]] int contentEndFrame() const;
        [[nodiscard]] CompiledScene compile() const;

        /// Deterministic JSON export of this scene's space and time only — never
        /// content bytes. See `chrononmotion/motion/SceneExport.hpp`.
        [[nodiscard]] std::string toJson(int indentSpaces = 2) const;

    private:
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);

        [[nodiscard]] std::size_t indexOf(Layer::Id id) const;
        [[nodiscard]] std::size_t lightIndexOf(Light::Id id) const;
        void evaluateLights(const MotionTime& time);

        /// Resolve one cross-layer link to that channel's value (or, in rate
        /// mode, its derivative), recursively and with a stack-scoped cycle
        /// guard: re-entering a channel that is already being resolved is a link
        /// cycle, not a second lookup.
        [[nodiscard]] LinkValue resolveChannel(Layer::Id layerId, TrackChannel channel, float time,
                                               std::vector<std::pair<Layer::Id, TrackChannel>>& visiting,
                                               bool rate) const;

        /// A resolver backed by `resolveChannel`, for `Layer::sample` /
        /// `Layer::keyedMotion`.
        [[nodiscard]] LinkResolver linkResolver(
                std::vector<std::pair<Layer::Id, TrackChannel>>& visiting, bool rate) const;

        /// Reconcile the `Object3D` parent/child graph with the authored
        /// `parentId` links. The graph is the one hierarchy; `parentId` is its
        /// id-addressed authoring form, kept in sync at every order rebuild so
        /// `parent`/`children`/`traverse`/`matrixWorld` are never a second truth.
        void syncGraph();

        /// True when the cached evaluation order no longer matches the layers:
        /// first use, a layer added or replaced, or any direct write to a
        /// Layer::parentId (a public field, so it cannot be intercepted).
        [[nodiscard]] bool orderStale() const;

        /// Rebuild the cached parents-first evaluation order. Iterative DFS over
        /// indices, with each layer's resolved parent index beside it, so the
        /// evaluate loop touches neither the id map nor the heap. Non-const
        /// because reconciling the object graph mutates the nodes.
        void rebuildOrder();

        std::string name_{};
        float fps_{24.f};
        std::deque<Layer> layers_{};
        std::unordered_map<Layer::Id, std::size_t> index_{};
        std::deque<LayerState> states_{};
        std::deque<std::unique_ptr<Light>> lights_{};
        std::unordered_map<Light::Id, std::size_t> lightIndex_{};
        std::deque<LightState> lightStates_{};
        int lastFrame_{0};
        float lastPts_{0.f};
        MotionTime lastTime_{0, 24.f, 0.f};
        bool hasEvaluated_{false};

        // The evaluation order, cached across frames: the hierarchy only changes
        // at authoring time, never while frames are being sampled. The third
        // array remembers the parent *id* each slot was built from, so any
        // direct write to a Layer::parentId is caught by an integer compare per
        // layer before the walk trusts the cache.
        mutable std::vector<std::size_t> order_{};
        mutable std::vector<std::size_t> orderParents_{};
        mutable std::vector<Layer::Id> orderParentIds_{};
        mutable bool orderDirty_{true};
        mutable bool orderValid_{false};
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_MOTIONSCENE_HPP
