// ChrononMotion — resolved camera sample round-trip.
// Offline lowering serializes complete CM3D-resolved camera snapshots; C3D
// receives no authoring tracks to reinterpret.

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "motion_check.hpp"

#include <cmath>
#include <memory>
#include <string>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

constexpr float kFps = 30.f;
constexpr float kEps = 2e-4f;

void checkCameraBake() {
    section("resolved camera samples: rig snapshots and runtime sampling");

    MotionScene scene("bake_roundtrip", kFps);
    auto& layer = scene.addContent(
        7, "bake card", ContentRef::image("bake-canary", Vector2(64.f, 64.f)));

    CameraRig rig(1, 55.f, 16.f / 9.f);
    rig.positionTrack().add(0.f, Vector3(0.f, 0.f, 1200.f), Easing::easeInOut());
    rig.positionTrack().add(1.f, Vector3(0.f, 0.f, 800.f), Easing::easeInOut());
    rig.targetTrack().add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
    rig.targetTrack().add(1.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
    rig.setFov(50.f);

    RenderPlanLoweringOptions options;
    options.width = 1920;
    options.height = 1080;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    options.cameraRig = &rig;
    chronon3d::render_plan::LayerPlan image;
    image.type = chronon3d::render_plan::LayerType::Image;
    image.asset = "bake-canary";
    image.size = {64.f, 64.f};
    image.size_dimensions = 2;
    options.contentLayers.emplace("7", std::move(image));

    const auto plan = lowerToRenderPlan(scene, options);
    check(plan.camera_samples.has_value(), "offline bake emits resolved camera samples");
    if (!plan.camera_samples) return;
    const auto& samples = *plan.camera_samples;
    check(samples.size() == 31, "bake emits one complete camera sample per frame (0..30)");
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const float seconds = static_cast<float>(i) / kFps;
        const CameraPose expected = rig.sample(seconds);
        check(samples[i].frame == static_cast<double>(i), "camera samples are ordered by absolute frame");
        checkNear(samples[i].camera.position[0], expected.position.x, kEps,
                  "resolved sample preserves rig position X");
        checkNear(samples[i].camera.position[1], expected.position.y, kEps,
                  "resolved sample preserves rig position Y");
        checkNear(samples[i].camera.position[2], -expected.position.z, kEps,
                  "resolved sample applies the canonical depth conversion");
        checkNear(samples[i].camera.fov_deg, expected.fov, kEps,
                  "resolved sample preserves rig FOV");
    }

    const auto plan2 = lowerToRenderPlan(scene, options);
    bool identical = plan2.camera_samples && plan2.camera_samples->size() == samples.size();
    for (std::size_t i = 0; identical && i < samples.size(); ++i)
        identical = plan2.camera_samples->at(i).camera.position == samples[i].camera.position &&
                    plan2.camera_samples->at(i).camera.rotation_deg == samples[i].camera.rotation_deg;
    check(identical, "offline camera samples are bit-deterministic across runs");

    auto runtimeRig = std::make_shared<CameraRig>();
    runtimeRig->positionTrack().add(0.f, Vector3(0.f, 0.f, 1200.f), Easing::easeInOut());
    runtimeRig->positionTrack().add(1.f, Vector3(120.f, 30.f, 800.f), Easing::linear());
    const auto provider = makeCameraSampleProvider(
        runtimeRig, kFps, "camera-sample-roundtrip-runtime-rig");
    constexpr double kSubframe = 15.5;
    const CameraPose expectedSubframe = runtimeRig->sample(
        static_cast<float>(kSubframe / static_cast<double>(kFps)));
    const auto resolvedSubframe = provider.sample(kSubframe);
    checkNear(resolvedSubframe.position[0], expectedSubframe.position.x, kEps,
              "runtime provider samples the rig at the exact fractional frame");
    checkNear(resolvedSubframe.position[1], expectedSubframe.position.y, kEps,
              "runtime provider preserves the fractional-frame vertical pose");
    checkNear(resolvedSubframe.position[2], -expectedSubframe.position.z, kEps,
              "runtime provider applies depth conversion at subframe time");
    check(provider.fingerprint == "camera-sample-roundtrip-runtime-rig",
          "runtime camera provider carries a stable identity");
    const auto preRoll = provider.sample(-0.25);
    const CameraPose expectedPreRoll = runtimeRig->sample(
        static_cast<float>(-0.25 / static_cast<double>(kFps)));
    checkNear(preRoll.position[0], expectedPreRoll.position.x, kEps,
              "runtime provider supports shutter samples before frame zero");

    section("resolved camera samples: parented CameraRig under Null controller");
    MotionScene parentedScene("parented_bake", kFps);
    auto& controller = parentedScene.addNull(100, "CameraRigNull");
    controller.tracks.position.add(0.f, Vector3(100.f, 50.f, -20.f), Easing::linear());
    controller.tracks.position.add(1.f, Vector3(200.f, 70.f, -40.f), Easing::linear());
    auto& content = parentedScene.addContent(
        101, "card", ContentRef::image("bake-canary", Vector2(64.f, 64.f)));
    (void)content;

    CameraRig parentedRig(102, 50.f, 16.f / 9.f);
    parentedRig.setParent(100);
    parentedRig.positionTrack().add(0.f, Vector3(0.f, 0.f, 10.f), Easing::linear());
    parentedRig.positionTrack().add(1.f, Vector3(10.f, -5.f, 10.f), Easing::linear());

    RenderPlanLoweringOptions parentedOptions = options;
    parentedOptions.contentLayers.clear();
    chronon3d::render_plan::LayerPlan cardPlan;
    cardPlan.type = chronon3d::render_plan::LayerType::Image;
    cardPlan.asset = "bake-canary";
    cardPlan.size = {64.f, 64.f};
    cardPlan.size_dimensions = 2;
    parentedOptions.contentLayers.emplace("101", std::move(cardPlan));
    parentedOptions.cameraRig = &parentedRig;

    const auto parentedPlan = lowerToRenderPlan(parentedScene, parentedOptions);
    check(parentedPlan.camera.has_value() && parentedPlan.camera_samples.has_value(),
          "parented CameraRig lowers to camera and camera_samples without throwing");
    if (parentedPlan.camera && parentedPlan.camera_samples) {
        checkNear(parentedPlan.camera->position[0], 100.f, kEps,
                  "parented camera initial X combines Null controller and local offset");
        checkNear(parentedPlan.camera->position[1], 50.f, kEps,
                  "parented camera initial Y combines Null controller and local offset");
        checkNear(parentedPlan.camera->position[2], 10.f, kEps,
                  "parented camera initial Z mirrors world Z=(-20+10)=-10 to +10");
        const auto& lastSample = parentedPlan.camera_samples->back().camera;
        checkNear(lastSample.position[0], 210.f, kEps,
                  "parented camera final sample X combines animated Null and local offset");
        checkNear(lastSample.position[1], 65.f, kEps,
                  "parented camera final sample Y combines animated Null and local offset");
        checkNear(lastSample.position[2], 30.f, kEps,
                  "parented camera final sample Z mirrors world Z=(-40+10)=-30 to +30");
    }

    (void)layer;
}

} // namespace

int main() {
    checkCameraBake();
    return chrononmotion_test::report();
}
