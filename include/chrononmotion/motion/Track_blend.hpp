// ChrononMotion — typed interpolation for motion-track values.
#ifndef CHRONONMOTION_MOTION_TRACK_BLEND_HPP
#define CHRONONMOTION_MOTION_TRACK_BLEND_HPP

#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/math/Color.hpp"

#include <cmath>

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

    // glTF CUBICSPLINE uses component-wise Hermite interpolation. Tangents are
    // derivatives per second and are scaled by the key interval duration.
    [[nodiscard]] inline float cubicSplineValue(float p0, float m0, float p1, float m1,
                                                 float span, float t) {
        const float t2 = t * t, t3 = t2 * t;
        return (2.f*t3 - 3.f*t2 + 1.f)*p0 + (t3 - 2.f*t2 + t)*span*m0 +
               (-2.f*t3 + 3.f*t2)*p1 + (t3 - t2)*span*m1;
    }
    [[nodiscard]] inline float cubicSplineDerivative(float p0, float m0, float p1, float m1,
                                                      float span, float t) {
        const float t2 = t * t;
        return ((6.f*t2 - 6.f*t)*p0 + (3.f*t2 - 4.f*t + 1.f)*span*m0 +
                (-6.f*t2 + 6.f*t)*p1 + (3.f*t2 - 2.f*t)*span*m1) / span;
    }
    [[nodiscard]] inline Vector3 cubicSplineValue(const Vector3& p0, const Vector3& m0,
                                                  const Vector3& p1, const Vector3& m1,
                                                  float span, float t) {
        return {cubicSplineValue(p0.x,m0.x,p1.x,m1.x,span,t),
                cubicSplineValue(p0.y,m0.y,p1.y,m1.y,span,t),
                cubicSplineValue(p0.z,m0.z,p1.z,m1.z,span,t)};
    }
    [[nodiscard]] inline Vector3 cubicSplineDerivative(const Vector3& p0, const Vector3& m0,
                                                        const Vector3& p1, const Vector3& m1,
                                                        float span, float t) {
        return {cubicSplineDerivative(p0.x,m0.x,p1.x,m1.x,span,t),
                cubicSplineDerivative(p0.y,m0.y,p1.y,m1.y,span,t),
                cubicSplineDerivative(p0.z,m0.z,p1.z,m1.z,span,t)};
    }
    [[nodiscard]] inline Quaternion cubicSplineValue(const Quaternion& p0, const Quaternion& m0,
                                                      const Quaternion& p1, const Quaternion& m1,
                                                      float span, float t) {
        Quaternion q{cubicSplineValue(p0.x,m0.x,p1.x,m1.x,span,t),
                     cubicSplineValue(p0.y,m0.y,p1.y,m1.y,span,t),
                     cubicSplineValue(p0.z,m0.z,p1.z,m1.z,span,t),
                     cubicSplineValue(p0.w,m0.w,p1.w,m1.w,span,t)};
        return q.normalize();
    }
    [[nodiscard]] inline Quaternion cubicSplineDerivative(const Quaternion& p0, const Quaternion& m0,
                                                            const Quaternion& p1, const Quaternion& m1,
                                                            float span, float t) {
        const float x = cubicSplineValue(p0.x,m0.x,p1.x,m1.x,span,t);
        const float y = cubicSplineValue(p0.y,m0.y,p1.y,m1.y,span,t);
        const float z = cubicSplineValue(p0.z,m0.z,p1.z,m1.z,span,t);
        const float w = cubicSplineValue(p0.w,m0.w,p1.w,m1.w,span,t);
        const float dx = cubicSplineDerivative(p0.x,m0.x,p1.x,m1.x,span,t);
        const float dy = cubicSplineDerivative(p0.y,m0.y,p1.y,m1.y,span,t);
        const float dz = cubicSplineDerivative(p0.z,m0.z,p1.z,m1.z,span,t);
        const float dw = cubicSplineDerivative(p0.w,m0.w,p1.w,m1.w,span,t);
        const float length = std::sqrt(x*x + y*y + z*z + w*w);
        if (!(length > 0.f)) return Quaternion(0.f, 0.f, 0.f, 0.f);
        const float nx=x/length, ny=y/length, nz=z/length, nw=w/length;
        const float projection=nx*dx+ny*dy+nz*dz+nw*dw;
        return Quaternion((dx-nx*projection)/length, (dy-ny*projection)/length,
                          (dz-nz*projection)/length, (dw-nw*projection)/length);
    }
    [[nodiscard]] inline Color cubicSplineValue(const Color& p0, const Color& m0,
                                                const Color& p1, const Color& m1,
                                                float span, float t) {
        Color color;
        color.r = cubicSplineValue(p0.r,m0.r,p1.r,m1.r,span,t);
        color.g = cubicSplineValue(p0.g,m0.g,p1.g,m1.g,span,t);
        color.b = cubicSplineValue(p0.b,m0.b,p1.b,m1.b,span,t);
        return color;
    }
    [[nodiscard]] inline Color cubicSplineDerivative(const Color& p0, const Color& m0,
                                                      const Color& p1, const Color& m1,
                                                      float span, float t) {
        Color color;
        color.r = cubicSplineDerivative(p0.r,m0.r,p1.r,m1.r,span,t);
        color.g = cubicSplineDerivative(p0.g,m0.g,p1.g,m1.g,span,t);
        color.b = cubicSplineDerivative(p0.b,m0.b,p1.b,m1.b,span,t);
        return color;
    }



} // namespace chrononmotion::motion

#endif // CHRONONMOTION_MOTION_TRACK_BLEND_HPP
