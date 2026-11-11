// https://github.com/mrdoob/three.js/blob/r129/src/geometries/CylinderGeometry.js
//
// ChrononMotion: procedural cylinder/cone geometry. Generates buffer attributes
// (position, normal, uv) and multi-material groups for torso, top cap, and bottom cap.

#ifndef CHRONONMOTION_CYLINDERGEOMETRY_HPP
#define CHRONONMOTION_CYLINDERGEOMETRY_HPP

#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/math/MathUtils.hpp"

#include <memory>
#include <string>

namespace chrononmotion {

    class CylinderGeometry: public BufferGeometry {

    public:
        struct Params {
            float radiusTop;
            float radiusBottom;
            float height;
            unsigned int radialSegments;
            unsigned int heightSegments;
            bool openEnded;
            float thetaStart;
            float thetaLength;

            explicit Params(float radiusTop = 1.f,
                            float radiusBottom = 1.f,
                            float height = 1.f,
                            unsigned int radialSegments = 16,
                            unsigned int heightSegments = 1,
                            bool openEnded = false,
                            float thetaStart = 0.f,
                            float thetaLength = math::TWO_PI);
        };

        const Params parameters;

        const float radiusTop;
        const float radiusBottom;
        const float height;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<CylinderGeometry> create(const Params& params);

        static std::shared_ptr<CylinderGeometry> create(
                float radiusTop = 1.f,
                float radiusBottom = 1.f,
                float height = 1.f,
                unsigned int radialSegments = 16,
                unsigned int heightSegments = 1,
                bool openEnded = false,
                float thetaStart = 0.f,
                float thetaLength = math::TWO_PI);

    protected:
        explicit CylinderGeometry(const Params& params);
    };

} // namespace chrononmotion

#endif // CHRONONMOTION_CYLINDERGEOMETRY_HPP
