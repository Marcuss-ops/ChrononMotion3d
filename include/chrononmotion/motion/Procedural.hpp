#ifndef CHRONONMOTION_MOTION_PROCEDURAL_HPP
#define CHRONONMOTION_MOTION_PROCEDURAL_HPP

#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/math/Vector2.hpp"

#include <algorithm>

#include <cstdint>
#include <cmath>
#include <functional>
#include <array>
#include <stdexcept>
#include <vector>

namespace chrononmotion::motion {

    enum class DriverKind {
        Spring,
        DampedFollow,
        Wiggle,
        Orbit,
        Float,
        Shake,
        Pulse
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

    /// Offline midpoint integration; the consumer supplies the canonical field.
    /// No noise implementation or renderer dependency is owned by Motion.
    [[nodiscard]] inline std::vector<Vector2> integrateFlow(
            const Vector2& origin, float length, unsigned segments, float seconds,
            const std::function<Vector2(const Vector2&, float)>& field) {
        if (!field || !std::isfinite(origin.x) || !std::isfinite(origin.y) ||
            !std::isfinite(length) || length <= 0.f || length > 100000.f ||
            segments < 1 || segments > 4096 || !std::isfinite(seconds))
            throw std::invalid_argument("invalid flow integration inputs");
        const auto direction = [&](const Vector2& p) {
            const Vector2 v = field(p, seconds);
            if (!std::isfinite(v.x) || !std::isfinite(v.y))
                throw std::invalid_argument("flow field returned a non-finite vector");
            const float norm = std::hypot(v.x, v.y);
            if (!std::isfinite(norm)) throw std::invalid_argument("flow vector exceeds bounds");
            return norm > 1.e-7f ? Vector2(v.x / norm, v.y / norm) : Vector2(0.f, 0.f);
        };
        std::vector<Vector2> points;
        points.reserve(segments + 1);
        points.push_back(origin);
        const float step = length / static_cast<float>(segments);
        for (unsigned i = 0; i < segments; ++i) {
            const Vector2 p = points.back();
            const Vector2 d = direction(p);
            const Vector2 midpoint(p.x + d.x * step * 0.5f, p.y + d.y * step * 0.5f);
            const Vector2 m = direction(midpoint);
            const Vector2 next(p.x + m.x * step, p.y + m.y * step);
            if (!std::isfinite(next.x) || !std::isfinite(next.y))
                throw std::invalid_argument("flow integration exceeded finite coordinates");
            points.push_back(next);
        }
        return points;
    }

    /// Marching-squares contour segments. Ambiguous saddle cells use the
    /// sampled centre as an asymptotic-style tie breaker; budgets are bounded.
    [[nodiscard]] inline std::vector<std::array<Vector2, 2>> contourSegments(
            float width, float height, unsigned columns, unsigned rows, float level,
            const std::function<float(const Vector2&)>& sample) {
        if (!sample || !std::isfinite(width) || !std::isfinite(height) || width<=0 || height<=0 ||
            columns<1 || rows<1 || columns>256 || rows>256 || !std::isfinite(level))
            throw std::invalid_argument("invalid contour grid");
        const auto valueAt=[&](const Vector2& p) {
            const float value=sample(p);
            if (!std::isfinite(value)) throw std::invalid_argument("non-finite contour sample");
            return value;
        };
        std::vector<float> values((columns+1)*(rows+1));
        for (unsigned y=0;y<=rows;++y) for (unsigned x=0;x<=columns;++x)
            values[y*(columns+1)+x]=valueAt(Vector2(width*x/columns,height*y/rows));
        std::vector<std::array<Vector2,2>> result;
        for (unsigned y=0;y<rows;++y) for (unsigned x=0;x<columns;++x) {
            const std::array<Vector2,4> p{{{width*x/columns,height*y/rows},
                {width*(x+1)/columns,height*y/rows},{width*(x+1)/columns,height*(y+1)/rows},
                {width*x/columns,height*(y+1)/rows}}};
            const std::array<float,4> v{{values[y*(columns+1)+x],values[y*(columns+1)+x+1],
                values[(y+1)*(columns+1)+x+1],values[(y+1)*(columns+1)+x]}};
            std::array<Vector2,4> crossings;
            unsigned n=0;
            for (unsigned edge=0;edge<4;++edge) {
                const unsigned next=(edge+1)%4;
                if ((v[edge]>=level)==(v[next]>=level)) continue;
                const float t=(level-v[edge])/(v[next]-v[edge]);
                crossings[n++]=Vector2(p[edge].x+(p[next].x-p[edge].x)*t,
                                      p[edge].y+(p[next].y-p[edge].y)*t);
            }
            if (n==2) result.push_back({crossings[0],crossings[1]});
            else if (n==4) {
                const bool centre=valueAt(Vector2(width*(x+.5f)/columns,height*(y+.5f)/rows))>=level;
                if (centre==(v[0]>=level)) {
                    result.push_back({crossings[0],crossings[1]});
                    result.push_back({crossings[2],crossings[3]});
                } else {
                    result.push_back({crossings[0],crossings[3]});
                    result.push_back({crossings[1],crossings[2]});
                }
            }
        }
        return result;
    }

    [[nodiscard]] float proceduralNoise(float time, std::uint32_t seed);
    [[nodiscard]] Vector3 evaluateDriverOffset(const DriverSpec& spec, float seconds,
                                                const Vector3& base,
                                                const Vector3& target = Vector3{});

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_PROCEDURAL_HPP
