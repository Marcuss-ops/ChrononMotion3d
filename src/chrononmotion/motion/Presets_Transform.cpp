// ChrononMotion — presets that change a layer's own shape: uniform scale,
// opacity, the pivot, and orientation.
//
// Orientation is the interesting half. A flip, a card turn and a spinXYZ key the
// quaternion channel (so they interpolate along the shortest arc and never flip
// through ±180°), while spinZ keys the Euler channel on purpose: a full turn
// about one axis is a plain number, and a number is what an author wants to
// retime.

#include "Presets_detail.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion::presets {

    using detail::keyVector3;
    using detail::restOf;
    using detail::uniform;

    void scalePop(Layer& layer, int inFrame, int duration, float from, float to, float fps,
                  const Easing& easing) {

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.scale, t0, t1, uniform(from), uniform(to), easing);
    }

    void scaleOvershoot(Layer& layer, int inFrame, int duration, float from, float to, float fps) {

        scalePop(layer, inFrame, duration, from, to, fps, Easing::overshoot(1.2f));
    }

    void fadeIn(Layer& layer, int inFrame, int duration, float fps, const Easing& easing) {

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        layer.tracks.opacity.add(t0, 0.f, easing);
        layer.tracks.opacity.add(t1, layer.transform.opacity, Easing::linear());
    }

    void fadeOut(Layer& layer, int inFrame, int duration, float fps, const Easing& easing) {

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        layer.tracks.opacity.add(t0, layer.transform.opacity, easing);
        layer.tracks.opacity.add(t1, 0.f, Easing::linear());
    }

    void fadeSlide(Layer& layer, Direction direction, int inFrame, int duration, float distance, float fps) {

        slideIn(layer, direction, inFrame, duration, distance, fps, Easing::easeOut());
        fadeIn(layer, inFrame, duration, fps, Easing::easeOut());
    }

    void fadeScale(Layer& layer, int inFrame, int duration, float from, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const Vector3 start(rest.scale.x * from, rest.scale.y * from, rest.scale.z * from);

        keyVector3(layer.tracks.scale, t0, t1, start, rest.scale, Easing::overshoot());
        layer.tracks.opacity.add(t0, 0.f, Easing::easeOut());
        layer.tracks.opacity.add(t1, layer.transform.opacity, Easing::linear());
    }

    void anchorTo(Layer& layer, const Vector3& to, int inFrame, int duration, float fps,
                  const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.anchor, t0, t1, rest.anchor, to, easing);
    }

    void flipX(Layer& layer, int inFrame, int duration, float fps, float degrees) {
        flip(layer, Axis::X, inFrame, duration, fps, degrees);
    }

    void flipY(Layer& layer, int inFrame, int duration, float fps, float degrees) {
        flip(layer, Axis::Y, inFrame, duration, fps, degrees);
    }

    void flip(Layer& layer, Axis axis, int inFrame, int duration, float fps, float degrees) {

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        Quaternion turned;
        turned.setFromAxisAngle(axisVector(axis), math::degToRad(degrees));

        layer.tracks.orientation.add(t0, Quaternion(0.f, 0.f, 0.f, 1.f), Easing::easeInOut());
        layer.tracks.orientation.add(t1, turned, Easing::linear());
    }

    void cardTurn(Layer& layer, int inFrame, int duration, float fps) {

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        Quaternion folded;
        folded.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(-90.f));

        layer.tracks.orientation.add(t0, folded, Easing::easeOut());
        layer.tracks.orientation.add(t1, Quaternion(0.f, 0.f, 0.f, 1.f), Easing::linear());
    }

    void tilt3D(Layer& layer, int inFrame, int duration, float degrees, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.rotation, t0, t1, rest.rotation,
                   Vector3(rest.rotation.x + math::degToRad(degrees),
                           rest.rotation.y,
                           rest.rotation.z),
                   Easing::easeInOut());
    }

    void spinZ(Layer& layer, int inFrame, int duration, float turns, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.rotation, t0, t1, rest.rotation,
                   Vector3(rest.rotation.x,
                           rest.rotation.y,
                           rest.rotation.z + turns * math::TWO_PI),
                   Easing::linear());
    }

    void spinXYZ(Layer& layer, int inFrame, int duration, float turns, float fps) {

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        Vector3 axis(1.f, 1.f, 1.f);
        axis.normalize();

        const float total = turns * math::TWO_PI;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(total) / (math::PI * 0.9f))));

        auto& track = layer.tracks.orientation;
        for (int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            Quaternion key;
            key.setFromAxisAngle(axis, total * progress);
            track.add(t0 + (t1 - t0) * progress, key, Easing::linear());
        }
    }

    void spinIn(Layer& layer, Axis axis, int inFrame, int duration, float turns, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        const float total = turns * math::TWO_PI;
        const int steps = std::max(1, static_cast<int>(std::ceil(std::fabs(total) / (math::PI * 0.9f))));
        const Easing unwind = Easing::easeOut();

        // The unwinding *rate* is the easing; the key *times* stay evenly spaced, so
        // the curve is the only thing shaping the motion. Each key is the remaining
        // offset composed onto the resting orientation (`offset * rest`, i.e. the
        // rest pose first and then the spin about the world axis), which is why the
        // last key is the resting pose itself and not merely a pose near it.
        auto& track = layer.tracks.orientation;
        for (int i = 0; i <= steps; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(steps);
            const float remaining = 1.f - unwind(progress);

            Quaternion offset;
            offset.setFromAxisAngle(axisVector(axis), total * remaining);

            Quaternion key = offset;
            key.multiply(rest.orientation);
            track.add(t0 + (t1 - t0) * progress, key, Easing::linear());
        }
    }

    void pulseOpacity(Layer& layer, int inFrame, int duration, int cycles, float fps, float floorOpacity) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const int count = std::max(cycles, 1);
        const float floorValue = std::clamp(floorOpacity, 0.f, 1.f);

        auto& track = layer.tracks.opacity;
        track.add(t0, rest.opacity, Easing::easeInOut());

        for (int cycle = 0; cycle < count; ++cycle) {
            const float down = (static_cast<float>(cycle) + 0.5f) / static_cast<float>(count);
            track.add(t0 + (t1 - t0) * down, floorValue, Easing::easeInOut());

            // The last cycle lands on `t1` itself rather than on a float accumulation
            // of the period, so a pulse train ends where the duration says it ends.
            const int next = cycle + 1;
            const float up = next == count
                                     ? t1
                                     : t0 + (t1 - t0) * (static_cast<float>(next) / static_cast<float>(count));
            track.add(up, rest.opacity, Easing::linear());
        }
    }

    // ----------------------------------------------------------- documentary

    namespace {

        /// The resting scale, multiplied on every axis. A ping and a draw both
        /// scale the layer the caller handed in, so the factor composes with
        /// whatever size that layer was authored at instead of replacing it.
        Vector3 scaledCopy(const Vector3& base, float factor) {

            Vector3 out = base;
            out.multiplyScalar(factor);
            return out;
        }

        /// The scale a draw-on starts from. Not zero: a zero component makes the
        /// layer's world matrix singular, and a singular transform has no
        /// inverse to place the layer with — so a `lineDraw` would be a plan the
        /// lowering is right to refuse. Read against the layer's own width, a
        /// thousandth of it is the same as nothing on screen.
        constexpr float kDrawOnStartScale = 1e-3f;

    }// namespace

    void radarPing(Layer& layer, int inFrame, int duration, int pulses,
                   float fromScale, float toScale, float fps, const Easing& easing) {

        if (duration <= 0) return;

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const int count = std::max(pulses, 1);
        const float ringSeconds = (t1 - t0) / static_cast<float>(count);
        if (!(ringSeconds > 0.f)) return;

        // Scale: one ring's expansion, *repeated* by the channel's own loop
        // window. A loop rather than a list of hand keys is what lets the ring
        // reset instantly at every boundary (the loop wraps time, so there are
        // no two keys on one frame to collide) without the number of pulses
        // changing the shape of a single ring.
        auto& scale = layer.tracks.scale;
        scale.add(t0, scaledCopy(rest.scale, fromScale), easing);
        scale.add(t0 + ringSeconds, scaledCopy(rest.scale, toScale), Easing::linear());
        scale.loop(t0, t0 + ringSeconds);

        // Opacity: fade to nothing across the same ring, then reset with it.
        auto& opacity = layer.tracks.opacity;
        opacity.add(t0, rest.opacity, Easing::linear());
        opacity.add(t0 + ringSeconds, 0.f, Easing::linear());
        opacity.loop(t0, t0 + ringSeconds);
    }

    void areaReveal(Layer& layer, int inFrame, int duration, float fps, const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.scale, t0, t1, scaledCopy(rest.scale, 1.04f), rest.scale, easing);
        layer.tracks.opacity.add(t0, 0.f, Easing::easeOut());
        layer.tracks.opacity.add(t1, rest.opacity, Easing::linear());
    }

    void extrudeRise(Layer& layer, int inFrame, int duration, float height, float fps,
                     const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        // Only the Z end differs, so an overshoot on the segment lifts the block
        // past its height and settles it back; X and Y are the same value at
        // both ends and cannot shear.
        const Vector3 raised(rest.position.x, rest.position.y, rest.position.z + height);
        keyVector3(layer.tracks.position, t0, t1, rest.position, raised, easing);
        layer.tracks.opacity.add(t0, 0.f, Easing::easeOut());
        layer.tracks.opacity.add(t1, rest.opacity, Easing::linear());
    }

    void lineDraw(Layer& layer, int inFrame, int duration, float fps, const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        // Uncovered along its own width: the height and depth keep the resting
        // value, so the rule grows out of its anchor rather than squashing.
        const Vector3 folded(kDrawOnStartScale, rest.scale.y, rest.scale.z);
        keyVector3(layer.tracks.scale, t0, t1, folded, rest.scale, easing);
        layer.tracks.opacity.add(t0, 0.f, Easing::easeOut());
        layer.tracks.opacity.add(t1, rest.opacity, Easing::linear());
    }

}// namespace chrononmotion::motion::presets
