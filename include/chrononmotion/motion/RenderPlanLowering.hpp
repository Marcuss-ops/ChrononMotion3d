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

// boundary-guardrail: allowed — this optional offline adapter is the explicit
// conversion boundary into Chronon3D typed RenderPlan authoring data.
#include <chronon3d/render_plan/render_plan.hpp>

#include <optional>
#include <string>
#include <unordered_map>

namespace chrononmotion::motion {

    struct RenderPlanLoweringOptions {
        int width{1920};
        int height{1080};
        std::string jobId{"chrononmotion_baked"};
        std::string outputPath{"output.png"};
        std::string schema{chronon3d::render_plan::kRenderPlanSchemaV2};
        // Content owners provide concrete renderer-facing plans by opaque
        // ContentRef id. Motion never invents assets, text bytes, or styling.
        // Caller-supplied content/shape/style identity, keyed by layer id.
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> contentLayers{};
        std::optional<chronon3d::render_plan::CameraPlan> camera{};
        /// Optional rig supplies deterministic time-varying pose samples during lowering.
        const CameraRig* cameraRig{nullptr};
    };

    /// Evaluate the MotionScene at each output frame, compile each evaluated
    /// frame to CompiledScene, then lower its world transforms to the typed
    /// RenderPlan. Hierarchy is flattened; unrepresentable anchors, shear,
    /// perspective, reflections, and singular transforms fail closed.
    [[nodiscard]] chronon3d::render_plan::RenderPlan lowerToRenderPlan(
        MotionScene& scene, const RenderPlanLoweringOptions& options = {});


    /// Lower a fully sampled camera pose to Chronon3D's camera contract.
    [[nodiscard]] chronon3d::render_plan::CameraPlan lowerCameraPose(
        const CameraPose& pose, float nearPlane = 1.f, float farPlane = 10000.f);

    /// Convert baked cubic paths to the exact ShapePathCommandPlan vocabulary
    /// shared with the Chronon3D v3 schema.
    [[nodiscard]] std::vector<chronon3d::render_plan::ShapePathCommandPlan>
    lowerBakedPath(const BakedPath& path);

    /// Attach already-baked geometry as static V3 shape-plan path operators.
    /// Animation remains represented by explicit typed morph tracks supplied by
    /// the caller, keeping the adapter free from per-frame shape solving.
    void applyBakedPathGeometry(chronon3d::render_plan::LayerPlan& layer,
                                const BakedPath& path,
                                std::span<const BakedPath> repeaterCopies = {});

} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_RENDER_PLAN_LOWERING_HPP