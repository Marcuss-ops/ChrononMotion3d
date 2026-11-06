#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "motion_check.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <utility>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

void checkTrackParity(const char* name, const Easing& easing) {
    constexpr float fps = 30.f;
    MotionScene scene(name, fps);
    auto& layer = scene.addContent(
        41, "eased position", ContentRef::image("easing-canary", Vector2(32.f, 32.f)));
    const Vector3 from{-17.f, 9.f, 2.f};
    const Vector3 to{83.f, -31.f, 22.f};
    layer.tracks.position.add(0.f, from, easing);
    layer.tracks.position.add(1.f, to, Easing::linear());

    RenderPlanLoweringOptions options;
    options.width = 320;
    options.height = 180;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    chronon3d::render_plan::LayerPlan image;
    image.type = chronon3d::render_plan::LayerType::Image;
    image.asset = "easing-canary";
    image.size = {32.f, 32.f};
    image.size_dimensions = 2;
    options.contentLayers.emplace("41", std::move(image));

    const auto plan = lowerToRenderPlan(scene, options);
    const auto layerPlan = std::find_if(plan.layers.begin(), plan.layers.end(),
        [](const auto& candidate) { return candidate.id == "41"; });
    check(layerPlan != plan.layers.end() && !layerPlan->transform_samples.empty(),
          "lowered easing canary layer and resolved samples exist");
    if (layerPlan == plan.layers.end()) return;
    check(!layerPlan->animation, "V3 easing lowers without a legacy integer-TRS animation");
    check(layerPlan->transform_samples.size() == 31,
          "adapter emits one resolved sample for every frame");
    for (const auto& sample : layerPlan->transform_samples) {
        const float seconds = static_cast<float>(sample.frame) / fps;
        const Vector3 motionSample = layer.tracks.position.sample(seconds);
        checkNear(sample.position[0], motionSample.x, 2e-4f, "RenderPlan X equals Motion sample");
        checkNear(sample.position[1], motionSample.y, 2e-4f, "RenderPlan Y equals Motion sample");
        // The adapter mirrors Motion's -Z-forward depth into RenderPlan's
        // +Z-forward space, just as it does for static layers and cameras.
        checkNear(sample.position[2], -motionSample.z, 2e-4f, "RenderPlan Z equals mirrored Motion depth");
    }
}

} // namespace

int main() {
    section("Motion → RenderPlan easing sample parity");
    const std::array<std::pair<const char*, Easing>, 12> cases{{
        {"linear", Easing::linear()},
        {"ease-in", Easing::easeIn()},
        {"ease-out", Easing::easeOut()},
        {"ease-in-out", Easing::easeInOut()},
        {"renderplan-in-out-cubic", Easing::inOutCubic()},
        {"cubic-bezier", Easing::cubicBezier(.18f, .04f, .72f, .96f)},
        {"overshoot", Easing::overshoot()},
        {"bounce-out", Easing::bounceOut()},
        {"elastic-out", Easing::elasticOut()},
        {"expo-out", Easing::expoOut()},
        {"circ-out", Easing::circOut()},
        {"steps", Easing::steps(5)},
    }};
    for (const auto& [name, easing] : cases) checkTrackParity(name, easing);
    return chrononmotion_test::report();
}
