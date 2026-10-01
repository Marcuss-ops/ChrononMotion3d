// ChrononMotion — Camera Motion V1 test suite.
//
// Verifies:
//   - ShotAnchor & CameraShot & CameraTransition & CameraFocusResolver primitives
//   - Target-lock tests & safe framing zone on 1920x1080 (B.x ≈ 960, B.y ≈ 540)
//   - DOF & focus distance tracking (A at Z=0, B at Z=-300)
//   - 90 degree orbit reveal (orientation delta ≈ 90 deg, zero quaternion flip)
//   - Corner turn with continuous tangents (angular velocity bounded)
//   - Continuity tests (position, rotation, FOV, focus distance, roll)
//   - Random access determinism (sample(90) == sample(0..90))
//   - Parented camera world transform evaluation
//   - All 15 authoritative recipes
//   - Definitive Canary: camera_motion_torture_v1 (15-20s multi-subject continuous scene)

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

    float at(int frame) {
        return static_cast<float>(frame) / kFps;
    }

    Vector2 projectTo1080p(const Vector3& worldPoint, const CameraRig& rig, const CameraPose& pose) {
        auto camera = rig.makeCamera();
        rig.apply(pose, *camera);
        Vector3 ndc = worldPoint;
        ndc.project(*camera);
        float screenX = (ndc.x + 1.f) * 0.5f * 1920.f;
        float screenY = (1.f - ndc.y) * 0.5f * 1080.f;
        return Vector2(screenX, screenY);
    }

    // ------------------------------------------------------------- 1. Primitives

    void testCameraMotionPrimitives() {
        section("Camera Motion V1 — Primitives (ShotAnchor, CameraShot, CameraTransition, CameraFocusResolver)");

        // ShotAnchor: world point vs layer anchor
        ShotAnchor worldAnchor = ShotAnchor::world(Vector3(10.f, 20.f, -5.f));
        checkNear(worldAnchor.resolve(), Vector3(10.f, 20.f, -5.f), 1e-4f, "ShotAnchor::world resolves to exact world point");

        MotionScene scene("PrimitiveScene", kFps);
        Layer& card = scene.addLayer(10, "Card");
        card.transform.position.set(5.f, 2.f, -10.f);

        ShotAnchor layerAnchor(10, Vector3(0.f, 1.f, 0.f), AnchorKind::WebCard);
        Vector3 resolvedLayer = layerAnchor.resolve(&scene, 0.f);
        checkNear(resolvedLayer, Vector3(5.f, 3.f, -10.f), 1e-4f, "ShotAnchor resolves layer local offset in world space");

        // CameraShot & Framing
        CameraShot shot;
        shot.withTarget(ShotAnchor::world(Vector3(0.f, 0.f, 0.f)))
            .withFraming(Framing::CloseUp)
            .withFov(50.f)
            .withFocus(CameraFocusMode::TargetDistance, 0.f, 12.f);

        CameraPose shotPose = shot.evaluatePose(&scene, 0.f);
        checkNear(shotPose.target, Vector3(0.f, 0.f, 0.f), 1e-4f, "CameraShot aims at its target anchor");
        checkNear(shotPose.fov, 50.f, 1e-4f, "CameraShot preserves authored FOV");
        checkNear(shotPose.aperture, 12.f, 1e-4f, "CameraShot preserves authored aperture");
        checkNear(shotPose.position.distanceTo(shotPose.target), 5.0f, 1e-4f, "CloseUp framing sets distance to 5 units");

        // CameraFocusResolver
        const Vector3 cam(0.f, 0.f, 10.f);
        const Vector3 tgtA(0.f, 0.f, 0.f);
        const Vector3 tgtB(0.f, 0.f, -20.f);

        float focTarget = CameraFocusResolver::resolveFocus(CameraFocusMode::TargetDistance, cam, tgtA);
        checkNear(focTarget, 10.f, 1e-4f, "CameraFocusResolver TargetDistance matches dist(cam, tgt)");

        float focBlend = CameraFocusResolver::resolveFocus(CameraFocusMode::BlendTargets, cam, tgtA, tgtB, 0.5f);
        checkNear(focBlend, 20.f, 1e-4f, "CameraFocusResolver BlendTargets midpoint is 20 units");

        float focForeground = CameraFocusResolver::resolveFocus(CameraFocusMode::ForegroundSubject, cam, tgtA, tgtB);
        checkNear(focForeground, 10.f, 1e-4f, "CameraFocusResolver ForegroundSubject selects closest distance");

        float focManual = CameraFocusResolver::resolveFocus(CameraFocusMode::Manual, cam, tgtA, tgtB, 0.f, 42.5f);
        checkNear(focManual, 42.5f, 1e-4f, "CameraFocusResolver Manual returns authored distance");
    }

    // ------------------------------------------------------------- 2. Target Lock & Safe Framing

    void testCameraTargetLockAndSafeFraming() {
        section("Camera Motion V1 — Target Lock & 1920x1080 Safe Framing");

        CameraRig rig(1, 60.f, 16.f / 9.f);
        const Vector3 subjectA(0.f, 0.f, 0.f);
        const Vector3 subjectB(15.f, 5.f, -10.f);

        cameraLateralSubjectToSubject(rig, ShotAnchor::world(subjectA), ShotAnchor::world(subjectB), 0, 60, kFps);

        // At frame 0, subject A must project precisely to (960, 540)
        CameraPose pose0 = rig.sample(at(0));
        Vector2 screenA0 = projectTo1080p(subjectA, rig, pose0);
        checkNear(screenA0.x, 960.f, 0.05f, "Frame 0: Subject A is centered horizontally at 960px");
        checkNear(screenA0.y, 540.f, 0.05f, "Frame 0: Subject A is centered vertically at 540px");

        // At frame 60, subject B must project precisely to (960, 540)
        CameraPose pose60 = rig.sample(at(60));
        Vector2 screenB60 = projectTo1080p(subjectB, rig, pose60);
        checkNear(screenB60.x, 960.f, 0.05f, "Frame 60: Subject B is centered horizontally at 960px");
        checkNear(screenB60.y, 540.f, 0.05f, "Frame 60: Subject B is centered vertically at 540px");

        // Safe framing zone: across the travel, the active tracked target stays within center ± 1 pixel
        bool targetAlwaysCentered = true;
        for (int f = 0; f <= 60; ++f) {
            CameraPose p = rig.sample(at(f));
            Vector2 screenT = projectTo1080p(p.target, rig, p);
            if (std::fabs(screenT.x - 960.f) > 0.1f || std::fabs(screenT.y - 540.f) > 0.1f) {
                targetAlwaysCentered = false;
            }
        }
        check(targetAlwaysCentered, "Active target remains locked within center pixel across entire transition");
    }

    // ------------------------------------------------------------- 3. DOF Canary

    void testCameraDofCanary() {
        section("Camera Motion V1 — DOF & Rack Focus Canary (A at Z=0, B at Z=-300)");

        CameraRig rig(1, 60.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 50.f));

        const ShotAnchor subjectA = ShotAnchor::world(Vector3(0.f, 0.f, 0.f));
        const ShotAnchor subjectB = ShotAnchor::world(Vector3(0.f, 0.f, -300.f));

        cameraRackFocusDuo(rig, subjectA, subjectB, 0, 60, 16.f, kFps);

        CameraPose p0 = rig.sample(at(0));
        CameraPose p60 = rig.sample(at(60));

        // Frame 0: focusDistance ≈ dist(camera, A) = 50.f
        checkNear(p0.focusDistance, 50.f, 0.1f, "Frame 0: focusDistance matches distance to subject A");
        checkNear(p0.aperture, 16.f, 0.01f, "Frame 0: aperture is set for shallow DOF");

        // Frame 60: focusDistance ≈ dist(camera, B) = 350.f
        checkNear(p60.focusDistance, 350.f, 0.1f, "Frame 60: focusDistance matches distance to subject B");
        checkNear(p60.aperture, 16.f, 0.01f, "Frame 60: aperture is maintained for background bokeh");

        // Camera position remains unchanged
        checkNear(p0.position, p60.position, 1e-4f, "Rack focus preserves stationary camera position");
    }

    // ------------------------------------------------------------- 4. 90 Orbit Reveal

    void testCameraOrbit90Reveal() {
        section("Camera Motion V1 — 90° Orbit Reveal & Quaternion Invariants");

        CameraRig rig(1, 60.f, 16.f / 9.f);
        const ShotAnchor frontSubject = ShotAnchor::world(Vector3(0.f, 0.f, 0.f));
        const ShotAnchor sideSubject = ShotAnchor::world(Vector3(10.f, 0.f, 10.f));
        const Vector3 pivot(0.f, 0.f, 10.f);

        cameraOrbit90Reveal(rig, frontSubject, sideSubject, pivot, 0, 60, kFps);

        CameraPose p0 = rig.sample(at(0));
        CameraPose p60 = rig.sample(at(60));

        // Orientation angle delta ≈ 90 degrees (pi/2 radians)
        float angleDelta = p0.orientation.angleTo(p60.orientation);
        checkNear(angleDelta, math::PI * 0.5f, 0.05f, "Orientation delta across 90° orbit is ~90 degrees");

        // Side subject is centered at final frame
        Vector2 screenSide = projectTo1080p(sideSubject.resolve(), rig, p60);
        checkNear(screenSide.x, 960.f, 0.1f, "Final frame: revealed subject is horizontally centered");
        checkNear(screenSide.y, 540.f, 0.1f, "Final frame: revealed subject is vertically centered");

        // No quaternion flips: consecutive dot products must all be non-negative
        bool noFlip = true;
        for (int f = 0; f < 60; ++f) {
            CameraPose qA = rig.sample(at(f));
            CameraPose qB = rig.sample(at(f + 1));
            if (qA.orientation.dot(qB.orientation) < 0.f) {
                noFlip = false;
            }
        }
        check(noFlip, "Orientation maintains shortest arc with zero quaternion sign flips");
    }

    // ------------------------------------------------------------- 5. Corner Turn Reveal

    void testCameraCornerTurnReveal() {
        section("Camera Motion V1 — Corner Turn Reveal (Smooth Tangent & Bounded Angular Velocity)");

        CameraRig rig(1, 60.f, 16.f / 9.f);
        const ShotAnchor subjectA = ShotAnchor::world(Vector3(0.f, 0.f, -10.f));
        const ShotAnchor subjectB = ShotAnchor::world(Vector3(20.f, 0.f, 0.f));
        const Vector3 cornerPoint(0.f, 0.f, 0.f);

        cameraCornerTurnReveal(rig, subjectA, subjectB, cornerPoint, 0, 60, 3.f, kFps);

        // Verify angular velocity is smooth and strictly bounded across all frames (no instant 90° jump)
        float maxAngularDeltaDeg = 0.f;
        for (int f = 0; f < 60; ++f) {
            CameraPose pA = rig.sample(at(f));
            CameraPose pB = rig.sample(at(f + 1));
            float dDeg = math::radToDeg(pA.orientation.angleTo(pB.orientation));
            if (dDeg > maxAngularDeltaDeg) {
                maxAngularDeltaDeg = dDeg;
            }
        }
        check(maxAngularDeltaDeg < 15.f, "Angular velocity around corner is smooth and bounded (< 15 deg/frame)");

        CameraPose pEnd = rig.sample(at(60));
        Vector2 screenB = projectTo1080p(subjectB.resolve(), rig, pEnd);
        checkNear(screenB.x, 960.f, 0.2f, "Corner turn ends facing subject B centered horizontally");
        checkNear(screenB.y, 540.f, 0.2f, "Corner turn ends facing subject B centered vertically");
    }

    // ------------------------------------------------------------- 6. Continuity Checks

    void testCameraContinuityInvariants() {
        section("Camera Motion V1 — Global Continuity Tests (Position, Rotation, FOV, Focus, Roll)");

        CameraRig rig(1, 60.f, 16.f / 9.f);
        ShotAnchor sA = ShotAnchor::world(Vector3(-10.f, 0.f, 0.f));
        ShotAnchor sB = ShotAnchor::world(Vector3(10.f, 2.f, -15.f));

        CameraShot shotA;
        shotA.withTarget(sA).withDistance(8.f).withFov(55.f).withFocus(CameraFocusMode::TargetDistance, 0.f, 8.f);
        CameraShot shotB;
        shotB.withTarget(sB).withDistance(12.f).withFov(45.f).withFocus(CameraFocusMode::TargetDistance, 0.f, 14.f);

        CameraTransition trans;
        trans.fromShot = shotA;
        trans.toShot = shotB;
        trans.inFrame = 0;
        trans.duration = 60;
        trans.path = TransitionPath::Arc;
        trans.apply(rig, nullptr, kFps);

        float maxDeltaPos = 0.f;
        float maxDeltaAngle = 0.f;
        float maxDeltaFov = 0.f;
        float maxDeltaFocus = 0.f;

        for (int f = 0; f < 60; ++f) {
            CameraPose p0 = rig.sample(at(f));
            CameraPose p1 = rig.sample(at(f + 1));

            maxDeltaPos = std::max(maxDeltaPos, p0.position.distanceTo(p1.position));
            maxDeltaAngle = std::max(maxDeltaAngle, p0.orientation.angleTo(p1.orientation));
            maxDeltaFov = std::max(maxDeltaFov, std::fabs(p1.fov - p0.fov));
            maxDeltaFocus = std::max(maxDeltaFocus, std::fabs(p1.focusDistance - p0.focusDistance));
        }

        check(maxDeltaPos < 2.0f, "TestCameraNoPositionDiscontinuity: position delta bounded");
        check(maxDeltaAngle < 0.15f, "TestCameraNoQuaternionFlip: rotation delta bounded and smooth");
        check(maxDeltaFov < 1.0f, "TestCameraNoFovPop: FOV delta bounded");
        check(maxDeltaFocus < 1.5f, "TestCameraNoFocusPop: focus distance delta bounded");
    }

    // ------------------------------------------------------------- 7. Determinism & Random Access

    void testCameraRandomAccessDeterminism() {
        section("Camera Motion V1 — Random Access Determinism & Stateless Sampling");

        CameraRig rig(1, 60.f, 16.f / 9.f);
        ShotAnchor sA = ShotAnchor::world(Vector3(0.f, 0.f, 0.f));
        ShotAnchor sB = ShotAnchor::world(Vector3(10.f, 0.f, 5.f));

        cameraDollyFocusHandoff(rig, sA, sB, 0, 90, Vector3(4.f, 1.f, -3.f), 10.f, kFps);

        // Sequential sampling
        for (int f = 0; f <= 90; ++f) {
            (void)rig.sample(at(f));
        }
        CameraPose seqPose90 = rig.sample(at(90));

        // Fresh rig sampled directly at frame 90
        CameraRig freshRig(1, 60.f, 16.f / 9.f);
        cameraDollyFocusHandoff(freshRig, sA, sB, 0, 90, Vector3(4.f, 1.f, -3.f), 10.f, kFps);
        CameraPose directPose90 = freshRig.sample(at(90));

        checkNear(seqPose90.position, directPose90.position, 1e-6f, "Direct sampling at frame 90 matches sequential sampling");
        checkNear(seqPose90.orientation.x, directPose90.orientation.x, 1e-6f, "Direct orientation matches sequential orientation");
        checkNear(seqPose90.focusDistance, directPose90.focusDistance, 1e-6f, "Direct focus distance matches sequential focus distance");
    }

    // ------------------------------------------------------------- 8. Parented Camera

    void testParentedCameraRig() {
        section("Camera Motion V1 — Parented Camera Rig (Controller + Local Motion)");

        MotionScene scene("ParentedScene", kFps);
        Layer& controller = scene.addNull(100, "RigController");
        controller.transform.position.set(100.f, 50.f, -20.f);

        CameraRig rig(1, 60.f, 16.f / 9.f);
        rig.setParent(100);
        rig.setPosition(Vector3(0.f, 0.f, 10.f));

        scene.evaluate(0);
        Matrix4 parentWorld = scene.worldMatrix(100);

        auto camera = rig.makeCamera();
        rig.apply(at(0), *camera, &parentWorld);

        // Camera world position should be controller position + camera local position = (100, 50, -10)
        checkNear(camera->position, Vector3(100.f, 50.f, -10.f), 1e-4f, "Parented camera combines controller and local transforms");
    }

    // ------------------------------------------------------------- 9. Remaining 15 Recipes

    void testAllRemainingRecipes() {
        section("Camera Motion V1 — Remaining 10 Authoritative Recipes");

        ShotAnchor sA = ShotAnchor::world(Vector3(0.f, 0.f, 0.f));
        ShotAnchor sB = ShotAnchor::world(Vector3(12.f, 0.f, -8.f));
        ShotAnchor sC = ShotAnchor::world(Vector3(24.f, 0.f, -16.f));

        // 7. camera_detach_turn_reacquire
        {
            CameraRig rig(7);
            cameraDetachTurnReacquire(rig, sA, sB, 0, 40, kFps);
            checkNear(rig.sample(at(0)).target, sA.resolve(), 1e-3f, "DetachReacquire starts on subject A");
            checkNear(rig.sample(at(40)).target, sB.resolve(), 1e-3f, "DetachReacquire settles on subject B");
        }

        // 8. camera_foreground_pass_reveal
        {
            CameraRig rig(8);
            cameraForegroundPassReveal(rig, sA, sB, 0, 40, Vector3(5.f, 0.f, -6.f), 14.f, kFps);
            check(rig.sample(at(0)).position.z > sA.resolve().z, "ForegroundPass starts in front of A");
            checkNear(rig.sample(at(0)).aperture, 14.f, 1e-3f, "ForegroundPass maintains shallow DOF aperture");
        }

        // 9. camera_depth_peel
        {
            CameraRig rig(9);
            cameraDepthPeel(rig, {sA, sB, sC}, 0, 60, 5.f, 12.f, kFps);
            check(rig.sample(at(60)).position.z < rig.sample(at(0)).position.z, "DepthPeel moves forward through Z layers");
        }

        // 10. camera_arc_between_subjects
        {
            CameraRig rig(10);
            cameraArcBetweenSubjects(rig, sA, sB, 0, 45, 4.f, kFps);
            CameraPose mid = rig.sample(at(22));
            check(mid.position.distanceTo(sA.resolve()) > 5.f, "Arc trajectory bulges away from linear path");
        }

        // 11. camera_whip_pan_handoff
        {
            CameraRig rig(11);
            cameraWhipPanHandoff(rig, sA, sB, 0, 30, kFps);
            CameraPose end = rig.sample(at(30));
            Vector2 screenB = projectTo1080p(sB.resolve(), rig, end);
            checkNear(screenB.x, 960.f, 0.1f, "WhipPan settles with target B centered");
            checkNear(screenB.y, 540.f, 0.1f, "WhipPan settles with zero residual drift");
        }

        // 12. camera_crane_up_reveal
        {
            CameraRig rig(12);
            cameraCraneUpReveal(rig, sA, sB, 0, 40, 8.f, 10.f, 40.f, 65.f, kFps);
            checkNear(rig.sample(at(0)).fov, 40.f, 1e-3f, "CraneUp starts on narrow FOV");
            checkNear(rig.sample(at(40)).fov, 65.f, 1e-3f, "CraneUp widens FOV to reveal context");
            check(rig.sample(at(40)).position.y > rig.sample(at(0)).position.y + 6.f, "CraneUp elevates camera");
        }

        // 13. camera_push_through_overlay
        {
            CameraRig rig(13);
            cameraPushThroughOverlay(rig, sA, sB, 0, 40, 15.f, kFps);
            check(rig.sample(at(40)).position.z < sA.resolve().z, "PushThrough passes beyond card A");
        }

        // 14. camera_parallax_gallery
        {
            CameraRig rig(14);
            cameraParallaxGallery(rig, {sA, sB, sC}, 0, 60, 8.f, 10.f, kFps);
            check(rig.sample(at(60)).position.x > rig.sample(at(0)).position.x, "ParallaxGallery travels laterally");
        }

        // 15. camera_focus_orbit_settle
        {
            CameraRig rig(15);
            cameraFocusOrbitSettle(rig, sA, 0, 40, 15.f, 5.f, 2.f, kFps);
            CameraPose p0 = rig.sample(at(0));
            CameraPose p40 = rig.sample(at(40));
            checkNear(p0.position, p40.position, 1e-3f, "OrbitSettle returns precisely to base pose");
            checkNear(p0.orientation.angleTo(p40.orientation), 0.f, 1e-3f, "OrbitSettle orientation settles with zero offset");
        }
    }

    // ------------------------------------------------------------- 10. Definitive Canary

    void testCameraMotionTortureV1() {
        section("Camera Motion V1 — Definitive Canary: camera_motion_torture_v1 (16s Continuous)");

        // Sequence:
        //   0 - 60   (2.0s): IMAGE A close-up, background blurred (Focus Push)
        //  60 - 120  (2.0s): Rack focus to IMAGE B
        // 120 - 180  (2.0s): Lateral camera travel
        // 180 - 240  (2.0s): 90° Turn Reveal
        // 240 - 300  (2.0s): WEB CARD C (Focus Push)
        // 300 - 340  (1.3s): Push through card
        // 340 - 400  (2.0s): MAP D (Arc around marker)
        // 400 - 480  (2.7s): Crane out & reveal A+B+C+D together (Total 16.0s / 480 frames)

        CameraRig rig(1, 60.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 8.f));

        const ShotAnchor anchorA = ShotAnchor::image(1, Vector3(0.f, 0.f, 0.f));
        const ShotAnchor anchorB = ShotAnchor::image(2, Vector3(5.f, 0.f, -40.f));
        const ShotAnchor anchorC = ShotAnchor::webCard(3, Vector3(25.f, 0.f, -10.f));
        const ShotAnchor anchorD = ShotAnchor::mapPoint(4, Vector3(40.f, -2.f, -30.f));
        const ShotAnchor overviewAnchor = ShotAnchor::world(Vector3(15.f, 5.f, -20.f));

        // 1. Focus push on A
        cameraFocusPush(rig, anchorA, 0, 60, 3.f, 4.f, 14.f, kFps);

        // 2. Rack focus to B
        cameraRackFocusDuo(rig, anchorA, anchorB, 60, 60, 14.f, kFps);

        // 3. Lateral camera travel towards C
        cameraLateralSubjectToSubject(rig, anchorA, anchorC, 120, 60, kFps);

        // 4. 90° Turn reveal
        cameraOrbit90Reveal(rig, anchorC, anchorD, Vector3(25.f, 0.f, -20.f), 180, 60, kFps);

        // 5. Push through card
        cameraPushThroughOverlay(rig, anchorC, anchorD, 240, 60, 12.f, kFps);

        // 6. Arc around marker D
        cameraArcBetweenSubjects(rig, anchorC, anchorD, 300, 60, 5.f, kFps);

        // 7. Crane out to reveal full scene
        cameraCraneUpReveal(rig, anchorD, overviewAnchor, 360, 120, 12.f, 25.f, 50.f, 75.f, kFps);

        check(rig.endTime() >= at(480), "camera_motion_torture_v1 keys full 480-frame timeline");

        // Verify global continuity across all 480 frames:
        // No position jumps, no quaternion flips, bounded derivative
        bool perfectlyContinuous = true;
        for (int f = 0; f < 480; ++f) {
            CameraPose pA = rig.sample(at(f));
            CameraPose pB = rig.sample(at(f + 1));

            if (pA.position.distanceTo(pB.position) > 2.5f) {
                perfectlyContinuous = false;
            }
            if (pA.orientation.dot(pB.orientation) < 0.f) {
                perfectlyContinuous = false;
            }
            if (std::isnan(pA.focusDistance) || pA.focusDistance < 0.f) {
                perfectlyContinuous = false;
            }
        }
        check(perfectlyContinuous, "camera_motion_torture_v1: flawless continuity across all 480 frames");

        CameraPose finalPose = rig.sample(at(480));
        checkNear(finalPose.fov, 75.f, 0.5f, "Canary final pose has wide overview FOV");
        check(finalPose.position.y > 5.f, "Canary final pose is crane-elevated");
    }

}// namespace

int main() {
    testCameraMotionPrimitives();
    testCameraTargetLockAndSafeFraming();
    testCameraDofCanary();
    testCameraOrbit90Reveal();
    testCameraCornerTurnReveal();
    testCameraContinuityInvariants();
    testCameraRandomAccessDeterminism();
    testParentedCameraRig();
    testAllRemainingRecipes();
    testCameraMotionTortureV1();

    return chrononmotion_test::report();
}
