// Improved Perlin noise, ported from the upstream threepp math primitive.
// Dependency-free and a pure function of (x, y, z): same input, same output.

#ifndef CHRONONMOTION_IMPROVEDNOISE_HPP
#define CHRONONMOTION_IMPROVEDNOISE_HPP

namespace chrononmotion {

    class ImprovedNoise {

    public:
        [[nodiscard]] float noise(float x, float y, float z) const;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_IMPROVEDNOISE_HPP
