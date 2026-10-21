#include "chrononmotion/motion/CameraFraming.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"

#include "chrononmotion/math/Matrix4.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion {
namespace {

Vector3 centerOf(const CameraFramingBounds& b) {
    return {(b.min.x + b.max.x) * 0.5f, (b.min.y + b.max.y) * 0.5f,
            (b.min.z + b.max.z) * 0.5f};
}

Vector3 weightedCenter(const std::vector<CameraFramingBounds>& bounds) {
    Vector3 sum{};
    float total = 0.f;
    for (const auto& b : bounds) {
        const float w = std::max(0.f, b.weight);
        sum.addScaledVector(centerOf(b), w);
        total += w;
    }
    if (total > 0.f) return sum.multiplyScalar(1.f / total);
    return bounds.empty() ? Vector3{} : centerOf(bounds.front());
}

std::vector<CameraFramingBounds> activeTargets(const CameraFramingRequest& req) {
    if (req.strategy != CameraFramingStrategy::FitHighest || req.targets.empty())
        return req.targets;
    const auto it = std::max_element(req.targets.begin(), req.targets.end(),
        [](const auto& a, const auto& b) { return a.weight < b.weight; });
    return {*it};
}

void aimAt(CameraPose& pose, const Vector3& target) {
    pose.target = target;
    pose.hasTarget = true;
    Matrix4 look;
    look.lookAt(pose.position, target, Vector3(0.f, 1.f, 0.f));
    pose.orientation.setFromRotationMatrix(look);
}

float requiredDistance(const std::vector<CameraFramingBounds>& bounds,
                       const Vector3& center, const Quaternion& orientation,
                       float fov, float aspect,
                       float safeWidth, float safeHeight) {
    const float tanHalf = std::max(std::tan(fov * 0.5f * 3.14159265358979323846f / 180.f), 1e-5f);
    Quaternion inverse = orientation.clone().invert();
    float distance = 0.f;
    for (const auto& b : bounds) {
        for (float x : {b.min.x, b.max.x}) for (float y : {b.min.y, b.max.y})
            for (float z : {b.min.z, b.max.z}) {
                Vector3 local{x - center.x, y - center.y, z - center.z};
                local.applyQuaternion(inverse);
                const float horizontal = std::abs(local.x) /
                    std::max(tanHalf * aspect * safeWidth * 0.5f, 1e-5f) + local.z;
                const float vertical = std::abs(local.y) /
                    std::max(tanHalf * safeHeight * 0.5f, 1e-5f) + local.z;
                distance = std::max(distance, std::max(horizontal, vertical));
            }
    }
    return distance;
}

float angleDegrees(const Vector3& a, const Vector3& b) {
    const float la = a.length(), lb = b.length();
    if (la < 1e-6f || lb < 1e-6f) return 0.f;
    const float cosine = std::clamp(a.dot(b) / (la * lb), -1.f, 1.f);
    return std::acos(cosine) * 180.f / 3.14159265358979323846f;
}

} // namespace

CameraFramingResult CameraFramingSolver::solve(const CameraFramingRequest& req,
                                               const CameraPose& base,
                                               CameraFramingSession* session) const {
    CameraFramingResult result;
    result.pose = base;
    if (req.targets.empty()) return result;
    if (!std::isfinite(req.viewport.x) || !std::isfinite(req.viewport.y) ||
        req.viewport.x <= 0.f || req.viewport.y <= 0.f ||
        !std::isfinite(base.fov) || base.fov <= 0.f || base.fov >= 180.f ||
        !std::isfinite(req.minDistance) || !std::isfinite(req.maxDistance) ||
        !std::isfinite(req.targetVelocity.x) || !std::isfinite(req.targetVelocity.y) ||
        !std::isfinite(req.targetVelocity.z) || !std::isfinite(req.lookAheadSeconds) ||
        req.lookAheadSeconds < 0.f || req.minDistance <= 0.f || req.maxDistance < req.minDistance) {
        result.ok = false;
        return result;
    }
    for (const auto& bounds : req.targets) {
        if (!std::isfinite(bounds.min.x) || !std::isfinite(bounds.min.y) || !std::isfinite(bounds.min.z) ||
            !std::isfinite(bounds.max.x) || !std::isfinite(bounds.max.y) || !std::isfinite(bounds.max.z) ||
            !std::isfinite(bounds.weight) || bounds.min.x > bounds.max.x ||
            bounds.min.y > bounds.max.y || bounds.min.z > bounds.max.z) {
            result.ok = false;
            return result;
        }
    }

    auto targets = activeTargets(req);
    Vector3 predictedOffset = req.targetVelocity;
    predictedOffset.multiplyScalar(req.lookAheadSeconds);
    if (req.lookAheadSeconds > 0.f) {
        for (auto& bounds : targets) {
            bounds.min.add(predictedOffset);
            bounds.max.add(predictedOffset);
        }
    }
    const Vector3 center = weightedCenter(targets);
    const float safeWidth = std::clamp(1.f - req.safeArea.left - req.safeArea.right, 0.05f, 1.f);
    const float safeHeight = std::clamp(1.f - req.safeArea.top - req.safeArea.bottom, 0.05f, 1.f);
    const float aspect = req.viewport.x / req.viewport.y;
    Vector3 forward(0.f, 0.f, -1.f);
    if (base.hasTarget) {
        forward.subVectors(base.target, base.position);
        if (forward.lengthSq() > 1e-10f) forward.normalize();
    } else {
        forward.applyQuaternion(base.orientation);
        forward.normalize();
    }
    CameraPose orientationPose = base;
    orientationPose.position = center;
    orientationPose.position.addScaledVector(forward, -1.f);
    aimAt(orientationPose, center);
    result.requiredDistance = std::clamp(requiredDistance(targets, center, orientationPose.orientation, base.fov,
        aspect, safeWidth, safeHeight), req.minDistance, req.maxDistance);

    const bool aimEnabled = req.strategy != CameraFramingStrategy::DollyOnly;
    const bool dollyEnabled = req.strategy != CameraFramingStrategy::AimOnly;
    CameraPose framed = base;

    const float currentDistance = base.hasTarget
        ? base.position.distanceTo(base.target)
        : result.requiredDistance;
    const float desiredDistance = dollyEnabled ? result.requiredDistance : currentDistance;
    Vector3 target = aimEnabled ? center : (base.hasTarget ? base.target : center);
    if (aimEnabled && req.aimErrorDegrees > 0.f && session && session->hasPrevious &&
        session->previous.hasTarget) {
        const Vector3 oldDirection = Vector3{}.subVectors(session->previous.target, base.position);
        const Vector3 newDirection = Vector3{}.subVectors(target, base.position);
        if (angleDegrees(oldDirection, newDirection) < req.aimErrorDegrees)
            target = session->previous.target;
    }
    if (dollyEnabled) {
        framed.position = target;
        framed.position.addScaledVector(forward, -desiredDistance);
    }
    if (aimEnabled) aimAt(framed, target);

    if (req.strategy == CameraFramingStrategy::RuleOfThirds) {
        CameraPose projected = framed;
        ScreenCameraIntrinsics intrinsics{req.viewport, base.fov};
        const float depth = std::max(viewDepth(center, framed), 1e-4f);
        const Vector2 screen = projectToScreen(center, projected, intrinsics);
        const Vector2 desired{req.viewport.x * std::clamp(req.compositionPoint.x, 0.f, 1.f),
                              req.viewport.y * std::clamp(req.compositionPoint.y, 0.f, 1.f)};
        translatePoseForScreenDelta(framed, Vector2(desired.x - screen.x, desired.y - screen.y),
                                    depth, intrinsics);
    }

    const CameraPose desiredPose = framed;
    if (session && session->hasPrevious) {
        const float alpha = std::clamp(req.hysteresis, 0.f, 1.f);
        const float positionChange = desiredPose.position.distanceTo(session->previous.position);
        const float positionBase = std::max(session->previous.position.distanceTo(target), 1.f);
        const bool holdPosition = positionChange / positionBase < std::max(req.dollyDeadZone, 0.f);
        const bool holdAim = desiredPose.hasTarget && session->previous.hasTarget &&
            angleDegrees(Vector3{}.subVectors(desiredPose.target, desiredPose.position),
                         Vector3{}.subVectors(session->previous.target, session->previous.position)) <
                std::max(req.aimDeadZoneDegrees, 0.f);
        framed.position = session->previous.position;
        if (!holdPosition) framed.position.addScaledVector(Vector3{}.subVectors(
            desiredPose.position, session->previous.position), alpha);
        if (aimEnabled && session->previous.hasTarget) {
            framed.target = session->previous.target;
            if (!holdAim) {
                framed.target.addScaledVector(Vector3{}.subVectors(
                    desiredPose.target, session->previous.target), alpha);
            }
            framed.hasTarget = true;
            aimAt(framed, framed.target);
        }
    }

    result.pose = framed;
    if (session) { session->previous = framed; session->hasPrevious = true; }
    result.ok = true;
    return result;
}

float CameraFramingSolver::distanceForBounds(float halfWidth, float halfHeight,
                                              float halfDepth, float fovDegrees,
                                              const Vector2& viewport,
                                              const CameraSafeArea& safeArea) {
    if (!std::isfinite(halfWidth) || !std::isfinite(halfHeight) || !std::isfinite(halfDepth) ||
        halfWidth < 0.f || halfHeight < 0.f || halfDepth < 0.f ||
        !std::isfinite(fovDegrees) || fovDegrees <= 0.f || fovDegrees >= 180.f ||
        !std::isfinite(viewport.x) || !std::isfinite(viewport.y) || viewport.x <= 0.f || viewport.y <= 0.f)
        throw std::invalid_argument("distanceForBounds: extents, FOV and viewport must be valid");
    const float aspect = viewport.x / viewport.y;
    const float safeWidth = std::clamp(1.f - safeArea.left - safeArea.right, 0.05f, 1.f);
    const float safeHeight = std::clamp(1.f - safeArea.top - safeArea.bottom, 0.05f, 1.f);
    const float fill = std::min(safeWidth, safeHeight);
    const float tanHalf = std::tan(std::max(fovDegrees * 0.5f * 3.14159265358979323846f / 180.f, 1e-4f));
    const float fitWidth = halfWidth * 2.f;
    const float fitHeight = halfHeight * 2.f * aspect;
    const float fit = std::max(fitWidth, fitHeight);
    return fit / (2.f * tanHalf * aspect * fill) + halfDepth;
}

} // namespace chrononmotion::motion
