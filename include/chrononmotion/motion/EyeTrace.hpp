// ChrononMotion — Eye-Trace Continuity Director.
//
// The eye-trace contract: when the camera hands off from subject A to subject
// B, whatever the viewer was looking at must not jump on screen. The focus
// anchor of the incoming subject lands on exactly the pixels the outgoing
// subject occupied at the cut, and stays pinned there while the incoming shot
// plays — even when that shot's camera keeps moving.
//
// Why this needs no iterative solver: a camera-plane translation that moves
// the position AND the look-at target by the same world delta leaves the view
// basis (orientation) untouched, so the projected displacement of any world
// point is exact for any delta magnitude — one closed-form step. The
// position↔target distance (the optical focus distance) is invariant, so DOF
// authoring survives the correction untouched, as do FOV, lens and roll.
//
// Two composition models:
//
//   - `cameraEyeTraceHandoff` (continuous shot): phase 1 frames subject A and
//     pins its anchor on the authored screen point; phase 2 transitions to B
//     so that B's anchor occupies those same pixels at the last frame.
//
//   - `EyeTraceDirector` (shot sequence with cuts): sequential shots, each
//     wrapping any of the authoritative camera recipes. For every consecutive
//     pair the director records the landing — what the viewer is actually
//     looking at on the corrected timeline at that cut — then rewrites the
//     incoming shot's frames from a pre-sampled baseline so its anchor is
//     pinned to those pixels on every frame. Recording from the corrected
//     timeline is what makes chains of three or more shots consistent.
//
// Shots must not share a boundary frame (>= 1 frame of separation), so a
// landing is never applied to the outgoing shot's last rendered frame.
//
// No content/render dependencies: math only (the tree's boundary guardrail).

#ifndef CHRONONMOTION_MOTION_EYETRACE_HPP
#define CHRONONMOTION_MOTION_EYETRACE_HPP

#include "chrononmotion/motion/CameraMotion.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

    // ------------------------------------------------------------ ScreenLandingKey

    /// A recorded landing: from `inTime` (the cut between the outgoing and the
    /// incoming shot) until the next landing, `focusAnchor` — the INCOMING
    /// shot's focus anchor — is pinned onto `landingPx`, the screen pixels
    /// (origin top-left) the OUTGOING shot's anchor occupied at the cut. The
    /// viewer's gaze point stays fixed while the subject of attention changes.
    struct ScreenLandingKey {
        ShotAnchor focusAnchor{};        ///< the incoming anchor pinned to the landing.
        Vector2 landingPx{960.f, 540.f}; ///< target screen position, pixels.
        float inTime{0.f};               ///< seconds, on the rig's own timeline.
    };

    // ---------------------------------------------------------- EyeTraceDirector

    /// Sequential composition of camera moves with continuous screen-space
    /// attention. Add shots (each wrapping any of the authoritative camera
    /// recipes), then `bakeInto`: every cut is corrected so the incoming
    /// shot's focus anchor lands exactly on the pixels the outgoing anchor
    /// occupied, and stays pinned for the whole incoming shot.
    ///
    /// World-space anchors are always available; layer-anchored shots resolve
    /// through the scene passed to `withScene`.
    class EyeTraceDirector {
    public:
        struct Shot {
            std::function<void(CameraRig&)> recipe; ///< writes this shot's keys into the rig.
            ShotAnchor focusAnchor{};               ///< what the viewer watches during this shot.
            int inFrame{0};
            int duration{30};
        };

        explicit EyeTraceDirector(float fps = 30.f, ScreenCameraIntrinsics intrinsics = {})
            : fps_(fps), intrinsics_(std::move(intrinsics)) {}

        /// Layer-anchored ShotAnchors resolve through this scene.
        EyeTraceDirector& withScene(const MotionScene* scene) {
            scene_ = scene;
            return *this;
        }

        Shot& addShot(Shot shot) {
            if (shot.duration <= 0) {
                throw std::invalid_argument("EyeTraceDirector::addShot: duration must be positive");
            }
            shots_.push_back(std::move(shot));
            return shots_.back();
        }

        /// The landing each baked cut aims at (empty before the first bake).
        /// Exposed for tests and director-level introspection.
        [[nodiscard]] const std::vector<ScreenLandingKey>& landings() const { return landings_; }

        /// Compose every shot onto the rig, then apply the landing corrections
        /// cut by cut. Shots must be sequential with at least one frame of
        /// separation so a landing never rewrites the outgoing shot's last
        /// rendered frame.
        void bakeInto(CameraRig& rig) {
            validateSequential();

            landings_.clear();
            for (Shot& shot : shots_) {
                if (shot.recipe) shot.recipe(rig);
            }
            if (shots_.size() < 2) return;

            // Baseline: the ORIGINAL pose of every frame on the timeline,
            // sampled before any modification. The orientation is part of it:
            // the per-frame landing solve needs the true view basis.
            struct Baseline {
                float time{0.f};
                Vector3 position{0.f, 0.f, 0.f};
                Vector3 target{0.f, 0.f, 0.f};
                Quaternion orientation{0.f, 0.f, 0.f, 1.f};
                bool hasTarget{false};
            };
            const float end = rig.endTime();
            const int lastFrame = static_cast<int>(std::floor(end * fps_ + 0.5f));
            std::vector<Baseline> baseline;
            baseline.reserve(static_cast<std::size_t>(lastFrame + 1));
            for (int f = 0; f <= lastFrame; ++f) {
                const float t = static_cast<float>(f) / fps_;
                const CameraPose pose = rig.sample(t);
                baseline.push_back(Baseline{t, pose.position,
                                            pose.hasTarget ? pose.target : pose.position,
                                            pose.orientation,
                                            pose.hasTarget});
            }

            // Cut by cut: record what the viewer is looking at on the CURRENT
            // (already corrected) timeline, then rewrite the incoming shot's
            // frames from the baseline so its anchor is pinned to those pixels.
            for (std::size_t i = 0; i + 1 < shots_.size(); ++i) {
                const Shot& prev = shots_[i];
                const Shot& next = shots_[i + 1];

                const float cutTime = static_cast<float>(prev.inFrame + prev.duration) / fps_;
                const float fromTime = static_cast<float>(next.inFrame) / fps_;
                const float toTime = static_cast<float>(next.inFrame + next.duration) / fps_;

                const CameraPose cutPose = rig.sample(cutTime);
                const Vector3 prevAnchorWorld = prev.focusAnchor.resolve(scene_, cutTime);
                const Vector2 landingPx = projectToScreen(prevAnchorWorld, cutPose, intrinsics_);
                landings_.push_back(ScreenLandingKey{next.focusAnchor, landingPx, cutTime});

                removeKeysInRange(rig.positionTrack(), fromTime, toTime);
                removeKeysInRange(rig.targetTrack(), fromTime, toTime);

                for (const Baseline& s : baseline) {
                    if (s.time < fromTime - 1e-5f || s.time > toTime + 1e-5f) continue;

                    CameraPose pose;
                    pose.position = s.position;
                    pose.orientation = s.orientation;
                    pose.hasTarget = s.hasTarget;
                    pose.target = s.target;

                    const Vector3 anchorWorld = next.focusAnchor.resolve(scene_, s.time);
                    const float depth = viewDepth(anchorWorld, pose);
                    if (depth > 1e-3f) {
                        const Vector2 current = projectToScreen(anchorWorld, pose, intrinsics_);
                        const Vector2 delta = landingPx - current;
                        constexpr float kEpsilonPx = 1e-3f;
                        if (std::fabs(delta.x) >= kEpsilonPx || std::fabs(delta.y) >= kEpsilonPx) {
                            translatePoseForScreenDelta(pose, delta, depth, intrinsics_);
                        }
                    }

                    rig.positionTrack().add(s.time, pose.position, Easing::linear());
                    if (s.hasTarget) {
                        rig.targetTrack().add(s.time, pose.target, Easing::linear());
                    }
                }
            }
        }

        /// The exact single-step solve at one instant (test/introspection API):
        /// the pose the rig would need at `seconds` so the active anchor
        /// projects onto its landing.
        [[nodiscard]] CameraPose correctedPoseAt(const CameraRig& rig, float seconds) const {
            CameraPose pose = rig.sample(seconds);
            const ScreenLandingKey* active = activeLandingAt(seconds);
            if (!active) return pose;

            const Vector3 anchorWorld = active->focusAnchor.resolve(scene_, seconds);
            const float depth = viewDepth(anchorWorld, pose);
            if (depth <= 1e-3f) return pose;

            const Vector2 current = projectToScreen(anchorWorld, pose, intrinsics_);
            translatePoseForScreenDelta(pose, active->landingPx - current, depth, intrinsics_);
            return pose;
        }

    private:
        [[nodiscard]] const ScreenLandingKey* activeLandingAt(float seconds) const {
            const ScreenLandingKey* active = nullptr;
            for (const ScreenLandingKey& key : landings_) {
                if (seconds >= key.inTime + 0.5f / fps_) active = &key;
            }
            return active;
        }

        void validateSequential() const {
            float cursor = -1.f;
            for (const Shot& shot : shots_) {
                const float start = static_cast<float>(shot.inFrame) / fps_;
                if (cursor >= 0.f && start < cursor + 0.5f / fps_) {
                    throw std::invalid_argument(
                            "EyeTraceDirector::bakeInto: shots must be sequential with >= 1 frame of separation");
                }
                cursor = static_cast<float>(shot.inFrame + shot.duration) / fps_;
            }
        }

        template<typename T>
        static void removeKeysInRange(Track<T>& track, float fromTime, float toTime) {
            std::vector<float> times;
            for (const Keyframe<T>& key : track.keys()) {
                if (key.time >= fromTime - 1e-5f && key.time <= toTime + 1e-5f) {
                    times.push_back(key.time);
                }
            }
            for (const float time : times) {
                track.remove(time);
            }
        }

        float fps_{30.f};
        ScreenCameraIntrinsics intrinsics_{};
        const MotionScene* scene_{nullptr};
        std::vector<Shot> shots_;
        std::vector<ScreenLandingKey> landings_;
    };

    // ------------------------------------------------------ cameraEyeTraceHandoff

    /// The canonical two-subject eye-trace handoff (continuous shot):
    ///
    ///   - frames [inFrame, inFrame + duration/2): static aim on subject A,
    ///     corrected so A's anchor projects exactly onto `screenAnchor`.
    ///   - frames [inFrame + duration/2, inFrame + duration]: eased transition
    ///     to subject B whose final frame puts B's anchor on those same pixels.
    ///
    /// The end pose is built in closed form: stand back at A's viewing
    /// distance (so the subject scale does not jump at the handoff), aim at B,
    /// then apply the exact in-plane translation that lands B on the anchor.
    inline void cameraEyeTraceHandoff(CameraRig& rig,
                                      const ShotAnchor& subjectA,
                                      const ShotAnchor& subjectB,
                                      int inFrame, int duration,
                                      float fps = 30.f,
                                      const Easing& easing = Easing::easeInOut(),
                                      const MotionScene* scene = nullptr,
                                      const Vector2& screenAnchor = Vector2(960.f, 540.f),
                                      const Vector2& viewport = Vector2(1920.f, 1080.f)) {
        if (duration < 2) {
            throw std::invalid_argument("cameraEyeTraceHandoff: duration must be at least 2 frames");
        }

        const float t0 = static_cast<float>(inFrame) / fps;
        const int half = duration / 2;
        const float tMid = static_cast<float>(inFrame + half) / fps;
        const float t1 = static_cast<float>(inFrame + duration) / fps;

        // --- Phase 1: frame A and pin its anchor on the authored screen point.
        CameraPose poseA = rig.sample(t0);
        {
            const Vector3 aWorld = subjectA.resolve(scene, t0);
            poseA.hasTarget = true;
            poseA.target = aWorld;
            CameraRig::lookAtPose(poseA, aWorld);

            const ScreenCameraIntrinsics intrinsics{viewport, poseA.fov};
            const float depth = viewDepth(aWorld, poseA);
            if (depth > 1e-3f) {
                const Vector2 current = projectToScreen(aWorld, poseA, intrinsics);
                translatePoseForScreenDelta(poseA, screenAnchor - current, depth, intrinsics);
            }

            rig.setTarget(poseA.target);
            rig.positionTrack().add(t0, poseA.position, Easing::linear());
            rig.targetTrack().add(t0, poseA.target, Easing::linear());
        }

        // --- Phase 2: the closed-form end pose on B. The landing is
        // `screenAnchor` by construction: phase 1 pinned A there and the pose
        // holds until tMid.
        const ScreenCameraIntrinsics intrinsics{viewport, poseA.fov};
        const Vector3 bWorld = subjectB.resolve(scene, t1);

        CameraPose poseB;
        poseB.hasTarget = true;
        poseB.target = bWorld;

        // Stand back at A's viewing distance so subject scale is continuous.
        const float standBack = std::max(poseA.position.distanceTo(poseA.target), 1.f);
        Vector3 dir = poseA.position - bWorld;
        if (dir.lengthSq() < 1e-9f) {
            dir.set(0.f, 0.f, 1.f);
        } else {
            dir.normalize();
        }
        poseB.position = bWorld + dir * standBack;
        CameraRig::lookAtPose(poseB, bWorld);

        // Exact in-plane landing of B onto the anchor at B's own depth.
        {
            const float depth = std::max(viewDepth(bWorld, poseB), 1e-3f);
            const Vector2 current = projectToScreen(bWorld, poseB, intrinsics);
            translatePoseForScreenDelta(poseB, screenAnchor - current, depth, intrinsics);
        }

        // Keyed eased transition; the final key carries the exact landing.
        const unsigned int steps = static_cast<unsigned int>(std::max(half, 4));
        for (unsigned int i = 0; i <= steps; ++i) {
            const float alpha = static_cast<float>(i) / static_cast<float>(steps);
            const float time = tMid + (t1 - tMid) * alpha;
            const float u = easing.ease(alpha);

            Vector3 pos;
            pos.lerpVectors(poseA.position, poseB.position, u);
            Vector3 tgt;
            tgt.lerpVectors(poseA.target, poseB.target, u);

            rig.positionTrack().add(time, pos, Easing::linear());
            rig.targetTrack().add(time, tgt, Easing::linear());
        }
        rig.positionTrack().add(t1, poseB.position, Easing::linear());
        rig.targetTrack().add(t1, poseB.target, Easing::linear());
    }

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_EYETRACE_HPP
