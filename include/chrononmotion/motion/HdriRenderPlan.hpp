#ifndef CHRONONMOTION_MOTION_HDRI_RENDER_PLAN_HPP
#define CHRONONMOTION_MOTION_HDRI_RENDER_PLAN_HPP

#include "chrononmotion/assets/HdriReader.hpp"
#include <chronon3d/render_plan/render_plan.hpp>

#include <optional>
#include <string>

namespace chrononmotion::motion {
/// Attach a decoded HDRI probe to a V3 plan. The caller owns storing
/// `environment.serialize()` at `assetPath`; this helper adds its reference,
/// intensity, rotation, and diffuse SH coefficients to the plan.
void attachHdriEnvironmentToRenderPlan(
    chronon3d::render_plan::RenderPlan& plan,
    const chrononmotion::HdriEnvironment& environment,
    std::string assetPath,
    std::optional<chronon3d::render_plan::AnimationTrackPlan> rotationAnimation = std::nullopt);
}

#endif
