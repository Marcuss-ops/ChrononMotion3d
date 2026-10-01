// ChrononMotion — the producer's half of the RenderPlan contract.
//
// `lowerToRenderPlan` decides *what* a plan says; this decides how it is
// written down. The two are kept apart on purpose: a writer that also lowered
// would be a second place an anchor or a singular transform could be accepted
// or rejected, and the lowering is the one that owns that judgement.
//
// Every value here is copied from the plan and nothing is derived from it, so
// the emitted document is a faithful transcription and never a second opinion.
// Anything the schema has no field for is refused by name rather than dropped:
// a plan that loses a mask on the way out renders as a different composition,
// and a file that renders differently from its model is worse than no file.

#include "chrononmotion/motion/RenderPlanEmission.hpp"

#include "JsonWriter.hpp"

#include <chronon3d/render_plan/color_utils.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {

    namespace {

        using chronon3d::render_plan::AnimationKeyframePlan;
        using chronon3d::render_plan::AnimationPlan;
        using chronon3d::render_plan::AnimationTrackPlan;
        using chronon3d::render_plan::CameraPlan;
        using chronon3d::render_plan::CameraPropertyPlan;
        using chronon3d::render_plan::CameraTrackPlan;
        using chronon3d::render_plan::FitMode;
        using chronon3d::render_plan::LayerPlan;
        using chronon3d::render_plan::LayerStylePlan;
        using chronon3d::render_plan::LayerType;
        using chronon3d::render_plan::OutputFormat;
        using chronon3d::render_plan::RenderBudget;
        using chronon3d::render_plan::RenderPlan;

        // ---------------------------------------------------------------- names

        /// The easing vocabulary the plan contract defines. An unknown name is a
        /// write-time problem, not something to hand to the decoder to discover.
        [[nodiscard]] bool isKnownEasing(const std::string& easing) {

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

        [[nodiscard]] const char* layerTypeName(LayerType type) {

            switch (type) {
                case LayerType::Image: return "image";
                case LayerType::Video: return "video";
                case LayerType::Text: return "text";
                case LayerType::Color: return "color";
                case LayerType::Shape: return "shape";
                case LayerType::Particles: return "particles";
                case LayerType::Light: return "light";
                case LayerType::Adjustment: return "adjustment";
                case LayerType::Mesh: return "mesh";
            }
            return "unknown";
        }

        /// The wire spelling of a blend mode. These are exactly the names the
        /// canonical decoder accepts, so a plan this emitter writes round-trips
        /// instead of being silently composited as normal.
        [[nodiscard]] const char* blendModeName(chronon3d::BlendMode mode) {

            switch (mode) {
                case chronon3d::BlendMode::Normal: return "normal";
                case chronon3d::BlendMode::Add: return "add";
                case chronon3d::BlendMode::Multiply: return "multiply";
                case chronon3d::BlendMode::Screen: return "screen";
                case chronon3d::BlendMode::Overlay: return "overlay";
                case chronon3d::BlendMode::Darken: return "darken";
                case chronon3d::BlendMode::Lighten: return "lighten";
                case chronon3d::BlendMode::Difference: return "difference";
                case chronon3d::BlendMode::Exclusion: return "exclusion";
                case chronon3d::BlendMode::SoftLight: return "soft_light";
                case chronon3d::BlendMode::HardLight: return "hard_light";
                case chronon3d::BlendMode::ColorDodge: return "color_dodge";
                case chronon3d::BlendMode::ColorBurn: return "color_burn";
                case chronon3d::BlendMode::Subtract: return "subtract";
                case chronon3d::BlendMode::Divide: return "divide";
                case chronon3d::BlendMode::VividLight: return "vivid_light";
                case chronon3d::BlendMode::PinLight: return "pin_light";
                case chronon3d::BlendMode::LinearLight: return "linear_light";
                case chronon3d::BlendMode::Hue: return "hue";
                case chronon3d::BlendMode::Saturation: return "saturation";
                case chronon3d::BlendMode::Color: return "color";
                case chronon3d::BlendMode::Luminosity: return "luminosity";
            }
            return "normal";
        }

        [[nodiscard]] const char* fitName(FitMode fit) {

            switch (fit) {
                case FitMode::Cover: return "cover";
                case FitMode::Contain: return "contain";
                case FitMode::Stretch: return "stretch";
                case FitMode::None: return "none";
            }
            return "none";
        }

        [[nodiscard]] const char* outputFormatName(OutputFormat format) {

            switch (format) {
                case OutputFormat::Png: return "png";
                case OutputFormat::PngSequence: return "png_sequence";
                case OutputFormat::Gif: return "gif";
                case OutputFormat::Mp4: return "mp4";
                case OutputFormat::Mkv: return "mkv";
                case OutputFormat::WebM: return "webm";
            }
            return "png";
        }

        [[nodiscard]] const char* cameraPropertyName(CameraPropertyPlan property) {

            switch (property) {
                case CameraPropertyPlan::PositionX: return "camera_position_x";
                case CameraPropertyPlan::PositionY: return "camera_position_y";
                case CameraPropertyPlan::PositionZ: return "camera_position_z";
                case CameraPropertyPlan::RotationX: return "camera_rotation_x";
                case CameraPropertyPlan::RotationY: return "camera_rotation_y";
                case CameraPropertyPlan::RotationZ: return "camera_rotation_z";
                case CameraPropertyPlan::FovDeg: return "camera_fov_deg";
                case CameraPropertyPlan::Zoom: return "camera_zoom";
            }
            return "camera_position_z";
        }

        [[nodiscard]] std::string layerOwner(const LayerPlan& layer) {

            return layer.id.empty() ? std::string("a layer") : "layer '" + layer.id + "'";
        }

        // ------------------------------------------------- what cannot be written

        /// Every reason a text style cannot be written down, in the order a
        /// reader would look for them.
        [[nodiscard]] std::string styleProblem(const LayerPlan& layer) {

            if (!layer.style) return {};
            const LayerStylePlan& style = *layer.style;
            const std::string owner = layerOwner(layer);

            if (style.text_fit_mode != "none") {
                return owner + " carries style text_fit_mode '" + style.text_fit_mode +
                       "', which this emitter does not write";
            }
            if (style.min_font_size || style.max_font_size) {
                return owner + " carries an explicit font-size range, which this emitter does not write";
            }
            if (style.stroke) return owner + " carries a text stroke, which this emitter does not write";
            if (style.shadow || !style.shadows.empty()) {
                return owner + " carries a text shadow, which this emitter does not write";
            }
            if (style.backdrop) return owner + " carries a backdrop blur, which this emitter does not write";
            if (style.background) return owner + " carries a text background, which this emitter does not write";
            return {};
        }

        /// Everything a track may carry that the lowering bakes away instead of
        /// publishing: a baked track is linear samples with no per-key timing and
        /// no driver, so one of these showing up means the plan came from
        /// somewhere this writer has not been taught.
        [[nodiscard]] std::string trackProblem(const std::string& owner, const AnimationTrackPlan& track) {

            if (!isKnownEasing(track.easing)) {
                return owner + " declares easing '" + track.easing + "', which the plan contract does not define";
            }
            if (track.bezier_curve) return owner + " carries a track-level bezier curve";
            if (track.spring) return owner + " carries a track spring";
            if (track.loop_mode != "none") return owner + " carries loop mode '" + track.loop_mode + "'";
            if (track.wiggle || track.oscillate || track.random_hold || track.posterize_time ||
                track.data_feed || track.audio_react) {
                return owner + " carries an animation driver, which the lowering bakes into samples";
            }
            for (const AnimationKeyframePlan& key : track.keyframes) {
                if (key.easing || key.interpolation || key.easing_xy || key.bezier_curve || key.spring ||
                    key.roving || key.temporal_in_dx || key.temporal_in_dy ||
                    key.temporal_out_dx || key.temporal_out_dy) {
                    return owner + " carries per-key timing overrides, which the lowering bakes into samples";
                }
            }
            return {};
        }

        [[nodiscard]] std::string animationProblem(const std::string& owner, const AnimationPlan& animation) {

            for (const AnimationTrackPlan& track : animation.tracks) {
                const std::string problem = trackProblem(owner + " track '" + track.property + "'", track);
                if (!problem.empty()) return problem;
            }
            return {};
        }

        [[nodiscard]] bool oneOf(const std::string& value,
                                 std::initializer_list<std::string_view> choices) {
            return std::find(choices.begin(), choices.end(), value) != choices.end();
        }

        [[nodiscard]] std::optional<std::string> textSpanColorHex(const chronon3d::Color& color);

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
                        (selector.order == "index" && (selector.index_step == 0 || selector.index_step > 1'000'000)))
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

        [[nodiscard]] std::string layerProblem(const LayerPlan& layer, const std::string& schema) {

            if (layer.id.empty()) return "a layer has no id";

            const std::string owner = layerOwner(layer);

            switch (layer.type) {
                case LayerType::Color:
                case LayerType::Image:
                case LayerType::Video:
                case LayerType::Text:
                    break;
                default:
                    return owner + " is a '" + layerTypeName(layer.type) +
                           "' layer, which this emitter does not write";
            }

            if (layer.text_spans.size() > 65'536 || layer.text_animators.size() > 65'536)
                return owner + " exceeds the RenderPlan v3 text span or animator count limit";

            if (layer.type == LayerType::Image && layer.asset.empty()) {
                return owner + " is an image layer with no asset";
            }
            if (layer.type == LayerType::Video && layer.source.empty()) {
                return owner + " is a video layer with no source";
            }
            if (layer.type == LayerType::Text && !layer.style) {
                return owner + " is a text layer with no style, which the plan contract requires";
            }

            if (layer.parent) return owner + " is parented; the plan contract already flattens hierarchy";
            if (layer.track_matte) return owner + " carries a track matte";
            if (layer.time_remap) {
                if (schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                    return owner + " carries a time remap, which requires RenderPlan v3";
                if (layer.type != LayerType::Video)
                    return owner + " carries a time remap, which is supported only for video layers";
                const auto& remap = *layer.time_remap;
                if (remap.speed && (!std::isfinite(*remap.speed) || *remap.speed <= 0.f || *remap.speed > 1000.f))
                    return owner + " carries an invalid time-remap speed";
                if (remap.freeze_frame && (remap.freeze_frame->integral() < 0 ||
                                           remap.freeze_frame->integral() > 1'000'000))
                    return owner + " carries an out-of-range time-remap freeze frame";
                if (remap.freeze_frame && remap.animation)
                    return owner + " carries both a time-remap freeze frame and animation";
                if (remap.animation) {
                    const AnimationTrackPlan& track = *remap.animation;
                    const std::string problem = trackProblem(owner + " time-remap animation", track);
                    if (!problem.empty()) return problem;
                    if (track.keyframes.empty()) return owner + " carries an empty time-remap animation";
                    for (const AnimationKeyframePlan& key : track.keyframes) {
                        if (key.frame.integral() < 0 || key.frame.integral() > 1'000'000 ||
                            key.value.size() != 1 || !std::isfinite(key.value.front()) ||
                            key.value.front() < 0.f || key.value.front() > 1'000'000.f)
                            return owner + " carries an invalid time-remap keyframe";
                    }
                }
            }
            if (!layer.text_spans.empty() && schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                return owner + " carries text spans, which require RenderPlan v3";
            std::size_t previousTextSpanEnd = 0;
            std::vector<std::string> semanticIds;
            const auto isUtf8Boundary = [&](std::size_t offset) {
                return offset == 0 || offset == layer.text.size() ||
                    (offset < layer.text.size() &&
                     (static_cast<unsigned char>(layer.text[offset]) & 0xc0U) != 0x80U);
            };
            for (const auto& span : layer.text_spans) {
                if (layer.type != LayerType::Text)
                    return owner + " carries text spans but is not a text layer";
                if (span.byte_start < previousTextSpanEnd || span.byte_start >= span.byte_end ||
                    span.byte_end > layer.text.size() || !isUtf8Boundary(span.byte_start) ||
                    !isUtf8Boundary(span.byte_end))
                    return owner + " carries overlapping, unsorted, or out-of-range UTF-8 text spans";
                previousTextSpanEnd = span.byte_end;
                if (span.font || span.font_size || span.baseline_shift)
                    return owner + " carries a text span font, absolute size, or baseline override that V3 does not serialize";
                if (span.tracking && (!std::isfinite(*span.tracking) ||
                                      *span.tracking < -1000.f || *span.tracking > 1000.f))
                    return owner + " carries text span tracking outside [-1000, 1000]";
            if (span.highlight && !span.highlight->background &&
                !span.highlight->padding_em && !span.highlight->radius_em)
                return owner + " carries an empty text highlight style";
            if (span.font_size_multiplier && (!std::isfinite(*span.font_size_multiplier) ||
                    *span.font_size_multiplier <= 0.f || *span.font_size_multiplier > 100.f))
                    return owner + " carries a text span font-size multiplier outside (0, 100]";
                if (span.semantic_id && (span.semantic_id->empty() || span.semantic_id->size() > 256))
                    return owner + " carries a text span semantic_id outside the V3 bounds";
                if (span.semantic_id) {
                    if (std::find(semanticIds.begin(), semanticIds.end(), *span.semantic_id) != semanticIds.end())
                        return owner + " carries duplicate text span semantic_id '" + *span.semantic_id + "'";
                    semanticIds.push_back(*span.semantic_id);
                }
                const auto validColor = [](const std::optional<chronon3d::Color>& color) {
                    return !color || (std::isfinite(color->r) && std::isfinite(color->g) &&
                        std::isfinite(color->b) && std::isfinite(color->a) &&
                        color->r >= 0.f && color->r <= 1.f && color->g >= 0.f && color->g <= 1.f &&
                        color->b >= 0.f && color->b <= 1.f && color->a >= 0.f && color->a <= 1.f);
                };
                const auto representableColor = [](const std::optional<chronon3d::Color>& color) {
                    return !color || textSpanColorHex(*color).has_value();
                };
                if (!validColor(span.color) || !representableColor(span.color))
                    return owner + " carries a non-finite, out-of-range, or non-serializable text span color";
                if (span.stroke) {
                    if (!validColor(span.stroke->color) || !representableColor(span.stroke->color))
                        return owner + " carries an invalid or non-serializable text span stroke color";
                    if (!std::isfinite(span.stroke->width_em) || span.stroke->width_em < 0.f || span.stroke->width_em > 100.f)
                        return owner + " carries a text span stroke width outside [0, 100]";
                    if (span.stroke->dash_period_em && *span.stroke->dash_period_em != 0.f)
                        return owner + " carries a text span dash pattern that V3 does not serialize";
                }
                if (span.highlight) {
                    if (!span.highlight->background && !span.highlight->padding_em && !span.highlight->radius_em)
                        return owner + " carries an empty text highlight style";
                    if (!validColor(span.highlight->background) || !representableColor(span.highlight->background))
                        return owner + " carries an invalid or non-serializable text highlight color";
                    for (const auto value : {span.highlight->padding_em, span.highlight->radius_em})
                        if (value && (!std::isfinite(*value) || *value < 0.f || *value > 100.f))
                            return owner + " carries text highlight geometry outside [0, 100] em";
                }
            }
            if (!layer.font.empty()) {
                return owner + " carries a normalized font path, which the plan derives from style.font";
            }
            if (layer.motion_blur) return owner + " carries analytic motion blur";
            if (layer.shape) return owner + " carries shape content";
            if (layer.inline_mesh) return owner + " carries an inline mesh";
            if (layer.particle_emitter) return owner + " carries a particle emitter";
            if (layer.light) return owner + " carries a light layer";
            if (layer.path_mask || !layer.masks.empty()) return owner + " carries a mask";
            if (!layer.effects.empty() || !layer.effect_param_tracks.empty()) return owner + " carries an effect";
            if (layer.flow_layout || layer.grid_layout) return owner + " carries a layout group";
            if (layer.follow_path) return owner + " carries a follow-path driver";
            // The plan contract has no layer-level auto-orient: `auto_orient`
            // exists only inside a follow-path driver, which this emitter
            // already refuses above. Writing it on the layer itself made every
            // emitted plan fail the consumer's schema (additionalProperties:
            // false), so a Motion scene could be lowered and never rendered.
            if (layer.auto_orient) return owner + " carries an auto-orient flag, which the plan expresses only inside a follow-path driver";
            if (layer.skew_angle_deg != 0.f || layer.skew_axis_deg != 0.f) return owner + " carries a skew";
            if (layer.transition_in || layer.transition_out) return owner + " carries a transition";
            if (layer.fill_gradient_animation) return owner + " carries a gradient animation";
            const std::string textAnimators = textAnimatorProblem(layer, schema);
            if (!textAnimators.empty()) return textAnimators;
            if (layer.safe_insets) {
                if (schema != chronon3d::render_plan::kRenderPlanSchemaV3)
                    return owner + " carries safe insets, which require RenderPlan v3";
                for (const float inset : *layer.safe_insets)
                    if (!std::isfinite(inset) || inset < 0.f || inset > 1'000'000.f)
                        return owner + " carries safe insets outside the RenderPlan v3 bounds";
            }
            if (layer.size_dimensions == 3) {
                return owner + " has a three-component size, which the plan contract cannot express";
            }
            if (layer.position_dimensions != 0 && layer.position_dimensions < 2) {
                return owner + " has a single-component position, which the plan contract cannot express";
            }
            if (layer.scale_dimensions == 1) {
                return owner + " has a single-component scale, which the plan contract cannot express";
            }

            const std::string style = styleProblem(layer);
            if (!style.empty()) return style;

            if (layer.animation) {
                const std::string animation = animationProblem(owner + " animation", *layer.animation);
                if (!animation.empty()) return animation;
            }
            return {};
        }

        /// The budget fields a plan can carry. `max_temporal_pixels` has a schema
        /// field; the rest are resolution ceilings with no plan representation, so
        /// a caller that lowered them has nowhere to put them.
        [[nodiscard]] std::string cameraProblem(const CameraPlan& camera) {

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
            return {};
        }

        [[nodiscard]] std::string budgetProblem(const RenderPlan& plan) {

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

        [[nodiscard]] std::string planProblem(const RenderPlan& plan) {

            if (plan.job_id.empty()) return "the plan has no job id";
            if (plan.canvas.width <= 0 || plan.canvas.height <= 0) return "the plan has an empty canvas";
            if (plan.canvas.duration.integral() <= 0) return "the plan has no duration";
            if (plan.output.path.empty()) return "the plan has no output path";
            if (plan.schema != chronon3d::render_plan::kRenderPlanSchemaV2 &&
                plan.schema != chronon3d::render_plan::kRenderPlanSchemaV3) {
                return "the plan declares schema '" + plan.schema + "', which this emitter does not write";
            }
            if (plan.content_fingerprint != 0) {
                return "the plan carries a content fingerprint, which the plan contract cannot express";
            }
            if (!plan.warnings.empty()) {
                return "the plan carries warnings, which the plan contract cannot express";
            }
            if (plan.temporal_motion_blur) return "the plan carries scene motion blur";
            if (plan.camera) {
                const std::string camera = cameraProblem(*plan.camera);
                if (!camera.empty()) return camera;
            }
            if (plan.camera_shake) return "the plan carries camera shake";
            if (plan.lighting_rig) return "the plan carries a lighting rig";
            if (plan.audio) return "the plan carries an audio mix";
            if (plan.prepared_overlay_package) return "the plan carries a prepared overlay package";

            const std::string budget = budgetProblem(plan);
            if (!budget.empty()) return budget;

            if (plan.camera_animation) {
                for (const CameraTrackPlan& track : *plan.camera_animation) {
                    if (!isKnownEasing(track.easing)) {
                        return "camera track '" + std::string(cameraPropertyName(track.property)) +
                               "' declares easing '" + track.easing +
                               "', which the plan contract does not define";
                    }
                    if (track.bezier_curve) return "a camera track carries a bezier curve";
                    for (const AnimationKeyframePlan& key : track.keyframes) {
                        if (key.easing || key.interpolation || key.easing_xy || key.bezier_curve || key.spring ||
                            key.roving || key.temporal_in_dx || key.temporal_in_dy ||
                            key.temporal_out_dx || key.temporal_out_dy) {
                            return "a camera track carries per-key timing overrides";
                        }
                    }
                }
            }

            for (const LayerPlan& layer : plan.layers) {
                const std::string problem = layerProblem(layer, plan.schema);
                if (!problem.empty()) return problem;
            }

            if (plan.output.codec != chronon3d::render_plan::VideoCodec::Auto ||
                plan.output.rate_control != chronon3d::render_plan::RateControlMode::Crf ||
                plan.output.bitrate != 0 || plan.output.crf != 0 || plan.output.qp != -1 ||
                !plan.output.profile_id.empty()) {
                return "the plan carries encoder settings, which belong to the render invocation";
            }
            return {};
        }

        // ---------------------------------------------------------------- writes

        void writeFloats(detail::Writer& writer, const float* values, std::size_t count) {

            writer.beginArray();
            for (std::size_t i = 0; i < count; ++i) {
                writer.item();
                writer.number(values[i]);
            }
            writer.endArray();
        }

        template<std::size_t N>
        void writeFloats(detail::Writer& writer, const std::array<float, N>& values, std::size_t count) {

            writeFloats(writer, values.data(), count);
        }

        void writeKeyframes(detail::Writer& writer, const std::vector<AnimationKeyframePlan>& keyframes,
                            bool scalar) {

            writer.beginArray();
            for (const AnimationKeyframePlan& key : keyframes) {
                writer.item();
                writer.beginObject();
                writer.key("frame");
                writer.integer(key.frame.integral());
                writer.key("value");
                if (scalar) {
                    // A camera channel is one number; a layer channel is the
                    // property's own arity. An empty value is the channel's zero,
                    // which is what the decoder would have produced for it.
                    writer.number(key.value.empty() ? 0.f : key.value.front());
                } else {
                    writeFloats(writer, key.value.data(), key.value.size());
                }
                writer.endObject();
            }
            writer.endArray();
        }

        [[nodiscard]] std::optional<std::string> textSpanColorHex(const chronon3d::Color& color) {
            if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b) ||
                !std::isfinite(color.a) || color.r < 0.f || color.r > 1.f ||
                color.g < 0.f || color.g > 1.f || color.b < 0.f || color.b > 1.f ||
                color.a < 0.f || color.a > 1.f)
                return std::nullopt;
            const auto quantize = [](float linear) {
                return static_cast<unsigned int>(std::lround(
                    chronon3d::render_plan::linear_to_srgb(linear) * 255.f));
            };
            const unsigned int channels[] = {quantize(color.r), quantize(color.g),
                quantize(color.b), static_cast<unsigned int>(std::lround(color.a * 255.f))};
            constexpr char digits[] = "0123456789ABCDEF";
            std::string result{"#"};
            for (const unsigned int channel : channels) {
                result.push_back(digits[(channel >> 4U) & 0x0fU]);
                result.push_back(digits[channel & 0x0fU]);
            }
            const auto parseChannel = [](char high, char low) {
                const auto nibble = [](char value) -> unsigned int {
                    if (value >= '0' && value <= '9') return static_cast<unsigned int>(value - '0');
                    if (value >= 'A' && value <= 'F') return static_cast<unsigned int>(value - 'A' + 10);
                    return static_cast<unsigned int>(value - 'a' + 10);
                };
                return static_cast<float>((nibble(high) * 16U + nibble(low)) / 255.0);
            };
            const float decoded[] = {
                chronon3d::render_plan::srgb_to_linear(parseChannel(result[1], result[2])),
                chronon3d::render_plan::srgb_to_linear(parseChannel(result[3], result[4])),
                chronon3d::render_plan::srgb_to_linear(parseChannel(result[5], result[6])),
                parseChannel(result[7], result[8])};
            if (std::abs(decoded[0] - color.r) > 1.0e-6f ||
                std::abs(decoded[1] - color.g) > 1.0e-6f ||
                std::abs(decoded[2] - color.b) > 1.0e-6f ||
                std::abs(decoded[3] - color.a) > 1.0e-6f)
                return std::nullopt;
            return result;
        }

        void writeTextSpans(detail::Writer& writer,
                            const std::vector<chronon3d::TextSpanOverride>& spans) {
            writer.beginArray();
            for (const auto& span : spans) {
                writer.item();
                writer.beginObject();
                writer.key("start"); writer.unsignedInteger(span.byte_start);
                writer.key("end"); writer.unsignedInteger(span.byte_end);
                if (span.semantic_id) { writer.key("semantic_id"); writer.text(*span.semantic_id); }
                const bool hasStyle = span.color || span.font_size_multiplier || span.tracking ||
                                      span.stroke || span.highlight;
                if (hasStyle) { writer.key("style"); writer.beginObject(); }
                if (span.color) {
                    writer.key("color");
                    writer.text(*textSpanColorHex(*span.color));
                }
                if (span.font_size_multiplier) { writer.key("font_size_mult"); writer.number(*span.font_size_multiplier); }
                if (span.tracking) { writer.key("tracking"); writer.number(*span.tracking); }
                if (span.stroke) {
                    writer.key("stroke"); writer.beginObject();
                    if (span.stroke->color) {
                        writer.key("color"); writer.text(*textSpanColorHex(*span.stroke->color));
                    }
                    writer.key("width_em"); writer.number(span.stroke->width_em);
                    writer.endObject();
                }
                if (span.highlight) {
                    writer.key("highlight"); writer.beginObject();
                    if (span.highlight->background) {
                        writer.key("background"); writer.text(*textSpanColorHex(*span.highlight->background));
                    }
                    if (span.highlight->padding_em) { writer.key("padding_em"); writer.number(*span.highlight->padding_em); }
                    if (span.highlight->radius_em) { writer.key("radius_em"); writer.number(*span.highlight->radius_em); }
                    writer.endObject();
                }
                if (hasStyle) writer.endObject();
                writer.endObject();
            }
            writer.endArray();
        }

        void writeTextSelectorTrack(detail::Writer& writer, const AnimationTrackPlan& track) {
            writer.beginObject();
            writer.key("easing");
            writer.text(track.easing);
            writer.key("keyframes");
            writeKeyframes(writer, track.keyframes, true);
            writer.endObject();
        }

        void writeTextAnimators(detail::Writer& writer,
                                const std::vector<chronon3d::render_plan::TextAnimatorPlan>& animators) {
            writer.beginArray();
            for (const auto& animator : animators) {
                writer.item();
                writer.beginObject();
                if (!animator.id.empty()) {
                    writer.key("id");
                    writer.text(animator.id);
                }
                writer.key("selectors");
                writer.beginArray();
                for (const auto& selector : animator.selectors) {
                    writer.item();
                    writer.beginObject();
                    if (!selector.id.empty()) { writer.key("id"); writer.text(selector.id); }
                    if (selector.semantic_id) { writer.key("semantic_id"); writer.text(*selector.semantic_id); }
                    writer.key("unit"); writer.text(selector.unit);
                    writer.key("shape"); writer.text(selector.shape);
                    writer.key("order"); writer.text(selector.order);
                    writer.key("combine"); writer.text(selector.combine);
                    if (!selector.start.keyframes.empty()) {
                        writer.key("start"); writeTextSelectorTrack(writer, selector.start);
                    }
                    if (!selector.end.keyframes.empty()) {
                        writer.key("end"); writeTextSelectorTrack(writer, selector.end);
                    }
                    if (!selector.offset.keyframes.empty()) {
                        writer.key("offset"); writeTextSelectorTrack(writer, selector.offset);
                    }
                    if (!selector.amount.keyframes.empty()) {
                        writer.key("amount"); writeTextSelectorTrack(writer, selector.amount);
                    }
                    writer.key("exclude_spaces"); writer.boolean(selector.exclude_spaces);
                    writer.key("randomize_order"); writer.boolean(selector.randomize_order);
                    if (selector.random_seed != 0) { writer.key("random_seed"); writer.unsignedInteger(selector.random_seed); }
                    if (selector.shape == "wiggly") {
                        writer.key("wiggly_frequency"); writer.number(selector.wiggly_frequency);
                        writer.key("wiggly_amplitude"); writer.number(selector.wiggly_amplitude);
                        if (selector.wiggly_seed != 0) { writer.key("wiggly_seed"); writer.unsignedInteger(selector.wiggly_seed); }
                    }
                    if (selector.shape == "noise") {
                        writer.key("noise_frequency"); writer.number(selector.noise_frequency);
                        writer.key("noise_amplitude"); writer.number(selector.noise_amplitude);
                        if (selector.noise_seed != 0) { writer.key("noise_seed"); writer.unsignedInteger(selector.noise_seed); }
                    }
                    if (selector.order == "index") { writer.key("index_step"); writer.unsignedInteger(selector.index_step); }
                    writer.endObject();
                }
                writer.endArray();
                writer.key("properties");
                writer.beginArray();
                for (const auto& track : animator.properties) {
                    writer.item();
                    writer.beginObject();
                    writer.key("property"); writer.text(track.property);
                    writer.key("easing"); writer.text(track.easing);
                    writer.key("keyframes");
                    writeKeyframes(writer, track.keyframes, false);
                    writer.endObject();
                }
                writer.endArray();
                writer.endObject();
            }
            writer.endArray();
        }

        void writeAnimation(detail::Writer& writer, const AnimationPlan& animation) {

            writer.beginObject();
            writer.key("tracks");
            writer.beginArray();
            for (const AnimationTrackPlan& track : animation.tracks) {
                writer.item();
                writer.beginObject();
                writer.key("property");
                writer.text(track.property);
                writer.key("easing");
                writer.text(track.easing);
                writer.key("keyframes");
                writeKeyframes(writer, track.keyframes, false);
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
        }

        void writeStyle(detail::Writer& writer, const LayerStylePlan& style) {

            writer.beginObject();
            writer.key("font");
            writer.text(style.font);
            if (style.font_size) {
                writer.key("font_size");
                writer.number(*style.font_size);
            }
            writer.key("fill");
            writer.text(style.fill);
            if (style.glow) {
                writer.key("glow");
                writer.beginObject();
                writer.key("radius");
                writer.number(style.glow->radius);
                writer.key("intensity");
                writer.number(style.glow->intensity);
                writer.key("color");
                writer.text(style.glow->color);
                writer.endObject();
            }
            writer.endObject();
        }

        void writeLayer(detail::Writer& writer, const LayerPlan& layer) {

            writer.beginObject();

            writer.key("id");
            writer.text(layer.id);
            writer.key("type");
            writer.token(layerTypeName(layer.type));

            if (!layer.asset.empty()) {
                writer.key("asset");
                writer.text(layer.asset);
            }
            if (!layer.source.empty()) {
                writer.key("source");
                writer.text(layer.source);
            }

            writer.key("color");
            writeFloats(writer, layer.color, layer.color.size());

            if (layer.size_dimensions != 0) {
                writer.key("size");
                writeFloats(writer, layer.size, layer.size_dimensions);
            }
            if (layer.position_dimensions != 0) {
                writer.key("position");
                writeFloats(writer, layer.position, layer.position_dimensions);
            }
            if (layer.scale_dimensions != 0) {
                writer.key("scale");
                writeFloats(writer, layer.scale, layer.scale_dimensions);
            }
            if (layer.rotation_dimensions != 0) {
                writer.key("rotation");
                writeFloats(writer, layer.rotation, layer.rotation_dimensions);
            }

            writer.key("enable_3d");
            writer.boolean(layer.enable_3d);
            writer.key("screen_space");
            writer.boolean(layer.screen_space);
            writer.key("loop");
            writer.boolean(layer.loop);

            if (layer.blend_mode) {
                writer.key("blend_mode");
                writer.token(blendModeName(*layer.blend_mode));
            }
            if (layer.opacity) {
                writer.key("opacity");
                writer.number(*layer.opacity);
            }
            if (layer.start_frame) {
                writer.key("start_frame");
                writer.integer(layer.start_frame->integral());
            }
            if (layer.duration_frames) {
                writer.key("duration_frames");
                writer.integer(layer.duration_frames->integral());
            }
            if (layer.depth_role) {
                writer.key("depth_role");
                writer.text(*layer.depth_role);
            }
            if (layer.depth_offset != 0.f) {
                writer.key("depth_offset");
                writer.number(layer.depth_offset);
            }
            if (layer.safe_insets) {
                writer.key("safe_insets");
                writer.beginObject();
                writer.key("top");
                writer.number((*layer.safe_insets)[0]);
                writer.key("right");
                writer.number((*layer.safe_insets)[1]);
                writer.key("bottom");
                writer.number((*layer.safe_insets)[2]);
                writer.key("left");
                writer.number((*layer.safe_insets)[3]);
                writer.endObject();
            }
            if (layer.radius) {
                writer.key("radius");
                writer.number(*layer.radius);
            }
            if (layer.fit) {
                writer.key("fit");
                writer.token(fitName(*layer.fit));
            }

            if (layer.type == LayerType::Text) {
                writer.key("text");
                writer.text(layer.text);
                writer.key("style");
                writeStyle(writer, *layer.style);
            }
            if (!layer.text_spans.empty()) {
                writer.key("spans");
                writeTextSpans(writer, layer.text_spans);
            }
            if (!layer.text_animators.empty()) {
                writer.key("text_animators");
                writeTextAnimators(writer, layer.text_animators);
            }
            if (layer.animation) {
                writer.key("animation");
                writeAnimation(writer, *layer.animation);
            }
            if (layer.time_remap) {
                writer.key("time_remap");
                const auto& remap = *layer.time_remap;
                writer.beginObject();
                if (remap.speed) {
                    writer.key("speed");
                    writer.number(*remap.speed);
                }
                if (remap.reverse) {
                    writer.key("reverse");
                    writer.boolean(true);
                }
                if (remap.freeze_frame) {
                    writer.key("freeze_frame");
                    writer.integer(remap.freeze_frame->integral());
                }
                if (remap.animation) {
                    writer.key("animation");
                    writer.beginObject();
                    writer.key("easing");
                    writer.text(remap.animation->easing);
                    writer.key("keyframes");
                    writeKeyframes(writer, remap.animation->keyframes, true);
                    writer.endObject();
                }
                writer.endObject();
            }

            writer.endObject();
        }

        void writeCamera(detail::Writer& writer, const CameraPlan& camera) {

            writer.beginObject();
            writer.key("type");
            writer.text(camera.type);
            writer.key("position");
            writeFloats(writer, camera.position, camera.position.size());
            writer.key("rotation_deg");
            writeFloats(writer, camera.rotation_deg, camera.rotation_deg.size());
            writer.key("fov_deg");
            writer.number(camera.fov_deg);
            writer.key("near");
            writer.number(camera.near);
            writer.key("far");
            writer.number(camera.far);
            writer.key("zoom");
            writer.number(camera.zoom);
            const CameraPlan defaults{};
            if (camera.dof_enabled || camera.dof_focus_distance != defaults.dof_focus_distance ||
                camera.dof_aperture != defaults.dof_aperture || camera.dof_max_blur != defaults.dof_max_blur) {
                writer.key("dof");
                writer.beginObject();
                writer.key("enabled");
                writer.boolean(camera.dof_enabled);
                writer.key("focus_distance");
                writer.number(camera.dof_focus_distance);
                writer.key("aperture");
                writer.number(camera.dof_aperture);
                writer.key("max_blur");
                writer.number(camera.dof_max_blur);
                writer.endObject();
            }
            writer.endObject();
        }

        void writeCameraAnimation(detail::Writer& writer, const std::vector<CameraTrackPlan>& tracks) {

            writer.beginObject();
            writer.key("tracks");
            writer.beginArray();
            for (const CameraTrackPlan& track : tracks) {
                writer.item();
                writer.beginObject();
                writer.key("property");
                writer.token(cameraPropertyName(track.property));
                writer.key("easing");
                writer.text(track.easing);
                writer.key("keyframes");
                writeKeyframes(writer, track.keyframes, true);
                writer.endObject();
            }
            writer.endArray();
            writer.endObject();
        }

    }// namespace

    std::string renderPlanEmissionProblem(const chronon3d::render_plan::RenderPlan& plan) {

        return planProblem(plan);
    }

    std::string renderPlanToJson(const chronon3d::render_plan::RenderPlan& plan,
                                 const RenderPlanEmissionOptions& options) {

        const std::string problem = planProblem(plan);
        if (!problem.empty()) throw std::invalid_argument("renderPlanToJson: " + problem);

        detail::Writer writer(options.indentSpaces);

        writer.beginObject();

        writer.key("schema");
        writer.text(plan.schema);
        writer.key("version");
        writer.integer(plan.schema == chronon3d::render_plan::kRenderPlanSchemaV3 ? 3 : 2);
        writer.key("job_id");
        writer.text(plan.job_id);

        writer.key("canvas");
        writer.beginObject();
        writer.key("width");
        writer.integer(plan.canvas.width);
        writer.key("height");
        writer.integer(plan.canvas.height);
        writer.key("fps_num");
        writer.integer(plan.canvas.fps.numerator);
        writer.key("fps_den");
        writer.integer(plan.canvas.fps.denominator);
        writer.key("duration_frames");
        writer.integer(plan.canvas.duration.integral());
        writer.endObject();

        // Only when the caller expressed one: the tree's default is "no opinion",
        // and writing it would lower a resolver ceiling nobody asked to lower.
        const RenderBudget defaultBudget{};
        if (plan.budget.max_temporal_pixels != defaultBudget.max_temporal_pixels) {
            writer.key("budget");
            writer.beginObject();
            writer.key("max_temporal_pixels");
            writer.unsignedInteger(plan.budget.max_temporal_pixels);
            writer.endObject();
        }

        if (plan.camera) {
            writer.key("camera");
            writeCamera(writer, *plan.camera);
        }
        if (plan.camera_animation) {
            writer.key("camera_animation");
            writeCameraAnimation(writer, *plan.camera_animation);
        }

        writer.key("layers");
        writer.beginArray();
        for (const LayerPlan& layer : plan.layers) {
            writer.item();
            writeLayer(writer, layer);
        }
        writer.endArray();

        writer.key("output");
        writer.beginObject();
        writer.key("path");
        writer.text(plan.output.path);
        writer.key("format");
        writer.token(outputFormatName(plan.output.format));
        writer.endObject();

        writer.endObject();

        return writer.take();
    }

}// namespace chrononmotion::motion
