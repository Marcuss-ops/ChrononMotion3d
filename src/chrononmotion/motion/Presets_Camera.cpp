// ChrononMotion — presets that move the camera rig.
//
// Every one of these samples the rig's *current* pose and keys the end of the
// move from it, so camera presets compose: a push after an orbit dollies along
// the view axis the orbit left behind, not along the world Z it started from.
// The shake is deterministic on purpose — whole sine cycles decaying to zero, no
// RNG — because a re-render has to shake the same way.

#include "Presets_detail.hpp"

#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/motion/Procedural.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace chrononmotion::motion::presets {

    using detail::authorizePresetForCamera;
    using detail::keyVector3;

    namespace {
        void cameraPushImpl(CameraRig& rig, int inFrame, int duration, float distance, float fps) {
            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);
            CameraPose start = rig.sample(t0);
            CameraPose end = start;
            CameraRig::dolly(end, distance);
            keyVector3(rig.positionTrack(), t0, t1, start.position, end.position, Easing::easeInOut());
        }

        void cameraOrbitImpl(CameraRig& rig, int inFrame, int duration, float yaw, float pitch, float fps) {
            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);
            const unsigned int steps = static_cast<unsigned int>(std::max(duration, 2));
            const CameraPose start = rig.sample(t0);
            auto& position = rig.positionTrack();
            for (unsigned int step = 0; step <= steps; ++step) {
                const float progress = static_cast<float>(step) / static_cast<float>(steps);
                CameraPose sample = start;
                CameraRig::orbit(sample, yaw * progress, pitch * progress);
                position.add(t0 + (t1 - t0) * progress, sample.position,
                             step == steps ? Easing::linear() : Easing::easeInOut());
            }
        }

        void cameraShakeImpl(CameraRig& rig, int inFrame, int duration, float amplitude, float fps) {
            const float t0 = frameToTime(inFrame, fps);
            const CameraPose base = rig.sample(t0);
            constexpr int steps = 16;
            auto& track = rig.positionTrack();
            for (int i = 0; i <= steps; ++i) {
                const int frame = inFrame + (duration * i) / steps;
                const float progress = static_cast<float>(i) / static_cast<float>(steps);
                const float decay = 1.f - progress;
                const float shakeX = std::sin(progress * math::TWO_PI * 2.f) * decay;
                const float shakeY = std::sin(progress * math::TWO_PI * 3.f) * decay;
                track.add(frameToTime(frame, fps),
                    Vector3(base.position.x + amplitude * shakeX,
                            base.position.y - amplitude * 0.6f * shakeY, base.position.z),
                    Easing::linear());
            }
        }

        void cameraFovImpl(CameraRig& rig, int inFrame, int duration, float fromFov, float toFov, float fps) {
            const float t0 = frameToTime(inFrame, fps);
            const float t1 = frameToTime(inFrame + duration, fps);
            rig.fovTrack().add(t0, fromFov, Easing::easeInOut());
            rig.fovTrack().add(t1, toFov, Easing::linear());
        }
    }

    void cameraPush(CameraRig& rig, int inFrame, int duration, float distance, float fps) {
        authorizePresetForCamera(rig, "cameraPush");
        cameraPushImpl(rig, inFrame, duration, distance, fps);
    }

    void cameraPull(CameraRig& rig, int inFrame, int duration, float distance, float fps) {
        authorizePresetForCamera(rig, "cameraPull");
        cameraPushImpl(rig, inFrame, duration, -distance, fps);
    }

    void cameraPan(CameraRig& rig, int inFrame, int duration, const Vector2& delta, float fps) {
        authorizePresetForCamera(rig, "cameraPan");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        CameraPose start = rig.sample(t0);
        CameraPose end = start;
        CameraRig::pan(end, delta);

        keyVector3(rig.positionTrack(), t0, t1, start.position, end.position, Easing::easeInOut());
    }

    void orbitMild(CameraRig& rig, int inFrame, int duration, float fps) {
        authorizePresetForCamera(rig, "orbitMild");
        cameraOrbitImpl(rig, inFrame, duration, math::degToRad(12.f), math::degToRad(4.f), fps);
    }

    void cameraOrbit(CameraRig& rig, int inFrame, int duration, float yaw, float pitch, float fps) {
        authorizePresetForCamera(rig, "cameraOrbit");
        cameraOrbitImpl(rig, inFrame, duration, yaw, pitch, fps);
    }

    void shakeMild(CameraRig& rig, int inFrame, int duration, float amplitude, float fps) {
        authorizePresetForCamera(rig, "shakeMild");
        cameraShakeImpl(rig, inFrame, duration, amplitude, fps);
    }

    void focusPush(CameraRig& rig, int inFrame, int duration, float distance, float fps) {
        authorizePresetForCamera(rig, "focusPush");
        cameraPushImpl(rig, inFrame, duration, distance, fps);
    }

    void rackFocus(CameraRig& rig, int inFrame, int duration,
                   float fromDistance, float toDistance, float fps) {
        authorizePresetForCamera(rig, "rackFocus");

        if (!std::isfinite(fromDistance) || !std::isfinite(toDistance) ||
            fromDistance < 0.f || toDistance < 0.f) {
            throw std::invalid_argument("rackFocus: focus distances must be finite and non-negative");
        }

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        rig.focusDistanceTrack().add(t0, fromDistance, Easing::easeInOut());
        rig.focusDistanceTrack().add(t1, toDistance, Easing::linear());
    }

    void cameraShake(CameraRig& rig, int inFrame, int duration, float amplitude, float fps) {
        authorizePresetForCamera(rig, "cameraShake");
        cameraShakeImpl(rig, inFrame, duration, amplitude, fps);
    }

    void handheldShake(CameraRig& rig, float intensity_units, float frequency_hz,
                       std::uint32_t seed, float start_s, float end_s) {
        authorizePresetForCamera(rig, "handheldShake");

        if (!std::isfinite(intensity_units) || intensity_units < 0.f ||
            !std::isfinite(frequency_hz) || frequency_hz <= 0.f ||
            !std::isfinite(start_s) || !std::isfinite(end_s) || !(end_s > start_s)) {
            throw std::invalid_argument("handheldShake: expected finite non-negative intensity, positive frequency, and start < end");
        }

        constexpr float step = 1.f / 60.f;
        constexpr std::size_t maxKeys = 65536;
        const double intervalCount = std::ceil(static_cast<double>(end_s - start_s) / step);
        if (!std::isfinite(intervalCount) || intervalCount > static_cast<double>(maxKeys - 1)) {
            throw std::invalid_argument("handheldShake: generated tracks exceed the 65536 key limit");
        }

        DriverSpec xDriver;
        xDriver.kind = DriverKind::Wiggle;
        xDriver.property = DriverProperty::Position;
        xDriver.seed = seed;
        xDriver.axis.set(1.f, 0.f, 0.f);
        xDriver.amplitude = intensity_units;
        xDriver.frequency = frequency_hz;
        xDriver.startTime = start_s;
        xDriver.endTime = end_s;

        DriverSpec yDriver = xDriver;
        yDriver.seed = seed + 1u;
        yDriver.axis.set(0.f, 1.f, 0.f);
        yDriver.amplitude = intensity_units * 0.7f;
        yDriver.frequency = frequency_hz * 1.17f;

        DriverSpec rollDriver = xDriver;
        rollDriver.property = DriverProperty::Rotation;
        rollDriver.seed = seed + 2u;
        rollDriver.axis.set(0.f, 0.f, 1.f);
        rollDriver.amplitude = intensity_units * 0.01f;
        rollDriver.frequency = frequency_hz * 0.73f;

        if (!std::isfinite(yDriver.amplitude) || !std::isfinite(rollDriver.amplitude) ||
            !std::isfinite(yDriver.frequency) || !std::isfinite(rollDriver.frequency)) {
            throw std::invalid_argument("handheldShake: intensity or frequency overflows a driver channel");
        }

        const int intervals = static_cast<int>(intervalCount);
        std::vector<float> times;
        std::vector<CameraPose> basePoses;
        times.reserve(static_cast<std::size_t>(intervals) + 1);
        basePoses.reserve(static_cast<std::size_t>(intervals) + 1);
        for (int i = 0; i <= intervals; ++i) {
            const float time = i == intervals ? end_s : start_s + static_cast<float>(i) * step;
            if (i > 0 && !(time > times.back())) {
                throw std::invalid_argument("handheldShake: fixed step is below float time precision");
            }
            times.push_back(time);
            basePoses.push_back(rig.sample(time));
        }

        auto& position = rig.positionTrack();
        auto& roll = rig.rollTrack();
        for (int i = 0; i <= intervals; ++i) {
            const float time = times[static_cast<std::size_t>(i)];
            const float progress = (time - start_s) / (end_s - start_s);
            const float window = (i == 0 || i == intervals) ? 0.f : std::sin(math::PI * progress);
            Vector3 offset = evaluateDriverOffset(xDriver, time, Vector3{});
            offset.add(evaluateDriverOffset(yDriver, time, Vector3{}));
            offset.multiplyScalar(window);
            Vector3 posePosition = basePoses[static_cast<std::size_t>(i)].position;
            posePosition.add(offset);
            position.add(time, posePosition, Easing::linear());

            const Vector3 rollOffset = evaluateDriverOffset(rollDriver, time, Vector3{});
            roll.add(time, basePoses[static_cast<std::size_t>(i)].roll + rollOffset.z * window, Easing::linear());
        }
    }

    void cameraFovBreath(CameraRig& rig, int inFrame, int duration, float fromFov, float toFov, float fps) {
        authorizePresetForCamera(rig, "cameraFovBreath");
        cameraFovImpl(rig, inFrame, duration, fromFov, toFov, fps);
    }

    void cameraFov(CameraRig& rig, int inFrame, int duration, float fromFov, float toFov, float fps) {
        authorizePresetForCamera(rig, "cameraFov");
        cameraFovImpl(rig, inFrame, duration, fromFov, toFov, fps);
    }

    void orbitalSlamZoom(CameraRig& rig, int inFrame, int duration,
                         float yawDeg, float pitchDeg, float dollyDistance,
                         float fromFov, float toFov, float fps) {
        authorizePresetForCamera(rig, "orbitalSlamZoom");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);

        CameraPose start = rig.sample(t0);
        CameraPose end = start;
        CameraRig::orbit(end, math::degToRad(yawDeg), math::degToRad(pitchDeg));
        CameraRig::dolly(end, dollyDistance);

        // The slam curve: little travel at first, the bulk of it through the
        // middle, then a long soft landing. That asymmetry is what the eye reads
        // as a dive, where the symmetric ease a plain push uses reads as a slide.
        const Easing slam = Easing::cubicBezier(0.72f, 0.f, 0.2f, 1.f);
        keyVector3(rig.positionTrack(), t0, t1, start.position, end.position, slam);
        rig.fovTrack().add(t0, fromFov, slam);
        rig.fovTrack().add(t1, toFov, Easing::linear());
    }

    void cameraDrift(CameraRig& rig, int inFrame, int duration,
                     float yawRateDegPerSec, float pitchRateDegPerSec, float fps) {
        authorizePresetForCamera(rig, "cameraDrift");

        const float t0 = frameToTime(inFrame, fps);
        const float t1 = frameToTime(inFrame + duration, fps);
        const float span = t1 - t0;
        if (!(span > 0.f)) return;

        // One pose sampled once, then the *whole* drift drawn as an orbit about
        // it, keyed at a fixed step. Keying a rate instead would need one key at
        // each end, and a two-key orbit is exactly the move that freezes; the
        // step is what keeps the camera alive across the tail of the shot.
        const CameraPose base = rig.sample(t0);
        const int steps = std::max(2, static_cast<int>(std::round(span * fps / 6.f)));
        auto& track = rig.positionTrack();
        for (int i = 0; i <= steps; ++i) {
            const float time = i == steps
                                       ? t1
                                       : t0 + span * (static_cast<float>(i) / static_cast<float>(steps));
            const float elapsed = time - t0;
            CameraPose pose = base;
            CameraRig::orbit(pose, math::degToRad(yawRateDegPerSec) * elapsed,
                             math::degToRad(pitchRateDegPerSec) * elapsed);
            track.add(time, pose.position, Easing::linear());
        }
    }

}// namespace chrononmotion::motion::presets
