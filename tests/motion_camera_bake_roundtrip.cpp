// ChrononMotion — resolved camera sample round-trip.
// Offline lowering serializes complete CM3D-resolved camera snapshots; C3D
// receives no authoring tracks to reinterpret.

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "motion_check.hpp"

#include <chronon3d/animation/temporal/temporal_samples.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
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

    auto resolvedLayerOptions = options;
    resolvedLayerOptions.requireResolvedLayerSamples = true;
    const auto resolvedLayerPlan = lowerToRenderPlan(scene, resolvedLayerOptions);
    const auto resolvedImage = std::find_if(resolvedLayerPlan.layers.begin(), resolvedLayerPlan.layers.end(),
        [](const auto& candidate) { return candidate.id == "7"; });
    check(resolvedImage != resolvedLayerPlan.layers.end() &&
              resolvedImage->transform_samples.size() == samples.size() &&
              !resolvedImage->animation,
          "production camera mode emits resolved per-frame layer samples without integer TRS tracks");

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
    check(parentedPlan.layers.size() == 1 && parentedPlan.layers.front().id == "101",
          "parent controller Null is folded into the camera and omitted from renderable plan layers");
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

    section("parented runtime camera uses MotionScene at shutter times");
    auto runtimeScene = std::make_shared<MotionScene>("runtime_parent", kFps);
    auto& runtimeParent = runtimeScene->addNull(200, "AnimatedParent");
    runtimeParent.tracks.position.add(0.f, Vector3(50.f, -20.f, -10.f), Easing::easeInOut());
    runtimeParent.tracks.position.add(1.f, Vector3(180.f, 40.f, -50.f), Easing::linear());
    runtimeParent.tracks.rotation.add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
    runtimeParent.tracks.rotation.add(1.f, Vector3(0.f, 0.4f, 0.f), Easing::linear());
    auto worldRig = std::make_shared<CameraRig>(201, 50.f, 16.f / 9.f);
    worldRig->setParent(200).setPosition({10.f, 5.f, 20.f}).setRoll(0.3f);
    worldRig->positionTrack().add(0.f, Vector3(10.f, 5.f, 20.f), Easing::easeInOut());
    worldRig->positionTrack().add(1.f, Vector3(30.f, 5.f, 20.f), Easing::linear());

    bool localParentRejected = false;
    try { (void)makeCameraSampleProvider(worldRig, kFps, "parent-without-scene"); }
    catch (const std::invalid_argument&) { localParentRejected = true; }
    check(localParentRejected, "parented rig without a scene is rejected");
    bool localBakeRejected = false;
    try { (void)bakeResolvedCameraSamples(*worldRig, kFps, 1); }
    catch (const std::invalid_argument&) { localBakeRejected = true; }
    check(localBakeRejected, "parented rig cannot be baked as a local pose");

    // An identity parent must not apply an already sampled roll a second time.
    Matrix4 identity;
    auto localPose = worldRig->sample(0.f);
    auto worldPose = worldRig->sampleWorld(0.f, identity);
    localPose.hasTarget = false;
    localPose.roll = 0.f;
    const auto localCamera = lowerCameraPose(localPose);
    const auto identityCamera = lowerCameraPose(worldPose);
    for (int axis = 0; axis < 3; ++axis)
        checkNear(identityCamera.rotation_deg[axis], localCamera.rotation_deg[axis], 1e-3f,
                  "identity parent does not apply sampled roll twice");

    worldRig->setTarget({0.f, 0.f, 0.f}).setFocusDistance(200.f).setFStop(2.8f);
    worldRig->targetTrack().add(0.f, Vector3(0.f, 0.f, 0.f), Easing::easeInOut());
    worldRig->targetTrack().add(1.f, Vector3(30.f, 10.f, -20.f), Easing::linear());
    worldRig->fovTrack().add(0.f, 50.f, Easing::easeInOut());
    worldRig->fovTrack().add(1.f, 70.f, Easing::linear());
    worldRig->focusDistanceTrack().add(0.f, 200.f, Easing::easeInOut());
    worldRig->focusDistanceTrack().add(1.f, 500.f, Easing::linear());
    RenderPlanLoweringOptions worldOptions;
    worldOptions.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    worldOptions.cameraRig = worldRig.get();
    const auto worldPlan = lowerToRenderPlan(*runtimeScene, worldOptions);
    const auto worldProvider = makeCameraSampleProvider(worldRig, runtimeScene, "parent-and-rig-v1");
    check(worldPlan.camera_samples.has_value(), "parented camera bake exists");
    if (worldPlan.camera_samples) {
        for (const auto& baked : *worldPlan.camera_samples) {
            const auto runtime = worldProvider.sample(baked.frame);
            for (int axis = 0; axis < 3; ++axis) {
                checkNear(runtime.position[axis], baked.camera.position[axis], kEps,
                          "parented runtime and offline position agree at whole frames");
                checkNear(runtime.rotation_deg[axis], baked.camera.rotation_deg[axis], 1e-3f,
                          "parented runtime and offline rotation agree at whole frames");
            }
        }
    }
    for (double frame : {-0.25, 3.125, 15.375, 29.875, 3.125}) {
        const auto actual = worldProvider.sample(frame);
        const double whole = std::floor(frame);
        runtimeScene->evaluate(MotionTime{static_cast<int>(whole), kFps,
                                          static_cast<float>(frame - whole)});
        const auto expected = lowerCameraPose(worldRig->sampleWorld(
            static_cast<float>(frame / kFps), runtimeScene->worldMatrix(200)));
        for (int axis = 0; axis < 3; ++axis) {
            checkNear(actual.position[axis], expected.position[axis], kEps,
                      "parented runtime samples exact fractional world position");
            checkNear(actual.rotation_deg[axis], expected.rotation_deg[axis], 1e-3f,
                      "parented runtime samples exact fractional world orientation");
        }
    }

    section("explicit shutter camera samples and JSON timestamp precision");
    chronon3d::temporal::TemporalSampleParams shutter;
    for (int center : {0, 15, 30}) {
        const auto schedule = chronon3d::temporal::make_temporal_sample_plan(
            shutter, 8, chronon3d::Frame{center}, chronon3d::FrameRate{30, 1}, 64, 64);
        check(schedule.valid(), "C3D shutter scheduler returns a valid eight-sample plan");
        for (const auto& context : schedule.contexts)
            worldOptions.cameraSampleFrames.push_back(context.time.frame);
    }
    worldOptions.cameraSampleFrames.push_back(15.123456789012345);
    std::sort(worldOptions.cameraSampleFrames.begin(), worldOptions.cameraSampleFrames.end());
    runtimeScene->evaluate(7);
    const auto exactPlan = lowerToRenderPlan(*runtimeScene, worldOptions);
    check(runtimeScene->lastEvaluatedFrame() == 7, "explicit bake restores scene evaluation");
    check(exactPlan.camera_samples && exactPlan.camera_samples->size() == worldOptions.cameraSampleFrames.size(),
          "explicit bake uses all requested shutter timestamps");
    if (exactPlan.camera_samples) {
        const auto json = nlohmann::json::parse(renderPlanToJson(exactPlan));
        for (std::size_t i = 0; i < exactPlan.camera_samples->size(); ++i) {
            const auto& sample = exactPlan.camera_samples->at(i);
            check(sample.frame == worldOptions.cameraSampleFrames[i], "explicit timestamp preserved in plan");
            check(json.at("camera_samples").at(i).at("frame").get<double>() == sample.frame,
                  "explicit timestamp round-trips through JSON without float narrowing");
            const auto runtime = worldProvider.sample(sample.frame);
            checkNear(runtime.fov_deg, sample.camera.fov_deg, kEps,
                      "explicit offline and runtime eased FOV agree");
            checkNear(runtime.dof_focus_distance, sample.camera.dof_focus_distance, kEps,
                      "explicit offline and runtime eased focus agree");
            checkNear(runtime.f_stop, sample.camera.f_stop, kEps,
                      "explicit offline and runtime aperture agree");
            for (int axis = 0; axis < 3; ++axis) {
                checkNear(runtime.position[axis], sample.camera.position[axis], kEps,
                          "explicit offline and runtime world positions agree");
                checkNear(runtime.rotation_deg[axis], sample.camera.rotation_deg[axis], 1e-3f,
                          "explicit offline and runtime world rotations agree");
            }
        }
    }
    worldOptions.cameraSampleFrames = {1.0, 1.0};
    bool repeatedTimeRejected = false;
    try { (void)lowerToRenderPlan(*runtimeScene, worldOptions); }
    catch (const std::invalid_argument&) { repeatedTimeRejected = true; }
    check(repeatedTimeRejected, "duplicate explicit sample times are rejected");

    section("resolved camera samples: runtime look-at and roll normalization");
    auto aimedRig = std::make_shared<CameraRig>(1, 50.f, 16.f / 9.f, 0.1f, 10000.f);
    aimedRig->setPosition({0.f, 0.f, 1200.f});
    aimedRig->setTarget({120.f, 30.f, 0.f});
    aimedRig->setRoll(0.25f);

    bool directRejected = false;
    try {
        (void)lowerCameraPose(aimedRig->sample(0.f), 0.1f, 10000.f);
    } catch (const std::invalid_argument&) {
        directRejected = true;
    }
    check(directRejected,
          "lowerCameraPose still fails closed on unresolved look-at/roll metadata");

    const auto aimedProvider = makeCameraSampleProvider(
        aimedRig, kFps, "camera-sample-look-at-roll", 0.1f, 10000.f);
    const auto resolvedAimed = aimedProvider.sample(0.0);
    auto aimedPose = aimedRig->sample(0.f);
    aimedPose.hasTarget = false;
    aimedPose.roll = 0.f;
    const auto expectedAimed = lowerCameraPose(aimedPose, 0.1f, 10000.f);
    checkNear(resolvedAimed.position[0], expectedAimed.position[0], kEps,
              "runtime provider accepts look-at and preserves the aimed position X");
    checkNear(resolvedAimed.position[1], expectedAimed.position[1], kEps,
              "runtime provider accepts look-at and preserves the aimed position Y");
    checkNear(resolvedAimed.position[2], expectedAimed.position[2], kEps,
              "runtime provider accepts look-at and preserves the depth-converted Z");
    for (int axis = 0; axis < 3; ++axis)
        checkNear(resolvedAimed.rotation_deg[axis], expectedAimed.rotation_deg[axis], 1e-3f,
                  "runtime provider bakes look-at and roll into the lowered rotation");

    const auto aimedBake = bakeResolvedCameraSamples(*aimedRig, kFps, 1, 0.1f, 10000.f);
    check(aimedBake.size() == 1, "offline bake still emits the aimed camera sample");
    if (!aimedBake.empty()) {
        for (int axis = 0; axis < 3; ++axis)
            checkNear(aimedBake[0].camera.rotation_deg[axis], resolvedAimed.rotation_deg[axis], 1e-3f,
                      "offline bake and runtime provider agree on the resolved aimed rotation");
    }

    constexpr double kAimedSubframe = 15.5;
    const auto resolvedSubframeAimed = aimedProvider.sample(kAimedSubframe);
    auto subframePose = aimedRig->sample(
        static_cast<float>(kAimedSubframe / static_cast<double>(kFps)));
    subframePose.hasTarget = false;
    subframePose.roll = 0.f;
    const auto expectedSubframeAimed = lowerCameraPose(subframePose, 0.1f, 10000.f);
    checkNear(resolvedSubframeAimed.position[0], expectedSubframeAimed.position[0], kEps,
              "aimed runtime provider samples the exact fractional frame");
    for (int axis = 0; axis < 3; ++axis)
        checkNear(resolvedSubframeAimed.rotation_deg[axis], expectedSubframeAimed.rotation_deg[axis], 1e-3f,
                  "aimed runtime provider keeps look-at/roll parity at subframe time");

    section("consumer shutter calendar resolves camera and nonlinear layer motion together");
    MotionScene shutterScene("exact layer shutter", kFps);
    auto& shutterLayer = shutterScene.addContent(7, "shutter card",
        ContentRef::image("bake-canary", Vector2(64.f, 64.f)));
    shutterLayer.is3D = true;
    shutterLayer.lifetime = FrameRange::between(0, 2);
    shutterLayer.tracks.position.add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::easeInOut())
        .add(2.f / kFps, Vector3{30.f, 10.f, -15.f});
    shutterLayer.tracks.opacity.add(0.f, .2f, Easing::easeInOut()).add(2.f / kFps, .9f);
    shutterScene.setShutter(ShutterSettings{180.f, 8});
    CameraRig shutterRig(80, 50.f, 16.f / 9.f);
    auto shutterOptions = options;
    shutterOptions.cameraRig = &shutterRig;
    const auto shutterPlan = lowerToRenderPlan(shutterScene, shutterOptions);
    const auto calendar = chronon3d::render_plan::resolved_sample_frames(shutterPlan.canvas, shutterPlan.temporal_motion_blur);
    check(calendar.size() == 27, "three frames include integer times plus eight shutter samples each");
    check(shutterPlan.camera_samples && shutterPlan.camera_samples->size() == calendar.size(),
          "camera bake consumes the renderer-owned shutter calendar");
    auto explicitShutterOptions = shutterOptions;
    explicitShutterOptions.cameraSampleFrames = calendar;
    explicitShutterOptions.cameraSampleFrames.push_back(calendar.back() + 1.0);
    const auto explicitShutterPlan = lowerToRenderPlan(shutterScene, explicitShutterOptions);
    check(explicitShutterPlan.camera_samples &&
              explicitShutterPlan.camera_samples->size() == explicitShutterOptions.cameraSampleFrames.size(),
          "a complete explicit shutter calendar may include additional samples");
    explicitShutterOptions.cameraSampleFrames = calendar;
    explicitShutterOptions.cameraSampleFrames.erase(explicitShutterOptions.cameraSampleFrames.begin());
    bool missingShutterTimeRejected = false;
    try { (void)lowerToRenderPlan(shutterScene, explicitShutterOptions); }
    catch (const std::invalid_argument&) { missingShutterTimeRejected = true; }
    check(missingShutterTimeRejected, "incomplete explicit shutter calendar is rejected");
    explicitShutterOptions.cameraSampleFrames = {0., 1., 2.};
    bool integerOnlyShutterRejected = false;
    try { (void)lowerToRenderPlan(shutterScene, explicitShutterOptions); }
    catch (const std::invalid_argument&) { integerOnlyShutterRejected = true; }
    check(integerOnlyShutterRejected, "integer-only samples cannot approximate an exact shutter bake");
    const auto& resolvedLayer = shutterPlan.layers.front();
    check(!resolvedLayer.animation && resolvedLayer.transform_samples.size() == calendar.size(),
          "exact layer bake replaces approximate integer transform tracks");
    for (std::size_t index = calendar.size(); index-- > 0;) {
        const double time = calendar[index];
        const int whole = static_cast<int>(std::floor(time));
        shutterScene.evaluate(MotionTime{whole, kFps, static_cast<float>(time - whole)});
        const auto expected = shutterScene.compile().transforms.front();
        const auto& sample = resolvedLayer.transform_samples[index];
        check(sample.frame == time, "layer shutter timestamp is not rounded");
        checkNear(sample.position[0], expected.world.elements[12], kEps, "nonlinear layer X matches exact Motion evaluation");
        checkNear(sample.position[2], -expected.world.elements[14], kEps, "layer Z keeps the renderer conversion at shutter times");
        checkNear(sample.opacity, expected.visible ? expected.opacity : 0.f, kEps, "layer opacity matches exact Motion evaluation");
    }
    const auto shutterJson = nlohmann::json::parse(renderPlanToJson(shutterPlan));
    check(shutterJson["layers"][0]["transform_samples"].size() == calendar.size(),
          "resolved layer samples are emitted rather than silently dropped");
    for (std::size_t index = 0; index < calendar.size(); ++index)
        check(shutterJson["layers"][0]["transform_samples"][index]["frame"].get<double>() == calendar[index],
              "layer timestamps preserve double precision through JSON");
    (void)layer;
}

} // namespace

int main() {
    checkCameraBake();
    return chrononmotion_test::report();
}
