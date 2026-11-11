// https://github.com/mrdoob/three.js/blob/r129/src/geometries/TorusGeometry.js
//
// ChrononMotion: procedural torus geometry (ring/donut).

#ifndef CHRONONMOTION_TORUSGEOMETRY_HPP
#define CHRONONMOTION_TORUSGEOMETRY_HPP

#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/math/MathUtils.hpp"

#include <memory>
#include <string>

namespace chrononmotion {

    class TorusGeometry: public BufferGeometry {

    public:
        struct Params {
            float radius;
            float tube;
            unsigned int radialSegments;
            unsigned int tubularSegments;
            float arc;

            explicit Params(float radius = 1.f,
                            float tube = 0.4f,
                            unsigned int radialSegments = 20,
                            unsigned int tubularSegments = 64,
                            float arc = math::TWO_PI)
                : radius(radius), tube(tube),
                  radialSegments(radialSegments), tubularSegments(tubularSegments), arc(arc) {}
        };

        const Params parameters;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<TorusGeometry> create(const Params& params);

        static std::shared_ptr<TorusGeometry> create(
                float radius = 1.f,
                float tube = 0.4f,
                unsigned int radialSegments = 20,
                unsigned int tubularSegments = 64,
                float arc = math::TWO_PI);

    protected:
        explicit TorusGeometry(const Params& params);
    };

} // namespace chrononmotion

#endif // CHRONONMOTION_TORUSGEOMETRY_HPP
