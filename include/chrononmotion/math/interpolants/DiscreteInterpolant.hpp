
#ifndef CHRONONMOTION_DISCRETEINTERPOLANT_HPP
#define CHRONONMOTION_DISCRETEINTERPOLANT_HPP

#include "chrononmotion/math/Interpolant.hpp"

namespace chrononmotion {

    class DiscreteInterpolant: public Interpolant {

    public:
        template<typename... Args>
        explicit DiscreteInterpolant(Args&&... args): Interpolant(std::forward<Args>(args)...) {}

        Sample interpolate_(size_t i1, [[maybe_unused]] float t0, [[maybe_unused]] float t, [[maybe_unused]] float t1) override {

            return copySampleValue_(i1-1);
        }
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_DISCRETEINTERPOLANT_HPP
