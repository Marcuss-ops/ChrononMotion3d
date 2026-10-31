#include "RenderPlanEmissionInternal.hpp"
#include <chronon3d/render_plan/camera_contract.hpp>

#include <cmath>

namespace chrononmotion::motion::detail {
using chronon3d::render_plan::CameraPlan;
using chronon3d::render_plan::RenderBudget;
using chronon3d::render_plan::RenderPlan;

std::string emissionCameraProblem(const CameraPlan& camera) {
    if (const auto problem = chronon3d::render_plan::camera_contract_problem(camera)) return *problem;

    const auto finite = [](float value) { return std::isfinite(value); };
    for (const float value : camera.position)
        if (!finite(value)) return "the camera has a non-finite position";
    for (const float value : camera.rotation_deg)
        if (!finite(value)) return "the camera has a non-finite rotation";
    if (camera.type != "perspective" && camera.type != "orthographic")
        return "the camera has unsupported type '" + camera.type + "'";
    if (!finite(camera.fov_deg) || camera.fov_deg < 10.f || camera.fov_deg > 120.f)
        return "the camera has an FOV outside [10, 120] degrees";
    if (!finite(camera.near) || camera.near <= 0.f || !finite(camera.far) || camera.far <= camera.near)
        return "the camera has an invalid clipping interval";
    if (!finite(camera.zoom) || camera.zoom <= 0.f)
        return "the camera has a non-positive or non-finite zoom";
    if (!finite(camera.dof_focus_distance) || camera.dof_focus_distance <= 0.f ||
        camera.dof_focus_distance > 10'000'000.f || !finite(camera.dof_aperture) ||
        camera.dof_aperture < 0.f || camera.dof_aperture > 1.f ||
        !finite(camera.dof_max_blur) || camera.dof_max_blur < 0.f || camera.dof_max_blur > 256.f)
        return "the camera has depth-of-field values outside the RenderPlan contract";
    if (!finite(camera.sensor_width_mm) || camera.sensor_width_mm < 1.f || camera.sensor_width_mm > 100.f ||
        !finite(camera.lens_shift[0]) || !finite(camera.lens_shift[1]) ||
        std::abs(camera.lens_shift[0]) > 1.f || std::abs(camera.lens_shift[1]) > 1.f)
        return "the camera sensor width or lens shift is outside the RenderPlan contract";
    if (!finite(camera.focal_length_mm) ||
        (camera.focal_length_mm != 0.f &&
         (camera.focal_length_mm < 12.f || camera.focal_length_mm > 200.f)) ||
        !finite(camera.f_stop) ||
        (camera.f_stop != 0.f && (camera.f_stop < 0.7f || camera.f_stop > 32.f)) ||
        camera.iris_blades < 0 || camera.iris_blades > 12)
        return "the camera has lens values outside the RenderPlan contract";
    if (!finite(camera.dof_f_stop) ||
        (camera.dof_f_stop != 0.f &&
         (camera.dof_f_stop < 0.7f || camera.dof_f_stop > 32.f)) ||
        camera.dof_iris_blades < 0 || camera.dof_iris_blades > 12 ||
        !finite(camera.dof_highlight_gain) ||
        camera.dof_highlight_gain < 0.f || camera.dof_highlight_gain > 8.f)
        return "the camera has physical depth-of-field values outside the RenderPlan contract";
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
