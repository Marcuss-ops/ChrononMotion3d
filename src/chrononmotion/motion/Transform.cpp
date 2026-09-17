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
            Matrix4 anchorMatrix;
            anchorMatrix.makeTranslation(-anchor.x, -anchor.y, -anchor.z);
            target.multiply(anchorMatrix);
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
