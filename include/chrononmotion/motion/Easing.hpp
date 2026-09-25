// ChrononMotion — temporal easing.
//
// One authority for "how does a value travel between two keys". Every track —
// scalar, vector, quaternion — samples through the same Easing, so a preset
// cannot ease its opacity one way and its position another by accident.

#ifndef CHRONONMOTION_MOTION_EASING_HPP
#define CHRONONMOTION_MOTION_EASING_HPP

namespace chrononmotion::motion {

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
            ElasticOut,
            ExpoOut,
            CircOut,
            Steps
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

        /// Exponential ease-out: fast launch, long tail. The CSS `ease-out` shape,
        /// for a value that should feel instant and then settle without a bounce.
        [[nodiscard]] static Easing expoOut();

        /// Circular ease-out: a gentler tail than expoOut.
        [[nodiscard]] static Easing circOut();

        /// Stepped hold with `steps` discrete levels. The progress is the index of
        /// the level containing t, so the value holds flat between levels and jumps
        /// at the boundaries. `steps == 1` is a single jump at the end.
        [[nodiscard]] static Easing steps(int count);

        [[nodiscard]] float operator()(float t) const;
        [[nodiscard]] float ease(float t) const { return (*this)(t); }

        /// d/dt of the eased progress, per unit of the *input* t.
        ///
        /// This is the normalized derivative: it does NOT include the stretch of
        /// the segment, so a track segment must scale it by 1/segmentSeconds to get
        /// real units per second. `Track::velocity` does that. Endpoints of the
        /// piecewise curves are reported as the interior one-sided slope, except
        /// where a jump exists (Steps), which is reported as a finite slope between
        /// levels, not an infinite impulse — a delta has no single-point derivative
        /// and a NaN here would poison every consumer.
        [[nodiscard]] float derivative(float t) const;

        [[nodiscard]] Kind kind() const { return kind_; }
        [[nodiscard]] bool isLinear() const { return kind_ == Kind::Linear; }
        [[nodiscard]] float x1() const { return x1_; }
        [[nodiscard]] float y1() const { return y1_; }
        [[nodiscard]] float x2() const { return x2_; }
        [[nodiscard]] float y2() const { return y2_; }
        /// Number of levels for `Steps`; 0 for every other kind.
        [[nodiscard]] int stepCount() const { return stepCount_; }

        bool operator==(const Easing& other) const;
        bool operator!=(const Easing& other) const { return !(*this == other); }

    private:
        Easing(Kind kind, float x1, float y1, float x2, float y2, int stepCount = 0);

        Kind kind_{Kind::Linear};
        float x1_{0.f};
        float y1_{0.f};
        float x2_{1.f};
        float y2_{1.f};
        int stepCount_{0};
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_EASING_HPP
