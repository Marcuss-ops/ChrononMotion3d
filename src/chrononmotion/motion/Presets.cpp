#include "chrononmotion/motion/Presets.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion {

    float frameToTime(int frame, float fps) {

        return fps > 0.f ? static_cast<float>(frame) / fps : 0.f;
    }

    namespace presets {

        namespace {

            /// The pose a preset starts from: the layer as it is sampled just
            /// before the preset writes its first key.
            [[nodiscard]] Transform restOf(const Layer& layer, int inFrame, float fps) {

                return layer.sample(frameToTime(inFrame, fps));
            }

            [[nodiscard]] Vector3 uniform(float value) {

                return Vector3(value, value, value);
            }

            void keyVector3(Track<Vector3>& track, float t0, float t1,
                            const Vector3& from, const Vector3& to, const Easing& easing) {

                track.add(t0, from, easing);
                track.add(t1, to, Easing::linear());
            }

        }// namespace

        Vector3 directionOffset(Direction direction) {

            switch (direction) {
                case Direction::Left:
                    return Vector3(-1.f, 0.f, 0.f);
                case Direction::Right:
                    return Vector3(1.f, 0.f, 0.f);
                case Direction::Up:
                    return Vector3(0.f, 1.f, 0.f);
                case Direction::Down:
                    return Vector3(0.f, -1.f, 0.f);
            }
            return Vector3(0.f, 0.f, 0.f);
        }

        Vector3 axisVector(Axis axis) {

            switch (axis) {
                case Axis::X:
                    return Vector3(1.f, 0.f, 0.f);
                case Axis::Y:
                    return Vector3(0.f, 1.f, 0.f);
                case Axis::Z:
                    return Vector3(0.f, 0.f, 1.f);
            }
            return Vector3(0.f, 0.f, 0.f);
        }

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

        void arcMotion(Layer& layer, const Vector3& to, const Vector3& apex, int inFrame, int duration, float fps) {

            const Transform rest = restOf(layer, inFrame, fps);
            const MotionPath path = MotionPath::quadraticBezier(rest.position, apex, to);
            pathMotion(layer, path, inFrame, duration, fps, false, 16);
        }

        void pathMotion(Layer& layer, const MotionPath& path, int inFrame, int duration, float fps,
                        bool orient, unsigned int steps) {

            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);
            const unsigned int segments = std::max(steps, 2u);

            for (unsigned int i = 0; i <= segments; ++i) {
                const float progress = static_cast<float>(i) / static_cast<float>(segments);
                const float time = t0 + (t1 - t0) * progress;

                layer.tracks.position.add(time, path.sampleUniform(progress), Easing::linear());

                if (orient) {
                    layer.tracks.orientation.add(time, path.orientToPath(progress), Easing::linear());
                }
            }
        }

        void staggeredEntrance(const std::vector<Layer*>& layers, Direction direction,
                               int inFrame, int stagger, int duration, float distance, float fps) {

            for (std::size_t i = 0; i < layers.size(); ++i) {
                if (layers[i] == nullptr) continue;
                const int start = inFrame + static_cast<int>(i) * stagger;
                fadeSlide(*layers[i], direction, start, duration, distance, fps);
            }
        }

        void parentGroupReveal(Layer& parent, const std::vector<Layer*>& children,
                               int inFrame, int duration, float fps) {

            const Transform rest = parent.sample(frameToTime(inFrame, fps));
            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);

            const Vector3 folded(rest.scale.x * 0.85f, rest.scale.y * 0.85f, rest.scale.z * 0.85f);
            keyVector3(parent.tracks.scale, t0, t1, folded, rest.scale, Easing::overshoot());
            parent.tracks.opacity.add(t0, 0.f, Easing::easeOut());
            parent.tracks.opacity.add(t1, rest.opacity, Easing::linear());

            const int childWindow = std::max(1, duration / 2);
            const int childCount = static_cast<int>(children.size());

            for (int i = 0; i < childCount; ++i) {
                if (children[static_cast<std::size_t>(i)] == nullptr) continue;
                const int start = inFrame + (childWindow * i) / std::max(1, childCount);
                fadeIn(*children[static_cast<std::size_t>(i)], start, childWindow, fps);
            }
        }

        void cameraPush(CameraRig& rig, int inFrame, int duration, float distance, float fps) {

            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);

            CameraPose start = rig.sample(t0);
            CameraPose end = start;
            CameraRig::dolly(end, distance);

            keyVector3(rig.positionTrack(), t0, t1, start.position, end.position, Easing::easeInOut());
        }

        void cameraPull(CameraRig& rig, int inFrame, int duration, float distance, float fps) {

            cameraPush(rig, inFrame, duration, -distance, fps);
        }

        void cameraPan(CameraRig& rig, int inFrame, int duration, const Vector2& delta, float fps) {

            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);

            CameraPose start = rig.sample(t0);
            CameraPose end = start;
            CameraRig::pan(end, delta);

            keyVector3(rig.positionTrack(), t0, t1, start.position, end.position, Easing::easeInOut());
        }

        void cameraOrbit(CameraRig& rig, int inFrame, int duration, float yaw, float pitch, float fps) {

            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);

            CameraPose start = rig.sample(t0);
            CameraPose end = start;
            CameraRig::orbit(end, yaw, pitch);

            keyVector3(rig.positionTrack(), t0, t1, start.position, end.position, Easing::easeInOut());
        }

        void cameraShake(CameraRig& rig, int inFrame, int duration, float amplitude, float fps) {

            const float t0 = frameToTime(inFrame, fps);
            const CameraPose base = rig.sample(t0);

            constexpr int steps = 16;
            auto& track = rig.positionTrack();

            for (int i = 0; i <= steps; ++i) {
                const int frame = inFrame + (duration * i) / steps;
                const float time = frameToTime(frame, fps);
                const float progress = static_cast<float>(i) / static_cast<float>(steps);

                // A decaying pair of whole sine cycles, at different frequencies
                // per axis. Whole cycles start and end at zero, and the decay is
                // zero at the end too, so the shake begins and resolves exactly on
                // the base pose; no RNG, so a re-render shakes identically.
                const float decay = 1.f - progress;
                const float shakeX = std::sin(progress * math::TWO_PI * 2.f) * decay;
                const float shakeY = std::sin(progress * math::TWO_PI * 3.f) * decay;

                track.add(time,
                          Vector3(base.position.x + amplitude * shakeX,
                                  base.position.y - amplitude * 0.6f * shakeY,
                                  base.position.z),
                          Easing::linear());
            }
        }

        void cameraFov(CameraRig& rig, int inFrame, int duration, float fromFov, float toFov, float fps) {

            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);

            rig.fovTrack().add(t0, fromFov, Easing::easeInOut());
            rig.fovTrack().add(t1, toFov, Easing::linear());
        }

    }// namespace presets

}// namespace chrononmotion::motion
