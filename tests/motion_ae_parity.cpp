// CM3D-side After Effects authoring parity.
//
// This test owns only the contracts ChrononMotion3D can decide:
//   - AE Zoom and vertical FOV describe the same pinhole focal length;
//   - a two-node camera keeps its target centred through a full orbit and
//     through the vertical look-at singularity without a 180-degree flip;
//   - AE's full Cone Angle and percentage Feather convert to the half-angle
//     and normalized penumbra the render plan carries.
//
// Renderer kernels (point falloff, shadow filtering) are outside this tree and
// are deliberately not modelled here.

#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"
#include "chrononmotion/motion/Lighting.hpp"

#include "motion_check.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kDegreesPerRadian = 180.f / kPi;

    Vector3 forwardOf(const CameraPose& pose) {

        Vector3 forward(0.f, 0.f, -1.f);
        forward.applyQuaternion(pose.orientation);
        return forward;
    }

    Vector3 rightOf(const CameraPose& pose) {

        Vector3 right(1.f, 0.f, 0.f);
        right.applyQuaternion(pose.orientation);
        const Vector3 forward = forwardOf(pose);
        right.addScaledVector(forward, -right.dot(forward));
        if (right.lengthSq() > 0.f) right.normalize();
        return right;
    }

    bool finitePose(const CameraPose& pose) {

        return std::isfinite(pose.position.x) && std::isfinite(pose.position.y) &&
               std::isfinite(pose.position.z) && std::isfinite(pose.orientation.x) &&
               std::isfinite(pose.orientation.y) && std::isfinite(pose.orientation.z) &&
               std::isfinite(pose.orientation.w);
    }

    void aeZoomProjectsAtOneToOne() {

        section("AE zoom -> pinhole projection");

        // 50 mm on a 36 mm-wide, 16:9 gate: the gate is 20.25 mm high.
        constexpr float gateHeightMm = 20.25f;
        constexpr float focalMm = 50.f;
        const float fovDegrees =
                2.f * std::atan(gateHeightMm / (2.f * focalMm)) * kDegreesPerRadian;
        const float zoomPixels = pinholeFocal(1080.f, fovDegrees);

        checkNear(zoomPixels, 1080.f * focalMm / gateHeightMm, 1e-3f,
                  "AE Zoom is viewportHeight * focalLength / gateHeight");
        checkNear(fovDegrees, 2.f * std::atan(gateHeightMm / (2.f * focalMm)) * kDegreesPerRadian,
                  1e-6f, "the AE gate/FOV relation is the pinhole relation");

        PerspectiveCamera camera;
        camera.fov = fovDegrees;
        camera.aspect = 16.f / 9.f;
        camera.nearPlane = 0.1f;
        camera.farPlane = 100000.f;
        camera.position.set(0.f, 0.f, zoomPixels);
        camera.lookAt(Vector3(0.f, 0.f, 0.f));
        camera.updateMatrixWorld(true);
        camera.updateProjectionMatrix();

        PerspectiveView view(camera, Vector2(1920.f, 1080.f));
        MotionPlane plane;
        plane.size.set(400.f, 400.f);
        plane.anchor.set(0.5f, 0.5f);

        const ScreenPlane projected = view.projectPlane(plane, Matrix4{});
        check(projected.visible, "the 400x400 plane is in front of the camera");
        checkNear(projected.size, Vector2(400.f, 400.f), 1e-3f,
                  "at camera Z = AE Zoom, 400 world units project to 400 pixels");
        checkNear(projected.center, Vector2(960.f, 540.f), 1e-3f,
                  "the plane lands on the viewport centre");
    }

    void cameraSurvivesTheVerticalPole() {

        section("two-node camera at the vertical pole");

        constexpr float radius = 10.f;
        constexpr int orbitSteps = 720;
        const Vector3 target(0.f, 0.f, 0.f);
        const ScreenCameraIntrinsics intrinsics{Vector2(1920.f, 1080.f), 50.f};

        CameraRig rig(1, 50.f, 16.f / 9.f, 0.1f, 100000.f);
        rig.setTarget(target);

        double maxOrbitCentreError = 0.0;
        for (int i = 0; i < orbitSteps; ++i) {
            const float theta = 2.f * kPi * static_cast<float>(i) / static_cast<float>(orbitSteps);
            rig.setPosition(Vector3(radius * std::cos(theta), 0.f, radius * std::sin(theta)));
            const CameraPose pose = rig.sample(0.f);
            const Vector2 centre = projectToScreen(target, pose, intrinsics);
            maxOrbitCentreError = std::max(maxOrbitCentreError,
                                           static_cast<double>(std::hypot(centre.x - 960.f,
                                                                          centre.y - 540.f)));
        }
        check(maxOrbitCentreError < 0.01, "the target stays centred through a full 360-degree orbit");

        // Sweep through the pole. The old fixed-up look-at swapped its roll by
        // 180 degrees as the horizontal component changed sign; the conditioned
        // frame must arrive continuously instead.
        double maxPoleCentreError = 0.0;
        double maxRollStepDegrees = 0.0;
        bool allFinite = true;
        bool havePreviousRight = false;
        Vector3 previousRight;
        constexpr int poleSteps = 400;
        constexpr float poleHalfRangeDegrees = 5.f;
        for (int i = -poleSteps; i <= poleSteps; ++i) {
            const float phi = poleHalfRangeDegrees * static_cast<float>(i) /
                              static_cast<float>(poleSteps) / kDegreesPerRadian;
            rig.setPosition(Vector3(radius * std::sin(phi), radius * std::cos(phi), 0.f));
            const CameraPose pose = rig.sample(0.f);
            allFinite = allFinite && finitePose(pose);

            const Vector2 centre = projectToScreen(target, pose, intrinsics);
            maxPoleCentreError = std::max(maxPoleCentreError,
                                          static_cast<double>(std::hypot(centre.x - 960.f,
                                                                         centre.y - 540.f)));
            allFinite = allFinite && std::isfinite(centre.x) && std::isfinite(centre.y);

            const Vector3 right = rightOf(pose);
            if (havePreviousRight) {
                const float dot = std::clamp(previousRight.dot(right), -1.f, 1.f);
                const double step = std::acos(static_cast<double>(dot)) *
                                    static_cast<double>(kDegreesPerRadian);
                maxRollStepDegrees = std::max(maxRollStepDegrees, step);
            }
            previousRight = right;
            havePreviousRight = true;
        }
        check(allFinite, "no pose or projection goes non-finite near the pole");
        check(maxPoleCentreError < 0.01, "the target stays centred through the pole");
        check(maxRollStepDegrees < 20.0,
              "the view roll changes continuously at the pole, with no 180-degree flip");

        rig.setPosition(Vector3(0.f, radius, 0.f));
        const CameraPose pole = rig.sample(0.f);
        check(finitePose(pole), "the exact pole pose is finite");
        checkNear(forwardOf(pole), Vector3(0.f, -1.f, 0.f), 1e-5f,
                  "at the exact pole the camera looks straight down");
        const Vector2 poleCentre = projectToScreen(target, pole, intrinsics);
        checkNear(poleCentre, Vector2(960.f, 540.f), 0.01f,
                  "the exact pole still points at the target");

        rig.setPosition(Vector3(0.f, 0.f, radius));
        const CameraPose front = rig.sample(0.f);
        checkNear(forwardOf(front), Vector3(0.f, 0.f, -1.f), 1e-5f,
                  "the non-singular look-at direction is unchanged");
        checkNear(rightOf(front), Vector3(1.f, 0.f, 0.f), 1e-5f,
                  "the non-singular camera keeps world-up roll");
    }

    void aeSpotAuthoringUnits() {

        section("AE spot cone and feather");

        checkNear(aeSpotHalfAngleRadians(60.f), kPi / 6.f, 1e-6f,
                  "AE 60-degree full cone becomes a 30-degree half-angle");
        checkNear(aeSpotPenumbra(50.f), 0.5f, 1e-6f,
                  "AE 50% Feather becomes a 0.5 penumbra");

        SpotLight light(1);
        light.setAeConeAngleDegrees(60.f).setAeConeFeatherPercent(50.f);
        checkNear(light.angle, kPi / 6.f, 1e-6f, "the spot stores the half-angle");
        checkNear(light.penumbra, 0.5f, 1e-6f, "the spot stores the normalized feather");

        const LightSample sample = light.sample(0.f);
        checkNear(sample.angle, kPi / 6.f, 1e-6f, "the light sample carries the half-angle");
        checkNear(sample.penumbra, 0.5f, 1e-6f, "the light sample carries the normalised feather");

        PointLight point(2);
        point.falloff = LightFalloff::AeInverseSquareClamped;
        check(point.sample(0.f).falloff == LightFalloff::AeInverseSquareClamped,
              "the point-light sample carries the AE falloff model");

        auto rejects = [](auto&& call) {
            try {
                call();
            } catch (const std::invalid_argument&) {
                return true;
            }
            return false;
        };

        check(rejects([] { (void) aeSpotHalfAngleRadians(0.f); }),
              "a zero cone is rejected");
        check(rejects([] { (void) aeSpotHalfAngleRadians(360.1f); }),
              "a cone wider than 360 degrees is rejected");
        check(rejects([] { (void) aeSpotHalfAngleRadians(std::numeric_limits<float>::quiet_NaN()); }),
              "a non-finite cone is rejected");
        check(rejects([] { (void) aeSpotPenumbra(-1.f); }),
              "negative feather is rejected");
        check(rejects([] { (void) aeSpotPenumbra(101.f); }),
              "feather above 100% is rejected");
        check(rejects([] { (void) aeSpotPenumbra(std::numeric_limits<float>::infinity()); }),
              "non-finite feather is rejected");

        checkNear(aeSpotHalfAngleRadians(360.f), kPi, 1e-6f,
                  "a 360-degree full cone is the maximum allowed half-angle");
    }

}// namespace

int main() {

    aeZoomProjectsAtOneToOne();
    cameraSurvivesTheVerticalPole();
    aeSpotAuthoringUnits();
    return chrononmotion_test::report();
}
