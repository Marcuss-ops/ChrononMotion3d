#ifndef CHRONONMOTION_MOTION_CAMERASHAKE_HPP
#define CHRONONMOTION_MOTION_CAMERASHAKE_HPP

#include "chrononmotion/math/Vector3.hpp"

#include <cstdint>

namespace chrononmotion::motion::camera {

    /// Deterministic procedural shake parameters. Frequencies are evaluated
    /// against frame time, matching the original Chronon shake recipes.
    struct CameraShakeSpec {
        float positionFrequency{3.f}, positionAmplitude{15.f};
        float rotationFrequency{2.5f}, rotationAmplitudeDegrees{2.f};
        float zoomFrequency{1.f}, zoomAmplitude{0.f};
        std::uint32_t seed{42};
        float fadeInFrames{0.f}, fadeOutFrames{0.f};
    };

    struct CameraShakeSample {
        Vector3 position{0.f, 0.f, 0.f};
        Vector3 rotationRadians{0.f, 0.f, 0.f};
        float zoom{0.f};
    };

    [[nodiscard]] CameraShakeSample sampleCameraShake(const CameraShakeSpec& spec,
                                                       float frame, float durationFrames);
    [[nodiscard]] CameraShakeSpec handheldShake(float intensity = 1.f);
    [[nodiscard]] CameraShakeSpec earthquakeShake(float intensity = 1.f);
    [[nodiscard]] CameraShakeSpec driftShake(float intensity = 1.f);
    [[nodiscard]] CameraShakeSpec walkingShake(float intensity = 1.f);
    [[nodiscard]] CameraShakeSpec breathingShake(float intensity = 1.f);
    [[nodiscard]] CameraShakeSpec explosionShake(float intensity = 1.f);
    [[nodiscard]] CameraShakeSpec cinematicShake(float intensity = 1.f);

}

#endif
