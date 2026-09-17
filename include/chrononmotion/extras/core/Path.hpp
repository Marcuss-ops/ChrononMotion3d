// https://github.com/mrdoob/three.js/blob/r129/src/extras/core/Path.js

#ifndef CHRONONMOTION_PATH_HPP
#define CHRONONMOTION_PATH_HPP

#include "chrononmotion/extras/core/CurvePath.hpp"

namespace chrononmotion {

    class Path: public CurvePath<Vector2> {

    public:
        Vector2 currentPoint;

        explicit Path(const std::vector<Vector2>& points = {});

        Path& setFromPoints(const std::vector<Vector2>& points);

        Path& moveTo(float x, float y);

        Path& lineTo(float x, float y);

        Path& quadraticCurveTo(float aCPx, float aCPy, float aX, float aY);

        Path& bezierCurveTo(float aCP1x, float aCP1y, float aCP2x, float aCP2y, float aX, float aY);

        Path& splineThru(const std::vector<Vector2>& pts);

        Path& arc(float aX, float aY, float aRadius, float aStartAngle, float aEndAngle, bool aClockwise);

        Path& absarc(float aX, float aY, float aRadius, float aStartAngle, float aEndAngle, bool aClockwise = false);

        Path& absellipse(float aX, float aY, float xRadius, float yRadius, float aStartAngle, float aEndAngle, bool aClockwise = false, float aRotation = 0);
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_PATH_HPP
