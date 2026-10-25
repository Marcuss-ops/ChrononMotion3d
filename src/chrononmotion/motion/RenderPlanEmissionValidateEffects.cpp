// Effect and mask-stack write contract of the emitter.

#include "RenderPlanEmissionInternal.hpp"

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
#include <utility>
#include <vector>

namespace chrononmotion::motion::detail {
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
using chronon3d::render_plan::ShapeContentTypePlan;
using chronon3d::render_plan::ShapePathCommandPlan;
using chronon3d::render_plan::ShapePathCommandTypePlan;

        [[nodiscard]] std::string effectProblem(const LayerPlan& layer) {
            const std::string owner = layerOwner(layer);
            if (layer.effects.size() > 64) return owner + " exceeds the 64-effect layer limit";
            if (!layer.masks.empty() && layer.path_mask)
                return owner + " carries both legacy mask and ordered masks";
            if (layer.masks.size() > 16)
                return owner + " exceeds the 16-mask stack limit";
            for (const auto& effect : layer.effects) {
                if (effect.posterize_fps || effect.time_curve)
                    return owner + " carries an effect time control, which this emitter does not write";
                switch (effect.kind) {
                    case LayerPlan::EffectKindPlan::Catalog: {
                        if (effect.canonical_effect_id != "color.saturation" &&
                            effect.canonical_effect_id != "color.contrast")
                            return owner + " carries unsupported catalog effect '" + effect.canonical_effect_id + "'";
                        if (effect.canonical_params.size() != 1 ||
                            effect.canonical_params.front().param != "value")
                            return owner + " carries catalog grade parameters other than one 'value'";
                        const float value = effect.canonical_params.front().value;
                        if (!std::isfinite(value) || value < 0.f || value > 4.f)
                            return owner + " carries saturation/contrast outside [0, 4]";
                        break;
                    }
                    case LayerPlan::EffectKindPlan::Noise:
                        if (!std::isfinite(effect.amount) || effect.amount < 0.f || effect.amount > 1.f ||
                            !std::isfinite(effect.noise_size) || effect.noise_size <= 0.f || effect.noise_size > 256.f ||
                            !std::isfinite(effect.grain_shadow_response) || effect.grain_shadow_response < 0.f || effect.grain_shadow_response > 4.f ||
                            !std::isfinite(effect.grain_midtone_response) || effect.grain_midtone_response < 0.f || effect.grain_midtone_response > 4.f ||
                            !std::isfinite(effect.grain_highlight_response) || effect.grain_highlight_response < 0.f || effect.grain_highlight_response > 4.f)
                            return owner + " carries noise/grain values outside supported ranges";
                        break;
                    case LayerPlan::EffectKindPlan::Vignette:
                        if (!std::isfinite(effect.amount) || effect.amount < 0.f || effect.amount > 1.f ||
                            !std::isfinite(effect.radius) || effect.radius < 0.f || effect.radius > 1.f ||
                            !std::isfinite(effect.softness) || effect.softness < 0.f || effect.softness > 1.f ||
                            std::any_of(effect.effect_color.begin(), effect.effect_color.end(), [](float v) {
                                return !std::isfinite(v) || v < 0.f || v > 1.f;
                            }))
                            return owner + " carries vignette values outside [0, 1]";
                        break;
                    case LayerPlan::EffectKindPlan::Glow:
                        if (!std::isfinite(effect.radius) || effect.radius < 0.f || effect.radius > 256.f ||
                            !std::isfinite(effect.glow_intensity) || effect.glow_intensity < 0.f || effect.glow_intensity > 4.f ||
                            std::any_of(effect.effect_color.begin(), effect.effect_color.end(), [](float v) {
                                return !std::isfinite(v) || v < 0.f || v > 1.f;
                            }))
                            return owner + " carries glow values outside supported ranges";
                        break;
                    case LayerPlan::EffectKindPlan::Bloom:
                        if (!std::isfinite(effect.bloom_threshold) || effect.bloom_threshold < 0.f || effect.bloom_threshold > 1.f ||
                            !std::isfinite(effect.radius) || effect.radius < 0.f || effect.radius > 256.f ||
                            !std::isfinite(effect.bloom_intensity) || effect.bloom_intensity < 0.f || effect.bloom_intensity > 4.f)
                            return owner + " carries bloom values outside supported ranges";
                        break;
                    case LayerPlan::EffectKindPlan::RoughenEdges:
                        // Ranges mirror the plan decoder (roughen_edges): the
                        // emitter writes what the decoder accepts, nothing more.
                        if (!std::isfinite(effect.amount) || effect.amount < 0.f || effect.amount > 64.f ||
                            !std::isfinite(effect.roughen_scale) || effect.roughen_scale <= 0.f || effect.roughen_scale > 1024.f ||
                            effect.roughen_complexity < 1 || effect.roughen_complexity > 4)
                            return owner + " carries roughen-edge values outside supported ranges";
                        break;
                    default:
                        return owner + " carries an effect type this emitter does not write";
                }
            }
            return {};
        }

        [[nodiscard]] std::string effectParameterProblem(const LayerPlan& layer) {
            const std::string owner = layerOwner(layer);
            if (layer.effect_param_tracks.size() > 256)
                return owner + " exceeds the 256 effect-parameter-track limit";
            std::vector<std::string> identities;
            for (const auto& item : layer.effect_param_tracks) {
                if (item.effect_id.empty() || item.effect_id.size() > 128 || item.param.empty() || item.param.size() > 64)
                    return owner + " carries an invalid effect parameter track identity";
                const auto identity = item.effect_id + "." + item.param;
                if (std::find(identities.begin(), identities.end(), identity) != identities.end())
                    return owner + " carries a duplicate effect parameter track '" + identity + "'";
                identities.push_back(identity);
                const auto* definition = chronon3d::effects::find_animatable_effect_param(item.effect_id, item.param);
                if (!definition)
                    return owner + " carries a non-animatable effect parameter '" + identity + "'";
                const auto effectId = [](const LayerPlan::EffectPlan& effect) -> std::string_view {
                    switch (effect.kind) {
                        case LayerPlan::EffectKindPlan::Catalog: return effect.canonical_effect_id;
                        case LayerPlan::EffectKindPlan::Noise: return chronon3d::effects::ids::GenerateNoise;
                        case LayerPlan::EffectKindPlan::Vignette: return chronon3d::effects::ids::ColorVignette;
                        case LayerPlan::EffectKindPlan::Glow: return chronon3d::effects::ids::LightGlow;
                        case LayerPlan::EffectKindPlan::Bloom: return chronon3d::effects::ids::LightBloom;
                        default: break;
                    }
                    return {};
                };
                const bool targetExists = std::any_of(layer.effects.begin(), layer.effects.end(), [&](const auto& effect) {
                    return effectId(effect) == item.effect_id;
                });
                if (!targetExists)
                    return owner + " has an effect parameter track without a matching effect '" + item.effect_id + "'";
                const std::string trackProblemText = trackProblem(owner + " effect parameter track", item.track);
                if (!trackProblemText.empty()) return trackProblemText;
                if (item.track.keyframes.empty() || item.track.keyframes.size() > 65'536 ||
                    item.track.easing != "linear" || item.track.loop_mode != "none")
                    return owner + " effect parameter tracks require bounded baked linear keyframes";
                std::vector<std::int64_t> frames;
                frames.reserve(item.track.keyframes.size());
                for (const auto& key : item.track.keyframes) {
                    if (key.frame.integral() < 0 || key.frame.integral() > 1'000'000 ||
                        key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                        key.value.front() < definition->minimum || key.value.front() > definition->maximum)
                        return owner + " has effect parameter keyframes outside the declared range";
                    frames.push_back(key.frame.integral());
                }
                std::sort(frames.begin(), frames.end());
                if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                    return owner + " has duplicate effect parameter keyframe frames";
            }
            return {};
        }


} // namespace chrononmotion::motion::detail
