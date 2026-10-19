// ChrononMotion — engine-side interaction math for tool builders.
//
// A motion compiler needs no window, but the tools built on top of it need a
// viewport: click-to-select, drag-to-move, and a frame cursor that scrubs.
// This header is that contract, without a GUI: picking, gizmo deltas and a
// scrub session, all as pure functions over evaluated scene state plus one
// small cursor object. A timeline UI, a web viewer or a test harness consumes
// it; none of them re-derives projection or hierarchy math to do so.
//
// What it deliberately is not: a manipulator renderer, a picking framebuffer,
// or content-quad hit testing. Picking resolves to the layer whose anchor
// (the fixed point of its transform) projects nearest the cursor —
// quad-accurate picking needs the content rectangle's plane-scale contract,
// which lives with MotionPlane, not here. Gizmo deltas move points in the
// camera plane at the dragged point's depth; the caller applies them to
// `Layer::transform` with the ordinary authoring API.

#ifndef CHRONONMOTION_MOTION_INTERACTION_HPP
#define CHRONONMOTION_MOTION_INTERACTION_HPP

#include "chrononmotion/motion/CameraScreenSpace.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include "chrononmotion/math/Vector2.hpp"

#include <cstdint>

namespace chrononmotion::motion {

    /// Everything a screen-space query needs: the sampled camera pose plus
    /// the viewport intrinsics. Same camera, same pixels as the renderer
    /// contract (`projectToScreen`), so a tool and a preview agree.
    struct ScreenCamera {
        CameraPose pose{};
        ScreenCameraIntrinsics intrinsics{};
    };

    /// The nearest pickable layer under a cursor, or `hit == false`.
    struct PickHit {
        bool hit{false};
        Layer::Id id{Layer::kRoot};
        /// Distance from the cursor to the projected anchor, in pixels.
        float screenDistancePx{0.f};
        /// Depth of the anchor along the view axis, in world units.
        float viewDepth{0.f};
    };

    /// Nearest-anchor picking over the scene's evaluated, visible layers.
    ///
    /// The scene must be evaluated (`evaluate()` first): picking reads world
    /// state, and guessing from authoring data would disagree with the
    /// renderer by construction. Layers behind the camera never pick. Ties
    /// break deterministically: nearer cursor distance wins, then nearer
    /// depth, then the smaller layer id.
    [[nodiscard]] PickHit pickLayer(const MotionScene& scene, const ScreenCamera& camera,
                                    const Vector2& pixel, float maxDistancePx = 12.f);

    /// World-space delta that moves a point at `pointViewDepth` by
    /// `screenDeltaPx` pixels. Exact (no linearization): it inverts the
    /// pinhole projection at that depth, so applying the delta to
    /// `Layer::transform.position` lands the projected anchor exactly on the
    /// cursor. `pointViewDepth` is `viewDepth(anchor, pose)` — non-positive
    /// depths fail closed.
    [[nodiscard]] Vector3 dragWorldDelta(const ScreenCamera& camera, float pointViewDepth,
                                         const Vector2& screenDeltaPx);

    /// Signed rotation about the camera view axis, in radians, from a cursor
    /// arc around the projected pivot. Zero-radius drags fail closed: the
    /// angle is undefined there, not zero.
    [[nodiscard]] float dragViewRotation(const ScreenCamera& camera, const Vector2& pivotPx,
                                         const Vector2& fromPx, const Vector2& toPx);

    /// Uniform scale factor from a radial cursor drag around the projected
    /// pivot: |to - pivot| / |from - pivot|. A zero start radius fails closed.
    [[nodiscard]] float dragScaleFactor(const Vector2& pivotPx, const Vector2& fromPx, const Vector2& toPx);

    /// A deterministic frame cursor over a scene: seek, step, loop and play.
    /// Every move evaluates the scene, so state always matches the cursor and
    /// two sessions on the same scene and frame agree exactly.
    class ScrubSession {
    public:
        explicit ScrubSession(MotionScene& scene);

        /// Last frame with content (`contentEndFrame`, floored at 0): the
        /// cursor lives in [0, endFrame].
        [[nodiscard]] int endFrame() const;
        [[nodiscard]] int frame() const { return frame_; }
        [[nodiscard]] float seconds() const;
        [[nodiscard]] bool playing() const { return playing_; }
        [[nodiscard]] bool hasLoop() const { return loop_; }
        [[nodiscard]] int loopIn() const { return loopIn_; }
        [[nodiscard]] int loopOut() const { return loopOut_; }

        void seek(int frame);
        /// Step by `delta` frames (negative steps backwards). Past either end
        /// the cursor wraps inside the loop when one is set, else clamps.
        void step(int delta);
        void setLoop(int inFrame, int outFrame);
        void clearLoop();
        void play();
        void pause();
        /// One frame forward while playing; a no-op while paused. Wraps or
        /// stops at the end exactly like `step(1)`.
        void advance();

    private:
        MotionScene* scene_{nullptr};
        int frame_{0};
        bool playing_{false};
        bool loop_{false};
        int loopIn_{0};
        int loopOut_{0};
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_INTERACTION_HPP
