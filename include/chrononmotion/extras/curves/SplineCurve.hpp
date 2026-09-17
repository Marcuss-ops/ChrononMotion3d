// // https://github.com/mrdoob/three.js/blob/r129/src/extras/curves/SplineCurve.js

#ifndef CHRONONMOTION_SPLINECURVE_HPP
#define CHRONONMOTION_SPLINECURVE_HPP

#include "chrononmotion/extras/core/Curve.hpp"

namespace chrononmotion {

    class SplineCurve: public Curve2 {

    public:
        std::vector<Vector2> points;

        explicit SplineCurve(std::vector<Vector2> points = {});

        void getPoint(float t, Vector2& point) const override;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_SPLINECURVE_HPP
