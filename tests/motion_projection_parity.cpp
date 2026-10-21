// ChrononMotion — projection parity against the Chronon3D renderer contract.
//
// Two implementations project world points to screen pixels: Motion's
// `projectToScreen` (authoring side: safe-area checks, framing) and
// Chronon3D's `project_world_point` (render side: the pixels that ship).
// They must agree. The known, deliberate difference is the forward axis:
// Motion cameras look down local -Z while the render plan contract looks
// down +Z (the lowering mirrors depth at the boundary), so this test mirrors
// the camera position and compares everything else.
//
// The cases below include tilted/rolled poses after the actual RenderPlan
// lowering conversion, so quaternion-to-Euler and mirrored-Z parity are both
// exercised at the camera/render boundary.

#include "chrononmotion/math/Euler.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "motion_check.hpp"

// boundary-guardrail: allowed — this adapter test is the explicit projection
// parity boundary: it compares Motion authoring math against the renderer
// contract headers without touching content, shaping, or raster machinery.
#include <chronon3d/math/camera_projection_contract.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>

#include <array>
#include <cmath>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

constexpr float kEpsPx = 0.02f;

void checkOne(float viewportW, float viewportH, float fovDeg, float depth,
              float pointX, float pointY) {
    // Motion side: camera at +D looking down -Z at the z=0 plane.
    CameraPose pose;
    pose.position = Vector3(0.f, 0.f, depth);
    pose.fov = fovDeg;
    ScreenCameraIntrinsics intrinsics;
    intrinsics.viewport = Vector2(viewportW, viewportH);
    intrinsics.fovDegrees = fovDeg;
    const Vector2 motionScreen =
        projectToScreen(Vector3(pointX, pointY, 0.f), pose, intrinsics);

    // Renderer side: mirrored camera at -D looking down +Z, same lens.
    chronon3d::Camera2_5D camera;
    camera.position = chronon3d::Vec3(0.f, 0.f, -depth);
    camera.optics_mode = chronon3d::CameraOpticsMode::FieldOfView;
    camera.fov_deg = fovDeg;
    const chronon3d::camera_math::Viewport2D viewport{viewportW, viewportH};
    const chronon3d::camera_math::ProjectedPoint renderPoint = chronon3d::camera_math::project_world_point(
        camera, chronon3d::Vec3(pointX, pointY, 0.f), viewport);

    check(renderPoint.visible, "renderer contract sees the mirrored point");
    if (!renderPoint.visible) return;
    checkNear(motionScreen.x, renderPoint.screen.x, kEpsPx,
              "Motion and renderer agree on screen X");
    checkNear(motionScreen.y, renderPoint.screen.y, kEpsPx,
              "Motion and renderer agree on screen Y");
}

void checkFocal(float viewportH, float fovDeg) {
    chronon3d::Camera2_5D camera;
    camera.optics_mode = chronon3d::CameraOpticsMode::FieldOfView;
    camera.fov_deg = fovDeg;
    const chronon3d::camera_math::FocalPx focal = chronon3d::camera_math::focal_xy_from_camera(
        camera, viewportH, viewportH);
    const float expected =
        (viewportH * 0.5f) / std::tan(fovDeg * 0.5f * 3.14159265358979323846f / 180.f);
    checkNear(focal.y, expected, 1e-3f, "renderer focal matches the shared pinhole law");
    checkNear(focal.x, expected, 1e-3f, "renderer focal is square-pixel symmetric");
}

void checkPhysicalLensParity(float viewportW, float viewportH, float focalLengthMm) {
    constexpr float sensorWidthMm = 35.f;
    const float aspect = viewportW / viewportH;
    const float sensorHeightMm = sensorWidthMm / aspect;
    const float fovDeg = 2.f * std::atan(sensorHeightMm / (2.f * focalLengthMm)) *
                         180.f / 3.14159265358979323846f;

    CameraPose pose;
    pose.position = Vector3(0.f, 0.f, 1500.f);
    pose.fov = fovDeg;
    ScreenCameraIntrinsics intrinsics;
    intrinsics.viewport = Vector2(viewportW, viewportH);
    intrinsics.fovDegrees = fovDeg;

    chronon3d::Camera2_5D camera;
    camera.position = chronon3d::Vec3(0.f, 0.f, -1500.f);
    camera.optics_mode = chronon3d::CameraOpticsMode::PhysicalLens;
    camera.lens.focal_length = focalLengthMm;
    camera.lens.sensor_width = sensorWidthMm;
    camera.lens.sensor_height = sensorHeightMm;

    for (const auto& point : {Vector3(-320.f, -120.f, 0.f), Vector3(0.f, 0.f, 0.f),
                              Vector3(240.f, 180.f, 0.f)}) {
        const Vector2 motion = projectToScreen(point, pose, intrinsics);
        const auto render = chronon3d::camera_math::project_world_point(
            camera, chronon3d::Vec3(point.x, point.y, point.z),
            chronon3d::camera_math::Viewport2D{viewportW, viewportH});
        check(render.visible, "physical-lens parity point is visible");
        if (!render.visible) continue;
        checkNear(motion.x, render.screen.x, kEpsPx,
                  "35 mm physical-lens horizontal projection matches Motion FOV");
        checkNear(motion.y, render.screen.y, kEpsPx,
                  "35 mm physical-lens vertical projection matches Motion FOV");
    }
}

void checkLoweredOrientationParity(float viewportW, float viewportH,
                                   float rx, float ry, float rz) {
    CameraPose pose;
    pose.position = Vector3(90.f, -65.f, 1700.f);
    pose.orientation.setFromEuler(Euler(rx, ry, rz));
    pose.fov = 52.f;
    const auto lowered = lowerCameraPose(pose, 0.1f, 10000.f);

    chronon3d::Camera2_5D camera;
    camera.position = {lowered.position[0], lowered.position[1], lowered.position[2]};
    camera.rotation = {lowered.rotation_deg[0], lowered.rotation_deg[1], lowered.rotation_deg[2]};
    camera.optics_mode = chronon3d::CameraOpticsMode::FieldOfView;
    camera.fov_deg = lowered.fov_deg;

    Vector3 forward(0.f, 0.f, -1.f);
    Vector3 right(1.f, 0.f, 0.f);
    Vector3 up(0.f, 1.f, 0.f);
    forward.applyQuaternion(pose.orientation).normalize();
    right.applyQuaternion(pose.orientation).normalize();
    up.applyQuaternion(pose.orientation).normalize();
    for (const auto& offset : {Vector2(-260.f, -130.f), Vector2(0.f, 0.f), Vector2(230.f, 170.f)}) {
        Vector3 world = pose.position.clone().add(forward.clone().multiplyScalar(1050.f))
            .add(right.clone().multiplyScalar(offset.x)).add(up.clone().multiplyScalar(offset.y));
        ScreenCameraIntrinsics intrinsics;
        intrinsics.viewport = {viewportW, viewportH};
        intrinsics.fovDegrees = pose.fov;
        const Vector2 expected = projectToScreen(world, pose, intrinsics);
        const auto actual = chronon3d::camera_math::project_world_point(
            camera, chronon3d::Vec3(world.x, world.y, -world.z),
            chronon3d::camera_math::Viewport2D{viewportW, viewportH});
        check(actual.visible, "lowered rotated camera sees its forward-axis point");
        if (!actual.visible) continue;
        checkNear(expected.x, actual.screen.x, kEpsPx,
                  "RenderPlan-lowered quaternion preserves rotated screen X");
        checkNear(expected.y, actual.screen.y, kEpsPx,
                  "RenderPlan-lowered quaternion preserves rotated screen Y");
    }
}

} // namespace

int main() {
    section("Motion vs renderer projection parity (translation-only, mirrored Z)");
    constexpr std::array<float, 4> widths{1920.f, 1080.f, 1080.f, 640.f};
    constexpr std::array<float, 4> heights{1080.f, 1080.f, 1920.f, 480.f};
    constexpr std::array<float, 3> fovs{35.f, 50.f, 70.f};
    constexpr std::array<float, 3> depths{800.f, 1500.f, 3000.f};
    constexpr std::array<float, 5> xs{-400.f, -150.f, 0.f, 150.f, 400.f};
    constexpr std::array<float, 3> ys{-200.f, 0.f, 200.f};
    for (std::size_t v = 0; v < widths.size(); ++v) {
        for (float fov : fovs) {
            checkFocal(heights[v], fov);
            for (float depth : depths) {
                for (float x : xs) {
                    for (float y : ys) {
                        checkOne(widths[v], heights[v], fov, depth, x, y);
                    }
                }
            }
        }
        checkPhysicalLensParity(widths[v], heights[v], 50.f);
        checkLoweredOrientationParity(widths[v], heights[v], 0.12f, -0.18f, 0.07f);
        checkLoweredOrientationParity(widths[v], heights[v], -0.22f, 0.09f, -0.14f);
        checkLoweredOrientationParity(widths[v], heights[v], 0.f, 0.f, 0.31f);
        checkLoweredOrientationParity(widths[v], heights[v], 1.35f, 1.54f, -0.27f);
    }

    section("CAM-01 FOV changes framing on the same projected subject");
    chronon3d::Camera2_5D wide;
    wide.position = chronon3d::Vec3(0.f, 0.f, -1000.f);
    wide.optics_mode = chronon3d::CameraOpticsMode::FieldOfView;
    wide.fov_deg = 50.f;
    auto narrow = wide;
    narrow.fov_deg = 30.f;
    const chronon3d::camera_math::Viewport2D viewport{1920.f, 1080.f};
    const chronon3d::Vec3 offAxisPoint{250.f, 120.f, 0.f};
    const auto wideFrame = chronon3d::camera_math::project_world_point(wide, offAxisPoint, viewport);
    const auto narrowFrame = chronon3d::camera_math::project_world_point(narrow, offAxisPoint, viewport);
    check(wideFrame.visible && narrowFrame.visible, "CAM-01 subject is inside both FOVs");
    if (wideFrame.visible && narrowFrame.visible)
        check(std::abs(wideFrame.screen.x - narrowFrame.screen.x) > 100.f,
              "CAM-01 changing FOV from 50 to 30 degrees measurably changes framing");
    return chrononmotion_test::report();
}
