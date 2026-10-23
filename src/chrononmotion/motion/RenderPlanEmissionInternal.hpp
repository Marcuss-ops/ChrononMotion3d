#pragma once

#include <chronon3d/render_plan/render_plan.hpp>

#include <optional>
#include <string>

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
}

namespace chrononmotion::motion {
using detail::layerTypeName;
using detail::shapeTypeName;
using detail::blendModeName;
using detail::fitName;
using detail::outputFormatName;
using detail::textSpanColorHex;
}
