#include "chrononmotion/motion/Constraints.hpp"
#include "chrononmotion/math/Euler.hpp"
#include "chrononmotion/math/Finite.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion {

    bool applyConstraint(const ConstraintSpec& spec, Transform& local,
                         const Vector3& targetPosition,
                         const Quaternion& targetRotation,
                         const Vector3& targetScale,
                         float seconds) noexcept {
        if (!std::isfinite(spec.amount) || !std::isfinite(seconds) || !math::isFinite(targetPosition) ||
            !math::isFinite(targetScale) || !std::isfinite(targetRotation.lengthSq())) return false;
        const float amount = std::clamp(spec.amount, 0.f, 1.f);
        if (amount == 0.f) return true;
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
            case ConstraintKind::DampedAim:
            case ConstraintKind::LookAtWithRoll: {
                if (!std::isfinite(spec.damping) || spec.damping < 0.f || !std::isfinite(spec.roll)) return false;
                const Vector3 direction = targetPosition - local.position;
                if (direction.lengthSq() <= 1e-10f) return false;
                Quaternion desired = MotionPath::directionToQuaternion(direction);
                if (spec.kind == ConstraintKind::LookAtWithRoll) {
                    Quaternion roll; roll.setFromAxisAngle(Vector3::Z(), spec.roll); desired.multiply(roll).normalize();
                }
                const float weight = spec.kind == ConstraintKind::DampedAim ?
                    amount * (1.f - std::exp(-spec.damping * std::max(0.f, seconds))) : amount;
                const auto current = local.localQuaternion();
                local.orientation.slerpQuaternions(current, desired, weight); local.rotation = {};
                return true;
            }
            case ConstraintKind::SurfaceAttach: {
                if (!math::isFinite(spec.surfaceNormal) || !math::isFinite(spec.surfaceOffset) ||
                    std::abs(spec.surfaceNormal.lengthSq() - 1.f) > 1e-4f) return false;
                Vector3 normal = spec.surfaceNormal; normal.applyQuaternion(targetRotation);
                Vector3 offset = spec.surfaceOffset; offset.multiply(targetScale).applyQuaternion(targetRotation);
                Vector3 desired = local.position;
                desired.addScaledVector(normal, -Vector3(desired - targetPosition).dot(normal));
                desired.add(offset);
                local.position.lerpVectors(local.position, desired, amount);
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
            case ConstraintKind::LimitPosition:
            case ConstraintKind::LimitRotation:
            case ConstraintKind::LimitScale: {
                if (!math::isFinite(spec.minimum) || !math::isFinite(spec.maximum)) return false;
                for (unsigned i = 0; i < 3; ++i)
                    if (spec.minimum[i] > spec.maximum[i]) return false;
                if (spec.kind == ConstraintKind::LimitRotation && !local.orientation.equals(Quaternion{})) {
                    Euler combined;
                    combined.setFromQuaternion(local.localQuaternion(), Euler::XYZ);
                    local.rotation = {combined.x, combined.y, combined.z};
                    local.orientation.identity();
                }
                Vector3* channel = spec.kind == ConstraintKind::LimitPosition ? &local.position :
                                   spec.kind == ConstraintKind::LimitRotation ? &local.rotation : &local.scale;
                Vector3 limited = *channel;
                for (unsigned i = 0; i < 3; ++i)
                    limited[i] = std::clamp(limited[i], spec.minimum[i], spec.maximum[i]);
                if (amount == 1.f) *channel = limited;
                else channel->lerpVectors(*channel, limited, amount);
                return true;
            }
            case ConstraintKind::Parent: {
                local.position.lerpVectors(local.position, targetPosition, amount);
                const Quaternion current = local.localQuaternion();
                local.orientation.slerpQuaternions(current, targetRotation, amount);
                local.rotation = {};
                return true;
            }
            case ConstraintKind::StretchTo: {
                if (!std::isfinite(spec.restLength) || spec.restLength <= 0.f) return false;
                Vector3 direction = targetPosition - local.position;
                const float length = direction.length();
                if (!std::isfinite(length) || length <= 1e-6f) return false;
                const float desiredScale = length / spec.restLength;
                if (!std::isfinite(desiredScale)) return false;
                const Quaternion desired = MotionPath::directionToQuaternion(direction);
                const Quaternion current = local.localQuaternion();
                local.orientation.slerpQuaternions(current, desired, amount);
                local.rotation = {};
                local.scale.z += (desiredScale - local.scale.z) * amount;
                return true;
            }
            case ConstraintKind::Point:
            case ConstraintKind::CopyPosition: {
                local.position.lerpVectors(local.position, targetPosition, amount);
                return true;
            }
            case ConstraintKind::Orient: {
                const Quaternion current = local.localQuaternion();
                local.orientation.slerpQuaternions(current, targetRotation, amount);
                local.rotation = {};
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
