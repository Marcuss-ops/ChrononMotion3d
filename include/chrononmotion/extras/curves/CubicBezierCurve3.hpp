// https://github.com/mrdoob/three.js/blob/r129/src/extras/curves/CubicBezierCurve.js

#ifndef CHRONONMOTION_CUBICBEZIERCURVE3_HPP
#define CHRONONMOTION_CUBICBEZIERCURVE3_HPP

#include "chrononmotion/extras/core/Curve.hpp"

namespace chrononmotion {

    class CubicBezierCurve3: public Curve3 {

    public:
        Vector3 v0;
        Vector3 v1;
        Vector3 v2;
        Vector3 v3;

        explicit CubicBezierCurve3(const Vector3& v0 = {}, const Vector3& v1 = {}, const Vector3& v2 = {}, const Vector3& v3 = {});

        void getPoint(float t, Vector3& point) const override;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_CUBICBEZIERCURVE3_HPP
