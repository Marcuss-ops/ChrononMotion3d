#include "chrononmotion/motion/HdriRenderPlan.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace chrononmotion::motion {
void attachHdriEnvironmentToRenderPlan(
    chronon3d::render_plan::RenderPlan& plan,
    const chrononmotion::HdriEnvironment& environment,
    std::string assetPath,
    std::optional<chronon3d::render_plan::AnimationTrackPlan> rotationAnimation) {
    if (plan.schema != chronon3d::render_plan::kRenderPlanSchemaV3)
        throw std::invalid_argument("HDRI RenderPlan: environment lights require schema V3");
    if (environment.faceSize == 0 || environment.mips.empty() ||
        !std::isfinite(environment.rotationRadians) || !std::isfinite(environment.intensity))
        throw std::invalid_argument("HDRI RenderPlan: decoded environment is incomplete");
    chronon3d::render_plan::EnvironmentLightPlan light;
    light.probe_asset = std::move(assetPath);
    light.intensity = environment.intensity;
    light.rotation_y = environment.rotationRadians;
    light.irradiance_sh = environment.irradianceSH;
    light.rotation_animation = std::move(rotationAnimation);
    plan.environment_light = std::move(light);
}
} // namespace chrononmotion::motion
