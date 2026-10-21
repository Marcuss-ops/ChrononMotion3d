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

        [[nodiscard]] float expoOutValue(float t) {

            // t == 1 is special-cased: the general form is 1 - 2^(-10t), which
            // reaches exactly 1 only in the limit, and a keyed segment has to land
            // on its target value exactly or the layer drifts.
            if (t >= 1.f) return 1.f;
            return 1.f - std::pow(2.f, -10.f * t);
        }

        [[nodiscard]] float expoOutSlope(float t) {

            if (t >= 1.f) return 0.f;
            constexpr float k = 10.f * math::LN2;
            return k * std::pow(2.f, -10.f * t);
        }

        [[nodiscard]] float circOutValue(float t) {

            return std::sqrt(1.f - (t - 1.f) * (t - 1.f));
        }

        [[nodiscard]] float circOutSlope(float t) {

            // d/dt of sqrt(1 - (t-1)^2). Differentiating: the numerator is (1-(t-1))
            // = (2-t), NOT (1-(t-1)^2)'s "1-t": the chain rule on sqrt(u) is
            // u'/(2*sqrt(u)) with u = 1-(t-1)^2 and u' = -2(t-1), so the slope is
            // (1-t)/sqrt(1-(t-1)^2).
            //
            // That slope is unbounded as t -> 0 and -> 1, so it is only finite on
            // the open interval. Reporting 0 at the endpoints keeps a consumer
            // that differentiates at exactly 0 or 1 from getting an infinity.
            if (t >= 1.f || t <= 0.f) return 0.f;
            const float d = t - 1.f;
            return (1.f - t) / std::sqrt(1.f - d * d);
        }

    }// namespace

    Easing::Easing(Kind kind, float x1, float y1, float x2, float y2, int stepCount)
        : kind_(kind), x1_(x1), y1_(y1), x2_(x2), y2_(y2), stepCount_(stepCount) {}

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

    Easing Easing::inOutCubic() {

        return Easing(Kind::CubicInOut, 0.f, 0.f, 1.f, 1.f);
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

    Easing Easing::expoOut() {

        return Easing(Kind::ExpoOut, 0.f, 0.f, 1.f, 1.f);
    }

    Easing Easing::circOut() {

        return Easing(Kind::CircOut, 0.f, 0.f, 1.f, 1.f);
    }

    Easing Easing::steps(int count) {

        // A stepped curve with fewer than one level cannot be sampled, so it
        // degrades to a single jump rather than dividing by zero downstream.
        return Easing(Kind::Steps, 0.f, 0.f, 1.f, 1.f, std::max(count, 1));
    }

    float Easing::operator()(float t) const {

        if (kind_ == Kind::Linear) return t;

        const float x = std::clamp(t, 0.f, 1.f);

        switch (kind_) {
            case Kind::BounceOut:
                return bounceOutValue(x);
            case Kind::ElasticOut:
                return elasticOutValue(x);
            case Kind::ExpoOut:
                return expoOutValue(x);
            case Kind::CircOut:
                return circOutValue(x);
            case Kind::Steps: {
                // The level index of the interval containing t, so the value holds
                // flat and jumps exactly at the boundaries.
                const int levels = stepCount_ > 0 ? stepCount_ : 1;
                if (levels <= 1) {
                    // A single level has no level to jump to, but the contract is
                    // still "starts at 0, lands on 1", so it is one jump at the end.
                    return x >= 1.f ? 1.f : 0.f;
                }
                const int index = static_cast<int>(std::floor(x * static_cast<float>(levels)));
                return static_cast<float>(std::clamp(index, 0, levels - 1)) /
                       static_cast<float>(levels - 1);
            }
            case Kind::CubicBezier:
                return bezierComponent(solveForS(x, x1_, x2_), y1_, y2_);
            case Kind::CubicInOut:
                return x < 0.5f ? 4.f * x * x * x
                                : 1.f - std::pow(-2.f * x + 2.f, 3.f) * 0.5f;
            case Kind::Linear:
                break;
        }
        return x;
    }

    float Easing::derivative(float t) const {

        const float x = std::clamp(t, 0.f, 1.f);

        switch (kind_) {
            case Kind::Linear:
                return 1.f;
            case Kind::ExpoOut:
                return expoOutSlope(x);
            case Kind::CircOut:
                return circOutSlope(x);
            case Kind::BounceOut: {
                // The bounce is a piecewise parabola; differentiate the same
                // branches the value uses so the two never disagree.
                constexpr float n1 = 7.5625f;
                constexpr float d1 = 2.75f;
                if (x < 1.f / d1) return 2.f * n1 * x;
                if (x < 2.f / d1) {
                    const float u = x - 1.5f / d1;
                    return 2.f * n1 * u;
                }
                if (x < 2.5f / d1) {
                    const float u = x - 2.25f / d1;
                    return 2.f * n1 * u;
                }
                const float u = x - 2.625f / d1;
                return 2.f * n1 * u;
            }
            case Kind::ElasticOut: {
                if (x <= 0.f || x >= 1.f) return 0.f;
                constexpr float c4 = math::TWO_PI / 3.f;
                const float a = std::pow(2.f, -10.f * x);
                return a * (-10.f * math::LN2 * std::sin((x * 10.f - 0.75f) * c4) +
                            10.f * c4 * std::cos((x * 10.f - 0.75f) * c4));
            }
            case Kind::Steps: {
                // A step function has no ordinary derivative: the value jumps at
                // each boundary and is flat between them. Returning 0 between
                // boundaries and a finite per-interval slope at one is misleading
                // either way, so the honest answer for a *velocity* consumer is
                // zero except exactly on a boundary, where the finite difference
                // is one level per unit of t. Documented on the header.
                const int levels = stepCount_ > 0 ? stepCount_ : 1;
                if (levels <= 1) return 0.f;
                const float level = x * static_cast<float>(levels);
                const float nearest = std::round(level);
                return std::fabs(level - nearest) < 1e-6f
                               ? static_cast<float>(levels - 1)
                               : 0.f;
            }
            case Kind::CubicBezier: {
                // Chain rule through x(t): the y-component of the Bezier,
                // differentiated, scaled by dx/dt of the inverse solve.
                const float s = solveForS(x, x1_, x2_);
                const float dy = bezierSlope(s, y1_, y2_);
                const float dx = bezierSlope(s, x1_, x2_);
                // A flat x segment has an infinite dx/dt; the honest value there
                // is 0, matching what `operator()` does (it pins the endpoints).
                if (std::fabs(dx) < 1e-6f) return 0.f;
                return dy / dx;
            }
            case Kind::CubicInOut:
                return x < 0.5f ? 12.f * x * x
                                : 3.f * (1.f - x) * (1.f - x) * 4.f;
        }
        return 0.f;
    }

    bool Easing::operator==(const Easing& other) const {

        return kind_ == other.kind_ && x1_ == other.x1_ && y1_ == other.y1_ &&
               x2_ == other.x2_ && y2_ == other.y2_ && stepCount_ == other.stepCount_;
    }

}// namespace chrononmotion::motion
