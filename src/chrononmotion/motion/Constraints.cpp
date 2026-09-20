#include "chrononmotion/motion/Constraints.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion {

    bool applyConstraint(const ConstraintSpec& spec, Transform& local,
                         const Vector3& targetPosition,
                         const Quaternion& targetRotation,
                         const Vector3& targetScale,
                         float seconds) noexcept {
        const float amount = std::clamp(spec.amount, 0.f, 1.f);
        switch (spec.kind) {
            case ConstraintKind::Billboard:
            case ConstraintKind::Aim: {
                Vector3 direction;
                direction.subVectors(targetPosition, local.position);
                if (direction.lengthSq() < 1e-10f) return false;
                const Quaternion desired = MotionPath::directionToQuaternion(direction);
                local.orientation.slerpQuaternions(local.orientation, desired, amount);
                return true;
            }
            case ConstraintKind::KeepDistance: {
                Vector3 direction;
                direction.subVectors(local.position, targetPosition);
                if (direction.lengthSq() < 1e-10f) return false;
                direction.normalize().multiplyScalar(spec.distance);
                Vector3 desired = targetPosition;
                desired.add(direction);
                local.position.lerpVectors(local.position, desired, amount);
                return true;
            }
            case ConstraintKind::FollowPath: {
                if (!spec.path) return false;
                const Vector3 desired = spec.path->sampleUniform(std::clamp(spec.progress + seconds, 0.f, 1.f));
                local.position.lerpVectors(local.position, desired, amount);
                if (spec.orientToPath) {
                    const Quaternion desiredRotation = spec.path->orientToPath(std::clamp(spec.progress + seconds, 0.f, 1.f));
                    local.orientation.slerpQuaternions(local.orientation, desiredRotation, amount);
                }
                return true;
            }
            case ConstraintKind::CopyPosition: {
                local.position.lerpVectors(local.position, targetPosition, amount);
                return true;
            }
            case ConstraintKind::CopyRotation:
                local.orientation.slerpQuaternions(local.orientation, targetRotation, amount);
                return true;
            case ConstraintKind::CopyScale:
                local.scale.lerpVectors(local.scale, targetScale, amount);
                return true;
        }
        return false;
    }

}// namespace chrononmotion::motion
