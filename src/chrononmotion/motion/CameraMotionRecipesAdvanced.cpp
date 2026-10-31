#include "CameraRigTrackBatch.hpp"
// ChrononMotion — camera motion preset families.
#include "chrononmotion/motion/CameraMotion.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Presets.hpp"
#include "Presets_detail.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace chrononmotion::motion {
using presets::detail::authorizePresetForCamera;
using presets::detail::keyVector3;

    // 11. cameraWhipPanHandoff
    void cameraWhipPanHandoff(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                              int inFrame, int duration, float fps, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraWhipPanHandoff", scene);
        if (duration <= 0) throw std::invalid_argument("cameraWhipPanHandoff: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        CameraPose pA = start;
        CameraRig::lookAtPose(pA, targetA);
        CameraPose pB = start;
        CameraRig::lookAtPose(pB, targetB);

        rig.setTarget(targetA);
        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float whipU = Easing::cubicBezier(0.85f, 0.0f, 0.15f, 1.0f).ease(alpha);

            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, whipU);

            Quaternion q;
            q.slerpQuaternions(pA.orientation, pB.orientation, whipU);

            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.orientationTrack().add(time, q, Easing::linear());
            tracks.focusDistanceTrack().add(time, start.position.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 12. cameraCraneUpReveal
    void cameraCraneUpReveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& contextAnchor,
                             int inFrame, int duration, float craneUp, float craneBack,
                             float startFov, float endFov, float fps,
                             const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraCraneUpReveal", scene);
        if (duration <= 0) throw std::invalid_argument("cameraCraneUpReveal: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetCtx = contextAnchor.resolve(scene, t1);

        Vector3 backDir = start.position;
        backDir.sub(targetA);
        if (backDir.lengthSq() < 1e-4f) backDir.set(0.f, 0.f, 1.f);
        else backDir.normalize();

        Vector3 endPos = start.position;
        endPos.y += craneUp;
        endPos.addScaledVector(backDir, craneBack);

        keyVector3(tracks.positionTrack(), t0, t1, start.position, endPos, easing);

        rig.setTarget(targetA);
        tracks.targetTrack().add(t0, targetA, easing);
        tracks.targetTrack().add(t1, targetCtx, Easing::linear());

        const float startFovActual = (startFov > 0.f) ? startFov : start.fov;
        tracks.fovTrack().add(t0, startFovActual, easing);
        tracks.fovTrack().add(t1, endFov, Easing::linear());

        const float dist0 = start.position.distanceTo(targetA);
        const float dist1 = endPos.distanceTo(targetCtx);
        tracks.focusDistanceTrack().add(t0, dist0, easing);
        tracks.focusDistanceTrack().add(t1, dist1, Easing::linear());
        tracks.apply(rig);
    }

    // 13. cameraPushThroughOverlay
    void cameraPushThroughOverlay(CameraRig& rig, const ShotAnchor& cardA, const ShotAnchor& sceneB,
                                  int inFrame, int duration, float passThroughDistance,
                                  float fps, const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraPushThroughOverlay", scene);
        if (duration <= 0) throw std::invalid_argument("cameraPushThroughOverlay: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetCard = cardA.resolve(scene, t0);
        const Vector3 targetScene = sceneB.resolve(scene, t1);

        Vector3 forward = targetCard;
        forward.sub(start.position);
        if (forward.lengthSq() < 1e-4f) forward.set(0.f, 0.f, -1.f);
        else forward.normalize();

        Vector3 endPos = start.position;
        endPos.addScaledVector(forward, passThroughDistance);

        keyVector3(tracks.positionTrack(), t0, t1, start.position, endPos, easing);

        rig.setTarget(targetCard);
        tracks.targetTrack().add(t0, targetCard, easing);
        tracks.targetTrack().add(t1, targetScene, Easing::linear());

        const float d0 = start.position.distanceTo(targetCard);
        const float d1 = endPos.distanceTo(targetScene);
        tracks.focusDistanceTrack().add(t0, d0, easing);
        tracks.focusDistanceTrack().add(t1, d1, Easing::linear());
        tracks.apply(rig);
    }

    // 14. cameraParallaxGallery
    void cameraParallaxGallery(CameraRig& rig, const std::vector<ShotAnchor>& cards,
                               int inFrame, int duration, float cameraZOffset,
                               float fStop, float fps, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraParallaxGallery", scene);
        if (cards.size() < 2) {
            throw std::invalid_argument("cameraParallaxGallery: requires at least two cards");
        }
        if (duration <= 0) {
            throw std::invalid_argument("cameraParallaxGallery: duration must be positive");
        }

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const CameraPose start = rig.sample(t0);

        const Vector3 firstCard = cards.front().resolve(scene, t0);
        const Vector3 lastCard = cards.back().resolve(scene, t1);

        Vector3 startPos = start.position;
        if (startPos.distanceTo(firstCard) < 1e-4f) {
            startPos.set(firstCard.x, firstCard.y, firstCard.z + cameraZOffset);
        }
        Vector3 delta = lastCard;
        delta.sub(firstCard);

        Vector3 endPos = startPos;
        endPos.x += delta.x;
        endPos.y += delta.y;

        keyVector3(tracks.positionTrack(), t0, t1, startPos, endPos, Easing::easeInOut());

        tracks.fStopTrack().add(t0, fStop, Easing::linear());
        tracks.fStopTrack().add(t1, fStop, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        const std::size_t nCards = cards.size();

        rig.setTarget(firstCard);
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;

            const float cardIdxFloat = alpha * static_cast<float>(nCards - 1);
            const std::size_t idx0 = static_cast<std::size_t>(cardIdxFloat);
            const std::size_t idx1 = std::min(idx0 + 1, nCards - 1);
            const float blend = cardIdxFloat - static_cast<float>(idx0);

            const Vector3 pA = cards[idx0].resolve(scene, time);
            const Vector3 pB = cards[idx1].resolve(scene, time);
            Vector3 curTarget;
            curTarget.lerpVectors(pA, pB, blend);

            Vector3 curCam;
            curCam.lerpVectors(startPos, endPos, Easing::easeInOut().ease(alpha));

            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, curCam.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 15. cameraFocusOrbitSettle
    void cameraFocusOrbitSettle(CameraRig& rig, const ShotAnchor& subject,
                                int inFrame, int duration,
                                float yawSweepDeg, float pitchSweepDeg,
                                float pushDistance, float fps, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraFocusOrbitSettle", scene);
        if (duration <= 0) throw std::invalid_argument("cameraFocusOrbitSettle: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetPos = subject.resolve(scene, t0);

        rig.setTarget(targetPos);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 8));
        const float yawRad = math::degToRad(yawSweepDeg);
        const float pitchRad = math::degToRad(pitchSweepDeg);

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * progress;

            const float envelope = std::sin(progress * math::PI);
            const float currentYaw = yawRad * envelope;
            const float currentPitch = pitchRad * envelope;
            const float currentPush = pushDistance * envelope;

            CameraPose curPose = start;
            curPose.target = targetPos;
            curPose.hasTarget = true;
            CameraRig::orbitAround(curPose, targetPos, currentYaw, currentPitch);
            CameraRig::dolly(curPose, currentPush);

            tracks.positionTrack().add(time, curPose.position, Easing::linear());
            tracks.targetTrack().add(time, targetPos, Easing::linear());
            tracks.focusDistanceTrack().add(time, curPose.position.distanceTo(targetPos), Easing::linear());
        }
        tracks.apply(rig);
    }

    // =========================================================================
    // 15 NEW Ultra-Cinematic Recipes (V2 Suite)
    // =========================================================================

    // 16. cameraVertigoDollyZoom
    void cameraVertigoDollyZoom(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                                float startDistance, float endDistance, float startFov,
                                float fps, const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraVertigoDollyZoom", scene);
        if (duration <= 0) throw std::invalid_argument("cameraVertigoDollyZoom: duration must be positive");
        if (startDistance <= 0.f || endDistance <= 0.f) throw std::invalid_argument("cameraVertigoDollyZoom: distances must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const Vector3 targetPos = subject.resolve(scene, t0);
        rig.setTarget(targetPos);

        const float k = startDistance * std::tan(math::degToRad(startFov * 0.5f));
        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 8));

        const CameraPose startPose = rig.sample(t0);
        Vector3 forwardDir;
        if (startPose.position.distanceTo(targetPos) > 1e-4f) {
            forwardDir.subVectors(startPose.position, targetPos).normalize();
        } else {
            forwardDir.set(0.f, 0.f, 1.f);
        }

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            const float currentDist = math::lerp(startDistance, endDistance, eased);
            const float halfFovRad = std::atan(k / currentDist);
            const float currentFovDeg = std::clamp(math::radToDeg(halfFovRad * 2.f), 15.f, 115.f);

            Vector3 currentPos;
            currentPos.addVectors(targetPos, forwardDir.clone().multiplyScalar(currentDist));

            tracks.positionTrack().add(time, currentPos, Easing::linear());
            tracks.targetTrack().add(time, targetPos, Easing::linear());
            tracks.fovTrack().add(time, currentFovDeg, Easing::linear());
            tracks.focusDistanceTrack().add(time, currentDist, Easing::linear());
        }
        tracks.apply(rig);
    }

    // 17. cameraBulletTimeOrbitFreeze
    void cameraBulletTimeOrbitFreeze(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                                     float radius, float height, float sweepDegrees,
                                     float fStop, float fps, const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraBulletTimeOrbitFreeze", scene);
        if (duration <= 0) throw std::invalid_argument("cameraBulletTimeOrbitFreeze: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const Vector3 targetPos = subject.resolve(scene, t0);
        rig.setTarget(targetPos);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 16));
        const float sweepRad = math::degToRad(sweepDegrees);

        tracks.fStopTrack().add(t0, fStop, Easing::linear());
        tracks.fStopTrack().add(t1, fStop, Easing::linear());

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            const float angle = sweepRad * eased;
            Vector3 camPos(
                targetPos.x + radius * std::sin(angle),
                targetPos.y + height,
                targetPos.z + radius * std::cos(angle)
            );

            tracks.positionTrack().add(time, camPos, Easing::linear());
            tracks.targetTrack().add(time, targetPos, Easing::linear());
            tracks.focusDistanceTrack().add(
                time, camPos.distanceTo(targetPos), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 18. cameraFpvDroneDiveRoll
    void cameraFpvDroneDiveRoll(CameraRig& rig, const ShotAnchor& approachAnchor, const ShotAnchor& targetSubject,
                                int inFrame, int duration, float diveElevation, float maxBankRollDeg,
                                float fps, const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraFpvDroneDiveRoll", scene);
        if (duration <= 0) throw std::invalid_argument("cameraFpvDroneDiveRoll: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 p0 = approachAnchor.resolve(scene, t0);
        const Vector3 p1 = targetSubject.resolve(scene, t1);

        const Vector3 startPos(p0.x, p0.y + diveElevation, p0.z + 14.f);
        const Vector3 endPos(p1.x, p1.y + 0.4f, p1.z + 4.0f);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 12));
        const float maxRollRad = math::degToRad(maxBankRollDeg);

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 currentPos;
            currentPos.lerpVectors(startPos, endPos, eased);

            // Bank roll envelope: peaks at 40% of dive, recovers to 0
            const float rollEnvelope = std::sin(progress * math::PI);
            const float currentRoll = maxRollRad * rollEnvelope;

            Vector3 currentTarget;
            currentTarget.lerpVectors(p0, p1, eased);

            tracks.positionTrack().add(time, currentPos, Easing::linear());
            tracks.targetTrack().add(time, currentTarget, Easing::linear());
            tracks.rollTrack().add(time, currentRoll, Easing::linear());
            tracks.focusDistanceTrack().add(time, currentPos.distanceTo(currentTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 19. cameraSplitDiopterDualPlane
    void cameraSplitDiopterDualPlane(CameraRig& rig, const ShotAnchor& foregroundLeft, const ShotAnchor& backgroundRight,
                                     int inFrame, int duration, float fStop, float fps,
                                     const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraSplitDiopterDualPlane", scene);
        if (duration <= 0) throw std::invalid_argument("cameraSplitDiopterDualPlane: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pA = foregroundLeft.resolve(scene, t0);
        const Vector3 pB = backgroundRight.resolve(scene, t1);

        // Position camera laterally between both planes
        const Vector3 startPos(pA.x + 1.2f, pA.y, pA.z + 5.5f);
        const Vector3 endPos(pA.x + 2.5f, pA.y, pA.z + 5.0f);

        tracks.fStopTrack().add(t0, fStop, Easing::linear());
        tracks.fStopTrack().add(t1, fStop, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 8));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 currentPos;
            currentPos.lerpVectors(startPos, endPos, eased);

            Vector3 midTarget;
            midTarget.lerpVectors(pA, pB, 0.45f);

            // Focus oscillates or holds split optical distance
            const float distA = currentPos.distanceTo(pA);
            const float distB = currentPos.distanceTo(pB);
            const float splitFocus = (distA + distB) * 0.5f;

            tracks.positionTrack().add(time, currentPos, Easing::linear());
            tracks.targetTrack().add(time, midTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, splitFocus, Easing::linear());
        }
        tracks.apply(rig);
    }

    // 20. cameraSpiralHelixAscend
    void cameraSpiralHelixAscend(CameraRig& rig, const ShotAnchor& centerPillar, int inFrame, int duration,
                                 float climbHeight, float radius, float totalTurns,
                                 float fps, const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraSpiralHelixAscend", scene);
        if (duration <= 0) throw std::invalid_argument("cameraSpiralHelixAscend: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pillar = centerPillar.resolve(scene, t0);
        rig.setTarget(pillar);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 16));
        const float totalAngle = totalTurns * math::PI * 2.f;

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            const float angle = totalAngle * eased;
            const float y = pillar.y - climbHeight * 0.4f + climbHeight * eased;

            Vector3 camPos(
                pillar.x + radius * std::cos(angle),
                y,
                pillar.z + radius * std::sin(angle)
            );

            tracks.positionTrack().add(time, camPos, Easing::linear());
            tracks.targetTrack().add(time, pillar, Easing::linear());
            tracks.focusDistanceTrack().add(time, camPos.distanceTo(pillar), Easing::linear());
        }
        tracks.apply(rig);
    }

}
