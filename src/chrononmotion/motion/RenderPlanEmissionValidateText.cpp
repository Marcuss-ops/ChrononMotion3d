// Text animator and selector write contract of the emitter.

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

        [[nodiscard]] bool textTrackValueShape(const AnimationTrackPlan& track,
                                               std::initializer_list<std::size_t> arities) {
            if (track.keyframes.empty() || track.keyframes.size() > 65'536) return false;
            std::vector<std::int64_t> frames;
            frames.reserve(track.keyframes.size());
            for (const auto& key : track.keyframes) {
                if (key.frame.integral() < 0 || key.frame.integral() > 1'000'000 ||
                    !std::all_of(key.value.begin(), key.value.end(),
                                 [](float value) { return std::isfinite(value); }) ||
                    std::find(arities.begin(), arities.end(), key.value.size()) == arities.end())
                    return false;
                frames.push_back(key.frame.integral());
            }
            std::sort(frames.begin(), frames.end());
            return std::adjacent_find(frames.begin(), frames.end()) == frames.end();
        }

        [[nodiscard]] bool textTrackValueShape(const AnimationTrackPlan& track,
                                               std::size_t arity) {
            return textTrackValueShape(track,
                std::initializer_list<std::size_t>{arity});
        }

        [[nodiscard]] std::string textTrackValueProblem(const AnimationTrackPlan& track) {
            const std::string& property = track.property;
            const auto validArity = [&](std::size_t size) {
                if (property == "position") return size == 2 || size == 3;
                if (property == "scale") return size >= 1 && size <= 3;
                if (property == "rotation") return size == 1 || size == 3;
                if (property == "anchor") return size >= 1 && size <= 3;
                if (property == "fill_color" || property == "background_color" ||
                    property == "stroke_color") return size == 4;
                return size == 1;
            };
            if (track.keyframes.empty() || track.keyframes.size() > 65'536)
                return " has an empty or oversized keyframe list";
            std::vector<std::int64_t> frames;
            frames.reserve(track.keyframes.size());
            for (const auto& key : track.keyframes) {
                if (key.frame.integral() < 0 || key.frame.integral() > 1'000'000 ||
                    !validArity(key.value.size()) ||
                    !std::all_of(key.value.begin(), key.value.end(),
                                 [](float value) { return std::isfinite(value); }))
                    return " has keyframes with an invalid frame, non-finite component, or value arity";
                frames.push_back(key.frame.integral());
            }
            std::sort(frames.begin(), frames.end());
            if (std::adjacent_find(frames.begin(), frames.end()) != frames.end())
                return " has duplicate keyframe frames";
            if (property == "character_offset" &&
                (track.keyframes.size() != 1 || track.keyframes.front().frame.integral() != 0))
                return " must use exactly one static character_offset keyframe at frame 0";
            const bool colorProperty = property == "fill_color" || property == "background_color" ||
                                       property == "stroke_color";
            for (const auto& key : track.keyframes) {
                if (key.value.size() == 1) {
                    const float value = key.value.front();
                    if ((property == "opacity" && (value < 0.f || value > 1.f)) ||
                        (property == "baseline_shift" && (value < -1000.f || value > 1000.f)) ||
                        (property == "skew" && (value < -80.f || value > 80.f)) ||
                        (property == "skew_axis" && (value < -1'000'000.f || value > 1'000'000.f)) ||
                        ((property == "background_padding_em" || property == "background_radius_em") &&
                         (value < 0.f || value > 128.f)) ||
                        (property == "stroke_width" && (value < 0.f || value > 256.f)) ||
                        (property == "character_offset" &&
                         (value < -10'000.f || value > 10'000.f || std::floor(value) != value)))
                        return " has a property value outside the RenderPlan v3 bounds";
                }
                if (colorProperty &&
                    std::any_of(key.value.begin(), key.value.end(),
                        [](float component) { return component < 0.f || component > 1.f; }))
                    return " has a color component outside [0, 1]";
            }
            if (property == "opacity" || property == "blur" || property == "tracking") {
                for (const auto& key : track.keyframes)
                    if (key.value.front() < 0.f ||
                        (property == "opacity" && key.value.front() > 1.f))
                        return " has a property value outside the RenderPlan v3 bounds";
            }
            if ((property == "position" || property == "position_x" || property == "position_y" ||
                 property == "scale" || property == "scale_x" || property == "scale_y" ||
                 property == "rotation" || property == "anchor") &&
                std::any_of(track.keyframes.begin(), track.keyframes.end(),
                    [](const AnimationKeyframePlan& key) {
                        return std::any_of(key.value.begin(), key.value.end(),
                            [](float component) { return std::abs(component) > 1'000'000.f; });
                    }))
                return " has a component outside the RenderPlan v3 bounds [-1000000, 1000000]";
            if (property == "rotation" &&
                std::any_of(track.keyframes.begin(), track.keyframes.end(),
                    [](const AnimationKeyframePlan& key) { return key.value.size() == 3; }))
                return " uses a 3-component rotation track, which the text animator compiler does not lower";
            if (property == "anchor" && track.keyframes.size() > 1)
                return " uses an animated anchor track, which the text animator compiler does not lower";
            return {};
        }

        [[nodiscard]] std::string textAnimatorProblem(const LayerPlan& layer,
                                                      const std::string& schema) {
            if (layer.text_animators.empty()) return {};
            const std::string owner = layerOwner(layer);
            if (layer.text_animators.size() > 65'536)
                return owner + " exceeds the RenderPlan v3 text animator count limit";
            if (layer.type != LayerType::Text)
                return owner + " carries text animators but is not a text layer";
            if (schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                return owner + " carries text animators, which require RenderPlan v3";
            const auto trackIsEmpty = [](const AnimationTrackPlan& track) {
                return track.keyframes.empty() && track.property.empty() && track.easing == "linear" &&
                       !track.bezier_curve && !track.spring && track.loop_mode == "none" &&
                       !track.wiggle && !track.oscillate && !track.random_hold && !track.posterize_time &&
                       !track.data_feed && !track.audio_react;
            };
            std::vector<std::string> animatorIds;
            for (const auto& animator : layer.text_animators) {
                if (animator.selectors.empty() || animator.properties.empty())
                    return owner + " has a text animator without selectors or property tracks";
                if (!animator.id.empty()) {
                    if (animator.id.size() > 256 ||
                        std::find(animatorIds.begin(), animatorIds.end(), animator.id) != animatorIds.end())
                        return owner + " has a duplicate or oversized text animator id";
                    animatorIds.push_back(animator.id);
                }
                if (animator.selectors.size() > 65'536 || animator.properties.size() > 65'536)
                    return owner + " exceeds the RenderPlan v3 selector or property count limit";
                std::vector<std::string> selectorIds;
                for (const auto& selector : animator.selectors) {
                    if (!selector.id.empty()) {
                        if (selector.id.size() > 256 ||
                            std::find(selectorIds.begin(), selectorIds.end(), selector.id) != selectorIds.end())
                            return owner + " has a duplicate or oversized text selector id";
                        selectorIds.push_back(selector.id);
                    }
                    if (selector.semantic_id && std::none_of(layer.text_spans.begin(), layer.text_spans.end(),
                            [&](const auto& span) { return span.semantic_id == selector.semantic_id; }))
                        return owner + " has a text selector referencing an unknown semantic_id '" +
                               *selector.semantic_id + "'";
                    if (selector.semantic_id &&
                        (selector.semantic_id->empty() || selector.semantic_id->size() > 256))
                        return owner + " has a text selector semantic_id outside the V3 bounds";
                    // The closed selector vocabulary is a plan *identity* the emitter
                    // validates, not shaping: Chronon owns the units (its
                    // TextSelectorPlan declares the default), and this side only refuses
                    // a plan that invents one. The glyph unit is spelled through the
                    // contract's own default rather than re-litigated here.
                    const std::string contractDefaultUnit = chronon3d::render_plan::TextSelectorPlan{}.unit;
                    if (!oneOf(selector.unit, {contractDefaultUnit, "grapheme", "character", "word", "line"}) ||
                        !oneOf(selector.shape, {"square", "ramp_up", "ramp_down", "triangle", "round", "smooth", "wiggly", "noise"}) ||
                        !oneOf(selector.order, {"forward", "reverse", "from_center", "to_center", "random", "index"}) ||
                        !oneOf(selector.combine, {"replace", "add", "subtract", "intersect", "min", "max"}))
                        return owner + " has a text selector with an unsupported unit, shape, order, or combine mode";
                    if (selector.order == "index" && selector.randomize_order)
                        return owner + " combines index order with randomize_order";
                    if ((selector.shape == "wiggly" &&
                         (!std::isfinite(selector.wiggly_frequency) || selector.wiggly_frequency <= 0.f ||
                          selector.wiggly_frequency > 1000.f || !std::isfinite(selector.wiggly_amplitude) ||
                          selector.wiggly_amplitude < 0.f || selector.wiggly_amplitude > 1.f)) ||
                        (selector.shape != "wiggly" &&
                         (selector.wiggly_frequency != 1.f || selector.wiggly_amplitude != 0.25f ||
                          selector.wiggly_seed != 0)) ||
                        (selector.shape == "noise" &&
                         (!std::isfinite(selector.noise_frequency) || selector.noise_frequency <= 0.f ||
                          selector.noise_frequency > 1000.f || !std::isfinite(selector.noise_amplitude) ||
                          selector.noise_amplitude < 0.f || selector.noise_amplitude > 1.f)) ||
                        (selector.shape != "noise" &&
                         (selector.noise_frequency != 1.f || selector.noise_amplitude != 0.25f ||
                          selector.noise_seed != 0)) ||
                        (selector.order == "index" && (selector.index_step == 0 || selector.index_step > 1'000'000)) ||
                        !std::isfinite(selector.ease_low) || selector.ease_low < 0.f ||
                        selector.ease_low > 100.f || !std::isfinite(selector.ease_high) ||
                        selector.ease_high < 0.f || selector.ease_high > 100.f ||
                        selector.ease_low > selector.ease_high)
                        return owner + " has a text selector outside the RenderPlan v3 bounds";
                    if ((!selector.start.keyframes.empty() && !textTrackValueShape(selector.start, 1)) ||
                        (!selector.end.keyframes.empty() && !textTrackValueShape(selector.end, 1)) ||
                        (!selector.offset.keyframes.empty() && !textTrackValueShape(selector.offset, 1)) ||
                        (!selector.amount.keyframes.empty() && !textTrackValueShape(selector.amount, 1)))
                        return owner + " has a malformed text selector track without keyframes";
                    for (const AnimationTrackPlan* track : {&selector.start, &selector.end,
                                                             &selector.offset, &selector.amount}) {
                        if (trackIsEmpty(*track)) continue;
                        if (!track->property.empty())
                            return owner + " carries a text selector track property that is ignored by selector position";
                        if (track->keyframes.size() > 65'536)
                            return owner + " has an oversized text selector keyframe list";
                        if (track->loop_mode != "none" || track->spring || track->bezier_curve ||
                            track->easing != "linear")
                            return owner + " text selector tracks require baked linear keyframes";
                        const std::string problem = trackProblem(owner + " text selector track", *track);
                        if (!problem.empty()) return problem;
                        if (track->keyframes.empty())
                            return owner + " has a non-empty text selector track without keyframes";
                        if (!textTrackValueShape(*track, 1))
                            return owner + " has a selector track with invalid frames or non-scalar values";
                    }
                }
                std::vector<std::string> seenProperties;
                for (const AnimationTrackPlan& track : animator.properties) {
                    if (track.property.empty() || track.keyframes.empty())
                        return owner + " has an empty text animator property track";
                    if (!oneOf(track.property, {"position", "position_x", "position_y", "scale",
                            "scale_x", "scale_y", "opacity", "blur", "tracking", "rotation",
                            "fill_color", "skew", "skew_axis", "anchor", "baseline_shift",
                            "background_color", "background_padding_em", "background_radius_em",
                            "stroke_color", "stroke_width", "character_offset"}))
                        return owner + " has an unsupported text animator property '" + track.property + "'";
                    if (track.property == "position_z" || track.property == "scale_z" ||
                        track.property == "rotation_x" || track.property == "rotation_y" ||
                        track.property == "rotation_z")
                        return owner + " has a text animator property that requires 3D semantics not expressible in a text run";
                    if (std::find(seenProperties.begin(), seenProperties.end(), track.property) != seenProperties.end())
                        return owner + " has a duplicate text animator property '" + track.property + "'";
                    seenProperties.push_back(track.property);
                    if (track.loop_mode != "none" || track.spring || track.bezier_curve || track.wiggle ||
                        track.oscillate || track.random_hold || track.posterize_time || track.data_feed ||
                        track.audio_react || track.easing != "linear")
                        return owner + " text animator properties require baked linear keyframes without track drivers";
                    const std::string problem = trackProblem(owner + " text animator property '" +
                                                             track.property + "'", track);
                    if (!problem.empty()) return problem;
                    const std::string valueProblem = textTrackValueProblem(track);
                    if (!valueProblem.empty())
                        return owner + " text animator property '" + track.property + "'" + valueProblem;
                }
            }
            return {};
        }


} // namespace chrononmotion::motion::detail
