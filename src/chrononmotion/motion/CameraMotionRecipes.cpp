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

    // ------------------------------------------------- 30 Authoritative Recipes

    // 1. cameraFocusPush
    void cameraFocusPush(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                         float pushDistance, float startFStop, float endFStop,
                         float fps, const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraFocusPush", scene);
        if (duration <= 0) throw std::invalid_argument("cameraFocusPush: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetPos = subject.resolve(scene, t0);

        Vector3 forward = targetPos;
        forward.sub(start.position);
        if (forward.lengthSq() < 1e-6f) {
            forward.set(0.f, 0.f, -1.f);
            forward.applyQuaternion(start.orientation);
        } else {
            forward.normalize();
        }

        Vector3 endPos = start.position;
        endPos.addScaledVector(forward, pushDistance);

        rig.setTarget(targetPos);
        keyVector3(tracks.positionTrack(), t0, t1, start.position, endPos, easing);
        tracks.targetTrack().add(t0, targetPos, easing);
        tracks.targetTrack().add(t1, targetPos, Easing::linear());

        tracks.fStopTrack().add(t0, startFStop, easing);
        tracks.fStopTrack().add(t1, endFStop, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);
            Vector3 pos;
            pos.lerpVectors(start.position, endPos, u);
            const float dist = pos.distanceTo(targetPos);
            tracks.focusDistanceTrack().add(time, dist, Easing::linear());
        }
        tracks.apply(rig);
    }

    // 2. cameraRackFocusDuo
    void cameraRackFocusDuo(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                            int inFrame, int duration, float fStop, float fps,
                            const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraRackFocusDuo", scene);
        if (duration <= 0) throw std::invalid_argument("cameraRackFocusDuo: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose current = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        const float distA = CameraFocusResolver::resolveFocus(
            CameraFocusMode::TargetDistance, current, targetA);
        const float distB = CameraFocusResolver::resolveFocus(
            CameraFocusMode::TargetDistance, current, targetB);

        tracks.focusDistanceTrack().add(t0, distA, easing);
        tracks.focusDistanceTrack().add(t1, distB, Easing::linear());

        tracks.fStopTrack().add(t0, fStop, easing);
        tracks.fStopTrack().add(t1, fStop, Easing::linear());
        tracks.apply(rig);
    }

    // 3. cameraDollyFocusHandoff
    void cameraDollyFocusHandoff(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                 int inFrame, int duration, const Vector3& dollyDelta,
                                 float fStop, float fps, const Easing& easing,
                                 const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraDollyFocusHandoff", scene);
        if (duration <= 0) throw std::invalid_argument("cameraDollyFocusHandoff: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        Vector3 right(1.f, 0.f, 0.f);
        Vector3 up(0.f, 1.f, 0.f);
        Vector3 forward(0.f, 0.f, -1.f);
        right.applyQuaternion(start.orientation);
        up.applyQuaternion(start.orientation);
        forward.applyQuaternion(start.orientation);

        Vector3 endPos = start.position;
        endPos.addScaledVector(right, dollyDelta.x);
        endPos.addScaledVector(up, dollyDelta.y);
        endPos.addScaledVector(forward, -dollyDelta.z);

        keyVector3(tracks.positionTrack(), t0, t1, start.position, endPos, easing);

        rig.setTarget(targetA);
        tracks.targetTrack().add(t0, targetA, easing);
        tracks.targetTrack().add(t1, targetB, Easing::linear());

        tracks.fStopTrack().add(t0, fStop, easing);
        tracks.fStopTrack().add(t1, fStop, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            pos.lerpVectors(start.position, endPos, u);
            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, u);

            tracks.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 4. cameraLateralSubjectToSubject
    void cameraLateralSubjectToSubject(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                       int inFrame, int duration, float fps,
                                       const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraLateralSubjectToSubject", scene);
        if (duration <= 0) throw std::invalid_argument("cameraLateralSubjectToSubject: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        Vector3 offset = start.position;
        offset.sub(targetA);
        Vector3 startPos = start.position;
        if (offset.lengthSq() < 1e-4f) {
            offset.set(0.f, 0.f, 10.f);
            startPos.addVectors(targetA, offset);
        }

        Vector3 endPos = targetB;
        endPos.add(offset);

        keyVector3(tracks.positionTrack(), t0, t1, startPos, endPos, easing);

        rig.setTarget(targetA);
        tracks.targetTrack().add(t0, targetA, easing);
        tracks.targetTrack().add(t1, targetB, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            pos.lerpVectors(startPos, endPos, u);
            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, u);

            tracks.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 5. cameraOrbit90Reveal
    void cameraOrbit90Reveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                             const Vector3& pivot, int inFrame, int duration, float fps,
                             const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraOrbit90Reveal", scene);
        if (duration <= 0) throw std::invalid_argument("cameraOrbit90Reveal: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        rig.setTarget(targetA);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            CameraPose temp = start;
            CameraRig::orbitAround(temp, pivot, 1.57079632679f * u, 0.f);

            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, u);

            tracks.positionTrack().add(time, temp.position, Easing::linear());
            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, temp.position.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 6. cameraCornerTurnReveal
    void cameraCornerTurnReveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                const Vector3& cornerPoint, int inFrame, int duration,
                                float cornerRadius, float fps, const Easing& easing,
                                const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraCornerTurnReveal", scene);
        if (duration <= 0) throw std::invalid_argument("cameraCornerTurnReveal: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        Vector3 A = start.position;
        Vector3 B = cornerPoint;
        Vector3 d1 = B; d1.sub(A);
        Vector3 toB = targetB; toB.sub(B);
        Vector3 d2(-d1.z, 0.f, d1.x);
        if (d2.dot(toB) < 0.f) d2.multiplyScalar(-1.f);
        if (d2.lengthSq() < 1e-4f) d2.set(10.f, 0.f, 0.f);
        else { d2.normalize(); d2.multiplyScalar(d1.length()); }
        Vector3 C = B; C.add(d2);

        const float len1 = d1.length();
        const float len2 = d2.length();
        const float rad = std::min(cornerRadius, std::min(len1, len2) * 0.45f);
        Vector3 v1 = (len1 > 1e-6f) ? d1.clone().normalize() : Vector3(0.f, 0.f, -1.f);
        Vector3 v2 = (len2 > 1e-6f) ? d2.clone().normalize() : Vector3(1.f, 0.f, 0.f);
        Vector3 E1 = B; E1.addScaledVector(v1, -rad);
        Vector3 E2 = B; E2.addScaledVector(v2, rad);
        const float seg1Len = A.distanceTo(E1);
        const float seg2Len = E1.distanceTo(B) + B.distanceTo(E2);
        const float seg3Len = E2.distanceTo(C);
        const float totalLen = seg1Len + seg2Len + seg3Len;

        rig.setTarget(targetA);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            const float distAlong = u * totalLen;
            if (distAlong <= seg1Len) {
                const float s = (seg1Len > 1e-6f) ? distAlong / seg1Len : 0.f;
                pos.lerpVectors(A, E1, s);
            } else if (distAlong <= seg1Len + seg2Len) {
                const float s = (seg2Len > 1e-6f) ? (distAlong - seg1Len) / seg2Len : 0.f;
                const float om = 1.f - s;
                pos.set(0.f, 0.f, 0.f);
                pos.addScaledVector(E1, om * om);
                pos.addScaledVector(B, 2.f * om * s);
                pos.addScaledVector(E2, s * s);
            } else {
                const float s = (seg3Len > 1e-6f) ? (distAlong - seg1Len - seg2Len) / seg3Len : 0.f;
                pos.lerpVectors(E2, C, s);
            }

            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, u);

            tracks.positionTrack().add(time, pos, Easing::linear());
            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 7. cameraDetachTurnReacquire
    void cameraDetachTurnReacquire(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                   int inFrame, int duration, float fps,
                                   const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraDetachTurnReacquire", scene);
        if (duration <= 0) throw std::invalid_argument("cameraDetachTurnReacquire: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        rig.setTarget(targetA);
        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 curTarget;
            if (u < 0.2f) {
                curTarget = targetA;
            } else if (u > 0.8f) {
                curTarget = targetB;
            } else {
                const float blend = (u - 0.2f) / 0.6f;
                const float smoothB = Easing::easeInOut().ease(blend);
                curTarget.lerpVectors(targetA, targetB, smoothB);
            }

            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, start.position.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }

    // 8. cameraForegroundPassReveal
    void cameraForegroundPassReveal(CameraRig& rig, const ShotAnchor& foregroundA, const ShotAnchor& subjectB,
                                    int inFrame, int duration, const Vector3& lateralPassOffset,
                                    float fStop, float fps, const Easing& easing,
                                    const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraForegroundPassReveal", scene);
        if (duration <= 0) throw std::invalid_argument("cameraForegroundPassReveal: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = foregroundA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        Vector3 startPos = start.position;
        if (startPos.distanceTo(targetA) < 1e-4f) {
            startPos.set(targetA.x, targetA.y, targetA.z + 4.f);
        }

        Vector3 endPos = startPos;
        endPos.add(lateralPassOffset);

        keyVector3(tracks.positionTrack(), t0, t1, startPos, endPos, easing);

        rig.setTarget(targetA);
        tracks.targetTrack().add(t0, targetA, easing);
        tracks.targetTrack().add(t1, targetB, Easing::linear());

        tracks.fStopTrack().add(t0, fStop, easing);
        tracks.fStopTrack().add(t1, fStop, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            pos.lerpVectors(start.position, endPos, u);

            const float distA = pos.distanceTo(targetA);
            const float distB = pos.distanceTo(targetB);
            const float foc = distA + (distB - distA) * u;
            tracks.focusDistanceTrack().add(time, foc, Easing::linear());
        }
        tracks.apply(rig);
    }

    // 9. cameraDepthPeel
    void cameraDepthPeel(CameraRig& rig, const std::vector<ShotAnchor>& subjects,
                         int inFrame, int duration, float pushPerSubject,
                         float fStop, float fps, const Easing& easing,
                         const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraDepthPeel", scene);
        if (subjects.size() < 2) {
            throw std::invalid_argument("cameraDepthPeel: requires at least two subjects");
        }
        if (duration <= 0) {
            throw std::invalid_argument("cameraDepthPeel: duration must be positive");
        }

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const CameraPose start = rig.sample(t0);

        Vector3 curPos = start.position;
        tracks.fStopTrack().add(t0, fStop, Easing::linear());
        tracks.fStopTrack().add(t1, fStop, Easing::linear());

        const std::size_t numSegs = subjects.size() - 1;
        const int segDuration = std::max(1, duration / static_cast<int>(numSegs));

        for (std::size_t i = 0; i < numSegs; ++i) {
            const int segStartFrame = inFrame + static_cast<int>(i) * segDuration;
            const int segEndFrame = (i == numSegs - 1) ? inFrame + duration : segStartFrame + segDuration;
            const float tStart = frameToTime(segStartFrame, fps);
            const float tEnd = frameToTime(segEndFrame, fps);

            const Vector3 targetCurr = subjects[i].resolve(scene, tStart);
            const Vector3 targetNext = subjects[i + 1].resolve(scene, tEnd);

            Vector3 nextPos = curPos;
            nextPos.z -= pushPerSubject;

            keyVector3(tracks.positionTrack(), tStart, tEnd, curPos, nextPos, easing);

            if (i == 0) rig.setTarget(targetCurr);
            tracks.targetTrack().add(tStart, targetCurr, easing);
            tracks.targetTrack().add(tEnd, targetNext, Easing::linear());

            const float distStart = curPos.distanceTo(targetCurr);
            const float distEnd = nextPos.distanceTo(targetNext);
            tracks.focusDistanceTrack().add(tStart, distStart, easing);
            tracks.focusDistanceTrack().add(tEnd, distEnd, Easing::linear());

            curPos = nextPos;
        }
        tracks.apply(rig);
    }

    // 10. cameraArcBetweenSubjects
    void cameraArcBetweenSubjects(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                  int inFrame, int duration, float arcBowDistance,
                                  float fps, const Easing& easing, const MotionScene* scene) {
        CameraRigTrackBatch tracks(rig);
        authorizePresetForCamera(rig, "cameraArcBetweenSubjects", scene);
        if (duration <= 0) throw std::invalid_argument("cameraArcBetweenSubjects: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose start = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        Vector3 offset = start.position;
        offset.sub(targetA);
        if (offset.lengthSq() < 1e-4f) offset.set(0.f, 0.f, 10.f);

        Vector3 P0 = start.position;
        Vector3 P1 = targetB;
        P1.add(offset);

        Vector3 apex;
        apex.addVectors(P0, P1).multiplyScalar(0.5f);
        Vector3 delta = P1; delta.sub(P0);
        Vector3 perp(-delta.z, 0.f, delta.x);
        if (perp.lengthSq() > 1e-6f) {
            perp.normalize().multiplyScalar(arcBowDistance);
            apex.add(perp);
        } else {
            apex.y += arcBowDistance;
        }

        rig.setTarget(targetA);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            const float om = 1.f - u;
            Vector3 pos;
            pos.addScaledVector(P0, om * om);
            pos.addScaledVector(apex, 2.f * om * u);
            pos.addScaledVector(P1, u * u);

            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, u);

            tracks.positionTrack().add(time, pos, Easing::linear());
            tracks.targetTrack().add(time, curTarget, Easing::linear());
            tracks.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
        tracks.apply(rig);
    }
}
