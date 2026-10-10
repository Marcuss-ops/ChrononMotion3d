// ChrononMotion — camera bake round-trip.
//
// The camera bake is the only passage from the Motion rig to the renderer:
// `lowerToRenderPlan` samples the CameraRig at integer frames and emits
// dense linear tracks (position with mirrored Z, rotation, FOV). This test
// pins that the baked tracks equal the rig samples, so the bake cannot drift
// from the authoring rig without failing loudly.

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "motion_check.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

constexpr float kFps = 30.f;
constexpr float kEps = 2e-4f;

const chronon3d::render_plan::CameraTrackPlan* findTrack(
    const std::vector<chronon3d::render_plan::CameraTrackPlan>& tracks,
    chronon3d::render_plan::CameraPropertyPlan property) {
    for (const auto& track : tracks) {
        if (track.property == property) return &track;
    }
    return nullptr;
}

void checkCameraBake() {
    section("camera bake round-trip: rig samples == baked tracks");

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
    check(plan.camera_animation.has_value(), "bake emits camera animation");
    if (!plan.camera_animation) return;
    const auto& tracks = *plan.camera_animation;

    const auto* posX = findTrack(tracks, chronon3d::render_plan::CameraPropertyPlan::PositionX);
    const auto* posY = findTrack(tracks, chronon3d::render_plan::CameraPropertyPlan::PositionY);
    const auto* posZ = findTrack(tracks, chronon3d::render_plan::CameraPropertyPlan::PositionZ);
    const auto* rotX = findTrack(tracks, chronon3d::render_plan::CameraPropertyPlan::RotationX);
    const auto* rotY = findTrack(tracks, chronon3d::render_plan::CameraPropertyPlan::RotationY);
    const auto* rotZ = findTrack(tracks, chronon3d::render_plan::CameraPropertyPlan::RotationZ);
    const auto* fov = findTrack(tracks, chronon3d::render_plan::CameraPropertyPlan::FovDeg);
    check(posX && posY && posZ && rotX && rotY && rotZ && fov,
          "bake emits position, rotation and FOV tracks");
    if (!posX || !posY || !posZ || !rotX || !rotY || !rotZ || !fov) return;

    check(posX->keyframes.size() == 31, "bake emits one key per frame (0..30)");
    check(posX->easing == "linear", "baked camera tracks are linear samples");
    for (const auto* track : {posX, posY, posZ, rotX, rotY, rotZ, fov}) {
        check(track->keyframes.size() == 31, "every camera track covers every frame");
    }

    for (std::size_t i = 0; i < posX->keyframes.size(); ++i) {
        const float seconds = static_cast<float>(i) / kFps;
        const CameraPose sample = rig.sample(seconds);
        checkNear(posX->keyframes[i].value[0], sample.position.x, kEps,
                  "baked position X equals the rig sample");
        checkNear(posY->keyframes[i].value[0], sample.position.y, kEps,
                  "baked position Y equals the rig sample");
        checkNear(posZ->keyframes[i].value[0], -sample.position.z, kEps,
                  "baked position Z equals the mirrored rig depth");
        checkNear(fov->keyframes[i].value[0], sample.fov, kEps,
                  "baked FOV equals the rig sample");
    }

    // Pure dolly: no rotation is authored, so every rotation key is constant.
    for (std::size_t i = 1; i < rotX->keyframes.size(); ++i) {
        checkNear(rotX->keyframes[i].value[0], rotX->keyframes[0].value[0], kEps,
                  "pure dolly keeps rotation X constant");
        checkNear(rotY->keyframes[i].value[0], rotY->keyframes[0].value[0], kEps,
                  "pure dolly keeps rotation Y constant");
        checkNear(rotZ->keyframes[i].value[0], rotZ->keyframes[0].value[0], kEps,
                  "pure dolly keeps rotation Z constant");
    }

    // Determinism: lowering twice yields identical bytes of intent.
    const auto plan2 = lowerToRenderPlan(scene, options);
    check(plan2.camera_animation.has_value(), "second bake emits camera animation");
    if (!plan2.camera_animation) return;
    const auto* posX2 =
        findTrack(*plan2.camera_animation, chronon3d::render_plan::CameraPropertyPlan::PositionX);
    check(posX2 != nullptr, "second bake emits position X");
    if (!posX2) return;
    bool identical = posX2->keyframes.size() == posX->keyframes.size();
    for (std::size_t i = 0; identical && i < posX->keyframes.size(); ++i) {
        identical = posX2->keyframes[i].value[0] == posX->keyframes[i].value[0];
    }
    check(identical, "camera bake is bit-deterministic across runs");

    (void)layer;
}

} // namespace

int main() {
    checkCameraBake();
    return chrononmotion_test::report();
}
