// https://github.com/mrdoob/three.js/blob/r129/src/geometries/PlaneGeometry.js
//
// ChrononMotion: re-imported with the geometry primitives. A plane faces +Z in
// the XY plane, which is the same orientation the 2.5D layer model places text
// and image planes in — so a `MotionPlane` and a `Mesh` carrying this geometry
// describe the same rectangle.

#ifndef CHRONONMOTION_PLANEGEOMETRY_HPP
#define CHRONONMOTION_PLANEGEOMETRY_HPP

#include "chrononmotion/core/BufferGeometry.hpp"

namespace chrononmotion {

    class PlaneGeometry: public BufferGeometry {

    public:
        struct Params {
            float width;
            float height;

            unsigned int widthSegments;
            unsigned int heightSegments;

            explicit Params(float width = 1,
                            float height = 1,
                            unsigned int widthSegments = 1,
                            unsigned int heightSegments = 1);
        };

        // Construction parameters, kept so the geometry can be re-created, diffed
        // or re-serialized in three.js' compact parametric form without reading
        // the vertex buffers back.
        const Params parameters;

        const float width;
        const float height;
        const unsigned int widthSegments;
        const unsigned int heightSegments;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<PlaneGeometry> create(const Params& params);

        static std::shared_ptr<PlaneGeometry> create(
                float width = 1,
                float height = 1,
                unsigned int widthSegments = 1,
                unsigned int heightSegments = 1);

    protected:
        explicit PlaneGeometry(const Params& params);
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_PLANEGEOMETRY_HPP
