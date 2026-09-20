#ifndef CHRONONMOTION_MOTION_PROCEDURAL_HPP
#define CHRONONMOTION_MOTION_PROCEDURAL_HPP

#include "chrononmotion/math/Vector3.hpp"

#include <algorithm>

#include <cstdint>

namespace chrononmotion::motion {

    enum class DriverKind {
        Spring,
        DampedFollow,
        Wiggle,
        Orbit,
        Float,
        Shake
    };

    enum class DriverProperty {
        Position,
        Rotation,
        Scale,
        Opacity
    };

    struct DriverSpec {
        DriverKind kind{DriverKind::Wiggle};
        DriverProperty property{DriverProperty::Position};
        std::uint32_t seed{0};
        std::uint32_t targetId{0};
        Vector3 axis{1.f, 1.f, 1.f};
        Vector3 center{0.f, 0.f, 0.f};
        float amplitude{1.f};
        float frequency{1.f};
        float damping{6.f};
        float stiffness{80.f};
        float radius{1.f};
        float phase{0.f};
        float startTime{0.f};
        float endTime{-1.f};

        [[nodiscard]] bool active(float seconds) const {
            return seconds >= startTime && (endTime < startTime || seconds <= endTime);
        }
    };

    [[nodiscard]] float proceduralNoise(float time, std::uint32_t seed);
    [[nodiscard]] Vector3 evaluateDriverOffset(const DriverSpec& spec, float seconds,
                                                const Vector3& base,
                                                const Vector3& target = Vector3{});

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_PROCEDURAL_HPP
