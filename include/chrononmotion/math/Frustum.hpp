#ifndef CHRONONMOTION_FRUSTUM_HPP
#define CHRONONMOTION_FRUSTUM_HPP

#include "chrononmotion/math/Plane.hpp"

#include <array>

namespace chrononmotion {

    class Box3;
    class Sphere;
    class Matrix4;

    class Frustum {
    public:
        explicit Frustum(Plane p0 = Plane(), Plane p1 = Plane(), Plane p2 = Plane(),
                         Plane p3 = Plane(), Plane p4 = Plane(), Plane p5 = Plane());

        Frustum& set(const Plane& p0, const Plane& p1, const Plane& p2,
                     const Plane& p3, const Plane& p4, const Plane& p5);
        Frustum& copy(const Frustum& frustum);
        Frustum& setFromProjectionMatrix(const Matrix4& matrix);

        [[nodiscard]] bool intersectsSphere(const Sphere& sphere) const;
        [[nodiscard]] bool intersectsBox(const Box3& box) const;
        [[nodiscard]] bool containsPoint(const Vector3& point) const;
        [[nodiscard]] const std::array<Plane, 6>& planes() const;

    private:
        std::array<Plane, 6> planes_;
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_FRUSTUM_HPP
