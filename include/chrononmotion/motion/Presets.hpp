// ChrononMotion — presets.
//
// The preset list is deliberately built *on top of* the primitives in
// Transform/Track/Easing/MotionPath/CameraRig — there is no preset engine. Each
// function only writes keyframes into a layer's (or a rig's) channels, so a
// preset is a starting point the caller can keep editing rather than a sealed
// animation.

#ifndef CHRONONMOTION_MOTION_PRESETS_HPP
#define CHRONONMOTION_MOTION_PRESETS_HPP

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CameraMotion.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/MotionPath.hpp"

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <cstdint>
#include <vector>

namespace chrononmotion::motion {

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

        /// Named aliases for the stable authoring vocabulary.
        void arcEntrance(Layer& layer, const Vector3& to, const Vector3& apex,
                         int inFrame, int duration, float fps);
        void flipX(Layer& layer, int inFrame, int duration, float fps, float degrees = 180.f);
        void flipY(Layer& layer, int inFrame, int duration, float fps, float degrees = 180.f);

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

        /// Spin *into place* about an axis: the layer starts `turns` full rotations
        /// away and unwinds onto its resting orientation.
        ///
        /// Keyed as a quaternion, like `spinXYZ`, so a multi-turn entrance walks
        /// the short arc segment by segment rather than taking the long way through
        /// ±180°. The unwind is eased (fast, then settling); the keys are evenly
        /// spaced in time and the curve is what shapes the rate.
        void spinIn(Layer& layer, Axis axis, int inFrame, int duration, float turns, float fps);

        // ------------------------------------------------------------- settles

        /// Arrive from an offset along an axis and **bounce** onto the resting pose.
        ///
        /// The layer starts `distance` away and rebounds into place. The rebounds
        /// are the segment's easing, not a physics step, so the entrance is exact on
        /// every frame and lands *on* the resting pose rather than near it.
        void bounceIn(Layer& layer, Axis axis, int inFrame, int duration, float distance, float fps);

        /// Arrive from an offset along an axis and **spring** onto the resting pose.
        ///
        /// The smooth counterpart of `bounceIn`: one soft overshoot and settle
        /// instead of discrete rebounds. Both ease onto the layer's own pose, so
        /// neither can leave the layer off its resting place at the end.
        void springSettle(Layer& layer, Axis axis, int inFrame, int duration, float amplitude, float fps);

        // ------------------------------------------------------------------- 2.5D

        /// Enter from depth: the parallax against the background is then automatic.
        void parallaxEntrance(Layer& layer, int inFrame, int duration, float fromDepth, float fps);

        /// Sine hover around the resting Y.
        void floatHover(Layer& layer, int inFrame, int duration, float amplitude, float fps);

        /// Four-second breathing cycle, scaling around the layer's resting size.
        void breathe(Layer& layer, int inFrame, int duration, float fps, float amplitude = 0.015f);

        /// 2.5-second vertical bob around the resting position.
        void bob(Layer& layer, int inFrame, int duration, float fps, float amplitude = 0.025f);

        /// One out-sine pulse from 1x to 1.03x and back over the duration.
        void pulse(Layer& layer, int inFrame, int duration, float fps);

        /// A keyed opacity pulse train: `cycles` dips to `floorOpacity` and back to
        /// the layer's resting opacity over the duration.
        ///
        /// The pulse lives in the keys rather than in an effect parameter, so it
        /// composes with whatever else is animating the layer; and because the top
        /// of every cycle is the layer's *own* resting opacity, a pulse never
        /// changes how opaque the layer settles.
        void pulseOpacity(Layer& layer, int inFrame, int duration, int cycles, float fps,
                          float floorOpacity = 0.f);

        /// Slow six-second XY drift with a subtle ±0.5° Z rotation.
        void floatDrift(Layer& layer, int inFrame, int duration, float fps);

        /// Straight, constant-speed drift by an offset.
        void drift(Layer& layer, int inFrame, int duration, const Vector3& offset, float fps);

        /// Curved travel from the resting position to `to`, bulging through `apex`.
        void arcMotion(Layer& layer, const Vector3& to, const Vector3& apex, int inFrame, int duration, float fps);

        /// Key an arbitrary motion path, optionally orienting local +Z along it.
        void pathMotion(Layer& layer, const MotionPath& path, int inFrame, int duration, float fps,
                        bool orient = false, unsigned int steps = 16);

        // ---------------------------------------------------------- documentary
        //
        // The motion-graphics vocabulary a documentary map sequence is built
        // from. Every one keys the transform of a layer the caller supplies (a
        // ring, a reticle, an area, a rule, a line); none of them invents
        // content, which is why they live here and not in the content engine.

        /// An expanding target ping: a ring that grows from `fromScale` to
        /// `toScale` while it fades to nothing, repeated `pulses` times across
        /// the duration.
        ///
        /// The repeat is a channel `Loop` over one ring's window rather than a
        /// list of hand-written keys, so the ring resets instantly at every
        /// boundary and the number of pulses never changes the shape of one
        /// ring — the only thing that decides how a single ping looks is the
        /// ring itself.
        void radarPing(Layer& layer, int inFrame, int duration, int pulses,
                       float fromScale, float toScale, float fps,
                       const Easing& easing = Easing::circOut());

        /// An area fill arriving: the layer fades up while a slight scale settle
        /// closes the loop it was drawn with. The fade is the reveal; the settle
        /// is only what makes it read as an area coming together instead of a
        /// rectangle switching on.
        void areaReveal(Layer& layer, int inFrame, int duration, float fps,
                        const Easing& easing = Easing::overshoot());

        /// A 2.5D extrusion: the layer rises off the plane by `height` on Z
        /// while it fades in, so a flat area gains a shallow prism without a
        /// second geometry. The rise overshoots and settles, so the block lands
        /// on its height rather than sliding onto it.
        void extrudeRise(Layer& layer, int inFrame, int duration, float height, float fps,
                         const Easing& easing = Easing::overshoot());

        /// A rule or leader line that draws itself: the layer is uncovered along
        /// its own width (scale X from nothing to the resting width) while it
        /// fades in, so it grows out of its anchor instead of popping in whole.
        void lineDraw(Layer& layer, int inFrame, int duration, float fps,
                      const Easing& easing = Easing::easeInOut());

        /// A reticle that locks on: each segment starts `outward` away from its
        /// own resting place and collapses onto it with a snap, so the segments
        /// close around the point together.
        ///
        /// `outward` is indexed in lockstep with `segments`; a segment with no
        /// offset, or a null entry, is skipped rather than given a guessed one.
        /// The snap is the segment's easing (`overshoot` by default), so the
        /// reticle settles exactly on the point it locked rather than near it.
        void crosshairLock(const std::vector<Layer*>& segments,
                           const std::vector<Vector3>& outward,
                           int inFrame, int duration, float fps,
                           const Easing& easing = Easing::overshoot(0.72f));

        // -------------------------------------------------------------- hierarchy

        /// The same entrance, offset per layer: the classic staggered reveal.
        void staggeredEntrance(const std::vector<Layer*>& layers, Direction direction,
                               int inFrame, int stagger, int duration, float distance, float fps);

        /// Reveal a group through its controller: the parent scales and fades while
        /// the children fade in under it.
        void parentGroupReveal(Layer& parent, const std::vector<Layer*>& children,
                               int inFrame, int duration, float fps);

        /// A travelling wave through a group: every layer travels `distance` along
        /// one direction, each one a further `1/layers` of a cycle out of phase.
        ///
        /// This is not `staggeredEntrance` with a different easing. There the same
        /// move is offset in *time*, so the group reads as a sequence; here one wave
        /// is sampled across the group, so the layers read as a single ripple passing
        /// through them. `cycles` is how many full waves cross the group over the
        /// duration. Layers are keyed at a fixed step, so the wave is a sampled curve
        /// and is reproducible frame for frame.
        void rippleWave(const std::vector<Layer*>& layers, Direction direction, int inFrame,
                        int duration, float cycles, float distance, float fps);

        /// Secondary motion: the follower adds the source's change since the
        /// composition's start, read `lagSeconds` late, to its own pose.
        ///
        /// This is the "with lag" technique — the borrowed motion arrives late, so
        /// the follower trails what it is attached to instead of being welded to it,
        /// which is what makes secondary motion read as weight. Nothing is baked: it
        /// is a channel `Link` plus a `Delay`, so the follower keeps its own keys and
        /// stays editable. Both layers must be in the same `MotionScene` for the link
        /// to resolve, and the source must be keyed from its own start for the delta
        /// to have a reference.
        void followThrough(Layer& layer, const Layer& source, float lagSeconds);

        // ----------------------------------------------------------------- camera

        /// Dolly forward (toward the look-at target, or along the view axis).
        void cameraPush(CameraRig& rig, int inFrame, int duration, float distance, float fps);

        /// Dolly backward.
        void cameraPull(CameraRig& rig, int inFrame, int duration, float distance, float fps);

        /// Translate the camera in its own plane.
        void cameraPan(CameraRig& rig, int inFrame, int duration, const Vector2& delta, float fps);

        /// Orbit the camera around its look-at target.
        void cameraOrbit(CameraRig& rig, int inFrame, int duration, float yaw, float pitch, float fps);

        /// Named aliases for common template-level motion vocabulary.
        void orbitMild(CameraRig& rig, int inFrame, int duration, float fps);
        void cameraFovBreath(CameraRig& rig, int inFrame, int duration, float fromFov, float toFov, float fps);
        void shakeMild(CameraRig& rig, int inFrame, int duration, float amplitude, float fps);
        void focusPush(CameraRig& rig, int inFrame, int duration, float distance, float fps);

        /// Rack focus: move the focal plane from one distance to another while
        /// the camera itself stays put. Keys only the focus-distance channel, so
        /// it composes with any move that is also on the rig.
        void rackFocus(CameraRig& rig, int inFrame, int duration,
                       float fromDistance, float toDistance, float fps);

        /// A light, deterministic shake (no RNG: the same frames always shake the same way).
        void cameraShake(CameraRig& rig, int inFrame, int duration, float amplitude, float fps);

        /// Deterministic handheld X/Y shake and micro-roll, baked at fixed time steps.
        void handheldShake(CameraRig& rig, float intensity_units, float frequency_hz,
                           std::uint32_t seed, float start_s, float end_s);

        /// Animated field of view.
        void cameraFov(CameraRig& rig, int inFrame, int duration, float fromFov, float toFov, float fps);

        /// The slam: one move that orbits, closes in and compresses the lens at
        /// once, on a curve that accelerates through the middle and lands softly.
        /// The camera arrives *over* the target rather than sliding onto it.
        ///
        /// `dollyDistance` is how far the camera closes along its own view axis
        /// (positive toward the target), `yawDeg`/`pitchDeg` how far it swings
        /// around the target, and the two fov ends compress the lens as it
        /// arrives.
        void orbitalSlamZoom(CameraRig& rig, int inFrame, int duration,
                             float yawDeg, float pitchDeg, float dollyDistance,
                             float fromFov, float toFov, float fps);

        /// A continuous drift: a slow orbit keyed at a fixed step so the camera
        /// never freezes after the target is locked, at `yawRateDegPerSec` and
        /// `pitchRateDegPerSec`. Deterministic, like every other camera move
        /// here: the same frames always drift the same way.
        void cameraDrift(CameraRig& rig, int inFrame, int duration,
                         float yawRateDegPerSec, float pitchRateDegPerSec, float fps);

        // -------------------------------------------------------- environment & background

        /// Compute the frustum coverage size (width, height) in world units for a plane
        /// at `distance` from the camera with the specified `fovDegrees` and `aspect` ratio,
        /// scaled by an optional `overscan` factor.
        [[nodiscard]] Vector2 frustumCoverage(float fovDegrees, float aspect, float distance, float overscan = 1.25f);

        /// Rig a background layer at depth `depth` behind the subject, scaled to cover
        /// the camera frustum with an overscan factor so it moves with natural parallax
        /// without exposing canvas edges.
        void setupBackgroundPlane(Layer& layer, float depth, float fovDegrees, float aspect, float overscan = 1.25f);

        /// Rig a layer as a 3D ground or horizon floor plane, placing it at elevation `floorY`
        /// and pitching it about X by `tiltDegrees` so it recedes into the distance.
        void horizonGrid(Layer& layer, float floorY, float depthZ = 0.f, float tiltDegrees = 80.0f);

        /// Smooth continuous ground travel along the Z axis, animating floor grid translation
        /// across the specified duration.
        void groundTravel(Layer& layer, int inFrame, int duration, float distanceZ, float fps,
                          const Easing& easing = Easing::linear());

        /// Ambient 3D drift field: keys subtle, organic multi-harmonic 3D floating motion on a
        /// group of particles, bokeh motes, or dust layers. Distributing layers across Z depths
        /// gives an immediate volumetric depth feeling during camera movement.
        void ambientDriftField(const std::vector<Layer*>& layers, int inFrame, int duration, float fps,
                               std::uint32_t seed = 42, float maxRadius = 15.0f, float speed = 1.0f);

        /// Aura blob / gradient anchor travel: keys smooth organic Lissajous travel for background
        /// glow blobs, light sources, or animated color anchors.
        void auraBlobMotion(Layer& layer, int inFrame, int duration, const Vector3& center,
                            const Vector2& radius, float frequency, float fps, std::uint32_t seed = 0);

        /// Auto-focus: continuously tracks the target layer's world position across the duration,
        /// keying the camera rig's focus distance track so the subject stays sharp while
        /// the background naturally falls into depth-of-field blur.
        void autoFocus(CameraRig& rig, const Layer& targetLayer, int inFrame, int duration, float fps);

    }// namespace presets

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_PRESETS_HPP
