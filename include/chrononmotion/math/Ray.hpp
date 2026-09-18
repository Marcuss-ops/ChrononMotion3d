#ifndef CHRONONMOTION_RAY_HPP
#define CHRONONMOTION_RAY_HPP

#include "chrononmotion/math/Vector3.hpp"

#include <optional>

namespace chrononmotion {

    class Box3;
    class Plane;
    class Sphere;
    class Matrix4;

    class Ray {
    public:
        Vector3 origin;
        Vector3 direction;

        explicit Ray(const Vector3& origin = Vector3(), const Vector3& direction = Vector3(0.f, 0.f, -1.f));

        Ray& set(const Vector3& origin, const Vector3& direction);
        Ray& copy(const Ray& ray);
        Vector3& at(float t, Vector3& target) const;
        Ray& lookAt(const Vector3& point);
        Ray& recast(float t);

        void closestPointToPoint(const Vector3& point, Vector3& target) const;
        [[nodiscard]] float distanceSqToPoint(const Vector3& point) const;
        [[nodiscard]] float distanceToPoint(const Vector3& point) const;

        [[nodiscard]] float distanceSqToSegment(const Vector3& start, const Vector3& end,
                                                Vector3* pointOnRay = nullptr,
                                                Vector3* pointOnSegment = nullptr) const;

        [[nodiscard]] bool intersectsSphere(const Sphere& sphere) const;
        void intersectSphere(const Sphere& sphere, Vector3& target) const;

        [[nodiscard]] float distanceToPlane(const Plane& plane) const;
        [[nodiscard]] bool intersectsPlane(const Plane& plane) const;
        void intersectPlane(const Plane& plane, Vector3& target) const;

        [[nodiscard]] bool intersectsBox(const Box3& box) const;
        void intersectBox(const Box3& box, Vector3& target) const;

        [[nodiscard]] std::optional<Vector3> intersectTriangle(const Vector3& a, const Vector3& b,
                                                                const Vector3& c, bool backfaceCulling) const;

        Ray& applyMatrix4(const Matrix4& matrix);
        [[nodiscard]] bool equals(const Ray& ray) const;
        [[nodiscard]] Ray clone() const;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_RAY_HPP
