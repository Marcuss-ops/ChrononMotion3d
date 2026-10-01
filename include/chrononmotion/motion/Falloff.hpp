#ifndef CHRONONMOTION_MOTION_FALLOFF_HPP
#define CHRONONMOTION_MOTION_FALLOFF_HPP

#include "chrononmotion/math/Vector2.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion {

    /// A bounded, deterministic scalar field centered at `origin`.
    /// Values are one inside innerRadius and fade to zero at radius.
    struct FalloffField {
        enum class Kind { Linear, Smooth, Radial, Box };

        Vector2 origin{};
        float radius{1.f};
        float innerRadius{0.f};
        Kind kind{Kind::Smooth};

        [[nodiscard]] float evaluate(const Vector2& point) const noexcept {
            if (!std::isfinite(point.x) || !std::isfinite(point.y) ||
                !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
                !std::isfinite(radius) || !std::isfinite(innerRadius) ||
                radius <= 0.f || innerRadius < 0.f || innerRadius >= radius) return 0.f;
            const double dx = std::fabs(static_cast<double>(point.x) - origin.x);
            const double dy = std::fabs(static_cast<double>(point.y) - origin.y);
            const double distance = kind == Kind::Box ? std::max(dx, dy) : std::hypot(dx, dy);
            if (!std::isfinite(distance) || distance >= radius) return 0.f;
            if (distance <= innerRadius) return 1.f;
            const float normalized = std::clamp(static_cast<float>(
                    (distance - innerRadius) / (radius - innerRadius)), 0.f, 1.f);
            const float inverse = 1.f - normalized;
            switch (kind) {
                case Kind::Linear:
                case Kind::Box: return inverse;
                case Kind::Smooth: return inverse * inverse * (3.f - 2.f * inverse);
                case Kind::Radial: return inverse * inverse;
            }
            return 0.f;
        }
    };

} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_FALLOFF_HPP
