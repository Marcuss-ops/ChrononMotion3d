// https://github.com/mrdoob/three.js/blob/r150/src/geometries/CapsuleGeometry.js
//
// ChrononMotion: procedural capsule/pill geometry.

#ifndef CHRONONMOTION_CAPSULEGEOMETRY_HPP
#define CHRONONMOTION_CAPSULEGEOMETRY_HPP

#include "chrononmotion/geometries/LatheGeometry.hpp"

#include <memory>
#include <string>

namespace chrononmotion {

    class CapsuleGeometry: public LatheGeometry {

    public:
        struct Params {
            float radius;
            float length;
            unsigned int capSegments;
            unsigned int radialSegments;

            explicit Params(float radius = 0.5f,
                            float length = 1.f,
                            unsigned int capSegments = 8,
                            unsigned int radialSegments = 16);
        };

        const Params parameters;

        const float radius;
        const float length;

        [[nodiscard]] std::string type() const override;

        static std::shared_ptr<CapsuleGeometry> create(const Params& params);

        static std::shared_ptr<CapsuleGeometry> create(
                float radius = 0.5f,
                float length = 1.f,
                unsigned int capSegments = 8,
                unsigned int radialSegments = 16);

    protected:
        explicit CapsuleGeometry(const Params& params);
    };

} // namespace chrononmotion

#endif // CHRONONMOTION_CAPSULEGEOMETRY_HPP
