// ChrononMotion — presets that move a layer: where it is, how deep it sits, and
// how long it lives there.
//
// Entrances/exits, depth, the lifetime-derived windows they key inside, and the
// 2.5D travel that reads as parallax. Nothing here touches scale, opacity, the
// pivot or the camera — those are the other families.

#include "Presets_detail.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <cmath>

namespace chrononmotion::motion::presets {

    using detail::keyVector3;
    using detail::restOf;

    // ---------------------------------------------------------------- in/out

    void slideIn(Layer& layer, Direction direction, int inFrame, int duration, float distance, float fps,
                 const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        Vector3 offset = directionOffset(direction);
        offset.multiplyScalar(distance);

        Vector3 from = rest.position;
        from.add(offset);

        keyVector3(layer.tracks.position, t0, t1, from, rest.position, easing);
    }

    void slideOut(Layer& layer, Direction direction, int inFrame, int duration, float distance, float fps,
                  const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        Vector3 offset = directionOffset(direction);
        offset.multiplyScalar(distance);

        Vector3 to = rest.position;
        to.add(offset);

        keyVector3(layer.tracks.position, t0, t1, rest.position, to, easing);
    }

    void pushInZ(Layer& layer, int inFrame, int duration, float fromDepth, float fps,
                 const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.position, t0, t1,
                   Vector3(rest.position.x, rest.position.y, fromDepth),
                   Vector3(rest.position.x, rest.position.y, rest.position.z),
                   easing);
    }

    void pullOutZ(Layer& layer, int inFrame, int duration, float toDepth, float fps,
                  const Easing& easing) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.position, t0, t1,
                   Vector3(rest.position.x, rest.position.y, rest.position.z),
                   Vector3(rest.position.x, rest.position.y, toDepth),
                   easing);
    }

    // -------------------------------------------------------- lifetime in/out

    LifetimeWindows lifetimeWindows(const Layer& layer, int duration) {

        LifetimeWindows windows;
        if (duration <= 0) return windows;

        if (layer.lifetime.openEnded()) {
            windows.in = FrameRange::between(layer.lifetime.inFrame, layer.lifetime.inFrame + duration);
            windows.hasIn = true;
            return windows;
        }

        const int span = layer.lifetime.duration();
        if (span < duration) return windows;

        windows.in = FrameRange::between(layer.lifetime.inFrame, layer.lifetime.inFrame + duration);
        windows.hasIn = true;

        // The exit has to start after the entrance has finished, so both can
        // only fit when the lifetime holds two whole windows.
        if (duration <= span / 2) {
            windows.out = FrameRange::between(layer.lifetime.outFrame - duration, layer.lifetime.outFrame);
            windows.hasOut = true;
        }

        return windows;
    }

    void lifetimeSlide(Layer& layer, Direction inDirection, Direction outDirection,
                       int duration, float distance, float fps,
                       const Easing& inEasing, const Easing& outEasing) {

        const LifetimeWindows windows = lifetimeWindows(layer, duration);

        if (windows.hasIn) {
            const int frames = windows.in.duration();
            slideIn(layer, inDirection, windows.in.inFrame, frames, distance, fps, inEasing);
            fadeIn(layer, windows.in.inFrame, frames, fps, inEasing);
        }

        if (windows.hasOut) {
            const int frames = windows.out.duration();
            slideOut(layer, outDirection, windows.out.inFrame, frames, distance, fps, outEasing);
            fadeOut(layer, windows.out.inFrame, frames, fps, outEasing);
        }
    }

    // ------------------------------------------------------------------- 2.5D

    void parallaxEntrance(Layer& layer, int inFrame, int duration, float fromDepth, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        keyVector3(layer.tracks.position, t0, t1,
                   Vector3(rest.position.x, rest.position.y, fromDepth),
                   Vector3(rest.position.x, rest.position.y, rest.position.z),
                   Easing::easeOut());
    }

    void floatHover(Layer& layer, int inFrame, int duration, float amplitude, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        constexpr int keys = 4;
        for (int i = 0; i <= keys; ++i) {
            const float progress = static_cast<float>(i) / static_cast<float>(keys);
            const float phase = std::sin(progress * math::TWO_PI);
            layer.tracks.position.add(t0 + (t1 - t0) * progress,
                                      Vector3(rest.position.x,
                                              rest.position.y + amplitude * phase,
                                              rest.position.z),
                                      Easing::easeInOut());
        }
    }

    void drift(Layer& layer, int inFrame, int duration, const Vector3& offset, float fps) {

        const Transform rest = restOf(layer, inFrame, fps);
        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        Vector3 to = rest.position;
        to.add(offset);

        keyVector3(layer.tracks.position, t0, t1, rest.position, to, Easing::linear());
    }

}// namespace chrononmotion::motion::presets
