// ChrononMotion — the producer's half of the RenderPlan contract.
//
// `lowerToRenderPlan` decides *what* a plan says; this decides how it is
// written down. The two are kept apart on purpose: a writer that also lowered
// would be a second place an anchor or a singular transform could be accepted
// or rejected, and the lowering is the one that owns that judgement.
//
// Every value here is copied from the plan and nothing is derived from it, so
// the emitted document is a faithful transcription and never a second opinion.
// Anything the schema has no field for is refused by name rather than dropped:
// a plan that loses a mask on the way out renders as a different composition,
// and a file that renders differently from its model is worse than no file.

#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/math/Finite.hpp"
#include "RenderPlanEmissionInternal.hpp"

#include "JsonWriter.hpp"

#include <chronon3d/render_plan/color_utils.hpp>
#include <chronon3d/effects/effect_param_animation.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cmath>
#include <limits>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

    namespace {

        using chronon3d::render_plan::AnimationKeyframePlan;
        using chronon3d::render_plan::AnimationPlan;
        using chronon3d::render_plan::AnimationTrackPlan;
        using chronon3d::render_plan::CameraPlan;
        using chronon3d::render_plan::FitMode;
        using chronon3d::render_plan::InlineMeshPlan;
        using chronon3d::render_plan::LayerPlan;
        using chronon3d::render_plan::LayerStylePlan;
        using chronon3d::render_plan::LayerType;
        using chronon3d::render_plan::OutputFormat;
        using chronon3d::render_plan::PathOperatorKindPlan;
        using chronon3d::render_plan::RenderBudget;
        using chronon3d::render_plan::RenderPlan;
        using chronon3d::render_plan::ShapeContentPlan;


        [[nodiscard]] std::string planProblem(const RenderPlan& plan) {

            if (plan.job_id.empty()) return "the plan has no job id";
            if (plan.canvas.width <= 0 || plan.canvas.height <= 0) return "the plan has an empty canvas";
            if (plan.canvas.duration.integral() <= 0) return "the plan has no duration";
            if (plan.output.path.empty()) return "the plan has no output path";
            if (plan.schema != chronon3d::render_plan::kRenderPlanSchemaV2 &&
                plan.schema != chronon3d::render_plan::kRenderPlanSchemaV3) {
                return "the plan declares schema '" + plan.schema + "', which this emitter does not write";
            }
            if (plan.content_fingerprint != 0) {
                return "the plan carries a content fingerprint, which the plan contract cannot express";
            }
            if (!plan.warnings.empty()) {
                return "the plan carries warnings, which the plan contract cannot express";
            }
            if (plan.temporal_motion_blur) {
                if (plan.schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                    return "scene motion blur requires RenderPlan V3";
                const auto& blur = *plan.temporal_motion_blur;
                if (!std::isfinite(blur.shutter_angle) || blur.shutter_angle <= 0.f ||
                    blur.shutter_angle > 360.f || blur.samples < 2 || blur.samples > 64)
                    return "scene motion blur requires shutter_angle in (0, 360] and samples in [2, 64]";
            }
            if (plan.camera) {
                const std::string camera = detail::emissionCameraProblem(*plan.camera);
                if (!camera.empty()) return camera;
            }
            if (plan.lighting_rig) {
                if (plan.schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                    return "lighting rig requires RenderPlan V3";
                const auto& rig = *plan.lighting_rig;
                const auto finite_range = [](float val, float low, float high) {
                    return std::isfinite(val) && val >= low && val <= high;
                };
                if (!std::all_of(rig.key_direction.begin(), rig.key_direction.end(),
                        [](float v) { return std::isfinite(v) && std::abs(v) <= 1.0f; }) ||
                    std::sqrt(rig.key_direction[0]*rig.key_direction[0] + rig.key_direction[1]*rig.key_direction[1] + rig.key_direction[2]*rig.key_direction[2]) < 1e-6f ||
                    !std::all_of(rig.key_color.begin(), rig.key_color.end(), [](float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; }) ||
                    !std::all_of(rig.ambient_color.begin(), rig.ambient_color.end(), [](float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; }) ||
                    !std::all_of(rig.rim_color.begin(), rig.rim_color.end(), [](float v) { return std::isfinite(v) && v >= 0.f && v <= 1.f; }) ||
                    !finite_range(rig.key_intensity, 0.0f, 16.0f) ||
                    !finite_range(rig.ambient_intensity, 0.0f, 16.0f) ||
                    !finite_range(rig.rim_intensity, 0.0f, 16.0f) ||
                    !finite_range(rig.rim_power, 1e-6f, 32.0f)) {
                    return "lighting rig values are outside supported finite ranges";
                }
            }
            if (plan.audio) return "the plan carries an audio mix";
            if (plan.prepared_overlay_package) return "the plan carries a prepared overlay package";

            const std::string budget = detail::emissionBudgetProblem(plan);
            if (!budget.empty()) return budget;

            if (plan.camera_samples) {
                double previous = -std::numeric_limits<double>::infinity();
                for (const auto& sample : *plan.camera_samples) {
                    if (!std::isfinite(sample.frame) || sample.frame <= previous ||
                        sample.frame < -1000000.0 || sample.frame > 1000000.0)
                        return "resolved camera sample frames must be finite, ordered, and in [-1000000, 1000000]";
                    previous = sample.frame;
                }
            }

            std::uint64_t inlineMeshBytes = 0;
            for (const LayerPlan& layer : plan.layers) {
                const std::string problem = layerProblem(layer, plan.schema);
                if (!problem.empty()) return problem;
                if (layer.inline_mesh) {
                    constexpr std::uint64_t vertexBytes = sizeof(chronon3d::render_plan::InlineMeshVertex);
                    constexpr std::uint64_t indexBytes = sizeof(std::uint32_t);
                    const auto vertexCount = static_cast<std::uint64_t>(layer.inline_mesh->vertices.size());
                    const auto indexCount = static_cast<std::uint64_t>(layer.inline_mesh->indices.size());
                    const std::uint64_t meshBytes = vertexCount * vertexBytes + indexCount * indexBytes;
                    if (meshBytes > chronon3d::render_plan::kInlineMeshMaxAggregateBytes - inlineMeshBytes)
                        return "the plan's inline mesh payload exceeds the 64 MiB aggregate byte limit";
                    inlineMeshBytes += meshBytes;
                }
            }

            if (plan.output.codec != chronon3d::render_plan::VideoCodec::Auto ||
                plan.output.rate_control != chronon3d::render_plan::RateControlMode::Crf ||
                plan.output.bitrate != 0 || plan.output.crf != 0 || plan.output.qp != -1 ||
                !plan.output.profile_id.empty()) {
                return "the plan carries encoder settings, which belong to the render invocation";
            }
            return {};
        }



    } // namespace

    namespace detail {
    std::string render_plan_emission_problem(const chronon3d::render_plan::RenderPlan& plan) {
        return planProblem(plan);
    }
    }

} // namespace chrononmotion::motion
