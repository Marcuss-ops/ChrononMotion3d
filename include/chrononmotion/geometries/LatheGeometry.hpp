// https://github.com/mrdoob/three.js/blob/r129/src/geometries/LatheGeometry.js
//
// ChrononMotion: procedural lathe geometry (surface of revolution from 2D points).

#ifndef CHRONONMOTION_LATHEGEOMETRY_HPP
#define CHRONONMOTION_LATHEGEOMETRY_HPP

#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/math/Vector2.hpp"

#include <memory>
#include <string>
#include <vector>

namespace chrononmotion {

    class LatheGeometry: public BufferGeometry {

    public:
        struct Params {
            std::vector<Vector2> points;
            unsigned int segments;
            float phiStart;
            float phiLength;

            explicit Params(std::vector<Vector2> points = {},
                            unsigned int segments = 12,
                            float phiStart = 0.f,
                            float phiLength = math::TWO_PI)
                : points(std::move(points)), segments(segments), phiStart(phiStart), phiLength(phiLength) {}
        };

        const Params parameters;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<LatheGeometry> create(const Params& params);

        static std::shared_ptr<LatheGeometry> create(const std::vector<Vector2>& points,
                                                     unsigned int segments = 12,
                                                     float phiStart = 0.f,
                                                     float phiLength = math::TWO_PI);

    protected:
        explicit LatheGeometry(const std::vector<Vector2>& points,
                               unsigned int segments = 12,
                               float phiStart = 0.f,
                               float phiLength = math::TWO_PI);
    };

} // namespace chrononmotion

#endif // CHRONONMOTION_LATHEGEOMETRY_HPP
