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
// Translation-only poses with identity orientation: rotation-convention
// parity (quaternion vs euler order) is owned by the camera lowering
// (`chrononCameraEuler` in RenderPlanLowering.cpp) and pinned by the camera
// bake round-trip, not here.

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"
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
    }
    return chrononmotion_test::report();
}
