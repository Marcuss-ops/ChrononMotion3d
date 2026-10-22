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
#include <nlohmann/json.hpp>

#ifdef CHRONONMOTION_HAS_CHRONON3D_CONSUMER
#include <chronon3d/render_plan/render_plan_validator.hpp>
#endif

#include <cstdio>
#include <stdexcept>
#include <string>

using chronon3d::render_plan::LayerPlan;
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
    checkJson([&] { return emitted.at("layers").size() == 2; },
              "every layer in the plan is written");
}

void checkWriterFailClosed() {
    section("inexpressible or invalid plans are refused, never silently dropped");

    const auto plan = makeBridgePlan();

    auto invalid = plan;
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
}
#endif

void checkBridge() {
    checkWriterDrift();
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
