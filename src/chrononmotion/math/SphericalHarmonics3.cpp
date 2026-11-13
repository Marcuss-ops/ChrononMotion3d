#include "chrononmotion/math/SphericalHarmonics3.hpp"

namespace chrononmotion {

    SphericalHarmonics3::SphericalHarmonics3() = default;

    SphericalHarmonics3::SphericalHarmonics3(const Coefficients& coefficients)
        : coefficients_(coefficients) {}

    SphericalHarmonics3& SphericalHarmonics3::set(const Coefficients& coefficients) {

        coefficients_ = coefficients;
        return *this;
    }

    SphericalHarmonics3& SphericalHarmonics3::zero() {

        for (auto& coefficient : coefficients_) coefficient.set(0.f, 0.f, 0.f);
        return *this;
    }

    void SphericalHarmonics3::getAt(const Vector3& normal, Vector3& target) const {

        const float x = normal.x, y = normal.y, z = normal.z;
        const auto& c = coefficients_;

        // band 0
        target.copy(c[0]).multiplyScalar(0.282095f);

        // band 1
        target.addScaledVector(c[1], 0.488603f * y);
        target.addScaledVector(c[2], 0.488603f * z);
        target.addScaledVector(c[3], 0.488603f * x);

        // band 2
        target.addScaledVector(c[4], 1.092548f * (x * y));
        target.addScaledVector(c[5], 1.092548f * (y * z));
        target.addScaledVector(c[6], 0.315392f * (3.f * z * z - 1.f));
        target.addScaledVector(c[7], 1.092548f * (x * z));
        target.addScaledVector(c[8], 0.546274f * (x * x - y * y));
    }

    void SphericalHarmonics3::getIrradianceAt(const Vector3& normal, Vector3& target) const {

        const float x = normal.x, y = normal.y, z = normal.z;
        const auto& c = coefficients_;

        // band 0
        target.copy(c[0]).multiplyScalar(0.886227f);// pi * 0.282095

        // band 1
        target.addScaledVector(c[1], 2.f * 0.511664f * y);// (2 * pi / 3) * 0.488603
        target.addScaledVector(c[2], 2.f * 0.511664f * z);
        target.addScaledVector(c[3], 2.f * 0.511664f * x);

        // band 2
        target.addScaledVector(c[4], 2.f * 0.429043f * x * y);// (pi / 4) * 1.092548
        target.addScaledVector(c[5], 2.f * 0.429043f * y * z);
        target.addScaledVector(c[6], 0.743125f * z * z - 0.247708f);// (pi / 4) * 0.315392 * 3
        target.addScaledVector(c[7], 2.f * 0.429043f * x * z);
        target.addScaledVector(c[8], 0.429043f * (x * x - y * y));// (pi / 4) * 0.546274
    }

    SphericalHarmonics3& SphericalHarmonics3::add(const SphericalHarmonics3& sh) {

        for (std::size_t i = 0; i < coefficients_.size(); ++i) {
            coefficients_[i].add(sh.coefficients_[i]);
        }
        return *this;
    }

    SphericalHarmonics3& SphericalHarmonics3::addScaled(const SphericalHarmonics3& sh, float scale) {

        for (std::size_t i = 0; i < coefficients_.size(); ++i) {
            coefficients_[i].addScaledVector(sh.coefficients_[i], scale);
        }
        return *this;
    }

    SphericalHarmonics3& SphericalHarmonics3::scale(float scale) {

        for (auto& coefficient : coefficients_) coefficient.multiplyScalar(scale);
        return *this;
    }

    SphericalHarmonics3& SphericalHarmonics3::lerp(const SphericalHarmonics3& sh, float alpha) {

        for (std::size_t i = 0; i < coefficients_.size(); ++i) {
            coefficients_[i].lerp(sh.coefficients_[i], alpha);
        }
        return *this;
    }

    const SphericalHarmonics3::Coefficients& SphericalHarmonics3::coefficients() const {
        return coefficients_;
    }

    SphericalHarmonics3::Coefficients& SphericalHarmonics3::coefficients() {
        return coefficients_;
    }

}// namespace chrononmotion
