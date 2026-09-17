// ChrononMotion — the layer transform.
//
// One type for every content kind. A text layer and an image layer are both a
// Transform, so position/anchor/scale/rotation/orientation/opacity cannot mean
// one thing for text and another for an image.

#ifndef CHRONONMOTION_MOTION_TRANSFORM_HPP
#define CHRONONMOTION_MOTION_TRANSFORM_HPP

#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector3.hpp"

namespace chrononmotion::motion {

    /// The full transform of a motion layer.
    ///
    /// Composition is the After Effects model:
    ///
    ///     local = T(position) * R * S(scale) * T(-anchor)
    ///     R     = orientation * eulerToQuaternion(rotation)
    ///
    /// so `anchor` is the single pivot for both rotation and scale, and a point
    /// `p` in layer space lands at `position + R * S * (p - anchor)`.
    struct Transform {
        /// Position XYZ, in the parent's space.
        Vector3 position{0.f, 0.f, 0.f};

        /// Anchor XYZ: the pivot, in the layer's own (pre-scale) space.
        Vector3 anchor{0.f, 0.f, 0.f};

        /// Scale XYZ.
        Vector3 scale{1.f, 1.f, 1.f};

        /// Rotation X/Y/Z as an XYZ Euler, in radians.
        Vector3 rotation{0.f, 0.f, 0.f};

        /// Orientation: the 3D rotation carried separately from `rotation`, so it
        /// is slerped as a quaternion and never flips through ±180°.
        Quaternion orientation{0.f, 0.f, 0.f, 1.f};

        /// Opacity, 0..1.
        float opacity{1.f};

        [[nodiscard]] static Transform identity();

        /// orientation * eulerToQuaternion(rotation), normalized.
        [[nodiscard]] Quaternion localQuaternion() const;
        void localQuaternion(Quaternion& out) const;

        /// local = T(position) * R * S(scale) * T(-anchor).
        Matrix4& compose(Matrix4& target) const;
        [[nodiscard]] Matrix4 toMatrix() const;

        /// Map a point from layer space to the parent's space.
        [[nodiscard]] Vector3 applyPoint(const Vector3& point) const;
        void applyPoint(const Vector3& point, Vector3& out) const;

        /// Mix two poses. Rotation takes the shortest arc (slerp with the
        /// antipodal sign fixed), so a blend can never travel the long way round.
        [[nodiscard]] static Transform blend(const Transform& a, const Transform& b, float t);

        [[nodiscard]] bool isIdentity() const;

        bool operator==(const Transform& other) const;
        bool operator!=(const Transform& other) const { return !(*this == other); }
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_TRANSFORM_HPP
