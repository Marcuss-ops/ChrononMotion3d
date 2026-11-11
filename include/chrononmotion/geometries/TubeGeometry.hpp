// https://github.com/mrdoob/three.js/blob/r129/src/geometries/TubeGeometry.js
//
// ChrononMotion: procedural tube geometry extruded along a 3D spline curve.

#ifndef CHRONONMOTION_TUBEGEOMETRY_HPP
#define CHRONONMOTION_TUBEGEOMETRY_HPP

#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/extras/core/Curve.hpp"
#include "chrononmotion/math/MathUtils.hpp"

#include <memory>
#include <string>
#include <variant>

namespace chrononmotion {

    class TubeGeometry: public BufferGeometry {

    public:
        struct Params {
            unsigned int tubularSegments;
            float radius;
            unsigned int radialSegments;
            bool closed;

            explicit Params(unsigned int tubularSegments = 64,
                            float radius = 1.f,
                            unsigned int radialSegments = 32,
                            bool closed = false);
        };

        const float radius;

        [[nodiscard]] std::string type() const override;

        [[nodiscard]] const FrenetFrames& getFrenetFrames() const {
            return frames_;
        }

        const Curve3* getPath() const {
            if (std::holds_alternative<Curve3*>(path_)) {
                return std::get<Curve3*>(path_);
            }
            return std::get<std::shared_ptr<Curve3>>(path_).get();
        }

        static std::shared_ptr<TubeGeometry> create(
                std::variant<Curve3*, std::shared_ptr<Curve3>> path,
                const Params& params);

        static std::shared_ptr<TubeGeometry> create(
                std::variant<Curve3*, std::shared_ptr<Curve3>> path,
                unsigned int tubularSegments = 64,
                float radius = 1.f,
                unsigned int radialSegments = 16,
                bool closed = false);

    private:
        FrenetFrames frames_;
        std::variant<Curve3*, std::shared_ptr<Curve3>> path_;

        TubeGeometry(std::variant<Curve3*, std::shared_ptr<Curve3>> path, const Params& params);
    };

} // namespace chrononmotion

#endif // CHRONONMOTION_TUBEGEOMETRY_HPP
