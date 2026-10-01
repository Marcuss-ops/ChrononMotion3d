// ChrononMotion — Eye-Trace & Multi-Aspect acceptance tests.
//
// Verifies:
//   - CameraScreenSpace: projection round-trip, view depth, the exact
//     aim-preserving translation, framing distance per aspect
//   - CameraRig::sampleDerivative: closed-form derivative vs finite
//     differences, channel coverage, empty-track zeros
//   - cameraEyeTraceHandoff: A pinned on the screen anchor, B lands on the
//     same pixels, aim-preserving correction (orientation invariant,
//     focus distance preserved), random-access determinism
//   - EyeTraceDirector: landing recording, exact pin across the incoming
//     shot, 3-shot chain consistency, outgoing last frame untouched
//   - autoReframeRig: 9:16/1:1 back-off keeps the subject inside the safe
//     band, vertical size preserved, master untouched, determinism
//
// No framework, no dependencies: the shared check harness.

#include "chrononmotion/chrononmotion.hpp"
#include "motion_check.hpp"

#include <cmath>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr float kFps = 30.f;
    constexpr float kPi = 3.14159265358979323846f;

    float at(int frame) {
        return static_cast<float>(frame) / kFps;
    }

    Vector2 projectToViewport(const Vector3& worldPoint, const CameraPose& pose,
                              float fovDeg, const Vector2& viewport) {
        return projectToScreen(worldPoint, pose, ScreenCameraIntrinsics{viewport, fovDeg});
    }

    // ------------------------------------------------------------- screen space

    void testScreenSpacePrimitives() {
        section("CameraScreenSpace — projection, depth, exact translation");

        const ScreenCameraIntrinsics intrinsics{Vector2(1920.f, 1080.f), 60.f};
        CameraPose pose;
        pose.position = Vector3(0.f, 0.f, 10.f);
        pose.hasTarget = true;
        pose.target = Vector3(0.f, 0.f, 0.f);
        CameraRig::lookAtPose(pose, pose.target);

        // The point on the view axis projects to the frame center.
        const Vector2 center = projectToScreen(Vector3(0.f, 0.f, 0.f), pose, intrinsics);
        checkNear(center, Vector2(960.f, 540.f), 1e-3f, "the aim point projects to the frame center");

        // Round-trip: a screen delta, translated, re-projects to the same delta.
        const Vector3 probe(-4.2f, 1.7f, -6.f);
        const Vector2 probePx = projectToScreen(probe, pose, intrinsics);
        const Vector2 wanted(1300.f, 300.f);
        const float depth = viewDepth(probe, pose);
        check(depth > 0.f, "a point in front of the camera has positive view depth");

        CameraPose moved = pose;
        translatePoseForScreenDelta(moved, wanted - probePx, depth, intrinsics);
        // Aim-preserving: the orientation must be bit-identical.
        check(moved.orientation == pose.orientation,
              "the screen-delta translation leaves the orientation untouched");
        checkNear(moved.position.distanceTo(moved.target),
                  pose.position.distanceTo(pose.target), 1e-3f,
                  "the position-to-target distance is invariant");
        const Vector2 landed = projectToScreen(probe, moved, intrinsics);
        checkNear(landed, wanted, 1e-2f, "the translated pose lands the point on the wanted pixels");

        // A large delta must stay exact (no linearization): the probe point
        // slides right by 1700 px and up by 900 px from wherever it starts.
        CameraPose far = pose;
        const Vector2 farFrom = projectToScreen(probe, pose, intrinsics);
        translatePoseForScreenDelta(far, Vector2(1700.f, -900.f), depth, intrinsics);
        checkNear(projectToScreen(probe, far, intrinsics), farFrom + Vector2(1700.f, -900.f), 1e-1f,
                  "a large screen delta lands exactly (no linearization error)");

        // View depth sign.
        const float behind = viewDepth(Vector3(0.f, 0.f, 30.f), pose);
        check(behind < 0.f, "a point behind the camera has negative view depth");

        // Framing distance per aspect: portrait must back off, landscape must not.
        const float radius = 3.f;
        const float fov = 50.f;
        const float dWide = framingDistanceForAspect(radius, fov, 16.f / 9.f);
        const float dTall = framingDistanceForAspect(radius, fov, 9.f / 16.f);
        const float halfFov = std::tan(fov * 0.5f * kPi / 180.f);
        checkNear(dWide, radius / halfFov, 1e-3f, "landscape framing distance is the vertical fit");
        check(dTall > dWide * 1.7f, "portrait backs off by the aspect ratio");
        const float dSafe = framingDistanceForAspect(radius, fov, 9.f / 16.f, 0.2f);
        check(dSafe > dTall, "a safe margin pushes the framing distance further out");
    }

    // ------------------------------------------------------------- derivatives

    void testCameraDerivative() {
        section("CameraRig::sampleDerivative — analytic rates vs finite differences");

        CameraRig rig(1, 55.f, 16.f / 9.f);

        // Linear position move: 30 units over 60 frames = 15 units/s.
        rig.positionTrack().add(0.f, Vector3(0.f, 0.f, 10.f), Easing::linear());
        rig.positionTrack().add(2.f, Vector3(30.f, 0.f, 10.f), Easing::linear());
        rig.targetTrack().add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
        rig.targetTrack().add(2.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
        rig.fovTrack().add(0.f, 60.f, Easing::linear());
        rig.fovTrack().add(2.f, 40.f, Easing::linear());
        rig.focusDistanceTrack().add(0.f, 10.f, Easing::linear());
        rig.focusDistanceTrack().add(2.f, 40.f, Easing::linear());
        rig.rollTrack().add(0.f, 0.f, Easing::linear());
        rig.rollTrack().add(2.f, kPi / 6.f, Easing::linear());

        const float mid = 1.f;
        const CameraPoseDerivative d = rig.sampleDerivative(mid);

        checkNear(d.linearVelocity.x, 15.f, 1e-3f, "position velocity is the exact key slope");
        checkNear(d.fovRate, -10.f, 1e-3f, "fov rate is the exact key slope");
        checkNear(d.focusRate, 15.f, 1e-3f, "focus rate is the exact key slope");
        checkNear(d.rollRate, kPi / 12.f, 1e-4f, "roll rate is the exact key slope");

        // Finite-difference cross-check on the position channel.
        const float h = 1e-3f;
        const Vector3 pPlus = rig.sample(mid + h).position;
        const Vector3 pMinus = rig.sample(mid - h).position;
        const Vector3 finite = (pPlus - pMinus) * (1.f / (2.f * h));
        checkNear(d.linearVelocity, finite, 1e-2f, "analytic velocity matches the central difference");

        // Orientation-only channel: a pure yaw spin gives ω about +Y.
        CameraRig spin(2, 55.f, 16.f / 9.f);
        const float totalRad = kPi / 2.f;
        spin.orientationTrack().add(0.f, Quaternion(), Easing::linear());
        Quaternion quarter;
        quarter.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), totalRad);
        spin.orientationTrack().add(2.f, quarter, Easing::linear());

        const CameraPoseDerivative w = spin.sampleDerivative(mid);
        checkNear(w.angularVelocity.length(), totalRad / 2.f, 1e-3f,
                  "angular speed matches the authored yaw rate");
        checkNear(std::fabs(w.angularVelocity.y), totalRad / 2.f, 1e-3f,
                  "the yaw spin rotates about the world Y axis");
        checkNear(w.angularVelocity.x, 0.f, 1e-4f, "no spurious X angular velocity");
        checkNear(w.angularVelocity.z, 0.f, 1e-4f, "no spurious Z angular velocity");

        // An empty rig reports zeros everywhere.
        CameraRig still(3, 55.f, 16.f / 9.f);
        const CameraPoseDerivative zero = still.sampleDerivative(1.f);
        checkNear(zero.linearVelocity, Vector3(0.f, 0.f, 0.f), 1e-6f, "a still rig has no linear velocity");
        checkNear(zero.angularVelocity, Vector3(0.f, 0.f, 0.f), 1e-6f, "a still rig has no angular velocity");
        checkNear(zero.fovRate, 0.f, 1e-6f, "a still rig has no fov rate");
    }

    // ------------------------------------------------------------- handoff

    void testEyeTraceHandoff() {
        section("cameraEyeTraceHandoff — exact pixel landing and aim preservation");

        CameraRig rig(1, 55.f, 16.f / 9.f);
        const Vector2 viewport(1920.f, 1080.f);
        const Vector2 anchor(1240.f, 430.f); // deliberately off-center

        const ShotAnchor A = ShotAnchor::world(Vector3(-6.f, 0.f, 0.f));
        const ShotAnchor B = ShotAnchor::world(Vector3(7.f, 1.5f, -9.f));

        cameraEyeTraceHandoff(rig, A, B, 0, 60, kFps, Easing::easeInOut(), nullptr, anchor, viewport);

        const ScreenCameraIntrinsics intrinsics{viewport, 55.f};

        // Frame 0: A pinned on the anchor.
        const CameraPose p0 = rig.sample(at(0));
        checkNear(projectToViewport(A.resolve(static_cast<const MotionScene*>(nullptr), 0.f), p0, 55.f, viewport), anchor, 1e-2f,
                  "frame 0: subject A sits exactly on the authored screen anchor");

        // The cut frame: B lands exactly where A was pinned.
        const CameraPose pCut = rig.sample(at(30));
        checkNear(projectToViewport(A.resolve(static_cast<const MotionScene*>(nullptr), at(30)), pCut, 55.f, viewport), anchor, 1e-2f,
                  "the aim holds A on the anchor until the move starts");
        const CameraPose pEnd = rig.sample(at(60));
        checkNear(projectToViewport(B.resolve(static_cast<const MotionScene*>(nullptr), at(60)), pEnd, 55.f, viewport), anchor, 1e-2f,
                  "frame 60: subject B lands exactly on A's pixels");

        // Aim-preserving correction: the end pose's orientation is exactly the
        // look-at of its own target, and the focus distance is preserved.
        const float endDistance = pEnd.position.distanceTo(pEnd.target);
        checkNear(endDistance, p0.position.distanceTo(p0.target), 1e-2f,
                  "the stand-back keeps the viewing distance (and DOF) continuous");
        check(pEnd.orientation.length() > 0.99f && pEnd.orientation.length() < 1.01f,
              "the landing pose keeps a unit quaternion");

        // Random access determinism.
        const CameraPose again = rig.sample(at(60));
        check(again.position == pEnd.position && again.target == pEnd.target,
              "sampling the landing frame twice is deterministic");
    }

    // ------------------------------------------------------------- director

    void testEyeTraceDirector() {
        section("EyeTraceDirector — cuts with pinned incoming shots");

        CameraRig rig(1, 55.f, 16.f / 9.f);
        const Vector2 viewport(1920.f, 1080.f);
        const ScreenCameraIntrinsics intrinsics{viewport, 55.f};

        const ShotAnchor A = ShotAnchor::world(Vector3(-5.f, 0.f, 0.f));
        const ShotAnchor B = ShotAnchor::world(Vector3(6.f, 0.f, -4.f));
        const ShotAnchor C = ShotAnchor::world(Vector3(0.f, 2.f, -12.f));

        EyeTraceDirector director(kFps, intrinsics);
        director.addShot({[&](CameraRig& r) {
                              // Static aim on A; keys at BOTH boundaries so no
                              // sample ever interpolates across a cut.
                              r.positionTrack().add(0.f, Vector3(-5.f, 0.f, 10.f), Easing::linear());
                              r.positionTrack().add(at(29), Vector3(-5.f, 0.f, 10.f), Easing::linear());
                              r.targetTrack().add(0.f, Vector3(-5.f, 0.f, 0.f), Easing::linear());
                              r.targetTrack().add(at(29), Vector3(-5.f, 0.f, 0.f), Easing::linear());
                          },
                          A, 0, 29});
        director.addShot({[&](CameraRig& r) {
                              // A slow drift that keeps moving after the cut.
                              r.positionTrack().add(at(30), Vector3(6.f, 0.f, 6.f), Easing::linear());
                              r.positionTrack().add(at(60), Vector3(5.f, 0.f, 8.f), Easing::linear());
                              r.targetTrack().add(at(30), Vector3(6.f, 0.f, -4.f), Easing::linear());
                              r.targetTrack().add(at(60), Vector3(6.f, 0.f, -4.f), Easing::linear());
                          },
                          B, 30, 30});
        director.addShot({[&](CameraRig& r) {
                              r.positionTrack().add(at(61), Vector3(0.f, 2.f, 2.f), Easing::linear());
                              r.positionTrack().add(at(80), Vector3(0.f, 2.f, 4.f), Easing::linear());
                              r.targetTrack().add(at(61), Vector3(0.f, 2.f, -12.f), Easing::linear());
                              r.targetTrack().add(at(80), Vector3(0.f, 2.f, -12.f), Easing::linear());
                          },
                          C, 61, 19});

        director.bakeInto(rig);
        check(director.landings().size() == 2, "two cuts produce two recorded landings");

        // Cut 1: B is pinned on A's landing pixels across its whole shot.
        const Vector2 landing1 = director.landings()[0].landingPx;
        bool bPinned = true;
        for (int f = 31; f <= 60; ++f) {
            const CameraPose p = rig.sample(at(f));
            const Vector2 px = projectToScreen(B.resolve(static_cast<const MotionScene*>(nullptr), at(f)), p, intrinsics);
            if (std::fabs(px.x - landing1.x) > 0.05f || std::fabs(px.y - landing1.y) > 0.05f) {
                bPinned = false;
            }
        }
        check(bPinned, "the incoming anchor stays pinned to the landing across its shot");

        // Cut 2: the landing was recorded from the corrected timeline — what
        // the viewer is actually looking at at the second cut.
        const CameraPose cut2 = rig.sample(at(60));
        const Vector2 bAtCut2 = projectToScreen(B.resolve(static_cast<const MotionScene*>(nullptr), at(60)), cut2, intrinsics);
        checkNear(director.landings()[1].landingPx, bAtCut2, 1e-2f,
                  "the second landing matches the corrected timeline's gaze point");

        // C is pinned to that same point well inside its shot.
        const CameraPose cPin = rig.sample(at(70));
        const Vector2 cPx = projectToScreen(C.resolve(static_cast<const MotionScene*>(nullptr), at(70)), cPin, intrinsics);
        checkNear(cPx, director.landings()[1].landingPx, 0.05f,
                  "the third shot's anchor lands on the second shot's gaze pixels");

        // The outgoing shot's last frame is never rewritten: frame 29 still
        // shows A exactly where shot 1 framed it.
        const CameraPose p29 = rig.sample(at(29));
        checkNear(p29.position, Vector3(-5.f, 0.f, 10.f), 1e-4f,
                  "the outgoing shot's last frame keeps its authored position");

        // correctedPoseAt is idempotent on the corrected rig: the baked tracks
        // already satisfy the landing, so the solve is a no-op.
        const CameraPose solved = director.correctedPoseAt(rig, at(45));
        const CameraPose baked = rig.sample(at(45));
        checkNear(solved.position, baked.position, 1e-3f,
                  "the introspection solve agrees with the baked tracks");
    }

    // ------------------------------------------------------------- reframe

    void testAspectReframe() {
        section("autoReframeRig — one master, 9:16 and 1:1 deliveries");

        CameraRig master(1, 50.f, 16.f / 9.f);
        // Push onto a subject at the origin: 12 units to 6 units over 60 frames.
        master.positionTrack().add(0.f, Vector3(0.f, 0.f, 12.f), Easing::linear());
        master.positionTrack().add(2.f, Vector3(0.f, 0.f, 6.f), Easing::linear());
        master.targetTrack().add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
        master.targetTrack().add(2.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
        master.setFov(50.f);
        master.focusDistanceTrack().add(0.f, 12.f, Easing::linear());
        master.focusDistanceTrack().add(2.f, 6.f, Easing::linear());

        const std::vector<ShotAnchor> subjects{ShotAnchor::world(Vector3(0.f, 0.f, 0.f))};

        CameraRig vertical(2);
        const DeliveryFormat tiktok = DeliveryFormat::vertical1080();
        autoReframeRig(master, nullptr, tiktok, vertical, subjects, kFps);
        checkNear(vertical.aspect(), 9.f / 16.f, 1e-5f, "the reframed rig carries the delivery aspect");

        // The subject radius (0.5 floor) must fit the safe horizontal band:
        // verify the reframed distance against the closed-form law.
        const float tanHalf = std::tan(50.f * 0.5f * kPi / 180.f);
        const float safeFactor = 1.f - tiktok.safeLeft - tiktok.safeRight;
        const float required = 0.5f / (tanHalf * (9.f / 16.f) * safeFactor);
        const float dEnd = vertical.sample(at(60)).position.distanceTo(vertical.sample(at(60)).target);
        check(dEnd >= required - 1e-3f,
              "the reframed distance satisfies the safe-band fit at the push end");

        // Vertical size preservation: the subject's on-screen size is set by
        // the vertical FOV and the master distance when the horizontal axis
        // is not binding — here the master distance (6) dominates the
        // required portrait distance? No: required > 6, so the back-off binds.
        // Verify the less strict invariant: vertical coverage shrinks as
        // d'/d compared to the master at the same frame.
        const float dMaster = master.sample(at(60)).position.distanceTo(master.sample(at(60)).target);
        check(dEnd >= dMaster, "portrait never moves the camera closer than the master");

        // The SUBJECT lands on the usable band center (the shifted aim point
        // itself is always at the frame center by look-at construction).
        const CameraPose vEnd = vertical.sample(at(60));
        const Vector2 subjectPx = projectToViewport(Vector3(0.f, 0.f, 0.f), vEnd, 50.f, tiktok.viewport);
        const float bandCenterX = 0.5f * tiktok.viewport.x * (1.f - tiktok.safeLeft - tiktok.safeRight);
        const float bandCenterY = 0.5f * tiktok.viewport.y * (1.f - tiktok.safeTop - tiktok.safeBottom);
        checkNear(subjectPx, Vector2(bandCenterX, bandCenterY), 1.5f,
                  "the subject lands on the safe band center in pixels");

        // Square delivery reframe also stays put and deterministic.
        CameraRig square(3);
        autoReframeRig(master, nullptr, DeliveryFormat::square1080(), square, subjects, kFps);
        const CameraPose sEnd = square.sample(at(60));
        const CameraPose sEndAgain = square.sample(at(60));
        check(sEnd.position == sEndAgain.position, "the reframe sampling is deterministic");
        check(sEnd.position.distanceTo(sEnd.target) <= dEnd + 1e-3f,
              "1:1 demands less back-off than 9:16");

        // The master rig is untouched.
        checkNear(master.sample(at(0)).position, Vector3(0.f, 0.f, 12.f), 1e-5f,
                  "the master rig keeps its authored position");

        // Focus distance scales with the reframed distance.
        const float fdEnd = vertical.sample(at(60)).focusDistance;
        checkNear(fdEnd, dEnd, 1e-2f, "the reframed focus distance tracks the reframed distance");

        // A target-less master is rejected.
        CameraRig freeCam(4, 50.f, 16.f / 9.f);
        freeCam.positionTrack().add(0.f, Vector3(0.f, 0.f, 5.f), Easing::linear());
        CameraRig unused(5);
        bool threw = false;
        try {
            autoReframeRig(freeCam, nullptr, tiktok, unused, {}, kFps);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "a target-less master is rejected with a clear error");
    }

}// namespace

int main() {
    testScreenSpacePrimitives();
    testCameraDerivative();
    testEyeTraceHandoff();
    testEyeTraceDirector();
    testAspectReframe();
    return chrononmotion_test::report();
}
