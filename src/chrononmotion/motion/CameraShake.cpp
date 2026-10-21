#include "chrononmotion/motion/CameraShake.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion::camera {
    namespace {
        float hashNoise(std::int32_t n, std::uint32_t seed) {
            std::uint32_t x = static_cast<std::uint32_t>(n) + seed;
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            return static_cast<float>(x % 10000u) / 5000.f - 1.f;
        }
        float smoothNoise(float t, std::uint32_t seed) {
            const auto i = static_cast<std::int32_t>(std::floor(t));
            const float f = t - static_cast<float>(i), u = f*f*(3.f-2.f*f);
            const float a = hashNoise(i, seed), b = hashNoise(i+1, seed);
            return a + (b-a)*u;
        }
        float wiggle(float frequency, float amplitude, float frame, std::uint32_t seed) {
            if (frequency <= 0.f || amplitude <= 0.f) return 0.f;
            constexpr float norm = 1.f / 1.75f;
            return (smoothNoise(frame*frequency, seed) +
                    .5f*smoothNoise(frame*frequency*2.f, seed+1000u) +
                    .25f*smoothNoise(frame*frequency*4.f, seed+2000u))*amplitude*norm;
        }
        CameraShakeSpec scaled(CameraShakeSpec spec, float intensity) {
            if (!std::isfinite(intensity) || intensity < 0.f)
                throw std::invalid_argument("camera shake intensity must be finite and non-negative");
            spec.positionAmplitude *= intensity;
            spec.rotationAmplitudeDegrees *= intensity;
            spec.zoomAmplitude *= intensity;
            return spec;
        }
    }

    CameraShakeSample sampleCameraShake(const CameraShakeSpec& spec, float frame, float durationFrames) {
        if (!std::isfinite(frame) || !std::isfinite(durationFrames) || frame < 0.f || durationFrames < 0.f)
            throw std::invalid_argument("sampleCameraShake: frame and duration must be finite and non-negative");
        float envelope = 1.f;
        if (spec.fadeInFrames > 0.f && frame < spec.fadeInFrames) envelope *= frame/spec.fadeInFrames;
        const float remaining = durationFrames - frame;
        if (spec.fadeOutFrames > 0.f && remaining < spec.fadeOutFrames)
            envelope *= remaining/spec.fadeOutFrames;
        envelope = std::clamp(envelope, 0.f, 1.f);
        if (envelope <= 0.f) return {};
        CameraShakeSample sample;
        sample.position = {wiggle(spec.positionFrequency, spec.positionAmplitude*envelope, frame, spec.seed),
                           wiggle(spec.positionFrequency, spec.positionAmplitude*envelope, frame, spec.seed+100u),
                           wiggle(spec.positionFrequency, spec.positionAmplitude*envelope, frame, spec.seed+200u)};
        const float radians = math::degToRad(spec.rotationAmplitudeDegrees*envelope);
        sample.rotationRadians = {wiggle(spec.rotationFrequency, radians, frame, spec.seed+50u),
                                  wiggle(spec.rotationFrequency, radians, frame, spec.seed+150u),
                                  wiggle(spec.rotationFrequency, radians, frame, spec.seed+250u)};
        sample.zoom = wiggle(spec.zoomFrequency, spec.zoomAmplitude*envelope, frame, spec.seed+150u);
        return sample;
    }

    CameraShakeSpec handheldShake(float i) { CameraShakeSpec s; s.positionFrequency=4.f; s.positionAmplitude=3.f; s.rotationFrequency=3.f; s.rotationAmplitudeDegrees=.5f; s.seed=42; return scaled(s,i); }
    CameraShakeSpec earthquakeShake(float i) { CameraShakeSpec s; s.positionFrequency=12.f; s.positionAmplitude=25.f; s.rotationFrequency=10.f; s.rotationAmplitudeDegrees=5.f; s.zoomFrequency=8.f; s.zoomAmplitude=10.f; s.fadeOutFrames=30.f; s.seed=77; return scaled(s,i); }
    CameraShakeSpec driftShake(float i) { CameraShakeSpec s; s.positionFrequency=.3f; s.positionAmplitude=8.f; s.rotationFrequency=.2f; s.rotationAmplitudeDegrees=1.f; s.zoomFrequency=.15f; s.zoomAmplitude=5.f; s.seed=123; return scaled(s,i); }
    CameraShakeSpec walkingShake(float i) { CameraShakeSpec s; s.positionFrequency=2.f; s.positionAmplitude=6.f; s.rotationFrequency=2.f; s.rotationAmplitudeDegrees=1.5f; s.seed=99; return scaled(s,i); }
    CameraShakeSpec breathingShake(float i) { CameraShakeSpec s; s.positionFrequency=.25f; s.positionAmplitude=2.f; s.rotationFrequency=.25f; s.rotationAmplitudeDegrees=.3f; s.seed=55; return scaled(s,i); }
    CameraShakeSpec explosionShake(float i) { CameraShakeSpec s; s.positionFrequency=15.f; s.positionAmplitude=40.f; s.rotationFrequency=12.f; s.rotationAmplitudeDegrees=8.f; s.zoomFrequency=10.f; s.zoomAmplitude=15.f; s.fadeOutFrames=20.f; s.seed=33; return scaled(s,i); }
    CameraShakeSpec cinematicShake(float i) { CameraShakeSpec s; s.positionFrequency=1.5f; s.positionAmplitude=1.5f; s.rotationFrequency=1.f; s.rotationAmplitudeDegrees=.2f; s.seed=200; return scaled(s,i); }
}
