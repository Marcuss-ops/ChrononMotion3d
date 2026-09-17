// https://github.com/mrdoob/three.js/blob/dev/src/geometries/BoxGeometry.js
//
// ChrononMotion: re-imported with the geometry primitives. A box is six
// subdivided planes with one group per face (see `groups`), so a multi-material
// mesh keeps its "which index range uses which material" semantics without a
// material system.

#ifndef CHRONONMOTION_BOX_GEOMETRY_HPP
#define CHRONONMOTION_BOX_GEOMETRY_HPP

#include "chrononmotion/core/BufferGeometry.hpp"

namespace chrononmotion {

    class BoxGeometry: public BufferGeometry {

    public:
        struct Params {
            float width;
            float height;
            float depth;

            unsigned int widthSegments;
            unsigned int heightSegments;
            unsigned int depthSegments;

            explicit Params(float width = 1,
                            float height = 1,
                            float depth = 1,
                            unsigned int widthSegments = 1,
                            unsigned int heightSegments = 1,
                            unsigned int depthSegments = 1);
        };

        // Construction parameters, kept so the geometry can be re-created, diffed
        // or re-serialized in three.js' compact parametric form without reading
        // the vertex buffers back.
        const Params parameters;

        const float width;
        const float height;
        const float depth;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<BoxGeometry> create(const Params& params);

        static std::shared_ptr<BoxGeometry> create(const Vector3& size, unsigned int widthSegments, unsigned int heightSegments = 1, unsigned int depthSegments = 1);

        static std::shared_ptr<BoxGeometry> create(float width = 1, float height = 1, float depth = 1, unsigned int widthSegments = 1, unsigned int heightSegments = 1, unsigned int depthSegments = 1);

    protected:
        explicit BoxGeometry(const Params& params);
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_BOX_GEOMETRY_HPP
