// ChrononMotion — typed interpolation for motion-track values.
#ifndef CHRONONMOTION_MOTION_TRACK_BLEND_HPP
#define CHRONONMOTION_MOTION_TRACK_BLEND_HPP

#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/math/Color.hpp"

namespace chrononmotion::motion {
    /// Blend two keyed values with the right geometry for their type.
    ///
    /// Quaternions slerp along the shortest arc (`slerpQuaternions` negates the
    /// antipodal operand), so an orientation track never takes the long way
    /// through ±180°.
    [[nodiscard]] inline float blendValue(float a, float b, float t) {

        return a + (b - a) * t;
    }

    [[nodiscard]] inline Vector3 blendValue(const Vector3& a, const Vector3& b, float t) {

        Vector3 out;
        out.lerpVectors(a, b, t);
        return out;
    }

    [[nodiscard]] inline Quaternion blendValue(const Quaternion& a, const Quaternion& b, float t) {

        Quaternion out;
        out.slerpQuaternions(a, b, t);
        return out;
    }

    [[nodiscard]] inline Color blendValue(const Color& a, const Color& b, float t) {

        Color out;
        out.lerpColors(a, b, t);
        return out;
    }



} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_TRACK_BLEND_HPP
