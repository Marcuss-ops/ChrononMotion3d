// ChrononMotion — Chronon3D V3 bridge drift regression.
//
// Pins the producer/consumer contract that both RenderPlan writers have to
// honour, so the two of them cannot drift apart silently.
//
// Part 1 — writer drift (always runs; needs only the adapter):
//   * `renderPlanToJson` writes the fields `lowerToRenderPlan` produced —
//     masks, track matte and effect parameter tracks — and writes all of them;
//   * writing is byte-reproducible;
//   * what this writer cannot express is refused up front by
//     `renderPlanEmissionProblem` / `renderPlanToJson` instead of being
//     silently dropped.
//
// Part 2 — canonical consumer round-trip (needs Chronon3D's compiled
// `validate_render_plan` / `decode_render_plan`, which live in the
// `chronon3d_render_plan` OBJECT library and are not reachable from the
// standalone ChrononMotion build):
//   * every key written exists in the consumer schema, so the document passes
//     `validate_render_plan` (`additionalProperties: false` — a stray key would
//     cost the whole clip);
//   * nothing is lost on the way through the canonical decoder.
//
// Part 2 is compiled when `CHRONONMOTION_HAS_CHRONON3D_CONSUMER` is defined,
// which ChrononMotion's CMake does only where that target exists. When it is
// absent the run reports it as NOT RUN rather than passing quietly.
//
// Written against the dependency-free motion_check.hpp harness (not doctest):
// the suite adds no test framework, so this file has to link with the rest of
// it.

#include "motion_check.hpp"

#include <chrononmotion/motion/RenderPlanEmission.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/graphics/pattern_tile.hpp>
#include <nlohmann/json.hpp>

#include <memory>

#ifdef CHRONONMOTION_HAS_CHRONON3D_CONSUMER
#include <chronon3d/render_plan/render_plan_validator.hpp>
#endif

#include <cstdio>
#include <stdexcept>
#include <string>

using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerStylePlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::RenderPlan;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::section;

namespace {

chronon3d::render_plan::AnimationKeyframePlan key(chronon3d::Frame frame, float value) {
    chronon3d::render_plan::AnimationKeyframePlan k{};
    k.frame = frame;
    k.value = {value};
    return k;
}

RenderPlan makeBridgePlan() {
    RenderPlan plan;
    plan.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    plan.job_id = "bridge-features";
    plan.canvas.width = 640;
    plan.canvas.height = 360;
    plan.canvas.fps = chronon3d::FrameRate{30, 1};
    plan.canvas.duration = chronon3d::Frame{12};
    plan.output.path = "/tmp/chrononmotion_bridge_features.png";

    LayerPlan image;
    image.type = LayerType::Image;
    image.id = "bridge-layer";
    image.asset = "bridge-image";
    image.size = {320.f, 180.f};
    image.size_dimensions = 2;
    image.casts_shadows = true;
    image.accepts_shadows = false;
    image.shadow_blur = 12.f;
    image.shadow_opacity = 0.65f;
    LayerPlan::PathMaskPlan mask;
    mask.type = "rounded_rect";
    mask.mode = "intersect";
    mask.position = {12.f, 18.f};
    mask.size = {320.f, 180.f};
    mask.radius = 14.f;
    mask.opacity_track = chronon3d::render_plan::AnimationTrackPlan{};
    mask.opacity_track->property = "mask_opacity";
    mask.opacity_track->easing = "linear";
    mask.opacity_track->keyframes = {key(chronon3d::Frame{0}, 0.f), key(chronon3d::Frame{8}, 1.f)};
    image.masks.push_back(mask);
    image.track_matte = chronon3d::render_plan::TrackMattePlan{
        "matte-layer", chronon3d::render_plan::TrackMatteTypePlan::AlphaInverted};
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::Catalog;
    effect.canonical_effect_id = "color.saturation";
    effect.canonical_params.push_back({"value", 1.f});
    image.effects.push_back(effect);
    effect.canonical_effect_id = "color.contrast";
    image.effects.push_back(effect);
    LayerPlan::EffectParamTrackPlan saturation;
    saturation.effect_id = "color.saturation";
    saturation.param = "value";
    saturation.track.property = "opacity";
    saturation.track.easing = "linear";
    saturation.track.keyframes = {key(chronon3d::Frame{0}, 0.7f), key(chronon3d::Frame{8}, 1.2f)};
    LayerPlan::EffectParamTrackPlan exposure;
    exposure.effect_id = "color.contrast";
    exposure.param = "value";
    exposure.track.property = "opacity";
    exposure.track.easing = "linear";
    exposure.track.keyframes = {key(chronon3d::Frame{0}, 0.f), key(chronon3d::Frame{8}, 1.f)};
    image.effect_param_tracks.push_back(saturation);
    image.effect_param_tracks.push_back(exposure);

    LayerPlan matte;
    matte.type = LayerType::Image;
    matte.id = "matte-layer";
    matte.asset = "matte-image";
    matte.size = {320.f, 180.f};
    matte.size_dimensions = 2;
    plan.layers = {image, matte};
    return plan;
}

// A text plan exercising every style property the bridge writes. Kept separate
// from makeBridgePlan so the mask/effect assertions above keep stable indices.
RenderPlan makeBridgeTextPlan() {
    RenderPlan plan;
    plan.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    plan.job_id = "bridge-text-style";
    plan.canvas.width = 640;
    plan.canvas.height = 360;
    plan.canvas.fps = chronon3d::FrameRate{30, 1};
    plan.canvas.duration = chronon3d::Frame{12};
    plan.output.path = "/tmp/chrononmotion_bridge_text_style.png";

    LayerPlan text;
    text.type = LayerType::Text;
    text.id = "bridge-text";
    text.text = "CHRONON 3D";
    LayerStylePlan style;
    style.font = "bridge-font";
    style.font_family = "Inter";
    style.font_weight = 700;
    style.font_style = "italic";
    style.variation_axes = "wght=650,wdth=92";
    style.text_direction = "rtl";
    style.text_align = "left";
    style.font_size = 48.f;
    style.tracking = 2.f;
    style.fill = "#FFFFFF";
    style.quality = "high";
    style.shadow_quality = "canonical";
    style.stroke_mode = "distance";
    style.gel_surface = true;
    style.gel_core = true;
    style.gel_soft_fusion = true;
    style.gel_debug_mode = 3;
    style.gel_transmission_enabled = false;
    style.gel_material_profile = "reference_20261107";
    style.gel_light_azimuth_radians = 0.5f;
    style.gel_light_elevation = 0.25f;
    style.glow = chronon3d::render_plan::GlowStyle{18.0f, 0.6f, "#22CCFF"};
    chronon3d::render_plan::TextCaretPlan caret;
    caret.byte_index = 4;
    caret.width = 3.f;
    caret.height = 48.f;
    caret.color = "#FFCC00";
    caret.opacity = 0.8f;
    caret.blink_period_frames = 30;
    caret.blink_duty = 0.4f;
    style.text_caret = caret;
    text.style = style;
    plan.layers = {text};
    return plan;
}

// Extrusion is mutually exclusive with the GEL material in the consumer, so
// it needs its own otherwise-complete text plan.
RenderPlan makeBridgeExtrudedTextPlan() {
    auto plan = makeBridgeTextPlan();
    auto& text = plan.layers[0];
    auto& style = *text.style;
    text.enable_3d = true;
    style.gel_surface = false;
    style.gel_core = false;
    style.gel_soft_fusion.reset();
    style.gel_debug_mode = 0;
    style.gel_transmission_enabled = true;
    style.gel_material_profile = "current";
    style.gel_light_azimuth_radians.reset();
    style.gel_light_elevation.reset();
    style.extruded_text = true;
    style.extrusion_depth = 18.0f;
    return plan;
}

// A shape assertion that touches a JSON path must not crash the binary when a
// key goes missing: that *is* the drift this file exists to report. Anything
// thrown counts as a failed check.
template <typename F>
void checkJson(F&& fn, const char* what) {
    bool ok = false;
    try {
        ok = static_cast<bool>(fn());
    } catch (...) {
        ok = false;
    }
    check(ok, what);
}

template <typename F>
void checkThrowsInvalidArgument(F&& fn, const char* what) {
    try {
        fn();
        check(false, what);// wrote a plan it had already reported inexpressible
    } catch (const std::invalid_argument&) {
        check(true, what);
    } catch (...) {
        check(false, what);// wrong exception type
    }
}

void checkWriterDrift() {
    section("writer drift: emission carries every field the lowering produced");

    const auto plan = makeBridgePlan();
    const auto emittedText = renderPlanToJson(plan);
    const auto emitted = nlohmann::json::parse(emittedText);

    checkJson([&] { return emittedText == renderPlanToJson(plan); },
              "emission is byte-reproducible");
    checkJson(
        [&] {
            return emitted.at("layers").at(0).at("masks").at(0).at("opacity_track")
                       .at("keyframes").size() == 2;
        },
        "mask opacity track keyframes are written");
    checkJson(
        [&] { return emitted.at("layers").at(0).at("track_matte").at("type") == "alpha_inverted"; },
        "track matte type is written");
    checkJson(
        [&] { return emitted.at("layers").at(0).at("effect_param_tracks").size() == 2; },
        "both effect parameter tracks are written");
    // Effect parameter tracks are scalar channels: `value` is written as a bare
    // number, not an array. Compared against a double literal because the writer
    // emits the shortest decimal that round-trips the authored float.
    checkJson(
        [&] {
            return emitted.at("layers").at(0).at("effect_param_tracks").at(0)
                       .at("keyframes").at(1).at("value") == 1.2;
        },
        "first effect parameter track keeps its sampled value");
    checkJson(
        [&] {
            return emitted.at("layers").at(0).at("effect_param_tracks").at(1)
                       .at("keyframes").at(1).at("value") == 1.0;
        },
        "second effect parameter track keeps its sampled value");
    checkJson(
        [&] { return emitted.at("layers").at(0).at("casts_shadows") == true; },
        "casts_shadows is written");
    checkJson(
        [&] { return emitted.at("layers").at(0).at("accepts_shadows") == false; },
        "accepts_shadows is written");
    checkJson(
        [&] { return emitted.at("layers").at(0).at("shadow_blur") == 12.0; },
        "shadow_blur is written");
    checkJson(
        [&] { return emitted.at("layers").at(0).at("shadow_opacity") == 0.65; },
        "shadow_opacity is written");
    checkJson([&] { return emitted.at("layers").size() == 2; },
              "every layer in the plan is written");
}

void checkTextStyleWriterDrift() {
    section("text style writer drift: every authored property reaches JSON");

    const auto plan = makeBridgeTextPlan();
    const auto emitted = nlohmann::json::parse(renderPlanToJson(plan));
    const auto& style = emitted.at("layers").at(0).at("style");

    checkJson([&] { return style.at("font_family") == "Inter"; },
              "font_family is written");
    checkJson([&] { return style.at("font_weight") == 700; },
              "font_weight is written");
    checkJson([&] { return style.at("font_style") == "italic"; },
              "font_style is written");
    checkJson([&] { return style.at("variation_axes") == "wght=650,wdth=92"; },
              "variation_axes is written");
    checkJson([&] { return style.at("direction") == "rtl"; },
              "direction is written");
    checkJson([&] { return style.at("text_align") == "left"; },
              "text_align is written");
    checkJson([&] { return style.at("tracking") == 2.0; },
              "tracking is written");
    checkJson([&] { return style.at("quality") == "high"; },
              "quality is written");
    checkJson([&] { return style.at("shadow_quality") == "canonical"; },
              "shadow_quality is written");
    checkJson([&] { return style.at("stroke_mode") == "distance"; },
              "stroke_mode is written");
    checkJson([&] { return style.at("font") == "bridge-font"; },
              "font path is written");
    checkJson([&] { return style.at("font_size") == 48.0; },
              "font_size is written");
    checkJson([&] { return style.at("fill") == "#FFFFFF"; },
              "fill is written");
    checkJson([&] { return style.at("gel_surface") == true; },
              "gel_surface is written");
    checkJson([&] { return style.at("gel_core") == true; },
              "gel_core is written");
    checkJson([&] { return style.at("gel_debug_mode") == 3; },
              "gel_debug_mode is written");
    checkJson([&] { return style.at("gel_soft_fusion") == true; },
              "gel_soft_fusion is written");
    checkJson([&] { return style.at("gel_transmission") == false; },
              "gel_transmission is written");
    checkJson([&] { return style.at("gel_material_profile") == "reference_20261107"; },
              "gel_material_profile is written");
    checkJson([&] { return style.at("gel_light").at("azimuth_radians") == 0.5; },
              "gel_light azimuth is written");
    checkJson([&] { return style.at("gel_light").at("elevation") == 0.25; },
              "gel_light elevation is written");
    checkJson([&] { return style.at("glow").at("radius") == 18.0; },
              "glow radius is written");
    checkJson([&] { return style.at("glow").at("intensity") == 0.6; },
              "glow intensity is written");
    checkJson([&] { return style.at("glow").at("color") == "#22CCFF"; },
              "glow color is written");
    checkJson([&] { return style.at("text_caret").at("byte_index") == 4; },
              "text_caret byte_index is written");
    checkJson([&] { return style.at("text_caret").at("width") == 3.0; },
              "text_caret width is written");
    checkJson([&] { return style.at("text_caret").at("color") == "#FFCC00"; },
              "text_caret color is written");
    checkJson([&] { return style.at("text_caret").at("blink_period_frames") == 30; },
              "text_caret blink period is written");

    const auto extruded = makeBridgeExtrudedTextPlan();
    const auto emittedExtruded = nlohmann::json::parse(renderPlanToJson(extruded));
    const auto& extrudedStyle = emittedExtruded.at("layers").at(0).at("style");
    checkJson([&] { return extrudedStyle.at("extruded_text") == true; },
              "extruded_text is written");
    checkJson([&] { return extrudedStyle.at("extrusion_depth") == 18.0; },
              "extrusion_depth is written");
}

void checkWriterFailClosed() {
    section("inexpressible or invalid plans are refused, never silently dropped");

    const auto plan = makeBridgePlan();

    auto invalid = plan;
    invalid.layers[0].shadow_blur = 200.f;
    check(!renderPlanEmissionProblem(invalid).empty(), "out-of-bounds shadow_blur is reported");
    checkThrowsInvalidArgument([&] { (void)renderPlanToJson(invalid); },
                               "out-of-bounds shadow_blur is refused at write time");

    invalid = plan;
    invalid.layers[0].shadow_opacity = -0.1f;
    check(!renderPlanEmissionProblem(invalid).empty(), "out-of-bounds shadow_opacity is reported");
    checkThrowsInvalidArgument([&] { (void)renderPlanToJson(invalid); },
                               "out-of-bounds shadow_opacity is refused at write time");

    invalid = plan;
    invalid.layers[0].masks[0].size[0] = -1.f;
    check(!renderPlanEmissionProblem(invalid).empty(), "negative mask size is reported");
    checkThrowsInvalidArgument([&] { (void)renderPlanToJson(invalid); },
                               "negative mask size is refused at write time");

    invalid = plan;
    invalid.layers[0].effect_param_tracks[0].param = "not_animatable";
    check(!renderPlanEmissionProblem(invalid).empty(),
          "non-animatable effect parameter is reported");

    invalid = plan;
    invalid.layers[0].effect_param_tracks[0].track.keyframes[1].value[0] = 5.f;
    check(!renderPlanEmissionProblem(invalid).empty(), "out-of-range effect value is reported");
    checkThrowsInvalidArgument([&] { (void)renderPlanToJson(invalid); },
                               "out-of-range effect value is refused at write time");

    invalid = plan;
    invalid.layers[0].effect_param_tracks[0].track.keyframes[1].frame =
        invalid.layers[0].effect_param_tracks[0].track.keyframes[0].frame;
    check(!renderPlanEmissionProblem(invalid).empty(), "non-increasing keyframe times are reported");
    checkThrowsInvalidArgument([&] { (void)renderPlanToJson(invalid); },
                               "non-increasing keyframe times are refused at write time");

    invalid = plan;
    invalid.layers[0].effect_param_tracks.push_back(invalid.layers[0].effect_param_tracks[0]);
    check(!renderPlanEmissionProblem(invalid).empty(),
          "duplicate effect parameter track is reported");
    checkThrowsInvalidArgument([&] { (void)renderPlanToJson(invalid); },
                               "duplicate effect parameter track is refused at write time");

    invalid = plan;
    invalid.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(invalid).empty(),
          "V3-only bridge features are reported when the plan asks for V2");

    const auto textPlan = makeBridgeTextPlan();
    auto invalidText = textPlan;
    invalidText.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "V3-only text style properties are reported when the plan asks for V2");
    checkThrowsInvalidArgument([&] { (void)renderPlanToJson(invalidText); },
                               "V3-only text style properties are refused at write time");

    invalidText = textPlan;
    invalidText.layers[0].style->quality = "ultra";
    check(!renderPlanEmissionProblem(invalidText).empty(), "unsupported text quality is reported");

    invalidText = textPlan;
    invalidText.layers[0].style->font_weight = 450;
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "font_weight outside the 100..900 hundred steps is reported");

    invalidText = textPlan;
    invalidText.layers[0].style->tracking = 2000.f;
    check(!renderPlanEmissionProblem(invalidText).empty(), "tracking outside [-1000, 1000] is reported");

    invalidText = textPlan;
    invalidText.layers[0].style->variation_axes = "wght";
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "malformed variation_axes is reported");

    invalidText = textPlan;
    invalidText.layers[0].style->fill_opacity = 0.5f;
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "non-opaque fill_opacity is reported instead of dropped");

    invalidText = textPlan;
    invalidText.layers[0].style->text_caret->width = 0.f;
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "invalid text_caret width is reported");

    invalidText = textPlan;
    invalidText.layers[0].style->text_fit_mode = "shrink_only";
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "text_fit_mode is reported instead of dropped");

    invalidText = textPlan;
    invalidText.layers[0].style->min_font_size = 12.f;
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "font-size limits are reported instead of dropped");

    invalidText = textPlan;
    chronon3d::render_plan::StrokeStyle textStroke;
    textStroke.color = "#FFFFFF";
    textStroke.width = 2.f;
    invalidText.layers[0].style->stroke = textStroke;
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "text stroke is reported instead of dropped");

    invalidText = textPlan;
    chronon3d::render_plan::ShadowStyle textShadow;
    textShadow.color = "#000000";
    textShadow.offset = {2.f, 2.f};
    textShadow.offset_dimensions = 2;
    invalidText.layers[0].style->shadow = textShadow;
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "text shadow is reported instead of dropped");

    invalidText = textPlan;
    invalidText.layers[0].style->backdrop =
        chronon3d::render_plan::BackdropBlurStyle{};
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "text backdrop blur is reported instead of dropped");

    invalidText = textPlan;
    invalidText.layers[0].style->fill_gradient =
        chronon3d::graphics::GradientDefinition{};
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "text fill gradient is reported instead of dropped");

    invalidText = textPlan;
    invalidText.layers[0].style->fill_pattern =
        std::make_shared<chronon3d::graphics::PatternTile>();
    check(!renderPlanEmissionProblem(invalidText).empty(),
          "text fill pattern is reported instead of dropped");

    // Rig-to-mesh frame transport is covered in Motion authoring tests;
    // consumer pixel proof remains explicitly blocked in the bridge checklist.
}

#ifdef CHRONONMOTION_HAS_CHRONON3D_CONSUMER
bool validWithChronon3D(const RenderPlan& plan) {
    const auto json = nlohmann::json::parse(renderPlanToJson(plan));
    if (!chronon3d::render_plan::validate_render_plan(json).ok()) return false;
    return chronon3d::render_plan::decode_render_plan(json).has_value();
}

void checkConsumerRoundTrip() {
    section("canonical consumer: validation and decoding of the emitted plan");

    const auto plan = makeBridgePlan();
    const auto emitted = nlohmann::json::parse(renderPlanToJson(plan));

    check(validWithChronon3D(plan), "emitted document passes Chronon3D validation and decoding");

    const auto decoded = chronon3d::render_plan::decode_render_plan(emitted);
    check(decoded.has_value(), "canonical decoder accepts the emitted V3 bridge plan");
    checkJson([&] { return decoded.value().layers[0].masks.size() == 1; },
              "masks survive the decode round trip");
    checkJson([&] { return decoded.value().layers[0].track_matte.has_value(); },
              "track matte survives the decode round trip");
    checkJson([&] { return decoded.value().layers[0].effect_param_tracks.size() == 2; },
              "effect parameter tracks survive the decode round trip");

    auto invalid = plan;
    invalid.layers[0].track_matte->source = "missing-matte";
    check(!validWithChronon3D(invalid), "dangling track matte source fails canonical validation");

    const auto textPlan = makeBridgeTextPlan();
    const auto textJson = nlohmann::json::parse(renderPlanToJson(textPlan));
    check(validWithChronon3D(textPlan),
          "emitted text style document passes Chronon3D validation and decoding");
    const auto decodedText = chronon3d::render_plan::decode_render_plan(textJson);
    check(decodedText.has_value(), "canonical decoder accepts the emitted text style plan");
    if (decodedText.has_value()) {
        const auto& decodedStyle = decodedText->layers[0].style;
        check(decodedStyle.has_value(), "text style survives the decode round trip");
        if (decodedStyle) {
            check(decodedStyle->font == "bridge-font", "font path survives the decode round trip");
            check(decodedStyle->font_size.has_value() && *decodedStyle->font_size == 48.f,
                  "font_size survives the decode round trip");
            check(decodedStyle->fill == "#FFFFFF", "fill survives the decode round trip");
            check(decodedStyle->font_family == "Inter", "font_family survives the decode round trip");
            check(decodedStyle->font_weight == 700, "font_weight survives the decode round trip");
            check(decodedStyle->font_style == "italic", "font_style survives the decode round trip");
            check(decodedStyle->variation_axes == "wght=650,wdth=92",
                  "variation_axes survives the decode round trip");
            check(decodedStyle->text_direction == "rtl", "direction survives the decode round trip");
            check(decodedStyle->text_align == "left", "text_align survives the decode round trip");
            check(decodedStyle->tracking.has_value() && *decodedStyle->tracking == 2.f,
                  "tracking survives the decode round trip");
            check(decodedStyle->quality == "high", "quality survives the decode round trip");
            check(decodedStyle->shadow_quality == "canonical",
                  "shadow_quality survives the decode round trip");
            check(decodedStyle->stroke_mode == "distance",
                  "stroke_mode survives the decode round trip");
            check(decodedStyle->gel_surface, "gel_surface survives the decode round trip");
            check(decodedStyle->gel_core, "gel_core survives the decode round trip");
            check(decodedStyle->gel_debug_mode == 3,
                  "gel_debug_mode survives the decode round trip");
            check(decodedStyle->gel_soft_fusion.has_value() &&
                      *decodedStyle->gel_soft_fusion,
                  "gel_soft_fusion survives the decode round trip");
            check(!decodedStyle->gel_transmission_enabled,
                  "gel_transmission survives the decode round trip");
            check(decodedStyle->gel_material_profile == "reference_20261107",
                  "gel_material_profile survives the decode round trip");
            check(decodedStyle->gel_light_azimuth_radians.has_value() &&
                      *decodedStyle->gel_light_azimuth_radians == 0.5f,
                  "gel_light azimuth survives the decode round trip");
            check(decodedStyle->gel_light_elevation.has_value() &&
                      *decodedStyle->gel_light_elevation == 0.25f,
                  "gel_light elevation survives the decode round trip");
            check(decodedStyle->glow.has_value(),
                  "glow survives the decode round trip");
            if (decodedStyle->glow) {
                check(decodedStyle->glow->radius == 18.f,
                      "glow radius survives the decode round trip");
                check(decodedStyle->glow->intensity == 0.6f,
                      "glow intensity survives the decode round trip");
                check(decodedStyle->glow->color == "#22CCFF",
                      "glow color survives the decode round trip");
            }
            check(decodedStyle->text_caret.has_value(), "text_caret survives the decode round trip");
            if (decodedStyle->text_caret) {
                check(decodedStyle->text_caret->byte_index == 4,
                      "text_caret byte_index survives the decode round trip");
                check(decodedStyle->text_caret->width == 3.f,
                      "text_caret width survives the decode round trip");
                check(decodedStyle->text_caret->color == "#FFCC00",
                      "text_caret color survives the decode round trip");
                check(decodedStyle->text_caret->blink_period_frames == 30,
                      "text_caret blink period survives the decode round trip");
            }
        }
    }

    const auto extrudedPlan = makeBridgeExtrudedTextPlan();
    const auto extrudedJson = nlohmann::json::parse(renderPlanToJson(extrudedPlan));
    check(validWithChronon3D(extrudedPlan),
          "emitted extrusion document passes Chronon3D validation and decoding");
    const auto decodedExtruded = chronon3d::render_plan::decode_render_plan(extrudedJson);
    check(decodedExtruded.has_value(), "canonical decoder accepts the extrusion plan");
    if (decodedExtruded.has_value() && decodedExtruded->layers[0].style) {
        const auto& style = *decodedExtruded->layers[0].style;
        check(style.extruded_text, "extruded_text survives the decode round trip");
        check(style.extrusion_depth == 18.f,
              "extrusion_depth survives the decode round trip");
    }
}
#endif

void checkBridge() {
    checkWriterDrift();
    checkTextStyleWriterDrift();
    checkWriterFailClosed();
#ifdef CHRONONMOTION_HAS_CHRONON3D_CONSUMER
    checkConsumerRoundTrip();
#else
    std::printf(
        "[INFO] chrononmotion_v3_bridge_regression: consumer round-trip NOT RUN "
        "(chronon3d_render_plan not available in this build tree)\n");
#endif
}

}// namespace

int main() {
    checkBridge();
    return chrononmotion_test::report();
}
