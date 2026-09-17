// ChrononMotion — temporal easing.
//
// One authority for "how does a value travel between two keys". Every track —
// scalar, vector, quaternion — samples through the same Easing, so a preset
// cannot ease its opacity one way and its position another by accident.

#ifndef THREEPP_MOTION_EASING_HPP
#define THREEPP_MOTION_EASING_HPP

namespace threepp::motion {

    /// A normalized timing curve: t in [0,1] -> eased progress.
    ///
    /// A cubic Bézier timing function with the fixed endpoints P0=(0,0) and
    /// P3=(1,1); only the two control points vary. `y` may leave [0,1] — that is
    /// how `overshoot()` produces a pop that passes its target and settles back —
    /// while `x` is clamped to [0,1] so the curve stays a function of time.
    ///
    /// The curve is stateless: the same t always yields the same progress, in any
    /// call order, which is what makes frame→value evaluation reproducible.
    class Easing {
    public:
        enum class Kind {
            Linear,
            CubicBezier,
            BounceOut,
            ElasticOut
        };

        Easing() = default;

        [[nodiscard]] static Easing linear();
        [[nodiscard]] static Easing easeIn();
        [[nodiscard]] static Easing easeOut();
        [[nodiscard]] static Easing easeInOut();
        [[nodiscard]] static Easing cubicBezier(float x1, float y1, float x2, float y2);

        /// Ease-out that travels past the target and settles: control y = 1 + amount.
        /// `amount == 0` is easeOut-ish; the default 0.56 is the classic back-out.
        [[nodiscard]] static Easing overshoot(float amount = 0.56f);
        [[nodiscard]] static Easing bounceOut();
        [[nodiscard]] static Easing elasticOut();

        [[nodiscard]] float operator()(float t) const;
        [[nodiscard]] float ease(float t) const { return (*this)(t); }

        [[nodiscard]] Kind kind() const { return kind_; }
        [[nodiscard]] bool isLinear() const { return kind_ == Kind::Linear; }
        [[nodiscard]] float x1() const { return x1_; }
        [[nodiscard]] float y1() const { return y1_; }
        [[nodiscard]] float x2() const { return x2_; }
        [[nodiscard]] float y2() const { return y2_; }

        bool operator==(const Easing& other) const;
        bool operator!=(const Easing& other) const { return !(*this == other); }

    private:
        Easing(Kind kind, float x1, float y1, float x2, float y2);

        Kind kind_{Kind::Linear};
        float x1_{0.f};
        float y1_{0.f};
        float x2_{1.f};
        float y2_{1.f};
    };

}// namespace threepp::motion

#endif//THREEPP_MOTION_EASING_HPP
