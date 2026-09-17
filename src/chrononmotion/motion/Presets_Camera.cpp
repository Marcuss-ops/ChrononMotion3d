// ChrononMotion — presets that move the camera rig.
//
// Every one of these samples the rig's *current* pose and keys the end of the
// move from it, so camera presets compose: a push after an orbit dollies along
// the view axis the orbit left behind, not along the world Z it started from.
// The shake is deterministic on purpose — whole sine cycles decaying to zero, no
// RNG — because a re-render has to shake the same way.

#include "Presets_detail.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <cmath>

namespace chrononmotion::motion::presets {

    using detail::keyVector3;

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

}// namespace chrononmotion::motion::presets
