#include "chrononmotion/motion/MotionPath.hpp"

#include "chrononmotion/extras/curves/CatmullRomCurve3.hpp"
#include "chrononmotion/extras/curves/CubicBezierCurve3.hpp"
#include "chrononmotion/extras/curves/LineCurve.hpp"
#include "chrononmotion/extras/curves/QuadraticBezierCurve3.hpp"

#include <utility>

namespace chrononmotion::motion {

    MotionPath::MotionPath(std::shared_ptr<Curve3> curve, bool closed)
        : curve_(std::move(curve)), closed_(closed) {}

    MotionPath MotionPath::line(const Vector3& from, const Vector3& to) {

        return MotionPath(std::make_shared<LineCurve3>(from, to));
    }

    MotionPath MotionPath::cubicBezier(const Vector3& from,
                                       const Vector3& control1,
                                       const Vector3& control2,
                                       const Vector3& to) {

        return MotionPath(std::make_shared<CubicBezierCurve3>(from, control1, control2, to));
    }

    MotionPath MotionPath::quadraticBezier(const Vector3& from,
                                           const Vector3& control,
                                           const Vector3& to) {

        return MotionPath(std::make_shared<QuadraticBezierCurve3>(from, control, to));
    }

    MotionPath MotionPath::catmullRom(std::vector<Vector3> points, bool closed, float tension) {

        auto curve = std::make_shared<CatmullRomCurve3>(std::move(points), closed,
                                                       CatmullRomCurve3::catmullrom, tension);
        return MotionPath(std::move(curve), closed);
    }

    void MotionPath::sample(float t, Vector3& out) const {

        curve_->getPoint(t, out);
    }

    Vector3 MotionPath::sample(float t) const {

        Vector3 out;
        curve_->getPoint(t, out);
        return out;
    }

    void MotionPath::sampleUniform(float u, Vector3& out) const {

        curve_->getPointAt(u, out);
    }

    Vector3 MotionPath::sampleUniform(float u) const {

        Vector3 out;
        curve_->getPointAt(u, out);
        return out;
    }

    void MotionPath::tangent(float u, Vector3& out) const {

        curve_->getTangentAt(u, out);
        out.normalize();
    }

    Vector3 MotionPath::tangent(float u) const {

        Vector3 out;
        tangent(u, out);
        return out;
    }

    float MotionPath::length() const {

        return curve_->getLength();
    }

    std::vector<Vector3> MotionPath::points(unsigned int divisions) const {

        return curve_->getPoints(divisions);
    }

    Quaternion MotionPath::directionToQuaternion(const Vector3& direction) {

        Quaternion out{0.f, 0.f, 0.f, 1.f};

        if (direction.lengthSq() < 1e-12f) return out;

        Vector3 normalized = direction;
        normalized.normalize();

        // local +Z -> direction, shortest arc (setFromUnitVectors handles the
        // antipodal case explicitly instead of degenerating).
        out.setFromUnitVectors(Vector3(0.f, 0.f, 1.f), normalized);
        return out;
    }

    void MotionPath::orientToPath(float u, Quaternion& out) const {

        Vector3 direction;
        tangent(u, direction);
        out = directionToQuaternion(direction);
    }

    Quaternion MotionPath::orientToPath(float u) const {

        Quaternion out;
        orientToPath(u, out);
        return out;
    }

}// namespace chrononmotion::motion
