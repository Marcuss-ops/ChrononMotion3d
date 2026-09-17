// ChrononMotion test harness.
//
// No framework, no dependencies: the four motion test binaries share these
// counters and the three shapes of assertion they need (bool, scalar, vector).

#ifndef CHRONONMOTION_TEST_CHECK_HPP
#define CHRONONMOTION_TEST_CHECK_HPP

#include "threepp/math/Vector2.hpp"
#include "threepp/math/Vector3.hpp"

#include <cmath>
#include <cstdio>

namespace chrononmotion_test {

    inline int failures = 0;
    inline int checks = 0;

    inline void check(bool condition, const char* what) {

        ++checks;
        if (!condition) {
            ++failures;
            std::printf("  FAIL  %s\n", what);
        }
    }

    inline void checkNear(float actual, float expected, float epsilon, const char* what) {

        ++checks;
        if (!(std::fabs(actual - expected) <= epsilon)) {
            ++failures;
            std::printf("  FAIL  %s (got %g, expected %g)\n", what, actual, expected);
        }
    }

    inline void checkNear(const threepp::Vector3& actual, const threepp::Vector3& expected,
                          float epsilon, const char* what) {

        ++checks;
        if (std::fabs(actual.x - expected.x) > epsilon ||
            std::fabs(actual.y - expected.y) > epsilon ||
            std::fabs(actual.z - expected.z) > epsilon) {
            ++failures;
            std::printf("  FAIL  %s (got [%g %g %g], expected [%g %g %g])\n", what,
                        actual.x, actual.y, actual.z, expected.x, expected.y, expected.z);
        }
    }

    inline void checkNear(const threepp::Vector2& actual, const threepp::Vector2& expected,
                          float epsilon, const char* what) {

        ++checks;
        if (std::fabs(actual.x - expected.x) > epsilon ||
            std::fabs(actual.y - expected.y) > epsilon) {
            ++failures;
            std::printf("  FAIL  %s (got [%g %g], expected [%g %g])\n", what,
                        actual.x, actual.y, expected.x, expected.y);
        }
    }

    inline void section(const char* name) {

        std::printf("== %s\n", name);
    }

    inline int report() {

        std::printf("\n%d checks, %d failures\n", checks, failures);
        return failures == 0 ? 0 : 1;
    }

}// namespace chrononmotion_test

#endif//CHRONONMOTION_TEST_CHECK_HPP
