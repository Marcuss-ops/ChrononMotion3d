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

    // 21. cameraFlyUnderReveal
    void cameraFlyUnderReveal(CameraRig& rig, const ShotAnchor& foregroundObstacle, const ShotAnchor& heroTarget,
                              int inFrame, int duration, float subFloorDepth, float fps,
                              const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraFlyUnderReveal", scene);
        if (duration <= 0) throw std::invalid_argument("cameraFlyUnderReveal: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pObstacle = foregroundObstacle.resolve(scene, t0);
        const Vector3 pHero = heroTarget.resolve(scene, t1);

        const Vector3 startPos(pObstacle.x, pObstacle.y - 0.5f, pObstacle.z + 7.f);
        const Vector3 underPos(pObstacle.x, pObstacle.y + subFloorDepth, pObstacle.z - 2.f);
        const Vector3 endPos(pHero.x, pHero.y + 0.5f, pHero.z + 6.f);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 12));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 currentPos;
            Vector3 currentTarget;
            if (progress < 0.5f) {
                const float pSeg = easing.ease(progress * 2.f);
                currentPos.lerpVectors(startPos, underPos, pSeg);
                currentTarget.lerpVectors(pObstacle, pHero, pSeg * 0.5f);
            } else {
                const float pSeg = easing.ease((progress - 0.5f) * 2.f);
                currentPos.lerpVectors(underPos, endPos, pSeg);
                currentTarget.lerpVectors(pObstacle, pHero, 0.5f + pSeg * 0.5f);
            }

            tracks.positionTrack().add(time, currentPos, Easing::linear());
            tracks.targetTrack().add(time, currentTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, currentPos.distanceTo(currentTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 22. cameraAnamorphicBarrelSweep
    void cameraAnamorphicBarrelSweep(CameraRig& rig, const ShotAnchor& anchorLeft, const ShotAnchor& anchorRight,
                                     int inFrame, int duration, float sweepSpanX, float fps,
                                     const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraAnamorphicBarrelSweep", scene);
        if (duration <= 0) throw std::invalid_argument("cameraAnamorphicBarrelSweep: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pL = anchorLeft.resolve(scene, t0);
        const Vector3 pR = anchorRight.resolve(scene, t1);

        const Vector3 startPos(pL.x - sweepSpanX * 0.5f, pL.y, pL.z + 8.5f);
        const Vector3 endPos(pR.x + sweepSpanX * 0.5f, pR.y, pR.z + 8.5f);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 8));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 curPos;
            curPos.lerpVectors(startPos, endPos, eased);

            Vector3 curTarget;
            curTarget.lerpVectors(pL, pR, eased);

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, curPos.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 23. cameraCorkscrewThroughRing
    void cameraCorkscrewThroughRing(CameraRig& rig, const ShotAnchor& ringCenter, const ShotAnchor& interiorCore,
                                    int inFrame, int duration, float plungeDistance, float fps,
                                    const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraCorkscrewThroughRing", scene);
        if (duration <= 0) throw std::invalid_argument("cameraCorkscrewThroughRing: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 ring = ringCenter.resolve(scene, t0);
        const Vector3 core = interiorCore.resolve(scene, t1);

        const Vector3 startPos(ring.x, ring.y, ring.z + plungeDistance);
        const Vector3 endPos(core.x, core.y, core.z + 3.0f);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 16));
        const float fullRoll = math::PI * 2.f;

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 curPos;
            curPos.lerpVectors(startPos, endPos, eased);

            const float currentRoll = fullRoll * eased;

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, core, Easing::linear());
            tracks.rollTrack().add(time, currentRoll, Easing::linear());
            tracks.focusDistanceTrack().add(time, curPos.distanceTo(core), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 24. cameraPendulumSwingReveal
    void cameraPendulumSwingReveal(CameraRig& rig, const ShotAnchor& startAnchor, const ShotAnchor& endAnchor,
                                   int inFrame, int duration, float pendulumDrop, float fps,
                                   const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraPendulumSwingReveal", scene);
        if (duration <= 0) throw std::invalid_argument("cameraPendulumSwingReveal: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pA = startAnchor.resolve(scene, t0);
        const Vector3 pB = endAnchor.resolve(scene, t1);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 12));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            const float x = math::lerp(pA.x - 5.f, pB.x + 5.f, eased);
            // Pendulum arc: drops in center
            const float y = pA.y + 4.f - pendulumDrop * std::sin(progress * math::PI);
            const float z = math::lerp(pA.z + 8.f, pB.z + 8.f, eased);

            Vector3 curPos(x, y, z);
            Vector3 curTarget;
            curTarget.lerpVectors(pA, pB, eased);

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, curPos.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 25. cameraCrashZoomSnap
    void cameraCrashZoomSnap(CameraRig& rig, const ShotAnchor& heroSubject, int inFrame, int duration,
                             float startDistance, float endDistance, float fps,
                             const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraCrashZoomSnap", scene);
        if (duration <= 0) throw std::invalid_argument("cameraCrashZoomSnap: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const Vector3 tgt = heroSubject.resolve(scene, t0);

        rig.setTarget(tgt);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 8));
        // Hold for first 30%, hyper-snap in next 50%, 20% micro-rebound settle
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * progress;

            float curDist = startDistance;
            if (progress >= 0.3f && progress < 0.85f) {
                const float pSnap = (progress - 0.3f) / 0.55f;
                const float eased = Easing::easeIn().ease(pSnap);
                curDist = math::lerp(startDistance, endDistance, eased);
            } else if (progress >= 0.85f) {
                const float pSettle = (progress - 0.85f) / 0.15f;
                // Micro overshoot
                const float bounce = std::sin(pSettle * math::PI) * 0.25f;
                curDist = endDistance - bounce;
            }

            Vector3 curPos(tgt.x, tgt.y, tgt.z + curDist);
            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, tgt, Easing::linear());
            tracks.focusDistanceTrack().add(time, curDist, Easing::linear());
        }
        tracks.apply(rig);
    }

    // 26. cameraInfinityMirrorDive
    void cameraInfinityMirrorDive(CameraRig& rig, const std::vector<ShotAnchor>& portalFrames, int inFrame, int duration,
                                  float totalZTravel, float fps, const Easing& easing,
                                  const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraInfinityMirrorDive", scene);
        if (duration <= 0) throw std::invalid_argument("cameraInfinityMirrorDive: duration must be positive");
        if (portalFrames.empty()) throw std::invalid_argument("cameraInfinityMirrorDive: portalFrames cannot be empty");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pFirst = portalFrames.front().resolve(scene, t0);
        const Vector3 startPos(pFirst.x, pFirst.y, pFirst.z + 8.f);
        const Vector3 endPos(pFirst.x, pFirst.y, pFirst.z + 8.f - totalZTravel);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 12));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 curPos;
            curPos.lerpVectors(startPos, endPos, eased);

            Vector3 lookAhead(curPos.x, curPos.y, curPos.z - 15.f);

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, lookAhead, Easing::linear());
            tracks.focusDistanceTrack().add(time, 8.f, Easing::linear());
        }
        tracks.apply(rig);
    }

    // 27. cameraOverTheShoulderDialogue
    void cameraOverTheShoulderDialogue(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                       int inFrame, int duration, float shoulderOffset, float fps,
                                       const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraOverTheShoulderDialogue", scene);
        if (duration <= 0) throw std::invalid_argument("cameraOverTheShoulderDialogue: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pA = subjectA.resolve(scene, t0);
        const Vector3 pB = subjectB.resolve(scene, t1);

        // Shot 1: Behind A's right shoulder, aiming at B
        const Vector3 otsA_to_B(pA.x + shoulderOffset, pA.y + 0.3f, pA.z + 2.0f);
        // Shot 2: Behind B's left shoulder, aiming at A
        const Vector3 otsB_to_A(pB.x - shoulderOffset, pB.y + 0.3f, pB.z - 2.0f);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 12));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 curPos;
            curPos.lerpVectors(otsA_to_B, otsB_to_A, eased);

            Vector3 curTarget;
            curTarget.lerpVectors(pB, pA, eased);

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, curPos.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 28. cameraBirdsEyeGodViewDescent
    void cameraBirdsEyeGodViewDescent(CameraRig& rig, const ShotAnchor& groundSubject, int inFrame, int duration,
                                      float godViewHeight, float eyeLevelDistance, float fps,
                                      const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraBirdsEyeGodViewDescent", scene);
        if (duration <= 0) throw std::invalid_argument("cameraBirdsEyeGodViewDescent: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const Vector3 subject = groundSubject.resolve(scene, t0);

        const Vector3 startGod(subject.x, subject.y + godViewHeight, subject.z + 0.1f);
        const Vector3 endEye(subject.x, subject.y + 0.2f, subject.z + eyeLevelDistance);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 16));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            Vector3 curPos;
            curPos.lerpVectors(startGod, endEye, eased);

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, subject, Easing::linear());
            tracks.focusDistanceTrack().add(time, curPos.distanceTo(subject), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 29. cameraSlalomCardWeave
    void cameraSlalomCardWeave(CameraRig& rig, const std::vector<ShotAnchor>& slalomGates, int inFrame, int duration,
                               float slalomAmplitude, float maxBankRollDeg, float fps,
                               const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraSlalomCardWeave", scene);
        if (duration <= 0) throw std::invalid_argument("cameraSlalomCardWeave: duration must be positive");
        if (slalomGates.empty()) throw std::invalid_argument("cameraSlalomCardWeave: slalomGates cannot be empty");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 p0 = slalomGates.front().resolve(scene, t0);
        const Vector3 p1 = slalomGates.back().resolve(scene, t1);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 16));
        const float maxRollRad = math::degToRad(maxBankRollDeg);

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            // Forward travel
            const float z = math::lerp(p0.z + 8.f, p1.z - 4.f, eased);
            // S-curve lateral wave
            const float wavePhase = eased * math::PI * 4.f; // 2 full S-turns
            const float x = math::lerp(p0.x, p1.x, eased) + slalomAmplitude * std::sin(wavePhase);
            // Coordinated centrifugal banking roll
            const float currentRoll = -maxRollRad * std::cos(wavePhase);

            Vector3 curPos(x, p0.y, z);
            Vector3 curTarget(x * 0.5f, p0.y, z - 8.f);

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.rollTrack().add(time, currentRoll, Easing::linear());
            tracks.focusDistanceTrack().add(time, 8.f, Easing::linear());
        }
        tracks.apply(rig);
    }

    // 30. cameraHeroLowAngleMonolithOrbit
    void cameraHeroLowAngleMonolithOrbit(CameraRig& rig, const ShotAnchor& monolith, int inFrame, int duration,
                                         float radius, float floorLevelY, float orbitSweepDeg,
                                         float upwardPitchDeg, float fps,
                                         const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraHeroLowAngleMonolithOrbit", scene);
        if (duration <= 0) throw std::invalid_argument("cameraHeroLowAngleMonolithOrbit: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 mono = monolith.resolve(scene, t0);
        const float pitchRad = math::degToRad(upwardPitchDeg);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 12));
        const float sweepRad = math::degToRad(orbitSweepDeg);
        const float startAngle = -sweepRad * 0.5f;

        for (unsigned int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float eased = easing.ease(progress);
            const float time = t0 + (t1 - t0) * progress;

            const float angle = startAngle + sweepRad * eased;
            Vector3 curPos(
                mono.x + radius * std::sin(angle),
                floorLevelY,
                mono.z + radius * std::cos(angle)
            );
            const float horizontalDistance = std::hypot(
                mono.x - curPos.x, mono.z - curPos.z);
            const Vector3 aimPoint(
                mono.x,
                curPos.y + horizontalDistance * std::tan(pitchRad),
                mono.z);

            tracks.positionTrack().add(time, curPos, Easing::linear());
            tracks.targetTrack().add(time, aimPoint, Easing::linear());
            tracks.focusDistanceTrack().add(time, curPos.distanceTo(aimPoint), Easing::linear());
        }
        tracks.apply(rig);
    }

}
