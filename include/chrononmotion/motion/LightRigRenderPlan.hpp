#ifndef CHRONONMOTION_MOTION_LIGHT_RIG_RENDER_PLAN_HPP
#define CHRONONMOTION_MOTION_LIGHT_RIG_RENDER_PLAN_HPP

#include "chrononmotion/motion/LightRig.hpp"
#include <chronon3d/render_plan/render_plan.hpp>

#include <cstddef>

namespace chrononmotion::motion {
    /// Append one animated RenderPlan light layer per rig source. Sampling is
    /// deterministic at integer frame times; V1 permits 1..4096 frames.
    void appendLightRigToRenderPlan(chronon3d::render_plan::RenderPlan& plan,
                                    const LightRig& rig, float fps,
                                    std::size_t frameCount);
}

#endif
