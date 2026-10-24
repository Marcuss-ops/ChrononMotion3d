#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <array>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace chrononmotion::motion::detail {
[[nodiscard]] const char* layerTypeName(chronon3d::render_plan::LayerType type);
[[nodiscard]] const char* shapeTypeName(chronon3d::render_plan::ShapeContentTypePlan type);
[[nodiscard]] const char* blendModeName(chronon3d::BlendMode mode);
[[nodiscard]] const char* fitName(chronon3d::render_plan::FitMode fit);
[[nodiscard]] const char* outputFormatName(chronon3d::render_plan::OutputFormat format);
[[nodiscard]] std::optional<std::string> textSpanColorHex(const chronon3d::Color& color);
[[nodiscard]] std::string render_plan_emission_problem(const chronon3d::render_plan::RenderPlan& plan);
[[nodiscard]] std::string emissionCameraProblem(const chronon3d::render_plan::CameraPlan& camera);
[[nodiscard]] std::string emissionBudgetProblem(const chronon3d::render_plan::RenderPlan& plan);
[[nodiscard]] std::string layerOwner(const chronon3d::render_plan::LayerPlan& layer);
[[nodiscard]] std::string styleProblem(const chronon3d::render_plan::LayerPlan& layer);
[[nodiscard]] std::string effectProblem(const chronon3d::render_plan::LayerPlan& layer);
[[nodiscard]] std::string effectParameterProblem(const chronon3d::render_plan::LayerPlan& layer);
[[nodiscard]] std::string trackProblem(const std::string& owner,
                                       const chronon3d::render_plan::AnimationTrackPlan& track);
[[nodiscard]] std::string animationProblem(const std::string& owner,
                                           const chronon3d::render_plan::AnimationPlan& animation);
[[nodiscard]] std::string textTrackValueProblem(const chronon3d::render_plan::AnimationTrackPlan& track);
[[nodiscard]] std::string textAnimatorProblem(const chronon3d::render_plan::LayerPlan& layer,
                                              const std::string& schema);
[[nodiscard]] std::string layerProblem(const chronon3d::render_plan::LayerPlan& layer,
                                       const std::string& schema);

/// The easing vocabulary the plan contract defines. An unknown name is a
/// write-time problem, not something to hand to the decoder to discover.
[[nodiscard]] inline bool isKnownEasing(const std::string& easing) {
    static constexpr std::array<std::string_view, 24> known{
            "linear",
            "in_quad", "out_quad", "in_out_quad",
            "in_cubic", "out_cubic", "in_out_cubic",
            "in_expo", "out_expo", "in_out_expo",
            "in_sine", "out_sine", "in_out_sine",
            "in_back", "out_back", "in_out_back",
            "in_elastic", "out_elastic", "in_out_elastic",
            "in_bounce", "out_bounce", "in_out_bounce",
            "smoothstep", "hold"};
    for (const std::string_view candidate : known) {
        if (candidate == easing) return true;
    }
    return false;
}

[[nodiscard]] inline bool oneOf(const std::string& value,
                                std::initializer_list<std::string_view> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}
}

namespace chrononmotion::motion {
using detail::layerTypeName;
using detail::shapeTypeName;
using detail::blendModeName;
using detail::fitName;
using detail::outputFormatName;
using detail::textSpanColorHex;
}
