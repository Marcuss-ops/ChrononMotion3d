// ChrononMotion — Multi-Aspect Auto-Reframing.
//
// One authored camera scene, N delivery formats. The reframed camera keeps
// the aim point (the semantic subject) centered in the usable band of the
// delivery viewport, and stands back just enough that the subject's
// world-space radius fits the narrower axis:
//
//   d' = max(d, r / (tan(fov/2) · min(1, aspect) · (1 - safeLeft - safeRight)))
//
// with d the master's viewing distance at that frame. The vertical axis needs
// no compensation (both formats share the vertical FOV), so a subject that
// fit the master's vertical framing keeps its on-screen size in portrait —
// the reframe only pulls back when the narrower horizontal axis demands it.
// Without subjects the reframe falls back to the conservative full-frame law
// (nothing the master frame showed may be cut): k = max(1, masterAspect /
// aspect) times the safe-area factor.
//
// The in-plane pan re-centers the subject on the usable band: in screen
// pixels the band center sits at viewport/2 · (safeLeft - safeRight) from the
// frame center (same shape vertically), and at the subject's view depth one
// world unit spans the same number of pixels vertically and horizontally
// (square pixels), so the world pan is the pixel offset over the local
// pixels-per-world-unit.
//
// Everything is a pure function of the sampled master pose, so the reframe is
// deterministic O(1) per frame and random access stays intact.
//
// No content/render dependencies: math only (the tree's boundary guardrail).

#ifndef CHRONONMOTION_MOTION_ASPECTREFRAME_HPP
#define CHRONONMOTION_MOTION_ASPECTREFRAME_HPP

#include "chrononmotion/motion/CameraMotion.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

    /// A delivery format: viewport in pixels plus optional safe margins
    /// (fractions of the axis, TikTok-style UI zones the composition must
    /// keep clear).
    struct DeliveryFormat {
        Vector2 viewport{1920.f, 1080.f};
        float safeLeft{0.f};
        float safeRight{0.f};
        float safeTop{0.f};
        float safeBottom{0.f};

        [[nodiscard]] float aspect() const {
            return viewport.x / std::max(viewport.y, 1e-6f);
        }

        /// 9:16 vertical with typical TikTok/Reels UI margins.
        [[nodiscard]] static DeliveryFormat vertical1080() {
            DeliveryFormat f;
            f.viewport = Vector2(1080.f, 1920.f);
            f.safeRight = 0.18f;  // like/comment rail
            f.safeBottom = 0.20f; // caption zone
            return f;
        }

        /// 1:1 square with a small caption zone.
        [[nodiscard]] static DeliveryFormat square1080() {
            DeliveryFormat f;
            f.viewport = Vector2(1080.f, 1080.f);
            f.safeBottom = 0.08f;
            return f;
        }

        /// The 16:9 master delivery itself.
        [[nodiscard]] static DeliveryFormat landscape1080() {
            DeliveryFormat f;
            f.viewport = Vector2(1920.f, 1080.f);
            return f;
        }
    };

    /// The screen-space intrinsics of a delivery format at a given FOV.
    [[nodiscard]] inline ScreenCameraIntrinsics intrinsicsFor(float fovDegrees,
                                                              const DeliveryFormat& format) {
        return ScreenCameraIntrinsics{format.viewport, fovDegrees};
    }

    /// The reframe solution for one master pose. Pure math, exposed for tests.
    struct AspectReframeSolution {
        float reframedDistance{0.f};   ///< camera-to-target distance after the reframe.
        Vector2 pixelOffset{0.f, 0.f}; ///< band-center offset from frame center, pixels.
        float pixelsPerWorldUnit{0.f}; ///< at the reframed depth.
    };

    [[nodiscard]] inline AspectReframeSolution solveAspectReframe(
            const CameraPose& pose, const DeliveryFormat& format, float fovDegrees,
            float subjectRadiusWorld = 0.f, float masterAspect = 16.f / 9.f) {
        AspectReframeSolution out;

        const float aspect = format.aspect();
        const float d = std::max(pose.position.distanceTo(pose.target), 1e-4f);
        const float halfFovRad = fovDegrees * 0.5f * 3.14159265358979323846f / 180.f;
        const float tanHalf = std::tan(halfFovRad);

        const float safeLR = std::clamp(format.safeLeft + format.safeRight, 0.f, 0.9f);

        float reframed = d;
        if (subjectRadiusWorld > 0.f) {
            // Semantic fit: the subject radius must fit the narrower axis.
            const float required =
                    subjectRadiusWorld / (tanHalf * std::min(aspect, 1.f) * std::max(1.f - safeLR, 0.1f));
            reframed = std::max(d, required);
        } else {
            // Conservative full-frame fit: nothing the master frame showed may
            // be cut by the narrower axis.
            const float k = std::max(1.f, masterAspect / aspect) / std::max(1.f - safeLR, 0.1f);
            reframed = d * k;
        }
        out.reframedDistance = reframed;

        // Pixels per world unit at the reframed depth (square pixels).
        out.pixelsPerWorldUnit = format.viewport.y / (2.f * reframed * tanHalf);

        // Usable-band center, in pixels from the frame center.
        out.pixelOffset = Vector2(0.5f * format.viewport.x * (format.safeLeft - format.safeRight),
                                  0.5f * format.viewport.y * (format.safeTop - format.safeBottom));
        return out;
    }

    /// Sample the master rig on its keyed range and write the reframed camera
    /// for `format` into `out`. The master rig is left untouched.
    ///
    /// `subjects` are the semantic anchors that must stay framed (their
    /// per-frame centroid and radius drive the back-off); empty means the
    /// conservative full-frame fit. Layer-anchored subjects resolve through
    /// `scene`. Throws when a master pose has no target: the reframe re-aims
    /// around the aim point, so a target-less master is not a defined input.
    inline void autoReframeRig(const CameraRig& master, const MotionScene* scene,
                               const DeliveryFormat& format, CameraRig& out,
                               const std::vector<ShotAnchor>& subjects = {},
                               float fps = 30.f) {
        const float masterEnd = master.endTime();
        if (masterEnd <= 0.f) {
            throw std::invalid_argument("autoReframeRig: master rig has no keyed motion");
        }

        out.clearTarget();
        out.setAspect(format.aspect());
        out.setFov(master.fov());

        for (float time = 0.f; time <= masterEnd + 1e-5f; time += 1.f / fps) {
            const CameraPose pose = master.sample(time);
            if (!pose.hasTarget) {
                throw std::invalid_argument(
                        "autoReframeRig: master pose has no target at some time (not a defined input)");
            }

            // Per-frame subject centroid and world radius.
            float subjectRadius = 0.f;
            if (!subjects.empty()) {
                Vector3 centroid;
                for (const ShotAnchor& anchor : subjects) {
                    centroid.add(anchor.resolve(scene, time));
                }
                centroid.multiplyScalar(1.f / static_cast<float>(subjects.size()));
                for (const ShotAnchor& anchor : subjects) {
                    subjectRadius = std::max(subjectRadius, anchor.resolve(scene, time).distanceTo(centroid));
                }
                // A degenerate radius would disable the back-off entirely.
                subjectRadius = std::max(subjectRadius, 0.5f);
            }

            const AspectReframeSolution sol = solveAspectReframe(pose, format, pose.fov, subjectRadius);

            // View basis of the master pose (normalized to survive unnormalized
            // input). The camera looks down its local -Z (three.js convention,
            // the same one lookAtPose bakes into the orientation), so the view
            // direction — camera toward subject — is local -Z in world space.
            Quaternion q = pose.orientation;
            if (q.lengthSq() > 0.f) q.normalize();
            Vector3 forward(0.f, 0.f, -1.f);
            forward.applyQuaternion(q);

            // Stand back along the view axis, aim point held: the subject
            // stays framed while the narrower axis stops clipping it.
            const float d = pose.position.distanceTo(pose.target);
            Vector3 reframedPos = pose.target - forward * sol.reframedDistance;

            // In-plane recentering on the usable band: after the back-off the
            // aim point sits at the frame center, so it translates by exactly
            // the band-center offset (aim-preserving: position and target move
            // by the SAME delta, the view basis is unchanged, and the frame
            // shifts over the subject instead of re-aiming back onto it).
            CameraPose reframed;
            reframed.position = reframedPos;
            reframed.orientation = q;
            reframed.hasTarget = true;
            reframed.target = pose.target;
            translatePoseForScreenDelta(reframed, sol.pixelOffset, sol.reframedDistance,
intrinsicsFor(pose.fov, format));

            out.positionTrack().add(time, reframed.position, Easing::linear());
            out.targetTrack().add(time, reframed.target, Easing::linear());
            if (pose.roll != 0.f) {
                out.rollTrack().add(time, pose.roll, Easing::linear());
            }

            // The optical focus distance scales with the new viewing distance.
            if (pose.focusDistance > 0.f) {
                out.focusDistanceTrack().add(time, pose.focusDistance * (sol.reframedDistance / d),
                                             Easing::linear());
            }
            if (pose.legacyDefocusStrength > 0.f) {
                out.legacyDefocusStrengthTrack().add(time, pose.legacyDefocusStrength, Easing::linear());
            }
            if (pose.fStop > 0.f) {
                out.fStopTrack().add(time, pose.fStop, Easing::linear());
            }
        }
    }

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_ASPECTREFRAME_HPP
