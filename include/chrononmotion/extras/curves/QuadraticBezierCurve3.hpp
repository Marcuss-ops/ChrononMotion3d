// https://github.com/mrdoob/three.js/blob/r129/src/extras/curves/QuadraticBezierCurve3.js

#ifndef CHRONONMOTION_QUADRATICBEZIERCURVE3_HPP
#define CHRONONMOTION_QUADRATICBEZIERCURVE3_HPP

#include "chrononmotion/extras/core/Curve.hpp"

namespace chrononmotion {

    class QuadraticBezierCurve3: public Curve3 {

    public:
        Vector3 v0;
        Vector3 v1;
        Vector3 v2;

        explicit QuadraticBezierCurve3(const Vector3& v0 = {}, const Vector3& v1 = {}, const Vector3& v2 = {});

        void getPoint(float t, Vector3& point) const override;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_QUADRATICBEZIERCURVE3_HPP
