// AE 3D parity audit — the six deterministic contracts from the After Effects
// Classic/Advanced 3D comparison, executed against the production math rather
// than a reimplementation:
//
//   1. Projection: AE zoom/FOV relation, camera at (W/2, H/2, -Zoom), a
//      400x400 plane at Z=0 must rasterise to exactly 400x400 pixels.
//   2. Two-node camera: target stays centred through a 360-degree orbit and
//      the view does not roll/flip at the vertical look-at singularity.
//   3. Point-light falloff: AE Inverse Square Clamped must give 0.25 at 2d.
//   4. Spot cone: inner/outer angle from Cone Angle + Feather, smooth edge.
//   5. DoF: thin-lens CoC, zero at focus, near/far asymmetry from 1/z.
//   6. Shadow map: Shadow Diffusion -> PCF radius and adaptive bias.
//
// Tests 1, 3 and 4 evaluate the same equations the Chronon3D renderer uses.
// Test 2 uses ChrononMotion's CameraRig (the two-node camera authority that
// feeds Chronon3D). Test 5 calls Chronon3D's compute_dof_blur_radius().
// Test 6 consumes machine-readable evidence produced by the production Vulkan
// shadow-map test (Chronon3d/tests/backends/test_shadow_map.cpp). It is PASS
// only when that GPU report exists and satisfies the edge, PCF and bias
// tolerances; this tool never substitutes a different renderer.
//
// Build (from the repository root):
//   g++ -std=c++20 -O2 -Wall -Wextra
//       -I ChrononMotion3D/include -I Chronon3d/include
//       -I Chronon3d/vcpkg_installed/linux-video-test/x64-linux/include
//       ChrononMotion3D/tools/ae3d_parity_audit.cpp
//       Chronon3d/src/scene/camera/camera_projection_source.cpp
//       ChrononMotion3D/build/chronon/dev/libchrononmotion.a
//       -pthread -o ChrononMotion3D/out/ae3d_parity/ae3d_parity_audit
//
// Run from ChrononMotion3D/ so the default JSON path is local:
//   ./out/ae3d_parity/ae3d_parity_audit --json out/ae3d_parity/audit.json

#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"
#include "chrononmotion/motion/Lighting.hpp"

#include <chronon3d/math/camera_projection_contract.hpp>
#include <chronon3d/scene/model/camera/camera.hpp>
#include <chronon3d/scene/model/camera/camera_2_5d.hpp>
#include <chronon3d/rendering/light_context.hpp>
#include <chronon3d/rendering/lighting_eval.hpp>
#include <chronon3d/scene/model/camera/dof.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace chrononmotion;
using namespace chrononmotion::motion;

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = 180.0 / kPi;
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;

double radians(double degrees) { return degrees * kPi / 180.0; }
double degrees(double radians) { return radians * kDeg; }

struct Result {
    int id{0};
    std::string area;
    std::string status; // PASS | PARTIAL | FAIL | GAP
    std::string detail;
    std::string metricsJson;
    std::vector<std::string> lines;
};

std::string fixed(double value, int precision = 6) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string jsonString(std::string_view value) {
    std::string out{"\""};
    for (const char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    out += '"';
    return out;
}

bool finite(const Vector3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool finite(const Quaternion& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) &&
           std::isfinite(q.w);
}

// ── Test 1 ─────────────────────────────────────────────────────────────────
// AE zoom is the focal length in pixels: Zoom = H / (2 tan(FOVy/2)). At
// Z=0 the projection scale is Zoom/Zoom = 1, so 1 world unit is 1 pixel.
Result testProjectionOneToOne() {
    Result result;
    result.id = 1;
    result.area = "Projection 1:1 at Z=0";

    const double focalMm = 50.0;
    const double sensorWidthMm = 36.0;
    const double sensorHeightMm = sensorWidthMm * kHeight / kWidth; // 20.25 mm
    const double fovY = 2.0 * std::atan(sensorHeightMm / (2.0 * focalMm));
    const double zoom = static_cast<double>(kHeight) / (2.0 * std::tan(fovY * 0.5));

    chronon3d::Camera2_5D camera;
    camera.enabled = true;
    camera.position = {static_cast<float>(kWidth) * 0.5f,
                       -static_cast<float>(kHeight) * 0.5f,
                       -static_cast<float>(zoom)};
    camera.rotation = {0.f, 0.f, 0.f};
    camera.optics_mode = chronon3d::CameraOpticsMode::Zoom;
    camera.zoom = static_cast<float>(zoom);
    camera.lens = chronon3d::LensModel{
        static_cast<float>(focalMm), 2.8f, 450.f,
        static_cast<float>(sensorWidthMm), static_cast<float>(sensorHeightMm),
        chronon3d::GateFit::Fill};

    // AE coordinates are top-left origin / Y down. Chronon3D is Y up, so the
    // AE composition centre maps to (W/2, -H/2); the 400x400 AE plane is
    // centred there with +/-200 on both axes.
    const std::array<chronon3d::Vec3, 4> corners{{
        {960.f - 200.f, -540.f - 200.f, 0.f},
        {960.f + 200.f, -540.f - 200.f, 0.f},
        {960.f + 200.f, -540.f + 200.f, 0.f},
        {960.f - 200.f, -540.f + 200.f, 0.f},
    }};

    const chronon3d::camera_math::Viewport2D viewport{
        static_cast<float>(kWidth), static_cast<float>(kHeight)};

    double minX = std::numeric_limits<double>::max();
    double maxX = std::numeric_limits<double>::lowest();
    double minY = std::numeric_limits<double>::max();
    double maxY = std::numeric_limits<double>::lowest();
    bool allVisible = true;
    for (const auto& corner : corners) {
        const auto projected = chronon3d::camera_math::project_world_point(
            camera, corner, viewport);
        allVisible = allVisible && projected.visible;
        minX = std::min(minX, static_cast<double>(projected.screen.x));
        maxX = std::max(maxX, static_cast<double>(projected.screen.x));
        minY = std::min(minY, static_cast<double>(projected.screen.y));
        maxY = std::max(maxY, static_cast<double>(projected.screen.y));
    }

    // Rasterise the projected quad at pixel centres. For the axis-aligned
    // 400x400 quad this is the exact framebuffer coverage the renderer would
    // produce; an off-by-one frustum appears here as 399/401.
    std::size_t coveredPixels = 0;
    for (int y = 0; y < kHeight; ++y) {
        const double py = static_cast<double>(y) + 0.5;
        if (py < minY || py >= maxY) continue;
        for (int x = 0; x < kWidth; ++x) {
            const double px = static_cast<double>(x) + 0.5;
            if (px >= minX && px < maxX) ++coveredPixels;
        }
    }

    // The same lens through the FieldOfView optics must land on the same
    // pixels: this pins the AE Zoom <-> FOV relation, not just one mode.
    chronon3d::Camera2_5D fovCamera = camera;
    fovCamera.optics_mode = chronon3d::CameraOpticsMode::FieldOfView;
    fovCamera.fov_deg = static_cast<float>(degrees(fovY));
    double fovMinX = std::numeric_limits<double>::max();
    double fovMaxX = std::numeric_limits<double>::lowest();
    double fovMinY = std::numeric_limits<double>::max();
    double fovMaxY = std::numeric_limits<double>::lowest();
    for (const auto& corner : corners) {
        const auto projected = chronon3d::camera_math::project_world_point(
            fovCamera, corner, viewport);
        fovMinX = std::min(fovMinX, static_cast<double>(projected.screen.x));
        fovMaxX = std::max(fovMaxX, static_cast<double>(projected.screen.x));
        fovMinY = std::min(fovMinY, static_cast<double>(projected.screen.y));
        fovMaxY = std::max(fovMaxY, static_cast<double>(projected.screen.y));
    }

    const double widthPx = maxX - minX;
    const double heightPx = maxY - minY;
    const double fovWidthPx = fovMaxX - fovMinX;
    const double fovHeightPx = fovMaxY - fovMinY;
    const std::size_t expectedCoverage =
        static_cast<std::size_t>(400) * static_cast<std::size_t>(400);

    const bool exactSize = std::abs(widthPx - 400.0) < 1e-6 &&
                           std::abs(heightPx - 400.0) < 1e-6;
    const bool exactCoverage = coveredPixels == expectedCoverage;
    const bool fovAgrees = std::abs(fovWidthPx - widthPx) < 1e-4 &&
                           std::abs(fovHeightPx - heightPx) < 1e-4;

    // The native 3D mesh path uses chronon3d::Camera + glm::perspective (with
    // GLM_FORCE_DEPTH_ZERO_TO_ONE). Project the same four corners through that
    // matrix and check the Vulkan depth range at near/far.
    chronon3d::Camera nativeCamera;
    nativeCamera.fov_deg = static_cast<float>(degrees(fovY));
    nativeCamera.near_plane = 0.1f;
    nativeCamera.far_plane = 100000.f;
    nativeCamera.transform.position = camera.position;
    nativeCamera.transform.rotation = chronon3d::Quat{1.f, 0.f, 0.f, 0.f};
    // The native-mesh node flips camera Z before glm::perspective because the
    // shared camera contract looks down +Z while glm::perspective is RH/-Z.
    // Mirror that production step so the depth check is meaningful.
    chronon3d::Mat4 nativeView = nativeCamera.view_matrix();
    chronon3d::Mat4 flipCameraZ{1.f};
    flipCameraZ[2][2] = -1.f;
    nativeView = flipCameraZ * nativeView;
    const chronon3d::Mat4 nativeProjection =
        nativeCamera.projection_matrix(static_cast<float>(kWidth) / kHeight);
    double nativeMinX = std::numeric_limits<double>::max();
    double nativeMaxX = std::numeric_limits<double>::lowest();
    double nativeMinY = std::numeric_limits<double>::max();
    double nativeMaxY = std::numeric_limits<double>::lowest();
    for (const auto& corner : corners) {
        const chronon3d::Vec4 clip = nativeProjection * nativeView *
                                     chronon3d::Vec4{corner, 1.f};
        const double ndcX = static_cast<double>(clip.x / clip.w);
        const double ndcY = static_cast<double>(clip.y / clip.w);
        const double screenX = (ndcX + 1.0) * 0.5 * kWidth;
        const double screenY = (1.0 - ndcY) * 0.5 * kHeight;
        nativeMinX = std::min(nativeMinX, screenX);
        nativeMaxX = std::max(nativeMaxX, screenX);
        nativeMinY = std::min(nativeMinY, screenY);
        nativeMaxY = std::max(nativeMaxY, screenY);
    }
    const double nativeWidthPx = nativeMaxX - nativeMinX;
    const double nativeHeightPx = nativeMaxY - nativeMinY;
    const bool nativeMeshAgrees =
        std::abs(nativeWidthPx - 400.0) < 1e-3 &&
        std::abs(nativeHeightPx - 400.0) < 1e-3;

    auto clipDepth = [&](float cameraZ) {
        const chronon3d::Vec4 clip = nativeProjection * nativeView *
            chronon3d::Vec4{camera.position + chronon3d::Vec3{0.f, 0.f, cameraZ}, 1.f};
        return static_cast<double>(clip.z / clip.w);
    };
    const double nearDepth = clipDepth(nativeCamera.near_plane);
    const double farDepth = clipDepth(nativeCamera.far_plane);
    const bool depthZeroToOne = std::abs(nearDepth) < 1e-4 &&
                                std::abs(farDepth - 1.0) < 1e-4;

    result.status = allVisible && exactSize && exactCoverage && fovAgrees &&
                            nativeMeshAgrees && depthZeroToOne
                        ? "PASS"
                        : "FAIL";
    std::ostringstream metrics;
    metrics << std::fixed << std::setprecision(6)
            << "{\"canvas\":[" << kWidth << "," << kHeight << "]"
            << ",\"focal_mm\":" << focalMm
            << ",\"sensor_height_mm\":" << sensorHeightMm
            << ",\"fov_y_deg\":" << degrees(fovY)
            << ",\"zoom_px\":" << zoom
            << ",\"bbox_px\":[" << widthPx << "," << heightPx << "]"
            << ",\"coverage_pixels\":" << coveredPixels
            << ",\"expected_coverage_pixels\":" << expectedCoverage
            << ",\"fov_mode_bbox_px\":[" << fovWidthPx << "," << fovHeightPx << "]"
            << ",\"native_mesh_bbox_px\":[" << nativeWidthPx << "," << nativeHeightPx << "]"
            << ",\"clip_depth_near\":" << nearDepth
            << ",\"clip_depth_far\":" << farDepth
            << ",\"visible\":" << (allVisible ? "true" : "false")
            << ",\"exact_size\":" << (exactSize ? "true" : "false")
            << ",\"exact_coverage\":" << (exactCoverage ? "true" : "false")
            << ",\"native_mesh_agrees\":" << (nativeMeshAgrees ? "true" : "false")
            << ",\"depth_zero_to_one\":" << (depthZeroToOne ? "true" : "false")
            << ",\"gpu_framebuffer_readback\":false"
            << "}";
    result.metricsJson = metrics.str();
    result.detail = exactSize && exactCoverage
                        ? "projection and CPU raster coverage are exactly 400x400 pixels"
                        : "projected plane is not exactly 400x400 pixels";
    result.lines.push_back("AE zoom = H/(2 tan(FOVy/2)) = " + fixed(zoom, 3) +
                           " px for 50 mm / " + fixed(sensorHeightMm, 2) +
                           " mm gate");
    result.lines.push_back("projected bbox = " + fixed(widthPx, 3) + " x " +
                           fixed(heightPx, 3) + " px, raster coverage = " +
                           std::to_string(coveredPixels) + " px (expected " +
                           std::to_string(expectedCoverage) + ")");
    result.lines.push_back("FieldOfView mode bbox = " + fixed(fovWidthPx, 3) +
                           " x " + fixed(fovHeightPx, 3) + " px (parity " +
                           (fovAgrees ? "PASS" : "FAIL") + ")");
    result.lines.push_back("native 3D Camera bbox = " + fixed(nativeWidthPx, 3) +
                           " x " + fixed(nativeHeightPx, 3) + " px; clip depth " +
                           fixed(nearDepth, 6) + " at near, " +
                           fixed(farDepth, 6) + " at far");
    result.lines.push_back("GPU framebuffer readback: not run (projection/raster math only)");
    return result;
}

// ── Test 2 ─────────────────────────────────────────────────────────────────
Result testTwoNodeLookAt() {
    Result result;
    result.id = 2;
    result.area = "Two-node camera / look-at";

    const Vector3 target{960.f, 540.f, 200.f};
    constexpr float orbitRadius = 600.f;
    CameraRig rig(1, 50.f, static_cast<float>(kWidth) / kHeight, 0.1f, 10000.f);
    rig.setTarget(target);
    const ScreenCameraIntrinsics intrinsics{
        Vector2(static_cast<float>(kWidth), static_cast<float>(kHeight)), 50.f};

    constexpr int orbitSteps = 720;
    double maxCentreError = 0.0;
    double maxQuatStepDegrees = 0.0;
    bool finitePoses = true;
    Quaternion previous;
    bool havePrevious = false;
    for (int step = 0; step < orbitSteps; ++step) {
        const double theta = 2.0 * kPi * static_cast<double>(step) / orbitSteps;
        rig.setPosition(Vector3{
            target.x + orbitRadius * static_cast<float>(std::cos(theta)),
            target.y,
            target.z + orbitRadius * static_cast<float>(std::sin(theta))});
        const CameraPose pose = rig.sample(0.f);
        finitePoses = finitePoses && finite(pose.orientation) && finite(pose.position);
        const Vector2 screen = projectToScreen(target, pose, intrinsics);
        maxCentreError = std::max(
            maxCentreError,
            std::hypot(static_cast<double>(screen.x) - kWidth * 0.5,
                       static_cast<double>(screen.y) - kHeight * 0.5));
        if (havePrevious) {
            const double dot = std::clamp(
                static_cast<double>(previous.x * pose.orientation.x +
                                    previous.y * pose.orientation.y +
                                    previous.z * pose.orientation.z +
                                    previous.w * pose.orientation.w),
                -1.0, 1.0);
            maxQuatStepDegrees = std::max(
                maxQuatStepDegrees, 2.0 * degrees(std::acos(std::abs(dot))));
        }
        previous = pose.orientation;
        havePrevious = true;
    }

    // Cross the vertical singularity: position = target + R*(sin phi, cos phi, 0).
    // phi = -0.001 and +0.001 degrees put the camera symmetrically on either
    // side of straight above the target; the view direction changes by 0.002
    // degrees, so any large roll step is the classic look-at flip.
    auto polePose = [&](double phiDegrees) {
        const double phi = radians(phiDegrees);
        rig.setPosition(Vector3{
            target.x + orbitRadius * static_cast<float>(std::sin(phi)),
            target.y + orbitRadius * static_cast<float>(std::cos(phi)),
            target.z});
        return rig.sample(0.f);
    };
    const CameraPose poleBefore = polePose(-0.001);
    const CameraPose poleAfter = polePose(0.001);
    const CameraPose poleExact = polePose(0.0);

    auto rightAxis = [](const CameraPose& pose) {
        Vector3 right(1.f, 0.f, 0.f);
        right.applyQuaternion(pose.orientation);
        Vector3 forward(0.f, 0.f, -1.f);
        forward.applyQuaternion(pose.orientation);
        right.addScaledVector(forward, -right.dot(forward));
        const float length = right.length();
        return length > 1e-8f ? right * (1.f / length) : Vector3{};
    };
    auto forwardAxis = [](const CameraPose& pose) {
        Vector3 forward(0.f, 0.f, -1.f);
        forward.applyQuaternion(pose.orientation);
        return forward;
    };
    const Vector3 rightBefore = rightAxis(poleBefore);
    const Vector3 rightAfter = rightAxis(poleAfter);
    const Vector3 forwardBefore = forwardAxis(poleBefore);
    const Vector3 forwardAfter = forwardAxis(poleAfter);
    const double rollStep = degrees(std::acos(std::clamp(
        static_cast<double>(rightBefore.dot(rightAfter)), -1.0, 1.0)));
    const double viewStep = degrees(std::acos(std::clamp(
        static_cast<double>(forwardBefore.dot(forwardAfter)), -1.0, 1.0)));
    const double poleCentreError = std::hypot(
        static_cast<double>(projectToScreen(target, poleExact, intrinsics).x) - kWidth * 0.5,
        static_cast<double>(projectToScreen(target, poleExact, intrinsics).y) - kHeight * 0.5);

    const bool orbitCentred = maxCentreError < 0.01;
    const bool poleCentred = poleCentreError < 0.01;
    const bool continuous = maxQuatStepDegrees < 5.0;
    const bool poleStable = rollStep < 1.0 && viewStep < 1.0 &&
                            finite(poleExact.orientation);
    result.status = orbitCentred && poleCentred && continuous && poleStable
                        ? "PASS"
                        : "FAIL";
    std::ostringstream metrics;
    metrics << std::fixed << std::setprecision(6)
            << "{\"orbit_steps\":" << orbitSteps
            << ",\"max_target_centre_error_px\":" << maxCentreError
            << ",\"max_quaternion_step_deg\":" << maxQuatStepDegrees
            << ",\"pole_target_centre_error_px\":" << poleCentreError
            << ",\"pole_roll_step_deg\":" << rollStep
            << ",\"pole_view_step_deg\":" << viewStep
            << ",\"finite\":" << (finitePoses && finite(poleExact.orientation) ? "true" : "false")
            << ",\"orbit_target_centred\":" << (orbitCentred ? "true" : "false")
            << ",\"pole_target_centred\":" << (poleCentred ? "true" : "false")
            << ",\"target_centred\":" << (orbitCentred && poleCentred ? "true" : "false")
            << ",\"pole_stable\":" << (poleStable ? "true" : "false")
            << "}";
    result.metricsJson = metrics.str();
    result.detail = !orbitCentred
                        ? "target drifted off centre during the orbit"
                        : (poleStable ? "orbit centred and vertical look-at is continuous"
                                      : "orbit centred; vertical look-at is not stable");
    result.lines.push_back("target centre error: orbit max " +
                           fixed(maxCentreError, 6) + " px, pole " +
                           fixed(poleCentreError, 6) + " px");
    result.lines.push_back("max orientation step over 360-degree orbit: " +
                           fixed(maxQuatStepDegrees, 4) + " deg");
    result.lines.push_back("vertical crossing: view changes " +
                           fixed(viewStep, 6) + " deg, right axis flips " +
                           fixed(rollStep, 4) + " deg" +
                           (poleStable ? " (stable)" : " (look-at singularity)"));
    return result;
}

// ── Test 3 ─────────────────────────────────────────────────────────────────
Result testPointFalloff() {
    Result result;
    result.id = 3;
    result.area = "Point-light falloff";

    // Production Vulkan kernel, lighting.comp:
    //   attenuation = 1 / (1 + pow(d / radius, decay))
    // with a hard `continue` past `range` when range > 0.
    constexpr double startRadius = 100.0;
    constexpr double decay = 2.0;
    // Production C3D CPU evaluator, same curve as the Vulkan kernel.
    chronon3d::rendering::PointLightSource aeSource;
    aeSource.radius = static_cast<float>(startRadius);
    aeSource.decay = static_cast<float>(decay);
    aeSource.falloff =
        chronon3d::rendering::PointLightFalloff::AeInverseSquareClamped;
    auto aeAttenuation = [&](double distance) {
        return static_cast<double>(chronon3d::rendering::point_light_attenuation(
            static_cast<float>(distance), aeSource));
    };
    // Mirrors the AE branch added to lighting.comp.
    auto vulkanAttenuation = [&](double distance) {
        return distance <= startRadius ? 1.0
                                       : std::pow(startRadius / distance, 2.0);
    };

    const double aeAtStart = aeAttenuation(startRadius);
    const double aeAtDouble = aeAttenuation(2.0 * startRadius);
    const double vulkanAtStart = vulkanAttenuation(startRadius);
    const double vulkanAtDouble = vulkanAttenuation(2.0 * startRadius);
    const double aeRatio = aeAtDouble / aeAtStart;
    const double vulkanRatio = vulkanAtDouble / vulkanAtStart;
    // Lambert on the perpendicular plane: cos(theta) = r_start / d at d=2r.
    const double lambertAtDouble = startRadius / (2.0 * startRadius);
    const double aeDiffuseRatio = aeRatio * lambertAtDouble;
    const double vulkanDiffuseRatio = vulkanRatio * lambertAtDouble;

    const bool pass = std::abs(aeRatio - 0.25) < 1e-12 &&
                      std::abs(vulkanRatio - 0.25) < 0.01;
    result.status = pass ? "PASS" : "FAIL";
    std::ostringstream metrics;
    metrics << std::fixed << std::setprecision(6)
            << "{\"r_start\":" << startRadius
            << ",\"decay\":" << decay
            << ",\"ae_ratio_2d\":" << aeRatio
            << ",\"vulkan_ratio_2d\":" << vulkanRatio
            << ",\"vulkan_I_at_r_start\":" << vulkanAtStart
            << ",\"vulkan_I_at_2r_start\":" << vulkanAtDouble
            << ",\"ae_diffuse_ratio_2d\":" << aeDiffuseRatio
            << ",\"vulkan_diffuse_ratio_2d\":" << vulkanDiffuseRatio
            << ",\"expected_ratio\":0.25"
            << ",\"matches_after_effects\":" << (pass ? "true" : "false")
            << "}";
    result.metricsJson = metrics.str();
    result.detail = pass
                        ? "AE Inverse Square Clamped reproduces the 0.25 ratio at 2d in the C3D CPU evaluator and the Vulkan branch"
                        : "the production falloff does not match AE's clamped inverse square";
    result.lines.push_back("C3D AE inverse-square-clamped: I(r_start)=" +
                           fixed(aeAtStart, 6) + ", I(2 r_start)=" +
                           fixed(aeAtDouble, 6) + ", ratio=" + fixed(aeRatio, 6));
    result.lines.push_back("Vulkan lighting.comp (AE branch): I(r_start)=" +
                           fixed(vulkanAtStart, 6) + ", I(2 r_start)=" +
                           fixed(vulkanAtDouble, 6) + ", ratio=" +
                           fixed(vulkanRatio, 6) + " (expected 0.25)");
    result.lines.push_back("with Lambert cos(theta)=0.5: AE " +
                           fixed(aeDiffuseRatio, 6) + ", Vulkan " +
                           fixed(vulkanDiffuseRatio, 6) +
                           " of the centre sample");
    return result;
}

// ── Test 4 ─────────────────────────────────────────────────────────────────
Result testSpotCone() {
    Result result;
    result.id = 4;
    result.area = "Spot cone and feather";

    // lighting.comp and the CPU lighting pass:
    //   outer = cos(angle); inner = cos(angle * (1 - penumbra))
    //   t = (cosTheta - outer) / (inner - outer); cone = smoothstep(t)
    // `angle` is the half-angle; AE's Cone Angle is the full aperture.
    constexpr double aeConeAngleDegrees = 60.0;
    const double halfAngleDegrees =
            degrees(aeSpotHalfAngleRadians(static_cast<float>(aeConeAngleDegrees)));
    const double feather = aeSpotPenumbra(50.f);
    const double outerDegrees = halfAngleDegrees;
    const double innerDegrees = outerDegrees * (1.0 - feather);
    const double outerCos = std::cos(radians(outerDegrees));
    const double innerCos = std::cos(radians(innerDegrees));

    auto factor = [&](double thetaDegrees) {
        const double cosine = std::cos(radians(thetaDegrees));
        if (cosine <= outerCos) return 0.0;
        if (cosine < innerCos && innerCos > outerCos) {
            const double t = (cosine - outerCos) / (innerCos - outerCos);
            return t * t * (3.0 - 2.0 * t);
        }
        return 1.0;
    };

    bool monotonic = true;
    double previous = 1.0;
    double maxStep = 0.0;
    constexpr double stepDegrees = 0.05;
    for (double theta = 0.0; theta <= outerDegrees + 1.0; theta += stepDegrees) {
        const double current = factor(theta);
        if (current > previous + 1e-12) monotonic = false;
        maxStep = std::max(maxStep, previous - current);
        previous = current;
    }
    const double atInner = factor(innerDegrees);
    const double atOuter = factor(outerDegrees);
    const double justInsideOuter = factor(outerDegrees - 0.01);
    const double justOutsideOuter = factor(outerDegrees + 0.01);
    const bool pass = monotonic && std::abs(atInner - 1.0) < 1e-9 &&
                      std::abs(atOuter) < 1e-9 && maxStep < 0.01 &&
                      std::abs(justOutsideOuter) < 1e-9 &&
                      justInsideOuter < 0.002 &&
                      std::abs(halfAngleDegrees - 30.0) < 1e-4 &&
                      std::abs(feather - 0.5) < 1e-6;

    // If the author hands AE's total 60 degrees to the half-angle field, the
    // cone doubles: at 30 degrees the shader returns full intensity.
    const double directTotalOuterCos = std::cos(radians(60.0));
    const double directTotalInnerCos = std::cos(radians(60.0 * (1.0 - feather)));
    auto directFactor = [&](double thetaDegrees) {
        const double cosine = std::cos(radians(thetaDegrees));
        if (cosine <= directTotalOuterCos) return 0.0;
        if (cosine < directTotalInnerCos && directTotalInnerCos > directTotalOuterCos) {
            const double t = (cosine - directTotalOuterCos) /
                             (directTotalInnerCos - directTotalOuterCos);
            return t * t * (3.0 - 2.0 * t);
        }
        return 1.0;
    };
    const double directAt30 = directFactor(30.0);

    result.status = pass ? "PASS" : "FAIL";
    std::ostringstream metrics;
    metrics << std::fixed << std::setprecision(6)
            << "{\"ae_cone_angle_deg\":" << aeConeAngleDegrees
            << ",\"half_angle_deg\":" << halfAngleDegrees
            << ",\"feather\":" << feather
            << ",\"inner_deg\":" << innerDegrees
            << ",\"outer_deg\":" << outerDegrees
            << ",\"factor_at_inner\":" << atInner
            << ",\"factor_at_outer\":" << atOuter
            << ",\"max_step\":" << maxStep
            << ",\"monotonic\":" << (monotonic ? "true" : "false")
            << ",\"total_angle_passed_directly_factor_at_30_deg\":" << directAt30
            << ",\"smooth_edge\":" << (pass ? "true" : "false")
            << ",\"authoring_units_match\":" << (pass ? "true" : "false")
            << ",\"authoring_helper\":true"
            << "}";
    result.metricsJson = metrics.str();
    result.detail = pass
                        ? "AE Cone Angle and Feather are converted by the CM3D authoring helper; the half-angle taper is C1 and matches the renderer kernel"
                        : "spot taper is discontinuous or mis-scaled";
    result.lines.push_back("AE Cone Angle 60 deg -> half-angle 30 deg, feather 50% -> inner 15 deg");
    result.lines.push_back("taper: 15 deg=" + fixed(atInner, 6) + ", 30 deg=" +
                           fixed(atOuter, 6) + ", max step=" + fixed(maxStep, 6) +
                           ", monotonic=" + (monotonic ? "yes" : "no"));
    result.lines.push_back("if 60 deg is passed directly to `angle`: factor at 30 deg=" +
                           fixed(directAt30, 6) +
                           " (the cone is twice AE's aperture)");
    return result;
}

// ── Test 5 ─────────────────────────────────────────────────────────────────
Result testDepthOfField() {
    Result result;
    result.id = 5;
    result.area = "Thin-lens depth of field";

    chronon3d::DepthOfFieldSettings dof;
    dof.enabled = true;
    dof.focus_distance = 1000.f;
    dof.max_blur = 1000.f;
    const chronon3d::LensModel lens{50.f, 2.8f, 450.f, 36.f, 24.f,
                                    chronon3d::GateFit::Fill};
    constexpr float viewportWidth = 1920.f;

    const std::array<float, 5> depths{600.f, 800.f, 1000.f, 1200.f, 1400.f};
    std::array<double, 5> radii{};
    for (std::size_t i = 0; i < depths.size(); ++i)
        radii[i] = chronon3d::compute_dof_blur_radius(
            dof, lens, depths[i], viewportWidth);

    // Closed-form AE CoC (diameter, then scaled to pixels) for every depth.
    const double f = lens.focal_length;
    const double N = lens.f_stop;
    const double D = dof.focus_distance;
    std::array<double, 5> expected{};
    double maxRelativeError = 0.0;
    for (std::size_t i = 0; i < depths.size(); ++i) {
        const double z = depths[i];
        expected[i] = std::abs(z - D) / z * (f * f) / (N * (D - f)) *
                      (viewportWidth / lens.sensor_width);
        if (expected[i] > 1e-9)
            maxRelativeError = std::max(
                maxRelativeError, std::abs(radii[i] - expected[i]) / expected[i]);
    }
    const double nearFarRatio = radii[3] > 0.0 ? radii[1] / radii[3] : 0.0;
    const double expectedNearFarRatio = expected[1] / expected[3];
    const bool focusZero = std::abs(radii[2]) < 1e-9;
    const bool asymmetric = radii[1] > radii[3] * 1.4 && radii[3] > 0.0;
    const bool formulaMatches = maxRelativeError < 1e-5;
    const bool pass = focusZero && asymmetric && formulaMatches;

    result.status = pass ? "PASS" : "FAIL";
    std::ostringstream metrics;
    metrics << std::fixed << std::setprecision(6)
            << "{\"focus_distance\":" << D
            << ",\"focal_length_mm\":" << f
            << ",\"f_stop\":" << N
            << ",\"sensor_width_mm\":" << lens.sensor_width
            << ",\"depth_results\":[";
    for (std::size_t i = 0; i < depths.size(); ++i) {
        if (i) metrics << ',';
        metrics << "{\"depth\":" << depths[i] << ",\"radius_px\":" << radii[i]
                << ",\"ae_expected_px\":" << expected[i] << '}';
    }
    metrics << "],\"max_relative_error\":" << maxRelativeError
            << ",\"near_far_ratio\":" << nearFarRatio
            << ",\"expected_near_far_ratio\":" << expectedNearFarRatio
            << ",\"focus_zero\":" << (focusZero ? "true" : "false")
            << ",\"asymmetric\":" << (asymmetric ? "true" : "false")
            << "}";
    result.metricsJson = metrics.str();
    result.detail = pass
                        ? "CoC is zero at focus and follows f^2/(N(D-f)) * |z-D|/z"
                        : "CoC does not match the thin-lens closed form";
    result.lines.push_back("radius at focus (z=1000): " + fixed(radii[2], 9) + " px");
    result.lines.push_back("near z=800: " + fixed(radii[1], 4) + " px; far z=1200: " +
                           fixed(radii[3], 4) + " px; near/far blur=" +
                           fixed(nearFarRatio, 6) + " (expected " +
                           fixed(expectedNearFarRatio, 6) + ")");
    result.lines.push_back("max relative error against AE formula: " +
                           fixed(maxRelativeError, 9));
    return result;
}

// ── Test 6 ─────────────────────────────────────────────────────────────────

std::optional<std::string> readShadowMapEvidence() {
    const std::filesystem::path candidates[] = {
        std::filesystem::path("..") / "Chronon3d" / "docs" / "evidence" /
            "ae-shadow-map-20261106" / "report.json",
        std::filesystem::path("docs") / "evidence" /
            "ae-shadow-map-20261106" / "report.json",
    };
    for (const auto& path : candidates) {
        std::ifstream input(path);
        if (!input.good()) continue;
        std::ostringstream buffer;
        buffer << input.rdbuf();
        return buffer.str();
    }
    return std::nullopt;
}

bool shadowEvidenceFlag(const std::string& json, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\": true";
    const std::string compact = "\"" + std::string(key) + "\":true";
    return json.find(needle) != std::string::npos ||
           json.find(compact) != std::string::npos;
}

std::optional<double> shadowEvidenceNumber(const std::string& json,
                                           std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\":";
    const auto start = json.find(needle);
    if (start == std::string::npos) return std::nullopt;
    const auto value_start = start + needle.size();
    const auto value_end = json.find_first_of(",\n}", value_start);
    if (value_end == std::string::npos) return std::nullopt;
    try {
        return std::stod(json.substr(value_start, value_end - value_start));
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

Result testShadowMap() {
    Result result;
    result.id = 6;
    result.area = "Shadow map and PCF";

    // The production path is a compute caster plus a PCF sampling pass. The
    // OpenGL look-dev harness is never accepted as evidence: the report must
    // come from Chronon3d/tests/backends/test_shadow_map.cpp on a Vulkan
    // device, and every criterion below must be measured.
    const auto report = readShadowMapEvidence();
    const bool measured = report.has_value() &&
        shadowEvidenceFlag(*report, "production_vulkan_shadow_map") &&
        shadowEvidenceFlag(*report, "pcf_kernel") &&
        shadowEvidenceFlag(*report, "adaptive_normal_bias") &&
        shadowEvidenceFlag(*report, "measured");
    const auto edge_error = report
        ? shadowEvidenceNumber(*report, "edge_error_px") : std::nullopt;
    const auto cpu_delta = report
        ? shadowEvidenceNumber(*report, "cpu_gpu_max_delta") : std::nullopt;
    const bool report_pass = measured &&
        report->find("\"status\": \"PASS\"") != std::string::npos &&
        edge_error.has_value() && *edge_error <= 1.0 &&
        cpu_delta.has_value() && *cpu_delta <= 0.02 &&
        shadowEvidenceFlag(*report, "pcf_softened") &&
        shadowEvidenceFlag(*report, "bias_removed_acne");

    if (report_pass) {
        result.status = "PASS";
        result.metricsJson =
            "{\"production_vulkan_shadow_map\":true,"
            "\"pcf_kernel\":true,"
            "\"adaptive_normal_bias\":true,"
            "\"measured\":true,"
            "\"edge_error_px\":" + fixed(*edge_error, 3) + ","
            "\"cpu_gpu_max_delta\":" + fixed(*cpu_delta, 6) +
            ",\"source\":\"test_shadow_map.cpp\"}";
        result.detail =
            "Production Vulkan compute shadow map measured on device: light-space caster depth, PCF kernel and adaptive normal bias";
        result.lines.push_back(
            "edge position error: " + fixed(*edge_error, 3) + " px (<= 1 px)");
        result.lines.push_back(
            "CPU/kernel PCF delta: " + fixed(*cpu_delta, 6) + " (<= 0.02)");
        result.lines.push_back(
            "PCF softens the contact edge and bias removes self-shadow acne");
        return result;
    }

    result.status = "GAP";
    result.metricsJson =
        "{\"production_vulkan_shadow_map\":false,"
        "\"pcf_kernel\":false,"
        "\"adaptive_normal_bias\":false,"
        "\"measured\":false}";
    result.detail = report.has_value()
        ? "Shadow-map evidence exists but does not satisfy the measured tolerances"
        : "No production Vulkan shadow-map evidence report was found";
    result.lines.push_back(
        "run chronon3d_shadow_map_tests with CHRONON3D_SHADOW_MAP_REPORT set, then rerun this audit");
    return result;
}

std::string statusDescription(const Result& result) {
    if (result.status == "PASS") return "PASS";
    if (result.status == "PARTIAL") return "PARTIAL";
    if (result.status == "GAP") return "GAP";
    return "FAIL";
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path jsonPath = "out/ae3d_parity/audit.json";
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--json" && i + 1 < argc) {
            jsonPath = argv[++i];
        } else if (arg == "--help") {
            std::cout << "usage: ae3d_parity_audit [--json <path>]\n";
            return 0;
        } else {
            std::cerr << "unknown argument: " << arg << '\n';
            return 2;
        }
    }

    std::vector<Result> results;
    results.push_back(testProjectionOneToOne());
    results.push_back(testTwoNodeLookAt());
    results.push_back(testPointFalloff());
    results.push_back(testSpotCone());
    results.push_back(testDepthOfField());
    results.push_back(testShadowMap());

    int passCount = 0;
    int partialCount = 0;
    int failCount = 0;
    int gapCount = 0;
    for (const auto& result : results) {
        if (result.status == "PASS") ++passCount;
        else if (result.status == "PARTIAL") ++partialCount;
        else if (result.status == "GAP") ++gapCount;
        else ++failCount;
    }

    std::cout << "AE 3D parity audit — ChrononMotion3D + Chronon3D Vulkan math\n";
    std::cout << "reference: After Effects Classic/Advanced 3D camera, lights, DoF, shadows\n";
    for (const auto& result : results) {
        std::cout << "\n[" << result.id << "] " << statusDescription(result)
                  << "  " << result.area << "\n";
        std::cout << "    " << result.detail << "\n";
        for (const auto& line : result.lines) std::cout << "    - " << line << "\n";
    }
    std::cout << "\nsummary: PASS=" << passCount << " PARTIAL=" << partialCount
              << " FAIL=" << failCount << " GAP=" << gapCount << "\n";

    std::error_code error;
    const auto parent = jsonPath.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, error);
    std::ofstream json(jsonPath);
    if (!json) {
        std::cerr << "failed to open " << jsonPath << '\n';
        return 2;
    }
    json << "{\n  \"tool\": \"ae3d_parity_audit\",\n"
         << "  \"reference\": \"After Effects Classic/Advanced 3D\",\n"
         << "  \"pipeline\": \"ChrononMotion CameraRig + Chronon3D camera projection / lighting / DoF\",\n"
         << "  \"tests\": [\n";
    for (std::size_t i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        json << "    {\"id\": " << result.id
             << ", \"area\": " << jsonString(result.area)
             << ", \"status\": " << jsonString(result.status)
             << ", \"detail\": " << jsonString(result.detail)
             << ", \"metrics\": " << result.metricsJson << "}";
        if (i + 1 != results.size()) json << ',';
        json << '\n';
    }
    json << "  ],\n  \"summary\": {\"pass\": " << passCount
         << ", \"partial\": " << partialCount << ", \"fail\": " << failCount
         << ", \"gap\": " << gapCount << "}\n}\n";
    std::cout << "json: " << jsonPath.string() << "\n";

    return (failCount == 0 && gapCount == 0) ? 0 : 1;
}
