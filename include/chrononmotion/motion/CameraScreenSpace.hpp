// ChrononMotion — Camera screen-space math.
//
// Pure functions over the sampled `CameraPose`: project a world point to
// screen pixels, convert a screen-space delta back into an exact camera-plane
// translation, and solve the optical distance a framing needs for a given
// aspect ratio and safe area.
//
// Design notes:
//   - Everything is a pure function of the pose and the intrinsics, so it is
//     deterministic O(1) per call and safe for random access, exactly like
//     `CameraRig::sample`.
//   - `translatePoseForScreenDelta` moves the position AND the look-at target
//     by the same world-space delta. The orientation therefore does not change
//     at all, and the projected displacement is exact for any delta magnitude
//     (no linearization): one step, no convergence loop.
//
// No content/render dependencies: math only (the tree's boundary guardrail).

#ifndef CHRONONMOTION_MOTION_CAMERASCREENSPACE_HPP
#define CHRONONMOTION_MOTION_CAMERASCREENSPACE_HPP

#include "chrononmotion/motion/CameraRig.hpp"

#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion {

    /// The camera intrinsics a screen-space query needs: viewport size in pixels
    /// and the vertical field of view in degrees.
    struct ScreenCameraIntrinsics {
        Vector2 viewport{1920.f, 1080.f};
        float fovDegrees{60.f};
    };

    /// Depth of `worldPoint` along the pose's view axis (its -Z in view space,
    /// three.js convention: the camera looks down its local -Z). Positive
    /// values are in front of the camera. The orientation quaternion is
    /// normalized first, so an unnormalized input pose is not projected with a
    /// rotated or sheared view basis.
    [[nodiscard]] inline float viewDepth(const Vector3& worldPoint, const CameraPose& pose) {
        Quaternion q = pose.orientation;
        if (q.lengthSq() > 0.f) q.normalize();
        Vector3 forward(0.f, 0.f, -1.f);
        forward.applyQuaternion(q);
        Vector3 rel = worldPoint;
        rel.sub(pose.position);
        return forward.dot(rel);
    }

    /// Project a world point to screen pixels, origin top-left, +Y down.
    /// Returns the raw (possibly off-screen) projection: points behind the
    /// camera mirror through the projection center, which callers may want to
    /// detect via `viewDepth` before trusting the coordinates.
    [[nodiscard]] inline Vector2 projectToScreen(const Vector3& worldPoint, const CameraPose& pose,
                                                 const ScreenCameraIntrinsics& intrinsics) {
        Quaternion q = pose.orientation;
        if (q.lengthSq() > 0.f) q.normalize();

        Vector3 rel = worldPoint;
        rel.sub(pose.position);
        rel.applyQuaternion(q.clone().invert());

        // The camera looks down its local -Z: flip to the +Z-forward view
        // space before the perspective divide.
        const float z = std::max(-rel.z, 1e-6f);
        const float f = 1.f / std::tan(intrinsics.fovDegrees * 0.5f * 3.14159265358979323846f / 180.f);
        const float aspect = intrinsics.viewport.x / std::max(intrinsics.viewport.y, 1e-6f);

        const float ndcX = (f * rel.x / z) / aspect;
        const float ndcY = f * rel.y / z;

        return Vector2((ndcX + 1.f) * 0.5f * intrinsics.viewport.x,
                       (1.f - ndcY) * 0.5f * intrinsics.viewport.y);
    }

    /// Move a pose so that a world point lands on an exact screen position.
    ///
    /// Translating the position and the look-at target by the same delta keeps
    /// the view basis (orientation) unchanged, so the landing displacement is
    /// exact rather than linearized and `pose.target` stays aim-consistent
    /// (subsequent `lookAtPose` resolves to the same orientation).
    inline void translatePoseForScreenDelta(CameraPose& pose, const Vector2& screenDeltaPx,
                                            float pointViewDepthZ,
                                            const ScreenCameraIntrinsics& intrinsics) {
        const float z = std::max(pointViewDepthZ, 1e-4f);
        const float f = 1.f / std::tan(intrinsics.fovDegrees * 0.5f * 3.14159265358979323846f / 180.f);
        const float aspect = intrinsics.viewport.x / std::max(intrinsics.viewport.y, 1e-6f);

        const float ndcDX = (screenDeltaPx.x / std::max(intrinsics.viewport.x, 1e-6f)) * 2.f;
        const float ndcDY = -(screenDeltaPx.y / std::max(intrinsics.viewport.y, 1e-6f)) * 2.f;

        const float worldPerNdcZ = z / f;

        Quaternion q = pose.orientation;
        if (q.lengthSq() > 0.f) q.normalize();
        Vector3 right(1.f, 0.f, 0.f);
        right.applyQuaternion(q);
        Vector3 up(0.f, 1.f, 0.f);
        up.applyQuaternion(q);

        const float ndcX = worldPerNdcZ * aspect * ndcDX;
        const float ndcY = worldPerNdcZ * ndcDY;

        // The rendered point moves opposite to the camera: to land the point
        // `screenDeltaPx` further right/down on screen, the camera (and its
        // aim target, by the SAME delta) translate left/up. Moving the target
        // with the position is what keeps the view basis — and therefore the
        // orientation `lookAtPose` re-derives at sample time — untouched.
        pose.position.addScaledVector(right, -ndcX);
        pose.position.addScaledVector(up, -ndcY);

        if (pose.hasTarget) {
            pose.target.addScaledVector(right, -ndcX);
            pose.target.addScaledVector(up, -ndcY);
        }
    }

    /// Optical distance at which a subject of `subjectRadius` fits the viewport
    /// with a `margin01` fraction of the vertical FOV spared on each side, for
    /// a given aspect ratio. This is the multi-aspect framing law: portrait
    /// formats shrink the horizontal coverage at constant vertical FOV, so the
    /// camera must back off to keep the subject inside the narrower axis.
    [[nodiscard]] inline float framingDistanceForAspect(float subjectRadius, float fovDegrees,
                                                        float aspect, float margin01 = 0.f) {
        const float halfFovRad = fovDegrees * 0.5f * 3.14159265358979323846f / 180.f;
        const float tanHalf = std::tan(halfFovRad);
        const float safeTanHalf = tanHalf * (1.f - std::clamp(margin01, 0.f, 0.9f));
        // At aspect < 1 the horizontal half-angle is the binding constraint.
        const float horizontalTanHalf = (aspect < 1.f) ? safeTanHalf * aspect : safeTanHalf;
        return subjectRadius / std::max(horizontalTanHalf, 1e-6f);
    }

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_CAMERASCREENSPACE_HPP
