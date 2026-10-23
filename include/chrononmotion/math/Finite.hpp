#ifndef CHRONONMOTION_MATH_FINITE_HPP
#define CHRONONMOTION_MATH_FINITE_HPP

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace chrononmotion::math {

template<std::size_t Size>
inline bool isFinite(const std::array<float, Size>& value) noexcept {
    return std::all_of(value.begin(), value.end(), [](float component) {
        return std::isfinite(component);
    });
}

inline bool isFinite(const Vector2& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

inline bool isFinite(const Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace chrononmotion::math

#endif // CHRONONMOTION_MATH_FINITE_HPP
