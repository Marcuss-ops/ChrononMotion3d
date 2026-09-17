// ChrononMotion — presets.
//
// The preset list is deliberately built *on top of* the primitives in
// Transform/Track/Easing/MotionPath/CameraRig — there is no preset engine. Each
// function only writes keyframes into a layer's (or a rig's) channels, so a
// preset is a starting point the caller can keep editing rather than a sealed
// animation.

#ifndef THREEPP_MOTION_PRESETS_HPP
#define THREEPP_MOTION_PRESETS_HPP

#include "threepp/motion/CameraRig.hpp"
#include "threepp/motion/Easing.hpp"
#include "threepp/motion/Layer.hpp"
#include "threepp/motion/MotionPath.hpp"

#include "threepp/math/Vector2.hpp"
#include "threepp/math/Vector3.hpp"

#include <vector>

namespace threepp::motion {

    /// Frames -> seconds. Presets are authored in frames (the timeline authority)
    /// and keyed in seconds; this is the one conversion they all use.
    [[nodiscard]] float frameToTime(int frame, float fps);

    namespace presets {

        enum class Direction {
            Left,
            Right,
            Up,
            Down
        };

        enum class Axis {
            X,
            Y,
            Z
        };

        /// Unit offset for a slide direction.
        [[nodiscard]] Vector3 directionOffset(Direction direction);

        /// Unit vector for a flip axis.
        [[nodiscard]] Vector3 axisVector(Axis axis);

        // ---------------------------------------------------------------- in/out

        /// Slide from off-frame to the layer's resting position.
        void slideIn(Layer& layer, Direction direction, int inFrame, int duration, float distance, float fps,
                     const Easing& easing = Easing::easeOut());

        /// Slide from the resting position off-frame.
        void slideOut(Layer& layer, Direction direction, int inFrame, int duration, float distance, float fps,
                      const Easing& easing = Easing::easeIn());

        /// Enter from a depth in front of (or behind) the resting Z.
        void pushInZ(Layer& layer, int inFrame, int duration, float fromDepth, float fps,
                     const Easing& easing = Easing::easeOut());

        /// Leave toward a depth.
        void pullOutZ(Layer& layer, int inFrame, int duration, float toDepth, float fps,
                      const Easing& easing = Easing::easeIn());

        /// Uniform scale from `from` to `to`, with a pop.
        void scalePop(Layer& layer, int inFrame, int duration, float from, float to, float fps,
                      const Easing& easing = Easing::overshoot());

        /// Uniform scale from `from` to `to` with a strong overshoot.
        void scaleOvershoot(Layer& layer, int inFrame, int duration, float from, float to, float fps);

        /// Fade from transparent to the layer's resting opacity.
        void fadeIn(Layer& layer, int inFrame, int duration, float fps,
                    const Easing& easing = Easing::easeOut());

        /// Fade from the resting opacity to transparent.
        void fadeOut(Layer& layer, int inFrame, int duration, float fps,
                     const Easing& easing = Easing::easeIn());

        /// The classic combination: fade while sliding into place.
        void fadeSlide(Layer& layer, Direction direction, int inFrame, int duration, float distance, float fps);

        /// Fade while scaling up into place.
        void fadeScale(Layer& layer, int inFrame, int duration, float from, float fps);

        // ----------------------------------------------------------------- pivot

        /// Move the pivot from the layer's resting anchor to `to`, keyed over time.
        ///
        /// The anchor is the single fixed point of *both* scale and rotation, so an
        /// animated anchor is what makes a spin turn about a moving point instead of
        /// about a pivot fixed once at author time.
        void anchorTo(Layer& layer, const Vector3& to, int inFrame, int duration, float fps,
                      const Easing& easing = Easing::easeInOut());

        // -------------------------------------------------------- lifetime in/out

        /// The frame windows a layer's own lifetime implies for a `duration`-frame
        /// entrance and an exit.
        ///
        /// Both windows are *derived from the lifetime*, never invented. A window
        /// the lifetime cannot hold is reported as absent rather than moved to a
        /// frame the layer is not alive for: an open-ended lifetime has no exit, and
        /// a closed lifetime shorter than the window has no entrance.
        struct LifetimeWindows {
            /// Frames of the entrance, when it fits.
            FrameRange in{};
            /// Frames of the exit, when the lifetime is long enough for both.
            FrameRange out{};
            bool hasIn{false};
            bool hasOut{false};
        };

        /// The entrance/exit windows the layer's lifetime implies. A closed
        /// lifetime must be at least `duration` frames long to hold the entrance,
        /// and at least twice that to hold the entrance *and* the exit.
        [[nodiscard]] LifetimeWindows lifetimeWindows(const Layer& layer, int duration);

        /// Key a fade + slide entrance and exit *inside* the layer's declared
        /// lifetime: the entrance starts on `inFrame` and the exit finishes on
        /// `outFrame`, both read from the lifetime itself.
        ///
        /// This is what ties an in/out animation to a layer's lifetime — the
        /// animation cannot drift from the range the layer is visible for, because
        /// nothing here takes a frame the lifetime did not supply.
        void lifetimeSlide(Layer& layer, Direction inDirection, Direction outDirection,
                           int duration, float distance, float fps,
                           const Easing& inEasing = Easing::easeOut(),
                           const Easing& outEasing = Easing::easeIn());

        // --------------------------------------------------------------- rotation

        /// Turn the layer `degrees` about an axis, as a quaternion (shortest path).
        void flip(Layer& layer, Axis axis, int inFrame, int duration, float fps, float degrees = 180.f);

        /// Turn a card into view: from -90° about Y to identity.
        void cardTurn(Layer& layer, int inFrame, int duration, float fps);

        /// Tilt about X, in degrees, added to the resting rotation.
        void tilt3D(Layer& layer, int inFrame, int duration, float degrees, float fps);

        /// Spin about Z by `turns` full rotations, as an Euler track.
        void spinZ(Layer& layer, int inFrame, int duration, float turns, float fps);

        /// Spin about a tilted axis by `turns` full rotations, as a quaternion track
        /// (so the spin has no Euler gimbal or ±180° flip).
        void spinXYZ(Layer& layer, int inFrame, int duration, float turns, float fps);

        // ------------------------------------------------------------------- 2.5D

        /// Enter from depth: the parallax against the background is then automatic.
        void parallaxEntrance(Layer& layer, int inFrame, int duration, float fromDepth, float fps);

        /// Sine hover around the resting Y.
        void floatHover(Layer& layer, int inFrame, int duration, float amplitude, float fps);

        /// Straight, constant-speed drift by an offset.
        void drift(Layer& layer, int inFrame, int duration, const Vector3& offset, float fps);

        /// Curved travel from the resting position to `to`, bulging through `apex`.
        void arcMotion(Layer& layer, const Vector3& to, const Vector3& apex, int inFrame, int duration, float fps);

        /// Key an arbitrary motion path, optionally orienting local +Z along it.
        void pathMotion(Layer& layer, const MotionPath& path, int inFrame, int duration, float fps,
                        bool orient = false, unsigned int steps = 16);

        // -------------------------------------------------------------- hierarchy

        /// The same entrance, offset per layer: the classic staggered reveal.
        void staggeredEntrance(const std::vector<Layer*>& layers, Direction direction,
                               int inFrame, int stagger, int duration, float distance, float fps);

        /// Reveal a group through its controller: the parent scales and fades while
        /// the children fade in under it.
        void parentGroupReveal(Layer& parent, const std::vector<Layer*>& children,
                               int inFrame, int duration, float fps);

        // ----------------------------------------------------------------- camera

        /// Dolly forward (toward the look-at target, or along the view axis).
        void cameraPush(CameraRig& rig, int inFrame, int duration, float distance, float fps);

        /// Dolly backward.
        void cameraPull(CameraRig& rig, int inFrame, int duration, float distance, float fps);

        /// Translate the camera in its own plane.
        void cameraPan(CameraRig& rig, int inFrame, int duration, const Vector2& delta, float fps);

        /// Orbit the camera around its look-at target.
        void cameraOrbit(CameraRig& rig, int inFrame, int duration, float yaw, float pitch, float fps);

        /// A light, deterministic shake (no RNG: the same frames always shake the same way).
        void cameraShake(CameraRig& rig, int inFrame, int duration, float amplitude, float fps);

        /// Animated field of view.
        void cameraFov(CameraRig& rig, int inFrame, int duration, float fromFov, float toFov, float fps);

    }// namespace presets

}// namespace threepp::motion

#endif//THREEPP_MOTION_PRESETS_HPP
