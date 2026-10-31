// Optional offline adapter to Chronon3D's typed RenderPlan v2/v3 API.
//
// This header is available only to consumers that explicitly add Chronon3D's
// include tree. ChrononMotion itself has no dependency on Chronon3D.
#ifndef CHRONONMOTION_MOTION_RENDER_PLAN_LOWERING_HPP
#define CHRONONMOTION_MOTION_RENDER_PLAN_LOWERING_HPP

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "chrononmotion/motion/RigBaking.hpp"

// boundary-guardrail: allowed — this optional offline adapter is the explicit
// conversion boundary into Chronon3D typed RenderPlan authoring data.
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/camera_sample_provider.hpp>

#include <nlohmann/json.hpp>

#include <optional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace chrononmotion::motion {

    struct RenderPlanLoweringOptions {
        int width{1920};
        int height{1080};
        std::string jobId{"chrononmotion_baked"};
        std::string outputPath{"output.png"};
        std::string schema{chronon3d::render_plan::kRenderPlanSchemaV2};
        // Content owners provide concrete renderer-facing plans by opaque
        // ContentRef id. For Mesh layers, Layer::mesh.geometry is explicitly
        // lowered to an inline static-triangle payload; an assetId without
        // geometry is not consumable by this RenderPlan contract. Motion never
        // invents assets, text bytes, or styling.
        // Caller-supplied content/shape/style identity, keyed by layer id.
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> contentLayers{};
        std::optional<chronon3d::render_plan::CameraPlan> camera{};
        /// Optional full-scene shutter accumulation (RenderPlan V3 only).
        /// Requires shutter_angle in (0, 360] and samples in [2, 64].
        std::optional<chronon3d::render_plan::MotionBlurPlan> temporalMotionBlur{};
        /// Optional rig supplies deterministic time-varying pose samples during lowering.
        const CameraRig* cameraRig{nullptr};
        /// Keep the rig's initial pose in `camera`, but leave time-varying camera
        /// evaluation to a runtime CameraSampleProvider instead of serializing resolved samples.
        bool bakeResolvedCameraSamples{true};
        /// Optional strictly increasing absolute fractional frame times supplied
        /// by the renderer's schedule. With shutter enabled, an empty list uses
        /// the canonical shutter calendar and an explicit list must include it.
        /// Without shutter, empty retains the integer frame bake; times not in
        /// an explicit list interpolate and may approximate nonlinear motion.
        std::vector<double> cameraSampleFrames{};
    };

    /// Evaluate the MotionScene at each output frame, compile each evaluated
    /// frame to CompiledScene, then lower its world transforms to the typed
    /// RenderPlan. Hierarchy is flattened; unrepresentable anchors, shear,
    /// perspective, reflections, and singular transforms fail closed.
    [[nodiscard]] chronon3d::render_plan::RenderPlan lowerToRenderPlan(
        MotionScene& scene, const RenderPlanLoweringOptions& options = {});


    /// Lower a sampled camera pose to Chronon3D's camera contract. The pose's
    /// orientation must already include target aiming and roll (as CameraRig::sample
    /// provides); target/roll metadata is not a separate RenderPlan channel.
    /// RenderPlan carries resolved pose, FOV, clipping planes, physical focal
    /// length/f-stop/iris, and the legacy blur-gain/focus/max-blur contract.
    /// Backend projection matrices remain renderer-derived from the canvas.
    [[nodiscard]] chronon3d::render_plan::CameraPlan lowerCameraPose(
        const CameraPose& pose, float nearPlane = 1.f, float farPlane = 10000.f);

    /// Sample a rig at every integer presentation frame and lower the complete
    /// renderer-facing camera state. `CameraSamplePlan::frame` is an absolute
    /// presentation-frame index; its time in seconds is frame * fps_den / fps_num
    /// from the enclosing canvas. For shutter rendering, C3D requests additional
    /// samples at sub-frame times derived from MotionBlurPlan::shutter_angle
    /// (angle / 360 / fps) and `samples`; these are sampled exactly by the runtime
    /// provider. This is the canonical offline camera bake used by RenderPlan
    /// lowering and external plan producers.
    [[nodiscard]] std::vector<chronon3d::render_plan::CameraSamplePlan>
    bakeResolvedCameraSamples(const CameraRig& rig, float fps, std::size_t frameCount,
                              float nearPlane = 1.f, float farPlane = 10000.f,
                              const Vector3& cameraPositionOffset = Vector3{});

    /// Serialize CM3D-resolved camera samples into the canonical RenderPlan V3
    /// JSON field shape. This is the shared bridge for Template plan producers.
    [[nodiscard]] nlohmann::json cameraSamplesToJson(
        const std::vector<chronon3d::render_plan::CameraSamplePlan>& samples);

    /// Create a runtime provider for an unparented rig at C3D's exact
    /// presentation/shutter sample times, including pre-roll. Parented rigs
    /// require the scene overload. The caller must keep the rig immutable and
    /// supply a stable fingerprint for that authored rig.
    [[nodiscard]] chronon3d::render_plan::RuntimeCameraSampleProvider
    makeCameraSampleProvider(std::shared_ptr<const CameraRig> rig, float fps,
                             std::string fingerprint,
                             float nearPlane = 1.f, float farPlane = 10000.f);

    /// Resolve a parented camera through a dedicated MotionScene at each exact
    /// requested frame. The provider owns and mutates the scene during sampling;
    /// callers must not mutate or evaluate it elsewhere for its lifetime. Calls
    /// on copies of this provider are serialized. The fingerprint must identify
    /// both the rig and the complete authored hierarchy and timing.
    [[nodiscard]] chronon3d::render_plan::RuntimeCameraSampleProvider
    makeCameraSampleProvider(std::shared_ptr<const CameraRig> rig,
                             std::shared_ptr<MotionScene> scene,
                             std::string fingerprint,
                             float nearPlane = 1.f, float farPlane = 10000.f);

    /// Replace the positions in a mesh layer with one baked RigProgram geometry
    /// frame. Existing UVs and material are retained when topology matches;
    /// normals are regenerated from the deformed triangles.
    void applyRigGeometryFrame(chronon3d::render_plan::LayerPlan& layer,
                               const RigFrameGeometry& geometry,
                               std::span<const std::uint32_t> indices);

    /// Append one topology-compatible baked geometry sample to a V3 inline mesh.
    /// Samples must be supplied in strictly increasing absolute output-frame
    /// order; UVs, indices and material remain the authored base mesh data.
    void appendRigGeometrySample(chronon3d::render_plan::LayerPlan& layer,
                                 const RigFrameGeometry& geometry,
                                 std::span<const std::uint32_t> indices,
                                 std::int64_t frame);

    /// Lower one geometry channel from a baked RigProgram over its full sampled
    /// interval. The requested geometry id must occur exactly once per frame.
    void applyRigGeometryBake(chronon3d::render_plan::LayerPlan& layer,
                              const BakedRigProgram& bake,
                              std::uint32_t geometryId,
                              std::span<const std::uint32_t> indices,
                              std::int64_t firstFrame);

    /// Convert baked cubic paths to the exact ShapePathCommandPlan vocabulary
    /// shared with the Chronon3D v3 schema.
    [[nodiscard]] std::vector<chronon3d::render_plan::ShapePathCommandPlan>
    lowerBakedPath(const BakedPath& path);

    /// Attach baked static geometry to a V3 path shape plan. `repeaterCopies`
    /// is the complete output of expandPathRepeater(), including its identity
    /// first copy; that copy is already the layer path and is not merged twice.
    void applyBakedPathGeometry(chronon3d::render_plan::LayerPlan& layer,
                                const BakedPath& path,
                                std::span<const BakedPath> repeaterCopies = {});

    /// Attach a baked, topology-compatible morph sequence as a native V3 Morph
    /// operator with deterministic scalar progress keys at `firstFrame + i`.
    void applyBakedPathMorph(chronon3d::render_plan::LayerPlan& layer,
                             std::span<const BakedPath> morphFrames,
                             int firstFrame = 0);

} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_RENDER_PLAN_LOWERING_HPP
