#include "chrononmotion/motion/Transform.hpp"

#include "chrononmotion/math/Euler.hpp"

namespace chrononmotion::motion {

    Transform Transform::identity() {

        return Transform{};
    }

    Quaternion Transform::localQuaternion() const {

        Quaternion out;
        localQuaternion(out);
        return out;
    }

    void Transform::localQuaternion(Quaternion& out) const {

        if (rotation.x == 0.f && rotation.y == 0.f && rotation.z == 0.f) {
            // No Euler rotation: the local quaternion is the orientation alone.
            // setFromEuler(0,0,0) is exactly the identity quaternion and
            // q * identity == q term-for-term, so skipping it skips six trig
            // calls per layer per frame and changes no bit.
            out.copy(orientation);
            out.normalize();
            return;
        }

        Euler euler(rotation.x, rotation.y, rotation.z);
        Quaternion eulerRotation;
        eulerRotation.setFromEuler(euler);

        out.multiplyQuaternions(orientation, eulerRotation);
        out.normalize();
    }

    Matrix4& Transform::compose(Matrix4& target) const {

        const Quaternion rotationQuaternion = localQuaternion();
        target.compose(position, rotationQuaternion, scale);

        if (anchor.x != 0.f || anchor.y != 0.f || anchor.z != 0.f) {
            // The anchor is T(-anchor) on the right of the product, and
            // multiplying by a pure translation only moves the translation
            // column (elements 12..14, column-strided). Folding it here is the
            // same arithmetic, in the same order, as target * T(-anchor) —
            // without the 4x4 temporary and the full 16-element multiply.
            const float ax = -anchor.x;
            const float ay = -anchor.y;
            const float az = -anchor.z;
            target.elements[12] += target.elements[0] * ax + target.elements[4] * ay + target.elements[8] * az;
            target.elements[13] += target.elements[1] * ax + target.elements[5] * ay + target.elements[9] * az;
            target.elements[14] += target.elements[2] * ax + target.elements[6] * ay + target.elements[10] * az;
        }

        return target;
    }

    Matrix4 Transform::toMatrix() const {

        Matrix4 matrix;
        compose(matrix);
        return matrix;
    }

    Vector3 Transform::applyPoint(const Vector3& point) const {

        Vector3 out;
        applyPoint(point, out);
        return out;
    }

    void Transform::applyPoint(const Vector3& point, Vector3& out) const {

        out.set(point.x - anchor.x, point.y - anchor.y, point.z - anchor.z);
        out.multiply(scale);
        out.applyQuaternion(localQuaternion());
        out.add(position);
    }

    Transform Transform::blend(const Transform& a, const Transform& b, float t) {

        Transform out;
        out.position.lerpVectors(a.position, b.position, t);
        out.anchor.lerpVectors(a.anchor, b.anchor, t);
        out.scale.lerpVectors(a.scale, b.scale, t);
        out.rotation.lerpVectors(a.rotation, b.rotation, t);
        out.orientation.slerpQuaternions(a.orientation, b.orientation, t);
        out.opacity = a.opacity + (b.opacity - a.opacity) * t;
        return out;
    }

    bool Transform::isIdentity() const {

        return *this == Transform{};
    }

    bool Transform::operator==(const Transform& other) const {

        return position == other.position &&
               anchor == other.anchor &&
               scale == other.scale &&
               rotation == other.rotation &&
               orientation == other.orientation &&
               opacity == other.opacity;
    }

}// namespace chrononmotion::motion
