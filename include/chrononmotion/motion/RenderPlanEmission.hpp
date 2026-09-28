// Optional offline adapter: Chronon3D's typed RenderPlan written out as
// `chronon.render-plan.v2` / `.v3` JSON.
//
// This header is available only to consumers that explicitly add Chronon3D's
// include tree. ChrononMotion itself has no dependency on Chronon3D.
//
// Why this exists: the offset of the bridge is asymmetric. Chronon3D *consumes*
// a plan (`validate_render_plan`, `decode_render_plan`) and ChrononMotion
// *produces* one (`lowerToRenderPlan`), but nothing in either tree could write
// the document the consumer reads — so a Motion scene could be lowered and then
// not handed over. This is the producer's half of that contract, and it is the
// only place a plan becomes bytes.
//
// What it writes is deliberately the same subset `lowerToRenderPlan` emits, plus
// the caller-supplied content identity it passes through for text layers:
//
//   root     schema, version, job_id, canvas, budget (only when the caller
//            expressed one), camera, camera_animation, layers, output
//   layer    id, type, asset, source, color, size, position, scale, rotation,
//            enable_3d, opacity, start_frame, duration_frames, text, style,
//            animation
//   style    font, font_size, fill, glow
//   track    property, easing, keyframes (frame + value)
//
// `renderPlanEmissionProblem` names the first thing in a plan that this writer
// cannot express, and `renderPlanToJson` refuses to write rather than silently
// drop it — a plan that loses a mask or an effect on the way out would render
// as a different composition, which is worse than no file. The unsupported
// families are the ones with no producer side here yet: hierarchy that is not
// already flattened, track mattes, masks, time remap, transitions, effects and
// their parameter tracks, shapes and inline meshes, particles, lights, blend
// modes, text animators and spans, gradient animation, a layer-level
// `auto_orient` (the plan expresses it only inside a follow-path driver, which
// is itself unsupported here), and every animation driver (wiggle, spring,
// oscillate, random hold, posterize, data feed, audio react) plus per-key
// easing, because the lowering bakes all of those into linear samples.
//
// Every key written here must exist in the consumer's schema: a plan that
// carries one unknown field is rejected wholesale by
// `validate_render_plan` (`additionalProperties: false`), so a stray key costs
// the whole clip rather than a single property.

#ifndef CHRONONMOTION_MOTION_RENDER_PLAN_EMISSION_HPP
#define CHRONONMOTION_MOTION_RENDER_PLAN_EMISSION_HPP

// boundary-guardrail: allowed — this optional offline adapter is the explicit
// conversion boundary out of Chronon3D typed RenderPlan authoring data.
#include <chronon3d/render_plan/render_plan.hpp>

#include <string>

namespace chrononmotion::motion {

    struct RenderPlanEmissionOptions {
        /// Indentation per nesting level; 0 produces a single line.
        int indentSpaces{2};
    };

    /// Empty when the whole plan is expressible, otherwise the first thing that
    /// is not — named with the layer it belongs to, so a caller can report it
    /// before a file is written. `renderPlanToJson` throws exactly this string.
    [[nodiscard]] std::string renderPlanEmissionProblem(
        const chronon3d::render_plan::RenderPlan& plan);

    /// Write `plan` as deterministic JSON.
    ///
    /// Byte-for-byte reproducible: object keys are written in a fixed order,
    /// arrays follow the plan's own order, and every number is the shortest
    /// decimal that round-trips back to the same `float`. Throws
    /// `std::invalid_argument` carrying `renderPlanEmissionProblem(plan)` when
    /// the plan is not expressible.
    [[nodiscard]] std::string renderPlanToJson(
        const chronon3d::render_plan::RenderPlan& plan,
        const RenderPlanEmissionOptions& options = {});

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_RENDER_PLAN_EMISSION_HPP
