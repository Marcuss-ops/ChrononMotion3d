// https://github.com/mrdoob/three.js/blob/r129/src/extras/curves/CubicBezierCurve.js

#ifndef CHRONONMOTION_CUBICBEZIERCURVE_HPP
#define CHRONONMOTION_CUBICBEZIERCURVE_HPP

#include "chrononmotion/extras/core/Curve.hpp"

namespace chrononmotion {

    class CubicBezierCurve: public Curve2 {

    public:
        Vector2 v0;
        Vector2 v1;
        Vector2 v2;
        Vector2 v3;

        explicit CubicBezierCurve(const Vector2& v0 = {}, const Vector2& v1 = {}, const Vector2& v2 = {}, const Vector2& v3 = {});

        void getPoint(float t, Vector2& point) const override;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_CUBICBEZIERCURVE_HPP
