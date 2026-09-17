// https://github.com/mrdoob/three.js/blob/r129/src/extras/curves/EllipseCurve.js

#ifndef CHRONONMOTION_ELLIPSECURVE_HPP
#define CHRONONMOTION_ELLIPSECURVE_HPP

#include "chrononmotion/extras/core/Curve.hpp"

#include "chrononmotion/math/MathUtils.hpp"

namespace chrononmotion {

    class EllipseCurve: public Curve<Vector2> {

    public:
        float aX;
        float aY;

        float xRadius;
        float yRadius;

        float aStartAngle;
        float aEndAngle;

        bool aClockwise;

        float aRotation;

        explicit EllipseCurve(
                float aX = 0, float aY = 0,
                float xRadius = 1, float yRadius = 1,
                float aStartAngle = 0, float aEndAngle = math::TWO_PI,
                bool aClockwise = false, float aRotation = 0);

        void getPoint(float t, Vector2& target) const override;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_ELLIPSECURVE_HPP
