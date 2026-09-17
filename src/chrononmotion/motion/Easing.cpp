#include "chrononmotion/motion/Easing.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion {

    namespace {

        // Cubic Bézier component, P0 = 0, P1 = a1, P2 = a2, P3 = 1:
        //   B(t) = (1-t)^3*0 + 3(1-t)^2*t*a1 + 3(1-t)*t^2*a2 + t^3*1
        // expanded in power basis so the solve loop is a couple of multiplies.
        [[nodiscard]] float bezierComponent(float t, float a1, float a2) {

            const float c = 3.f * a1;
            const float b = 3.f * (a2 - a1) - c;
            const float a = 1.f - c - b;
            return ((a * t + b) * t + c) * t;
        }

        [[nodiscard]] float bezierSlope(float t, float a1, float a2) {

            const float c = 3.f * a1;
            const float b = 3.f * (a2 - a1) - c;
            const float a = 1.f - c - b;
            return (3.f * a * t + 2.f * b) * t + c;
        }

        // Solve x(s) = x for s. Newton first (converges in a few steps for the
        // shapes an author writes), bisection when the slope is too flat to trust
        // so the result stays exact and branch-free of never-terminating cases.
        [[nodiscard]] float solveForS(float x, float x1, float x2) {

            if (x <= 0.f) return 0.f;
            if (x >= 1.f) return 1.f;

            float s = x;
            for (int i = 0; i < 8; ++i) {
                const float error = bezierComponent(s, x1, x2) - x;
                if (std::fabs(error) < 1e-6f) return s;
                const float slope = bezierSlope(s, x1, x2);
                if (std::fabs(slope) < 1e-6f) break;
                s -= error / slope;
            }

            float lo = 0.f;
            float hi = 1.f;
            s = x;
            for (int i = 0; i < 24; ++i) {
                const float value = bezierComponent(s, x1, x2);
                if (std::fabs(value - x) < 1e-6f) return s;
                if (value < x) {
                    lo = s;
                } else {
                    hi = s;
                }
                s = 0.5f * (lo + hi);
            }
            return s;
        }

        [[nodiscard]] float bounceOutValue(float t) {

            constexpr float n1 = 7.5625f;
            constexpr float d1 = 2.75f;

            if (t < 1.f / d1) return n1 * t * t;
            if (t < 2.f / d1) {
                t -= 1.5f / d1;
                return n1 * t * t + 0.75f;
            }
            if (t < 2.5f / d1) {
                t -= 2.25f / d1;
                return n1 * t * t + 0.9375f;
            }
            t -= 2.625f / d1;
            return n1 * t * t + 0.984375f;
        }

        [[nodiscard]] float elasticOutValue(float t) {

            // The formula asymptotes to 1 but does not reach it, so the endpoints
            // are pinned: a keyed animation must land exactly on its target.
            if (t <= 0.f) return 0.f;
            if (t >= 1.f) return 1.f;

            constexpr float c4 = math::TWO_PI / 3.f;
            return std::pow(2.f, -10.f * t) * std::sin((t * 10.f - 0.75f) * c4) + 1.f;
        }

    }// namespace

    Easing::Easing(Kind kind, float x1, float y1, float x2, float y2)
        : kind_(kind), x1_(x1), y1_(y1), x2_(x2), y2_(y2) {}

    Easing Easing::linear() {

        return Easing{};
    }

    Easing Easing::easeIn() {

        return Easing(Kind::CubicBezier, 0.42f, 0.f, 1.f, 1.f);
    }

    Easing Easing::easeOut() {

        return Easing(Kind::CubicBezier, 0.f, 0.f, 0.58f, 1.f);
    }

    Easing Easing::easeInOut() {

        return Easing(Kind::CubicBezier, 0.42f, 0.f, 0.58f, 1.f);
    }

    Easing Easing::cubicBezier(float x1, float y1, float x2, float y2) {

        return Easing(Kind::CubicBezier, std::clamp(x1, 0.f, 1.f), y1, std::clamp(x2, 0.f, 1.f), y2);
    }

    Easing Easing::overshoot(float amount) {

        return Easing(Kind::CubicBezier, 0.34f, 1.f + amount, 0.64f, 1.f);
    }

    Easing Easing::bounceOut() {

        return Easing(Kind::BounceOut, 0.f, 0.f, 1.f, 1.f);
    }

    Easing Easing::elasticOut() {

        return Easing(Kind::ElasticOut, 0.f, 0.f, 1.f, 1.f);
    }

    float Easing::operator()(float t) const {

        if (kind_ == Kind::Linear) return t;

        const float x = std::clamp(t, 0.f, 1.f);

        switch (kind_) {
            case Kind::BounceOut:
                return bounceOutValue(x);
            case Kind::ElasticOut:
                return elasticOutValue(x);
            case Kind::CubicBezier:
                return bezierComponent(solveForS(x, x1_, x2_), y1_, y2_);
            case Kind::Linear:
                break;
        }
        return x;
    }

    bool Easing::operator==(const Easing& other) const {

        return kind_ == other.kind_ && x1_ == other.x1_ && y1_ == other.y1_ && x2_ == other.x2_ && y2_ == other.y2_;
    }

}// namespace chrononmotion::motion
