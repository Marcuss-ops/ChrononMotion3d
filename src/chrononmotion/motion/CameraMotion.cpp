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
        pose.fStop = fStop;

        CameraRig::lookAtPose(pose, targetPos);
        pose.focusDistance = CameraFocusResolver::resolveFocus(
            focusMode, pose, targetPos, focusDistance);
        if (roll != 0.f) {
            CameraRig::roll(pose, roll);
        }

        return pose;
    }

    // --------------------------------------------------- CameraFocusResolver

    namespace {
    float opticalAxisDistance(const CameraPose& camera, const Vector3& target) {
        Vector3 forward(0.f, 0.f, -1.f);
        forward.applyQuaternion(camera.orientation);
        if (forward.lengthSq() <= 1e-12f) {
            throw std::invalid_argument("CameraFocusResolver: camera orientation has no view axis");
        }
        forward.normalize();
        const Vector3 displacement = target - camera.position;
        return std::max(0.f, displacement.dot(forward));
    }
    } // namespace

    float CameraFocusResolver::resolveFocus(CameraFocusMode mode,
                                           const CameraPose& camera,
                                           const Vector3& targetA,
                                           const Vector3& targetB,
                                           float blendProgress,
                                           float manualDistance) {
        switch (mode) {
            case CameraFocusMode::Manual:
                return manualDistance;
            case CameraFocusMode::TargetDistance:
                return opticalAxisDistance(camera, targetA);
            case CameraFocusMode::BlendTargets: {
                const float distA = opticalAxisDistance(camera, targetA);
                const float distB = opticalAxisDistance(camera, targetB);
                const float clamped = std::clamp(blendProgress, 0.f, 1.f);
                return distA + (distB - distA) * clamped;
            }
            case CameraFocusMode::ForegroundSubject: {
                const float distA = opticalAxisDistance(camera, targetA);
                const float distB = opticalAxisDistance(camera, targetB);
                return std::min(distA, distB);
            }
        }
        return opticalAxisDistance(camera, targetA);
    }

    float CameraFocusResolver::resolveFocus(CameraFocusMode mode,
                                           const CameraPose& camera,
                                           const Vector3& target,
                                           float manualDistance) {
        if (mode == CameraFocusMode::Manual) {
            return manualDistance;
        }
        return opticalAxisDistance(camera, target);
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

            const float curFStop = p0.fStop + (p1.fStop - p0.fStop) * u;
            rig.fStopTrack().add(time, curFStop, Easing::linear());

            const float curFocus = CameraFocusResolver::resolveFocus(
                focusMode, samplePose, p0.target, p1.target, u,
                p0.focusDistance + (p1.focusDistance - p0.focusDistance) * u
            );
            rig.focusDistanceTrack().add(time, curFocus, Easing::linear());
        }
    }
}
