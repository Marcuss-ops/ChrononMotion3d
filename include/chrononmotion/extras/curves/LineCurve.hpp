// https://github.com/mrdoob/three.js/blob/r129/src/extras/curves/LineCurve.js

#ifndef CHRONONMOTION_LINECURVE_HPP
#define CHRONONMOTION_LINECURVE_HPP

#include "chrononmotion/extras/core/Curve.hpp"

namespace chrononmotion {

    template<class T>
    class LineCurveT: public Curve<T> {

    public:
        T v1;
        T v2;

        LineCurveT(const T& v1, const T& v2);

        void getPoint(float t, T& point) const override;

        void getPointAt(float u, T& target) const override;

        void getTangent(float t, T& tangent) const override;
    };

    typedef LineCurveT<Vector2> LineCurve;
    typedef LineCurveT<Vector3> LineCurve3;

    extern template class chrononmotion::LineCurveT<Vector2>;
    extern template class chrononmotion::LineCurveT<Vector3>;

}// namespace chrononmotion

#endif//CHRONONMOTION_LINECURVE_HPP
