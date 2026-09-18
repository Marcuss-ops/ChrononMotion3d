#include "chrononmotion/math/Frustum.hpp"

#include "chrononmotion/math/Box3.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Sphere.hpp"

namespace chrononmotion {

    Frustum::Frustum(Plane p0, Plane p1, Plane p2, Plane p3, Plane p4, Plane p5)
        : planes_{p0, p1, p2, p3, p4, p5} {}

    Frustum& Frustum::set(const Plane& p0, const Plane& p1, const Plane& p2,
                          const Plane& p3, const Plane& p4, const Plane& p5) {
        planes_[0] = p0;
        planes_[1] = p1;
        planes_[2] = p2;
        planes_[3] = p3;
        planes_[4] = p4;
        planes_[5] = p5;
        return *this;
    }

    Frustum& Frustum::copy(const Frustum& frustum) {
        planes_ = frustum.planes_;
        return *this;
    }

    Frustum& Frustum::setFromProjectionMatrix(const Matrix4& matrix) {
        const auto& e = matrix.elements;
        planes_[0].setComponents(e[3] - e[0], e[7] - e[4], e[11] - e[8], e[15] - e[12]).normalize();
        planes_[1].setComponents(e[3] + e[0], e[7] + e[4], e[11] + e[8], e[15] + e[12]).normalize();
        planes_[2].setComponents(e[3] + e[1], e[7] + e[5], e[11] + e[9], e[15] + e[13]).normalize();
        planes_[3].setComponents(e[3] - e[1], e[7] - e[5], e[11] - e[9], e[15] - e[13]).normalize();
        planes_[4].setComponents(e[3] - e[2], e[7] - e[6], e[11] - e[10], e[15] - e[14]).normalize();
        planes_[5].setComponents(e[3] + e[2], e[7] + e[6], e[11] + e[10], e[15] + e[14]).normalize();
        return *this;
    }

    bool Frustum::intersectsSphere(const Sphere& sphere) const {
        const float negativeRadius = -sphere.radius;
        for (const auto& plane : planes_) {
            if (plane.distanceToPoint(sphere.center) < negativeRadius) return false;
        }
        return true;
    }

    bool Frustum::intersectsBox(const Box3& box) const {
        for (const auto& plane : planes_) {
            const Vector3 positive(
                    plane.normal.x >= 0.f ? box.max().x : box.min().x,
                    plane.normal.y >= 0.f ? box.max().y : box.min().y,
                    plane.normal.z >= 0.f ? box.max().z : box.min().z);
            if (plane.distanceToPoint(positive) < 0.f) return false;
        }
        return true;
    }

    bool Frustum::containsPoint(const Vector3& point) const {
        for (const auto& plane : planes_) {
            if (plane.distanceToPoint(point) < 0.f) return false;
        }
        return true;
    }

    const std::array<Plane, 6>& Frustum::planes() const {
        return planes_;
    }

}// namespace chrononmotion
