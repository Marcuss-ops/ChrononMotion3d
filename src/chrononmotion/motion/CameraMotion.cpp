// ChrononMotion — Camera Motion V1: authoritative shot-to-shot system implementation.

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

    // ------------------------------------------------------------- ShotAnchor

    Vector3 ShotAnchor::resolve(const MotionScene* scene, float seconds) const {
        if (kind == AnchorKind::WorldPoint || layerId == Layer::kRoot || scene == nullptr) {
            return worldPoint;
        }

        const Layer* l = scene->findLayer(layerId);
        if (!l) {
            return worldPoint;
        }

        // Trace up to root and compose world transform
        std::vector<const Layer*> chain;
        const Layer* curr = l;
        while (curr) {
            chain.push_back(curr);
            if (curr->parentId == Layer::kRoot) break;
            curr = scene->findLayer(curr->parentId);
        }

        Matrix4 worldMatrix;
        worldMatrix.identity();
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const Transform t = (*it)->sample(seconds);
            Matrix4 local;
            local.compose(t.position, t.localQuaternion(), t.scale);
            worldMatrix.multiply(local);
        }

        Vector3 result = localPoint;
        result.applyMatrix4(worldMatrix);
        return result;
    }

    Vector3 ShotAnchor::resolve(const Layer* l, float seconds) const {
        if (!l || kind == AnchorKind::WorldPoint) {
            return worldPoint;
        }

        const Transform t = l->sample(seconds);
        Matrix4 local;
        local.compose(t.position, t.localQuaternion(), t.scale);
        Vector3 result = localPoint;
        result.applyMatrix4(local);
        return result;
    }

    // ------------------------------------------------------------- CameraShot

    CameraPose CameraShot::evaluatePose(const MotionScene* scene, float seconds) const {
        const Vector3 targetPos = target.resolve(scene, seconds);
        const float dist = (distance > 0.f) ? distance : framingDistance(framing);

        const float cosElev = std::cos(elevation);
        const float sinElev = std::sin(elevation);
        const float cosAzim = std::cos(azimuth);
        const float sinAzim = std::sin(azimuth);

        Vector3 offset(dist * cosElev * sinAzim, dist * sinElev, dist * cosElev * cosAzim);

        CameraPose pose;
        pose.position.addVectors(targetPos, offset);
        pose.target = targetPos;
        pose.hasTarget = true;
        pose.fov = fov;
        pose.roll = roll;
        pose.aperture = aperture;
        pose.fStop = fStop;

        if (focusMode == CameraFocusMode::Manual) {
            pose.focusDistance = focusDistance;
        } else {
            pose.focusDistance = pose.position.distanceTo(targetPos);
        }

        CameraRig::lookAtPose(pose, targetPos);
        if (roll != 0.f) {
            CameraRig::roll(pose, roll);
        }

        return pose;
    }

    // --------------------------------------------------- CameraFocusResolver

    float CameraFocusResolver::resolveFocus(CameraFocusMode mode,
                                           const Vector3& cameraPos,
                                           const Vector3& targetA,
                                           const Vector3& targetB,
                                           float blendProgress,
                                           float manualDistance) {
        switch (mode) {
            case CameraFocusMode::Manual:
                return manualDistance;
            case CameraFocusMode::TargetDistance:
                return cameraPos.distanceTo(targetA);
            case CameraFocusMode::BlendTargets: {
                const float distA = cameraPos.distanceTo(targetA);
                const float distB = cameraPos.distanceTo(targetB);
                const float clamped = std::clamp(blendProgress, 0.f, 1.f);
                return distA + (distB - distA) * clamped;
            }
            case CameraFocusMode::ForegroundSubject: {
                const float distA = cameraPos.distanceTo(targetA);
                const float distB = cameraPos.distanceTo(targetB);
                return std::min(distA, distB);
            }
        }
        return cameraPos.distanceTo(targetA);
    }

    float CameraFocusResolver::resolveFocus(CameraFocusMode mode,
                                           const Vector3& cameraPos,
                                           const Vector3& target,
                                           float manualDistance) {
        if (mode == CameraFocusMode::Manual) {
            return manualDistance;
        }
        return cameraPos.distanceTo(target);
    }

    // ------------------------------------------------------ CameraTransition

    void CameraTransition::apply(CameraRig& rig, const MotionScene* scene, float fps) const {
        authorizePresetForCamera(rig, "CameraTransition", scene);
        if (duration <= 0) {
            throw std::invalid_argument("CameraTransition::apply: duration must be positive");
        }

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose p0 = fromShot.evaluatePose(scene, t0);
        const CameraPose p1 = toShot.evaluatePose(scene, t1);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));

        rig.setTarget(p0.target);

        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            if (path == TransitionPath::Linear || path == TransitionPath::Dolly) {
                pos.lerpVectors(p0.position, p1.position, u);
            } else if (path == TransitionPath::Arc) {
                Vector3 apex = arcApex;
                if (apex.lengthSq() < 1e-6f) {
                    apex.addVectors(p0.position, p1.position).multiplyScalar(0.5f);
                    Vector3 delta = p1.position;
                    delta.sub(p0.position);
                    Vector3 perp(-delta.z, 0.f, delta.x);
                    if (perp.lengthSq() > 1e-6f) {
                        perp.normalize().multiplyScalar(3.0f);
                        apex.add(perp);
                    } else {
                        apex.y += 2.0f;
                    }
                }
                const float om = 1.f - u;
                pos.set(0.f, 0.f, 0.f);
                pos.addScaledVector(p0.position, om * om);
                pos.addScaledVector(apex, 2.f * om * u);
                pos.addScaledVector(p1.position, u * u);
            } else if (path == TransitionPath::Orbit) {
                Vector3 pivot = orbitPivot;
                if (pivot.lengthSq() < 1e-6f) {
                    pivot.addVectors(p0.target, p1.target).multiplyScalar(0.5f);
                }
                CameraPose temp = p0;
                CameraRig::orbitAround(temp, pivot, orbitAngleRad * u, 0.f);
                pos = temp.position;
            } else if (path == TransitionPath::Corner) {
                const Vector3& A = p0.position;
                const Vector3& B = cornerPoint;
                const Vector3& C = p1.position;

                Vector3 d1 = B; d1.sub(A);
                Vector3 d2 = C; d2.sub(B);
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
            } else {
                pos.lerpVectors(p0.position, p1.position, u);
            }

            rig.positionTrack().add(time, pos, Easing::linear());

            Vector3 curTarget;
            if (orientationMode == OrientationMode::DetachReacquire) {
                if (u < 0.2f) {
                    curTarget = p0.target;
                } else if (u > 0.8f) {
                    curTarget = p1.target;
                } else {
                    const float blend = (u - 0.2f) / 0.6f;
                    const float smoothB = Easing::easeInOut().ease(blend);
                    curTarget.lerpVectors(p0.target, p1.target, smoothB);
                }
            } else {
                curTarget.lerpVectors(p0.target, p1.target, u);
            }
            rig.targetTrack().add(time, curTarget, Easing::linear());

            CameraPose samplePose;
            samplePose.position = pos;
            if (orientationMode == OrientationMode::InterpolateOrientation) {
                Quaternion q;
                q.slerpQuaternions(p0.orientation, p1.orientation, u);
                samplePose.orientation = q;
            } else if (orientationMode == OrientationMode::Whip) {
                const float whipU = Easing::cubicBezier(0.85f, 0.0f, 0.15f, 1.0f).ease(alpha);
                Quaternion q;
                q.slerpQuaternions(p0.orientation, p1.orientation, whipU);
                samplePose.orientation = q;
            } else {
                CameraRig::lookAtPose(samplePose, curTarget);
            }

            const float curRoll = p0.roll + (p1.roll - p0.roll) * u;
            if (curRoll != 0.f) {
                CameraRig::roll(samplePose, curRoll);
            }
            rig.orientationTrack().add(time, samplePose.orientation, Easing::linear());
            rig.rollTrack().add(time, curRoll, Easing::linear());

            const float curFov = p0.fov + (p1.fov - p0.fov) * u;
            rig.fovTrack().add(time, curFov, Easing::linear());

            const float curAp = p0.aperture + (p1.aperture - p0.aperture) * u;
            rig.apertureTrack().add(time, curAp, Easing::linear());

            const float curFocus = CameraFocusResolver::resolveFocus(
                focusMode, pos, p0.target, p1.target, u,
                p0.focusDistance + (p1.focusDistance - p0.focusDistance) * u
            );
            rig.focusDistanceTrack().add(time, curFocus, Easing::linear());
        }
    }

    // ------------------------------------------------- 15 Authoritative Recipes

    // 1. cameraFocusPush
    void cameraFocusPush(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                         float pushDistance, float startAperture, float endAperture,
                         float fps, const Easing& easing, const MotionScene* scene) {
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
        keyVector3(rig.positionTrack(), t0, t1, start.position, endPos, easing);
        rig.targetTrack().add(t0, targetPos, easing);
        rig.targetTrack().add(t1, targetPos, Easing::linear());

        rig.apertureTrack().add(t0, startAperture, easing);
        rig.apertureTrack().add(t1, endAperture, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);
            Vector3 pos;
            pos.lerpVectors(start.position, endPos, u);
            const float dist = pos.distanceTo(targetPos);
            rig.focusDistanceTrack().add(time, dist, Easing::linear());
        }
    }

    // 2. cameraRackFocusDuo
    void cameraRackFocusDuo(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                            int inFrame, int duration, float aperture, float fps,
                            const Easing& easing, const MotionScene* scene) {
        authorizePresetForCamera(rig, "cameraRackFocusDuo", scene);
        if (duration <= 0) throw std::invalid_argument("cameraRackFocusDuo: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const CameraPose current = rig.sample(t0);
        const Vector3 targetA = subjectA.resolve(scene, t0);
        const Vector3 targetB = subjectB.resolve(scene, t1);

        const float distA = current.position.distanceTo(targetA);
        const float distB = current.position.distanceTo(targetB);

        rig.focusDistanceTrack().add(t0, distA, easing);
        rig.focusDistanceTrack().add(t1, distB, Easing::linear());

        rig.apertureTrack().add(t0, aperture, easing);
        rig.apertureTrack().add(t1, aperture, Easing::linear());
    }

    // 3. cameraDollyFocusHandoff
    void cameraDollyFocusHandoff(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                 int inFrame, int duration, const Vector3& dollyDelta,
                                 float aperture, float fps, const Easing& easing,
                                 const MotionScene* scene) {
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

        keyVector3(rig.positionTrack(), t0, t1, start.position, endPos, easing);

        rig.setTarget(targetA);
        rig.targetTrack().add(t0, targetA, easing);
        rig.targetTrack().add(t1, targetB, Easing::linear());

        rig.apertureTrack().add(t0, aperture, easing);
        rig.apertureTrack().add(t1, aperture, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            pos.lerpVectors(start.position, endPos, u);
            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, u);

            rig.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
    }

    // 4. cameraLateralSubjectToSubject
    void cameraLateralSubjectToSubject(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                       int inFrame, int duration, float fps,
                                       const Easing& easing, const MotionScene* scene) {
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

        keyVector3(rig.positionTrack(), t0, t1, startPos, endPos, easing);

        rig.setTarget(targetA);
        rig.targetTrack().add(t0, targetA, easing);
        rig.targetTrack().add(t1, targetB, Easing::linear());

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = t0 + (t1 - t0) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            pos.lerpVectors(startPos, endPos, u);
            Vector3 curTarget;
            curTarget.lerpVectors(targetA, targetB, u);

            rig.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
    }

    // 5. cameraOrbit90Reveal
    void cameraOrbit90Reveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                             const Vector3& pivot, int inFrame, int duration, float fps,
                             const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, temp.position, Easing::linear());
            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, temp.position.distanceTo(curTarget), Easing::linear());
        }
    }

    // 6. cameraCornerTurnReveal
    void cameraCornerTurnReveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                const Vector3& cornerPoint, int inFrame, int duration,
                                float cornerRadius, float fps, const Easing& easing,
                                const MotionScene* scene) {
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

            rig.positionTrack().add(time, pos, Easing::linear());
            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
    }

    // 7. cameraDetachTurnReacquire
    void cameraDetachTurnReacquire(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                   int inFrame, int duration, float fps,
                                   const Easing& easing, const MotionScene* scene) {
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

            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, start.position.distanceTo(curTarget), Easing::linear());
        }
    }

    // 8. cameraForegroundPassReveal
    void cameraForegroundPassReveal(CameraRig& rig, const ShotAnchor& foregroundA, const ShotAnchor& subjectB,
                                    int inFrame, int duration, const Vector3& lateralPassOffset,
                                    float aperture, float fps, const Easing& easing,
                                    const MotionScene* scene) {
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

        keyVector3(rig.positionTrack(), t0, t1, startPos, endPos, easing);

        rig.setTarget(targetA);
        rig.targetTrack().add(t0, targetA, easing);
        rig.targetTrack().add(t1, targetB, Easing::linear());

        rig.apertureTrack().add(t0, aperture, easing);
        rig.apertureTrack().add(t1, aperture, Easing::linear());

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
            rig.focusDistanceTrack().add(time, foc, Easing::linear());
        }
    }

    // 9. cameraDepthPeel
    void cameraDepthPeel(CameraRig& rig, const std::vector<ShotAnchor>& subjects,
                         int inFrame, int duration, float pushPerSubject,
                         float aperture, float fps, const Easing& easing,
                         const MotionScene* scene) {
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
        rig.apertureTrack().add(t0, aperture, Easing::linear());
        rig.apertureTrack().add(t1, aperture, Easing::linear());

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

            keyVector3(rig.positionTrack(), tStart, tEnd, curPos, nextPos, easing);

            if (i == 0) rig.setTarget(targetCurr);
            rig.targetTrack().add(tStart, targetCurr, easing);
            rig.targetTrack().add(tEnd, targetNext, Easing::linear());

            const float distStart = curPos.distanceTo(targetCurr);
            const float distEnd = nextPos.distanceTo(targetNext);
            rig.focusDistanceTrack().add(tStart, distStart, easing);
            rig.focusDistanceTrack().add(tEnd, distEnd, Easing::linear());

            curPos = nextPos;
        }
    }

    // 10. cameraArcBetweenSubjects
    void cameraArcBetweenSubjects(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                  int inFrame, int duration, float arcBowDistance,
                                  float fps, const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, pos, Easing::linear());
            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, pos.distanceTo(curTarget), Easing::linear());
        }
    }

    // 11. cameraWhipPanHandoff
    void cameraWhipPanHandoff(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                              int inFrame, int duration, float fps, const MotionScene* scene) {
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

            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.orientationTrack().add(time, q, Easing::linear());
            rig.focusDistanceTrack().add(time, start.position.distanceTo(curTarget), Easing::linear());
        }
    }

    // 12. cameraCraneUpReveal
    void cameraCraneUpReveal(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& contextAnchor,
                             int inFrame, int duration, float craneUp, float craneBack,
                             float startFov, float endFov, float fps,
                             const Easing& easing, const MotionScene* scene) {
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

        keyVector3(rig.positionTrack(), t0, t1, start.position, endPos, easing);

        rig.setTarget(targetA);
        rig.targetTrack().add(t0, targetA, easing);
        rig.targetTrack().add(t1, targetCtx, Easing::linear());

        const float startFovActual = (startFov > 0.f) ? startFov : start.fov;
        rig.fovTrack().add(t0, startFovActual, easing);
        rig.fovTrack().add(t1, endFov, Easing::linear());

        const float dist0 = start.position.distanceTo(targetA);
        const float dist1 = endPos.distanceTo(targetCtx);
        rig.focusDistanceTrack().add(t0, dist0, easing);
        rig.focusDistanceTrack().add(t1, dist1, Easing::linear());
    }

    // 13. cameraPushThroughOverlay
    void cameraPushThroughOverlay(CameraRig& rig, const ShotAnchor& cardA, const ShotAnchor& sceneB,
                                  int inFrame, int duration, float passThroughDistance,
                                  float fps, const Easing& easing, const MotionScene* scene) {
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

        keyVector3(rig.positionTrack(), t0, t1, start.position, endPos, easing);

        rig.setTarget(targetCard);
        rig.targetTrack().add(t0, targetCard, easing);
        rig.targetTrack().add(t1, targetScene, Easing::linear());

        const float d0 = start.position.distanceTo(targetCard);
        const float d1 = endPos.distanceTo(targetScene);
        rig.focusDistanceTrack().add(t0, d0, easing);
        rig.focusDistanceTrack().add(t1, d1, Easing::linear());
    }

    // 14. cameraParallaxGallery
    void cameraParallaxGallery(CameraRig& rig, const std::vector<ShotAnchor>& cards,
                               int inFrame, int duration, float cameraZOffset,
                               float aperture, float fps, const MotionScene* scene) {
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

        keyVector3(rig.positionTrack(), t0, t1, startPos, endPos, Easing::easeInOut());

        rig.apertureTrack().add(t0, aperture, Easing::linear());
        rig.apertureTrack().add(t1, aperture, Easing::linear());

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

            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, curCam.distanceTo(curTarget), Easing::linear());
        }
    }

    // 15. cameraFocusOrbitSettle
    void cameraFocusOrbitSettle(CameraRig& rig, const ShotAnchor& subject,
                                int inFrame, int duration,
                                float yawSweepDeg, float pitchSweepDeg,
                                float pushDistance, float fps, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPose.position, Easing::linear());
            rig.targetTrack().add(time, targetPos, Easing::linear());
            rig.focusDistanceTrack().add(time, curPose.position.distanceTo(targetPos), Easing::linear());
        }
    }

    // =========================================================================
    // 15 NEW Ultra-Cinematic Recipes (V2 Suite)
    // =========================================================================

    // 16. cameraVertigoDollyZoom
    void cameraVertigoDollyZoom(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                                float startDistance, float endDistance, float startFov,
                                float fps, const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, currentPos, Easing::linear());
            rig.targetTrack().add(time, targetPos, Easing::linear());
            rig.fovTrack().add(time, currentFovDeg, Easing::linear());
            rig.focusDistanceTrack().add(time, currentDist, Easing::linear());
        }
    }

    // 17. cameraBulletTimeOrbitFreeze
    void cameraBulletTimeOrbitFreeze(CameraRig& rig, const ShotAnchor& subject, int inFrame, int duration,
                                     float radius, float height, float sweepDegrees,
                                     float aperture, float fps, const Easing& easing, const MotionScene* scene) {
        authorizePresetForCamera(rig, "cameraBulletTimeOrbitFreeze", scene);
        if (duration <= 0) throw std::invalid_argument("cameraBulletTimeOrbitFreeze: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const Vector3 targetPos = subject.resolve(scene, t0);
        rig.setTarget(targetPos);

        const unsigned int steps = static_cast<unsigned int>(std::max(duration, 16));
        const float sweepRad = math::degToRad(sweepDegrees);

        rig.apertureTrack().add(t0, aperture, Easing::linear());
        rig.apertureTrack().add(t1, aperture, Easing::linear());

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

            rig.positionTrack().add(time, camPos, Easing::linear());
            rig.targetTrack().add(time, targetPos, Easing::linear());
            rig.focusDistanceTrack().add(time, radius, Easing::linear());
        }
    }

    // 18. cameraFpvDroneDiveRoll
    void cameraFpvDroneDiveRoll(CameraRig& rig, const ShotAnchor& approachAnchor, const ShotAnchor& targetSubject,
                                int inFrame, int duration, float diveElevation, float maxBankRollDeg,
                                float fps, const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, currentPos, Easing::linear());
            rig.targetTrack().add(time, currentTarget, Easing::linear());
            rig.rollTrack().add(time, currentRoll, Easing::linear());
            rig.focusDistanceTrack().add(time, currentPos.distanceTo(currentTarget), Easing::linear());
        }
    }

    // 19. cameraSplitDiopterDualPlane
    void cameraSplitDiopterDualPlane(CameraRig& rig, const ShotAnchor& foregroundLeft, const ShotAnchor& backgroundRight,
                                     int inFrame, int duration, float aperture, float fps,
                                     const Easing& easing, const MotionScene* scene) {
        authorizePresetForCamera(rig, "cameraSplitDiopterDualPlane", scene);
        if (duration <= 0) throw std::invalid_argument("cameraSplitDiopterDualPlane: duration must be positive");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 pA = foregroundLeft.resolve(scene, t0);
        const Vector3 pB = backgroundRight.resolve(scene, t1);

        // Position camera laterally between both planes
        const Vector3 startPos(pA.x + 1.2f, pA.y, pA.z + 5.5f);
        const Vector3 endPos(pA.x + 2.5f, pA.y, pA.z + 5.0f);

        rig.apertureTrack().add(t0, aperture, Easing::linear());
        rig.apertureTrack().add(t1, aperture, Easing::linear());

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

            rig.positionTrack().add(time, currentPos, Easing::linear());
            rig.targetTrack().add(time, midTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, splitFocus, Easing::linear());
        }
    }

    // 20. cameraSpiralHelixAscend
    void cameraSpiralHelixAscend(CameraRig& rig, const ShotAnchor& centerPillar, int inFrame, int duration,
                                 float climbHeight, float radius, float totalTurns,
                                 float fps, const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, camPos, Easing::linear());
            rig.targetTrack().add(time, pillar, Easing::linear());
            rig.focusDistanceTrack().add(time, camPos.distanceTo(pillar), Easing::linear());
        }
    }

    // 21. cameraFlyUnderReveal
    void cameraFlyUnderReveal(CameraRig& rig, const ShotAnchor& foregroundObstacle, const ShotAnchor& heroTarget,
                              int inFrame, int duration, float subFloorDepth, float fps,
                              const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, currentPos, Easing::linear());
            rig.targetTrack().add(time, currentTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, currentPos.distanceTo(currentTarget), Easing::linear());
        }
    }

    // 22. cameraAnamorphicBarrelSweep
    void cameraAnamorphicBarrelSweep(CameraRig& rig, const ShotAnchor& anchorLeft, const ShotAnchor& anchorRight,
                                     int inFrame, int duration, float sweepSpanX, float fps,
                                     const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, curPos.distanceTo(curTarget), Easing::linear());
        }
    }

    // 23. cameraCorkscrewThroughRing
    void cameraCorkscrewThroughRing(CameraRig& rig, const ShotAnchor& ringCenter, const ShotAnchor& interiorCore,
                                    int inFrame, int duration, float plungeDistance, float fps,
                                    const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, core, Easing::linear());
            rig.rollTrack().add(time, currentRoll, Easing::linear());
            rig.focusDistanceTrack().add(time, curPos.distanceTo(core), Easing::linear());
        }
    }

    // 24. cameraPendulumSwingReveal
    void cameraPendulumSwingReveal(CameraRig& rig, const ShotAnchor& startAnchor, const ShotAnchor& endAnchor,
                                   int inFrame, int duration, float pendulumDrop, float fps,
                                   const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, curPos.distanceTo(curTarget), Easing::linear());
        }
    }

    // 25. cameraCrashZoomSnap
    void cameraCrashZoomSnap(CameraRig& rig, const ShotAnchor& heroSubject, int inFrame, int duration,
                             float startDistance, float endDistance, float fps,
                             const MotionScene* scene) {
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
            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, tgt, Easing::linear());
            rig.focusDistanceTrack().add(time, curDist, Easing::linear());
        }
    }

    // 26. cameraInfinityMirrorDive
    void cameraInfinityMirrorDive(CameraRig& rig, const std::vector<ShotAnchor>& portalFrames, int inFrame, int duration,
                                  float totalZTravel, float fps, const Easing& easing,
                                  const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, lookAhead, Easing::linear());
            rig.focusDistanceTrack().add(time, 8.f, Easing::linear());
        }
    }

    // 27. cameraOverTheShoulderDialogue
    void cameraOverTheShoulderDialogue(CameraRig& rig, const ShotAnchor& subjectA, const ShotAnchor& subjectB,
                                       int inFrame, int duration, float shoulderOffset, float fps,
                                       const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.focusDistanceTrack().add(time, curPos.distanceTo(curTarget), Easing::linear());
        }
    }

    // 28. cameraBirdsEyeGodViewDescent
    void cameraBirdsEyeGodViewDescent(CameraRig& rig, const ShotAnchor& groundSubject, int inFrame, int duration,
                                      float godViewHeight, float eyeLevelDistance, float fps,
                                      const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, subject, Easing::linear());
            rig.focusDistanceTrack().add(time, curPos.distanceTo(subject), Easing::linear());
        }
    }

    // 29. cameraSlalomCardWeave
    void cameraSlalomCardWeave(CameraRig& rig, const std::vector<ShotAnchor>& slalomGates, int inFrame, int duration,
                               float slalomAmplitude, float maxBankRollDeg, float fps,
                               const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, curTarget, Easing::linear());
            rig.rollTrack().add(time, currentRoll, Easing::linear());
            rig.focusDistanceTrack().add(time, 8.f, Easing::linear());
        }
    }

    // 30. cameraHeroLowAngleMonolithOrbit
    void cameraHeroLowAngleMonolithOrbit(CameraRig& rig, const ShotAnchor& monolith, int inFrame, int duration,
                                         float radius, float floorLevelY, float orbitSweepDeg,
                                         float upwardPitchDeg, float fps,
                                         const Easing& easing, const MotionScene* scene) {
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

            rig.positionTrack().add(time, curPos, Easing::linear());
            rig.targetTrack().add(time, aimPoint, Easing::linear());
            rig.focusDistanceTrack().add(time, curPos.distanceTo(aimPoint), Easing::linear());
        }
    }

}// namespace chrononmotion::motion
