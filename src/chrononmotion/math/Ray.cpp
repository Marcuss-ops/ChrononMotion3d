#include "chrononmotion/math/Ray.hpp"

#include "chrononmotion/math/Box3.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Plane.hpp"
#include "chrononmotion/math/Sphere.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace chrononmotion {

    namespace {
        constexpr float kEpsilon = 1e-8f;
        constexpr float kInfinity = std::numeric_limits<float>::infinity();
    }

    Ray::Ray(const Vector3& origin, const Vector3& direction)
        : origin(origin), direction(direction) {}

    Ray& Ray::set(const Vector3& origin, const Vector3& direction) {
        this->origin = origin;
        this->direction = direction;
        return *this;
    }

    Ray& Ray::copy(const Ray& ray) {
        return set(ray.origin, ray.direction);
    }

    Vector3& Ray::at(float t, Vector3& target) const {
        return target.copy(origin).addScaledVector(direction, t);
    }

    Ray& Ray::lookAt(const Vector3& point) {
        direction.copy(point).sub(origin).normalize();
        return *this;
    }

    Ray& Ray::recast(float t) {
        at(t, origin);
        return *this;
    }

    void Ray::closestPointToPoint(const Vector3& point, Vector3& target) const {
        const float directionLengthSq = direction.lengthSq();
        if (directionLengthSq <= kEpsilon) {
            target = origin;
            return;
        }
        const float t = Vector3(point).sub(origin).dot(direction) / directionLengthSq;
        target.copy(origin).addScaledVector(direction, std::max(0.f, t));
    }

    float Ray::distanceSqToPoint(const Vector3& point) const {
        const float directionLengthSq = direction.lengthSq();
        const Vector3 delta = Vector3(point).sub(origin);
        if (directionLengthSq <= kEpsilon) return delta.lengthSq();
        const float t = delta.dot(direction) / directionLengthSq;
        if (t <= 0.f) return delta.lengthSq();
        return Vector3(delta).addScaledVector(direction, -t).lengthSq();
    }

    float Ray::distanceToPoint(const Vector3& point) const {
        return std::sqrt(distanceSqToPoint(point));
    }

    float Ray::distanceSqToSegment(const Vector3& start, const Vector3& end,
                                   Vector3* pointOnRay, Vector3* pointOnSegment) const {
        const Vector3 segment = Vector3(end).sub(start);
        const float segmentLengthSq = segment.lengthSq();
        const float rayLengthSq = direction.lengthSq();

        if (rayLengthSq <= kEpsilon) {
            Vector3 closest = start;
            if (segmentLengthSq > kEpsilon) {
                const float t = std::clamp(Vector3(origin).sub(start).dot(segment) / segmentLengthSq, 0.f, 1.f);
                closest.copy(start).addScaledVector(segment, t);
            }
            if (pointOnRay) *pointOnRay = origin;
            if (pointOnSegment) *pointOnSegment = closest;
            return origin.distanceToSquared(closest);
        }

        if (segmentLengthSq <= kEpsilon) {
            if (pointOnRay) closestPointToPoint(start, *pointOnRay);
            if (pointOnSegment) *pointOnSegment = start;
            return distanceSqToPoint(start);
        }

        const Vector3 offset = Vector3(origin).sub(start);
        const float a = rayLengthSq;
        const float b = direction.dot(segment);
        const float c = segmentLengthSq;
        const float d = direction.dot(offset);
        const float e = segment.dot(offset);
        const float determinant = a * c - b * b;
        float rayT = 0.f;
        float segmentT = 0.f;

        if (determinant > kEpsilon) {
            rayT = (b * e - c * d) / determinant;
            segmentT = (a * e - b * d) / determinant;

            if (rayT < 0.f) {
                rayT = 0.f;
                segmentT = std::clamp(e / c, 0.f, 1.f);
            } else {
                segmentT = std::clamp(segmentT, 0.f, 1.f);
                if (segmentT == 0.f || segmentT == 1.f) {
                    const Vector3 endpoint = Vector3(start).addScaledVector(segment, segmentT);
                    rayT = std::max(0.f, direction.dot(Vector3(endpoint).sub(origin)) / a);
                }
            }
        } else {
            segmentT = std::clamp(e / c, 0.f, 1.f);
            const Vector3 closest = Vector3(start).addScaledVector(segment, segmentT);
            rayT = std::max(0.f, direction.dot(Vector3(closest).sub(origin)) / a);
        }

        Vector3 rayPoint;
        Vector3 segmentPoint;
        at(rayT, rayPoint);
        segmentPoint.copy(start).addScaledVector(segment, segmentT);
        if (pointOnRay) *pointOnRay = rayPoint;
        if (pointOnSegment) *pointOnSegment = segmentPoint;
        return rayPoint.distanceToSquared(segmentPoint);
    }

    bool Ray::intersectsSphere(const Sphere& sphere) const {
        return distanceSqToPoint(sphere.center) <= sphere.radius * sphere.radius;
    }

    void Ray::intersectSphere(const Sphere& sphere, Vector3& target) const {
        const float directionLengthSq = direction.lengthSq();
        if (directionLengthSq <= kEpsilon) {
            target.makeNan();
            return;
        }

        const Vector3 offset = Vector3(sphere.center).sub(origin);
        const float tca = offset.dot(direction) / directionLengthSq;
        const float perpendicularSq = offset.lengthSq() - tca * tca * directionLengthSq;
        const float radiusSq = sphere.radius * sphere.radius;
        if (perpendicularSq > radiusSq) {
            target.makeNan();
            return;
        }

        const float halfChord = std::sqrt(std::max(0.f, radiusSq - perpendicularSq) / directionLengthSq);
        const float nearT = tca - halfChord;
        const float farT = tca + halfChord;
        if (farT < 0.f) {
            target.makeNan();
        } else {
            at(std::max(0.f, nearT), target);
        }
    }

    float Ray::distanceToPlane(const Plane& plane) const {
        const float denominator = plane.normal.dot(direction);
        if (std::abs(denominator) <= kEpsilon) {
            return std::abs(plane.distanceToPoint(origin)) <= kEpsilon ? 0.f : kInfinity;
        }
        const float t = -(origin.dot(plane.normal) + plane.constant) / denominator;
        return t < 0.f ? kInfinity : t;
    }

    bool Ray::intersectsPlane(const Plane& plane) const {
        return std::isfinite(distanceToPlane(plane));
    }

    void Ray::intersectPlane(const Plane& plane, Vector3& target) const {
        const float denominator = plane.normal.dot(direction);
        if (std::abs(denominator) <= kEpsilon) {
            if (std::abs(plane.distanceToPoint(origin)) <= kEpsilon) target = origin;
            else target.makeNan();
            return;
        }
        const float t = -(origin.dot(plane.normal) + plane.constant) / denominator;
        if (t < 0.f) target.makeNan();
        else at(t, target);
    }

    bool Ray::intersectsBox(const Box3& box) const {
        Vector3 target;
        intersectBox(box, target);
        return !target.isNan();
    }

    void Ray::intersectBox(const Box3& box, Vector3& target) const {
        if (box.isEmpty()) {
            target.makeNan();
            return;
        }

        float nearT = 0.f;
        float farT = kInfinity;
        const float origins[3] = {origin.x, origin.y, origin.z};
        const float directions[3] = {direction.x, direction.y, direction.z};
        const float minimums[3] = {box.min().x, box.min().y, box.min().z};
        const float maximums[3] = {box.max().x, box.max().y, box.max().z};

        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(directions[axis]) <= kEpsilon) {
                if (origins[axis] < minimums[axis] || origins[axis] > maximums[axis]) {
                    target.makeNan();
                    return;
                }
                continue;
            }

            float axisNear = (minimums[axis] - origins[axis]) / directions[axis];
            float axisFar = (maximums[axis] - origins[axis]) / directions[axis];
            if (axisNear > axisFar) std::swap(axisNear, axisFar);
            nearT = std::max(nearT, axisNear);
            farT = std::min(farT, axisFar);
            if (nearT > farT) {
                target.makeNan();
                return;
            }
        }

        at(nearT, target);
    }

    std::optional<Vector3> Ray::intersectTriangle(const Vector3& a, const Vector3& b,
                                                  const Vector3& c, bool backfaceCulling) const {
        const Vector3 edge1 = Vector3(b).sub(a);
        const Vector3 edge2 = Vector3(c).sub(a);
        Vector3 pVector;
        pVector.crossVectors(direction, edge2);
        const float determinant = edge1.dot(pVector);

        if (backfaceCulling) {
            if (determinant <= kEpsilon) return std::nullopt;
        } else if (std::abs(determinant) <= kEpsilon) {
            return std::nullopt;
        }

        const float inverseDeterminant = 1.f / determinant;
        const Vector3 tVector = Vector3(origin).sub(a);
        const float u = tVector.dot(pVector) * inverseDeterminant;
        if (u < 0.f || u > 1.f) return std::nullopt;

        Vector3 qVector;
        qVector.crossVectors(tVector, edge1);
        const float v = direction.dot(qVector) * inverseDeterminant;
        if (v < 0.f || u + v > 1.f) return std::nullopt;

        const float distance = edge2.dot(qVector) * inverseDeterminant;
        if (distance < 0.f) return std::nullopt;

        Vector3 result;
        at(distance, result);
        return result;
    }

    Ray& Ray::applyMatrix4(const Matrix4& matrix) {
        Vector3 end;
        at(1.f, end);
        origin.applyMatrix4(matrix);
        end.applyMatrix4(matrix);
        direction.copy(end).sub(origin).normalize();
        return *this;
    }

    bool Ray::equals(const Ray& ray) const {
        return origin == ray.origin && direction == ray.direction;
    }

    Ray Ray::clone() const {
        return Ray(origin, direction);
    }

}// namespace chrononmotion
