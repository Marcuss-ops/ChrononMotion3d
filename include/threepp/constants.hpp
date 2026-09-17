// https://github.com/mrdoob/three.js/blob/r129/src/constants.js
//
// ChrononMotion: upstream's constants.hpp is the vocabulary of a renderer —
// blending modes, blend factors, depth functions, stencil operators, texture
// wrapping/filters/formats/types, tone mapping, shadow-map types, draw modes and
// GPU usage hints. None of those have a meaning here, so they were deleted with
// the renderer that read them. What a motion engine actually needs from this
// file is the animation vocabulary (interpolation, ending, blend mode), how a
// material faces (side) and how colours are tagged.

#ifndef THREEPP_CONSTANTS_HPP
#define THREEPP_CONSTANTS_HPP

namespace threepp {

    enum class Side {
        Front,
        Back,
        Double
    };

    enum class Interpolation {
        Discrete,
        Linear,
        Smooth
    };

    enum class Ending {
        ZeroCurvature,
        ZeroSlope,
        WrapAround
    };

    enum class AnimationBlendMode {
        Normal,
        Additive
    };

    // Color space tag for colour values, kept because `Color` carries one: it is
    // the difference between a colour written by a human (sRGB) and one a
    // compositor blends in (linear).
    // NoColorSpace (-1) means "raw data, no transform".
    enum class ColorSpace : int {
        NoColorSpace = -1,
        Linear = 3000,
        sRGB = 3001,
        Gamma = 3007,
        RGBE = 3002,
        LogLuv = 3003,
        RGBM7 = 3004,
        RGBM16 = 3005,
        RGBD = 3006
    };

    // three.js-style namespace constants for ergonomic use:
    //   color.setHex(0xff0000, SRGBColorSpace);
    inline constexpr auto NoColorSpace = ColorSpace::NoColorSpace;
    inline constexpr auto LinearSRGBColorSpace = ColorSpace::Linear;
    inline constexpr auto SRGBColorSpace = ColorSpace::sRGB;

}// namespace threepp

#endif//THREEPP_CONSTANTS_HPP
