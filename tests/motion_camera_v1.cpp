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

#include <array>
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
            .withFocus(CameraFocusMode::TargetDistance, 0.f, 0.12f);

        CameraPose shotPose = shot.evaluatePose(&scene, 0.f);
        checkNear(shotPose.target, Vector3(0.f, 0.f, 0.f), 1e-4f, "CameraShot aims at its target anchor");
        checkNear(shotPose.fov, 50.f, 1e-4f, "CameraShot preserves authored FOV");
        checkNear(shotPose.legacyDefocusStrength, 0.12f, 1e-4f, "CameraShot preserves normalized defocus strength");
        checkNear(shotPose.position.distanceTo(shotPose.target), 5.0f, 1e-4f, "CloseUp framing sets distance to 5 units");

        // CameraFocusResolver
        CameraPose focusCamera;
        focusCamera.position.set(0.f, 0.f, 0.f);
        focusCamera.orientation.identity();
        const Vector3 tgtA(0.f, 0.f, 0.f);
        const Vector3 tgtB(0.f, 4.f, -10.f);

        const Vector3 offAxisTarget(3.f, 0.f, -4.f);
        float focTarget = CameraFocusResolver::resolveFocus(CameraFocusMode::TargetDistance, focusCamera, offAxisTarget);
        checkNear(focTarget, 4.f, 1e-4f, "CameraFocusResolver TargetDistance measures along optical axis");

        float focBlend = CameraFocusResolver::resolveFocus(CameraFocusMode::BlendTargets, focusCamera, offAxisTarget, tgtB, 0.5f);
        checkNear(focBlend, 7.f, 1e-4f, "CameraFocusResolver blends optical-axis distances");

        float focForeground = CameraFocusResolver::resolveFocus(CameraFocusMode::ForegroundSubject, focusCamera, offAxisTarget, tgtB);
        checkNear(focForeground, 4.f, 1e-4f, "CameraFocusResolver ForegroundSubject selects nearest optical plane");

        float focManual = CameraFocusResolver::resolveFocus(CameraFocusMode::Manual, focusCamera, tgtA, tgtB, 0.f, 42.5f);
        checkNear(focManual, 42.5f, 1e-4f, "CameraFocusResolver Manual returns authored distance");
    }

    void testT16CameraTruthProjection() {
        section("T16 Camera Truth — FOV, dolly scale, and three-plane parallax");

        CameraRig rig(990, 45.f, 16.f / 9.f, 0.1f, 100.f);
        rig.setTarget(Vector3(0.f, 0.f, 0.f));
        rig.positionTrack().add(0.f, Vector3(-0.8f, 0.1f, 12.f));
        rig.positionTrack().add(149.f / kFps, Vector3(0.8f, 0.15f, 10.f));

        const std::array<Vector3, 3> anchors{
            Vector3(-1.1f, 0.f, 2.f), Vector3(0.f, 0.f, -0.5f),
            Vector3(1.1f, 0.f, -3.f)};
        std::array<Vector2, 3> first{};
        std::array<Vector2, 3> last{};
        for (std::size_t i = 0; i < anchors.size(); ++i) {
            first[i] = projectTo1080p(anchors[i], rig, rig.sample(at(0)));
            last[i] = projectTo1080p(anchors[i], rig, rig.sample(at(149)));
        }
        const float nearShift = last[0].x - first[0].x;
        const float farShift = last[2].x - first[2].x;
        check(std::abs(nearShift - farShift) > 30.f,
              "CAM-03 orbit/push produces measurable relative parallax across depth planes");

        const auto nearLeft = projectTo1080p(Vector3(-1.6f, 0.f, 2.f), rig, rig.sample(at(0)));
        const auto nearRight = projectTo1080p(Vector3(-0.6f, 0.f, 2.f), rig, rig.sample(at(0)));
        const auto farLeft = projectTo1080p(Vector3(0.6f, 0.f, -3.f), rig, rig.sample(at(0)));
        const auto farRight = projectTo1080p(Vector3(1.6f, 0.f, -3.f), rig, rig.sample(at(0)));
        check(std::abs(nearRight.x - nearLeft.x) > std::abs(farRight.x - farLeft.x),
              "CAM-02 nearer plane projects larger than an equal-sized far plane");

        rig.setFov(50.f);
        const auto wide = projectTo1080p(Vector3(2.f, 0.f, 0.f), rig, rig.sample(at(75)));
        rig.setFov(30.f);
        const auto narrow = projectTo1080p(Vector3(2.f, 0.f, 0.f), rig, rig.sample(at(75)));
        check(std::abs(narrow.x - wide.x) > 100.f,
              "CAM-01 changing FOV from 50 to 30 degrees changes subject framing");

        const auto frame30 = rig.sample(at(30));
        const auto frame75 = rig.sample(at(75));
        const auto frame120 = rig.sample(at(120));
        const auto frame149 = rig.sample(at(149));
        checkNear(frame30.position, rig.sample(at(30)).position, 1e-6f,
                  "CAM-07 repeated random-access samples are deterministic");
        check(frame75.position.distanceTo(frame30.position) > 0.f &&
              frame120.position.distanceTo(frame75.position) > 0.f &&
              frame149.position.distanceTo(frame120.position) > 0.f,
              "CAM-07 all required T16 sample times advance the camera");
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

        cameraRackFocusDuo(rig, subjectA, subjectB, 0, 60, 0.16f, kFps);

        CameraPose p0 = rig.sample(at(0));
        CameraPose p60 = rig.sample(at(60));

        // Frame 0: focusDistance ≈ dist(camera, A) = 50.f
        checkNear(p0.focusDistance, 50.f, 0.1f, "Frame 0: focusDistance matches distance to subject A");
        checkNear(p0.legacyDefocusStrength, 0.16f, 0.001f, "Frame 0: normalized defocus strength is set");

        // Frame 60: focusDistance ≈ dist(camera, B) = 350.f
        checkNear(p60.focusDistance, 350.f, 0.1f, "Frame 60: focusDistance matches distance to subject B");
        checkNear(p60.legacyDefocusStrength, 0.16f, 0.001f, "Frame 60: normalized defocus strength is maintained");

        // Camera position remains unchanged
        checkNear(p0.position, p60.position, 1e-4f, "Rack focus preserves stationary camera position");

        // Focus distance is measured along the optical axis. Decentered
        // subjects therefore focus at their view-axis depth, not their longer
        // Euclidean distance from the camera origin.
        CameraRig offAxisRig(2, 60.f, 16.f / 9.f);
        offAxisRig.setPosition(Vector3(0.f, 0.f, 0.f));
        offAxisRig.setOrientation(Quaternion{});
        cameraRackFocusDuo(
            offAxisRig,
            ShotAnchor::world(Vector3(30.f, 0.f, -100.f)),
            ShotAnchor::world(Vector3(30.f, 0.f, -200.f)),
            0, 30, 0.16f, kFps);
        checkNear(offAxisRig.sample(at(0)).focusDistance, 100.f, 1e-3f,
                  "Rack focus uses axial depth for the near decentered subject");
        checkNear(offAxisRig.sample(at(30)).focusDistance, 200.f, 1e-3f,
                  "Rack focus uses axial depth for the far decentered subject");
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
        shotA.withTarget(sA).withDistance(8.f).withFov(55.f).withFocus(CameraFocusMode::TargetDistance, 0.f, 0.08f);
        CameraShot shotB;
        shotB.withTarget(sB).withDistance(12.f).withFov(45.f).withFocus(CameraFocusMode::TargetDistance, 0.f, 0.14f);

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

        cameraDollyFocusHandoff(rig, sA, sB, 0, 90, Vector3(4.f, 1.f, -3.f), 0.10f, kFps);

        // Sequential sampling
        for (int f = 0; f <= 90; ++f) {
            (void)rig.sample(at(f));
        }
        CameraPose seqPose90 = rig.sample(at(90));

        // Fresh rig sampled directly at frame 90
        CameraRig freshRig(1, 60.f, 16.f / 9.f);
        cameraDollyFocusHandoff(freshRig, sA, sB, 0, 90, Vector3(4.f, 1.f, -3.f), 0.10f, kFps);
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
            cameraForegroundPassReveal(rig, sA, sB, 0, 40, Vector3(5.f, 0.f, -6.f), 0.14f, kFps);
            check(rig.sample(at(0)).position.z > sA.resolve().z, "ForegroundPass starts in front of A");
            checkNear(rig.sample(at(0)).legacyDefocusStrength, 0.14f, 1e-3f, "ForegroundPass maintains normalized defocus strength");
        }

        // 9. camera_depth_peel
        {
            CameraRig rig(9);
            cameraDepthPeel(rig, {sA, sB, sC}, 0, 60, 5.f, 0.12f, kFps);
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
            cameraParallaxGallery(rig, {sA, sB, sC}, 0, 60, 8.f, 0.10f, kFps);
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

        // 30. camera_hero_low_angle_monolith_orbit honors its authored pitch.
        {
            CameraRig rig(30);
            constexpr float pitchDeg = 26.f;
            cameraHeroLowAngleMonolithOrbit(
                rig, sA, 0, 30, 7.f, -3.f, 0.f, pitchDeg, kFps);
            const CameraPose pose = rig.sample(at(0));
            const float horizontalDistance = std::hypot(
                pose.target.x - pose.position.x,
                pose.target.z - pose.position.z);
            const float actualPitch = math::radToDeg(std::atan2(
                pose.target.y - pose.position.y, horizontalDistance));
            checkNear(actualPitch, pitchDeg, 1e-3f,
                      "MonolithOrbit applies the authored upward camera pitch");
        }

        // 29. camera_slalom_card_weave honors its authored easing curve.
        {
            CameraRig linearRig(29);
            CameraRig easedRig(29);
            cameraSlalomCardWeave(
                linearRig, {sA, sB}, 0, 60, 4.5f, 0.18f, kFps,
                Easing::linear());
            cameraSlalomCardWeave(
                easedRig, {sA, sB}, 0, 60, 4.5f, 0.18f, kFps,
                Easing::easeInOut());
            check(linearRig.sample(at(15)).position.distanceTo(
                      easedRig.sample(at(15)).position) > 0.1f,
                  "SlalomCardWeave uses its authored easing curve");
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
        cameraFocusPush(rig, anchorA, 0, 60, 3.f, 0.04f, 0.14f, kFps);

        // 2. Rack focus to B
        cameraRackFocusDuo(rig, anchorA, anchorB, 60, 60, 0.14f, kFps);

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
    testT16CameraTruthProjection();
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
