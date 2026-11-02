#include "RenderPlanEmissionInternal.hpp"
#include <chronon3d/render_plan/camera_contract.hpp>

namespace chrononmotion::motion::detail {
using chronon3d::render_plan::CameraPlan;
using chronon3d::render_plan::RenderBudget;
using chronon3d::render_plan::RenderPlan;

std::string emissionCameraProblem(const CameraPlan& camera) {
    if (const auto problem = chronon3d::render_plan::camera_contract_problem(camera)) return *problem;
    // `dof_shape_unsupported` is the shared seat of the shaped-bokeh rule: it
    // rejects iris-blade authoring only, because highlight weighting is
    // implemented by every DOF kernel. Refuse to serialize a lens no render
    // boundary can execute.
    if (chronon3d::dof_shape_unsupported(camera.iris_blades, camera.dof_highlight_gain))
        return "camera asks for iris-shaped bokeh (iris_blades) that no DOF kernel "
               "implements; keep iris_blades at zero";
    return {};
}

std::string emissionBudgetProblem(const RenderPlan& plan) {

    const RenderBudget defaults{};
    const RenderBudget& budget = plan.budget;
    const bool lowered = budget.max_width != defaults.max_width ||
                         budget.max_height != defaults.max_height ||
                         budget.max_total_pixels != defaults.max_total_pixels ||
                         budget.max_frames != defaults.max_frames ||
                         budget.max_layers != defaults.max_layers ||
                         budget.max_text_bytes != defaults.max_text_bytes ||
                         budget.max_asset_reference_bytes != defaults.max_asset_reference_bytes ||
                         budget.max_estimated_output_bytes != defaults.max_estimated_output_bytes ||
                         budget.max_peak_memory_bytes != defaults.max_peak_memory_bytes;
    if (lowered) {
        return "the plan carries a render budget beyond max_temporal_pixels, which the plan contract "
               "cannot express";
    }
    return {};
}


}  // namespace chrononmotion::motion::detail
