#include <doctest/doctest.h>

#include <chrononmotion/motion/RenderPlanEmission.hpp>
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/render_plan/render_plan_validator.hpp>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <string>

using chronon3d::render_plan::LayerPlan;
using chronon3d::render_plan::LayerType;
using chronon3d::render_plan::RenderPlan;
using namespace chrononmotion::motion;

namespace {
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
    mask.opacity_track->keyframes = {{chronon3d::Frame{0}, {0.f}},
                                     {chronon3d::Frame{8}, {1.f}}};
    image.masks.push_back(mask);
    image.track_matte = chronon3d::render_plan::TrackMattePlan{
        "matte-layer", chronon3d::render_plan::TrackMatteTypePlan::AlphaInverted};
    LayerPlan::EffectPlan effect;
    effect.kind = LayerPlan::EffectKindPlan::Catalog;
    effect.canonical_effect_id = "color.saturation";
    effect.canonical_params.push_back({"value", 1.f});
    image.effects.push_back(effect);
    effect.canonical_effect_id = "color.exposure";
    effect.canonical_params.front().param = "exposure";
    image.effects.push_back(effect);
    LayerPlan::EffectParamTrackPlan saturation;
    saturation.effect_id = "color.saturation";
    saturation.param = "value";
    saturation.track.property = "opacity";
    saturation.track.easing = "linear";
    saturation.track.keyframes = {{chronon3d::Frame{0}, {0.7f}},
                                  {chronon3d::Frame{8}, {1.2f}}};
    LayerPlan::EffectParamTrackPlan exposure;
    exposure.effect_id = "color.exposure";
    exposure.param = "exposure";
    exposure.track.property = "opacity";
    exposure.track.easing = "linear";
    exposure.track.keyframes = {{chronon3d::Frame{0}, {0.f}},
                                {chronon3d::Frame{8}, {1.f}}};
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

bool validWithChronon3D(const RenderPlan& plan) {
    const auto json = nlohmann::json::parse(renderPlanToJson(plan));
    if (!chronon3d::render_plan::validate_render_plan(json).ok()) return false;
    return chronon3d::render_plan::decode_render_plan(json).has_value();
}
}

TEST_CASE("ChrononMotion V3 bridge passes canonical Chronon3D validation and decoding") {
    auto plan = makeBridgePlan();
    const auto emittedText = renderPlanToJson(plan);
    const auto emitted = nlohmann::json::parse(emittedText);
    CHECK(emittedText == renderPlanToJson(plan));
    CHECK(emitted.at("layers").at(0).at("masks").at(0).at("opacity_track")
              .at("keyframes").size() == 2);
    CHECK(emitted.at("layers").at(0).at("track_matte").at("type") == "alpha_inverted");
    CHECK(emitted.at("layers").at(0).at("effect_param_tracks").size() == 2);
    CHECK(emitted.at("layers").at(0).at("effect_param_tracks").at(0).at("keyframes").at(1).at("value") == 1.2);
    CHECK(emitted.at("layers").at(0).at("effect_param_tracks").at(1).at("keyframes").at(1).at("value") == 1.0);
    CHECK(validWithChronon3D(plan));
    const auto decoded = chronon3d::render_plan::decode_render_plan(emitted);
    REQUIRE(decoded.has_value());
    CHECK(decoded.value().layers[0].masks.size() == 1);
    CHECK(decoded.value().layers[0].track_matte.has_value());
    CHECK(decoded.value().layers[0].effect_param_tracks.size() == 2);

    auto invalid = plan;
    invalid.layers[0].masks[0].size[0] = -1.f;
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    CHECK_THROWS_AS(renderPlanToJson(invalid), std::invalid_argument);
    invalid = plan;
    invalid.layers[0].track_matte->source = "missing-matte";
    CHECK_FALSE(validWithChronon3D(invalid));
    invalid = plan;
    invalid.layers[0].effect_param_tracks[0].param = "not_animatable";
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    invalid = plan;
    invalid.layers[0].effect_param_tracks[0].track.keyframes[1].value[0] = 5.f;
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    CHECK_THROWS_AS(renderPlanToJson(invalid), std::invalid_argument);
    invalid = plan;
    invalid.layers[0].effect_param_tracks[0].track.keyframes[1].frame =
        invalid.layers[0].effect_param_tracks[0].track.keyframes[0].frame;
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    CHECK_THROWS_AS(renderPlanToJson(invalid), std::invalid_argument);
    invalid = plan;
    invalid.layers[0].effect_param_tracks.push_back(invalid.layers[0].effect_param_tracks[0]);
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
    CHECK_THROWS_AS(renderPlanToJson(invalid), std::invalid_argument);
    invalid = plan;
    invalid.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    CHECK_FALSE(renderPlanEmissionProblem(invalid).empty());
}
