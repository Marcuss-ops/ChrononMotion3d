// https://github.com/mrdoob/three.js/blob/r129/src/math/SphericalHarmonics3.js
//
// Nine real SH coefficients, evaluated as radiance or as cosine-convolved
// irradiance. This is pure math: no texture, no renderer, no environment map.
// It is the representation HDRI lighting probes and IBL-style descriptors use.

#ifndef CHRONONMOTION_SPHERICALHARMONICS3_HPP
#define CHRONONMOTION_SPHERICALHARMONICS3_HPP

#include "chrononmotion/math/Vector3.hpp"

#include <array>

namespace chrononmotion {

    class SphericalHarmonics3 {

    public:
        using Coefficients = std::array<Vector3, 9>;

        SphericalHarmonics3();
        explicit SphericalHarmonics3(const Coefficients& coefficients);

        SphericalHarmonics3& set(const Coefficients& coefficients);

        SphericalHarmonics3& zero();

        /// Radiance in the direction of the (unit-length) normal.
        void getAt(const Vector3& normal, Vector3& target) const;

        /// Irradiance (radiance convolved with the cosine lobe) in the
        /// direction of the (unit-length) normal.
        /// https://graphics.stanford.edu/papers/envmap/envmap.pdf
        void getIrradianceAt(const Vector3& normal, Vector3& target) const;

        SphericalHarmonics3& add(const SphericalHarmonics3& sh);

        SphericalHarmonics3& addScaled(const SphericalHarmonics3& sh, float scale);

        SphericalHarmonics3& scale(float scale);

        SphericalHarmonics3& lerp(const SphericalHarmonics3& sh, float alpha);

        [[nodiscard]] const Coefficients& coefficients() const;
        [[nodiscard]] Coefficients& coefficients();

    private:
        Coefficients coefficients_{};
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_SPHERICALHARMONICS3_HPP
