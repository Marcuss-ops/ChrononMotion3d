// https://github.com/mrdoob/three.js/blob/r129/src/geometries/SphereGeometry.js
//
// ChrononMotion: re-imported with the geometry primitives. The one primitive
// whose vertices are generated in three dimensions, so it is what pins the
// bounding-sphere and `normalizeNormals` contracts of `BufferGeometry`.

#ifndef CHRONONMOTION_SPHEREGEOMETRY_HPP
#define CHRONONMOTION_SPHEREGEOMETRY_HPP

#include "chrononmotion/math/MathUtils.hpp"

#include "chrononmotion/core/BufferGeometry.hpp"

#include <memory>

namespace chrononmotion {

    class SphereGeometry: public BufferGeometry {

    public:
        struct Params {
            float radius;
            unsigned int widthSegments;
            unsigned int heightSegments;
            float phiStart;
            float phiLength;
            float thetaStart;
            float thetaLength;

            explicit Params(float radius = 1,
                            unsigned int widthSegments = 16,
                            unsigned int heightSegments = 12,
                            float phiStart = 0,
                            float phiLength = math::TWO_PI,
                            float thetaStart = 0,
                            float thetaLength = math::PI);
        };

        // Construction parameters, kept so the geometry can be re-created, diffed
        // or re-serialized in three.js' compact parametric form without reading
        // the vertex buffers back.
        const Params parameters;

        const float radius;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<SphereGeometry> create(const Params& params);

        static std::shared_ptr<SphereGeometry> create(
                float radius = 1,
                unsigned int widthSegments = 16,
                unsigned int heightSegments = 12,
                float phiStart = 0,
                float phiLength = math::TWO_PI,
                float thetaStart = 0,
                float thetaLength = math::PI);

    protected:
        explicit SphereGeometry(const Params& params);
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_SPHEREGEOMETRY_HPP
