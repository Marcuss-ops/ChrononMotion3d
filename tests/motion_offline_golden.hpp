#ifndef CHRONONMOTION_TESTS_MOTION_OFFLINE_GOLDEN_HPP
#define CHRONONMOTION_TESTS_MOTION_OFFLINE_GOLDEN_HPP

#include "chrononmotion/motion/OfflineBaking.hpp"

#include <cmath>
#include <cstdio>
#include <string_view>

namespace chrononmotion_test::offline_golden {

inline int failures = 0;
inline int checks = 0;

inline void check(bool actual, const char* label) {
    ++checks;
    if (!actual) {
        ++failures;
        std::printf("  FAIL  %s\n", label);
    }
}

inline void near(float actual, float expected, float tolerance, const char* label) {
    check(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, label);
    if (failures && (!std::isfinite(actual) || std::abs(actual - expected) > tolerance))
        std::printf("        actual=%.9g expected=%.9g tolerance=%.3g\n", actual, expected, tolerance);
}

inline int report() {
    std::printf("\n%d golden checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

} // namespace chrononmotion_test::offline_golden

#endif
