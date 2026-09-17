
#ifndef CHRONONMOTION_LINEARINTERPOLANT_HPP
#define CHRONONMOTION_LINEARINTERPOLANT_HPP

#include "chrononmotion/math/Interpolant.hpp"

namespace chrononmotion {

    class LinearInterpolant: public Interpolant {

    public:
        template<typename... Args>
        explicit LinearInterpolant(Args&&... args): Interpolant(std::forward<Args>(args)...) {}

        Sample interpolate_(size_t i1, float t0, float t, float t1) override {
            const auto result = this->resultBuffer;
            const auto& values = this->sampleValues;
            const auto stride = this->valueSize;

            const auto offset1 = i1 * stride;
            const auto offset0 = offset1 - stride;

            const auto weight1 = (t - t0) / (t1 - t0);
            const auto weight0 = 1 - weight1;

            for (auto i = 0; i != stride; ++i) {

                result->at(i) =
                        values[offset0 + i] * weight0 +
                        values[offset1 + i] * weight1;
            }

            return *result;
        }
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_LINEARINTERPOLANT_HPP
