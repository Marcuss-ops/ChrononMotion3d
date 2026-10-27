#pragma once

#include "JsonWriter.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <string>
#include <chronon3d/graphics/field2d.hpp>
#include <chronon3d/graphics/gradient.hpp>
#include <chronon3d/scene/model/shape/particles.hpp>
#include <chronon3d/text/text_span_override.hpp>

#include <vector>
#include <string_view>

namespace chrononmotion::motion {
// Shared bindings for the emission split TUs (the pre-split translation unit
// kept these in its anonymous namespace). Tracks is the internal shorthand for
// the layer effect-parameter track list.
using chronon3d::render_plan::AnimationPlan;
using chronon3d::render_plan::AnimationTrackPlan;
using chronon3d::render_plan::CameraPlan;
using chronon3d::render_plan::CameraSamplePlan;
using chronon3d::render_plan::InlineMeshPlan;
using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerStylePlan;
using chronon3d::render_plan::ShapeContentPlan;
using chronon3d::render_plan::ShapePathCommandPlan;
using Tracks = std::vector<chronon3d::render_plan::EffectParamTrackPlan>;
} // namespace chrononmotion::motion

namespace chrononmotion::motion::detail {
[[nodiscard]] const char* layerTypeName(chronon3d::render_plan::LayerType type);
[[nodiscard]] const char* shapeTypeName(chronon3d::render_plan::ShapeContentTypePlan type);
[[nodiscard]] const char* blendModeName(chronon3d::BlendMode mode);
[[nodiscard]] const char* fitName(chronon3d::render_plan::FitMode fit);
[[nodiscard]] const char* outputFormatName(chronon3d::render_plan::OutputFormat format);
[[nodiscard]] const char* pathCommandTypeName(chronon3d::render_plan::ShapePathCommandTypePlan type);
[[nodiscard]] const char* fieldGeneratorName(chronon3d::graphics::Field2DGenerator generator);
[[nodiscard]] std::optional<std::string> textSpanColorHex(const chronon3d::Color& color);
[[nodiscard]] std::string render_plan_emission_problem(const chronon3d::render_plan::RenderPlan& plan);
[[nodiscard]] std::string emissionCameraProblem(const chronon3d::render_plan::CameraPlan& camera);
[[nodiscard]] std::string emissionBudgetProblem(const chronon3d::render_plan::RenderPlan& plan);
void writeTextSpans(detail::Writer& writer, const std::vector<chronon3d::TextSpanOverride>& spans);
void writeTextSelectorTrack(detail::Writer& writer, const AnimationTrackPlan& track);
void writeTextAnimators(detail::Writer& writer, const std::vector<chronon3d::render_plan::TextAnimatorPlan>& animators);
void writeStyle(detail::Writer& writer, const LayerStylePlan& style);
void writeMediaStyle(detail::Writer& writer, const LayerStylePlan& style);
void writeEffects(detail::Writer& writer, const std::vector<LayerPlan::EffectPlan>& effects);
void writeInlineMesh(detail::Writer& writer, const InlineMeshPlan& mesh);
void writeMaskTrack(detail::Writer& writer, const AnimationTrackPlan& track);
void writeEffectParameterTracks(detail::Writer& writer, const Tracks& tracks);
void writeMask(detail::Writer& writer, const LayerPlan::PathMaskPlan& mask, bool legacyPathMask);
void writeTrackMatte(detail::Writer& writer, const chronon3d::render_plan::TrackMattePlan& matte);
void writeLayerMasks(detail::Writer& writer, const LayerPlan& layer);
void writePathCommands(detail::Writer& writer, const std::vector<ShapePathCommandPlan>& commands);
void writeStroke(detail::Writer& writer, const chronon3d::render_plan::StrokeStyle& stroke);
void writeTrimOperator(detail::Writer& writer, const chronon3d::render_plan::PathOperatorPlan& op);
void writeField(detail::Writer& writer, const chronon3d::graphics::Field2D& field);
void writeGradientDefinition(detail::Writer& writer, const chronon3d::graphics::GradientDefinition& gradient);
void writeShape(detail::Writer& writer, const ShapeContentPlan& shape);
void writeParticleEmitter(detail::Writer& writer, const chronon3d::ParticleEmitterParams& particles, const std::string& schema);
void writeLight(detail::Writer& writer, const chronon3d::render_plan::LightLayerPlan& light);

inline void writeFloats(detail::Writer& writer, const float* values, std::size_t count) {
    writer.beginArray();
    for (std::size_t i = 0; i < count; ++i) {
        writer.item();
        writer.number(values[i]);
    }
    writer.endArray();
}

template<std::size_t N>
inline void writeFloats(detail::Writer& writer, const std::array<float, N>& values,
                        std::size_t count) {
    writeFloats(writer, values.data(), count);
}

inline void writeKeyframes(
    detail::Writer& writer,
    const std::vector<chronon3d::render_plan::AnimationKeyframePlan>& keyframes,
    bool scalar) {
    writer.beginArray();
    for (const auto& key : keyframes) {
        writer.item();
        writer.beginObject();
        writer.key("frame");
        writer.integer(key.frame.integral());
        writer.key("value");
        if (scalar) writer.number(key.value.empty() ? 0.f : key.value.front());
        else writeFloats(writer, key.value.data(), key.value.size());
        writer.endObject();
    }
    writer.endArray();
}
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
