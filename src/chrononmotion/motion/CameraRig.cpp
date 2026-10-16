#include "chrononmotion/motion/CameraRig.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace chrononmotion::motion {

    CameraRig::CameraRig(Id id, float fov, float aspect, float nearPlane, float farPlane)
        : layer_(id, "Camera"), fov_(fov), aspect_(aspect), nearPlane_(nearPlane), farPlane_(farPlane) {

        if (!std::isfinite(fov) || fov <= 0.f || fov >= 180.f) {
            throw std::invalid_argument("CameraRig: fov must be finite and in (0, 180)");
        }
        if (!std::isfinite(aspect) || aspect <= 0.f) {
            throw std::invalid_argument("CameraRig: aspect must be finite and positive");
        }
        if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane <= 0.f || farPlane <= nearPlane) {
            throw std::invalid_argument("CameraRig: expected 0 < nearPlane < farPlane");
        }
    }

    CameraRig& CameraRig::setPosition(const Vector3& value) {

        layer_.transform.position.copy(value);
        return *this;
    }

    CameraRig& CameraRig::setOrientation(const Quaternion& value) {

        layer_.transform.orientation.copy(value);
        return *this;
    }

    CameraRig& CameraRig::setTarget(const Vector3& value) {

        target_.copy(value);
        hasTarget_ = true;
        return *this;
    }

    CameraRig& CameraRig::clearTarget() {

        hasTarget_ = false;
        return *this;
    }

    CameraRig& CameraRig::setFov(float degrees) {

        if (!std::isfinite(degrees) || degrees <= 0.f || degrees >= 180.f) {
            throw std::invalid_argument("CameraRig::setFov: fov must be finite and in (0, 180)");
        }
        fov_ = degrees;
        return *this;
    }

    CameraRig& CameraRig::setAspect(float widthOverHeight) {
        if (!std::isfinite(widthOverHeight) || widthOverHeight <= 0.f) {
            throw std::invalid_argument("CameraRig::setAspect: aspect must be finite and positive");
        }
        aspect_ = widthOverHeight;
        return *this;
    }

    CameraRig& CameraRig::setFocalLength(float millimetres) {

        if (!std::isfinite(millimetres) || millimetres <= 0.f) {
            throw std::invalid_argument("CameraRig::setFocalLength: focal length must be finite and positive");
        }
        focalLength_ = millimetres;
        return *this;
    }

    CameraRig& CameraRig::setRoll(float radians) {

        roll_ = radians;
        return *this;
    }

    CameraRig& CameraRig::setAperture(float millimetres) {

        if (!std::isfinite(millimetres) || millimetres < 0.f) {
            throw std::invalid_argument("CameraRig::setAperture: aperture must be finite and non-negative");
        }
        aperture_ = millimetres;
        return *this;
    }

    CameraRig& CameraRig::setFocusDistance(float distance) {

        if (!std::isfinite(distance) || distance < 0.f) {
            throw std::invalid_argument("CameraRig::setFocusDistance: focus distance must be finite and non-negative");
        }
        focusDistance_ = distance;
        return *this;
    }

    CameraRig& CameraRig::setMaxBlur(float pixels) {

        if (!std::isfinite(pixels) || pixels < 0.f || pixels > 256.f) {
            throw std::invalid_argument("CameraRig::setMaxBlur: maximum blur must be finite and in [0, 256]");
        }
        maxBlur_ = pixels;
        return *this;
    }

    CameraRig& CameraRig::setFStop(float fStop) {

        if (!std::isfinite(fStop) || fStop < 0.f) {
            throw std::invalid_argument("CameraRig::setFStop: f-stop must be finite and non-negative");
        }
        fStop_ = fStop;
        return *this;
    }

    CameraRig& CameraRig::setIrisBlades(int blades) {
        if (blades < 0 || blades > 12) {
            throw std::invalid_argument("CameraRig::setIrisBlades: blade count must be in [0, 12]");
        }
        irisBlades_ = blades;
        return *this;
    }

    CameraRig& CameraRig::setHighlightGain(float gain) {
        if (!std::isfinite(gain) || gain < 0.f || gain > 8.f) {
            throw std::invalid_argument("CameraRig::setHighlightGain: gain must be finite and in [0, 8]");
        }
        highlightGain_ = gain;
        return *this;
    }

    CameraRig& CameraRig::setParent(Layer::Id parentId) {

        layer_.parentId = parentId;
        return *this;
    }

    std::shared_ptr<PerspectiveCamera> CameraRig::makeCamera() const {

        return PerspectiveCamera::create(fov_, aspect_, nearPlane_, farPlane_);
    }

    float CameraRig::endTime() const {

        float end = layer_.endTime();
        end = std::max(end, targetTrack_.endTime());
        end = std::max(end, fovTrack_.endTime());
        end = std::max(end, focalTrack_.endTime());
        end = std::max(end, rollTrack_.endTime());
        end = std::max(end, apertureTrack_.endTime());
        end = std::max(end, focusDistanceTrack_.endTime());
        end = std::max(end, maxBlurTrack_.endTime());
        end = std::max(end, fStopTrack_.endTime());
        return end;
    }

    void CameraRig::lookAtPose(CameraPose& pose, const Vector3& target) {

        Matrix4 aim;
        aim.lookAt(pose.position, target, Vector3(0.f, 1.f, 0.f));
        pose.orientation.setFromRotationMatrix(aim);
    }

    void CameraRig::dolly(CameraPose& pose, float distance) {

        Vector3 forward;

        if (pose.hasTarget) {
            forward.subVectors(pose.target, pose.position);
            if (forward.lengthSq() < 1e-12f) {
                forward.set(0.f, 0.f, -1.f);
            } else {
                forward.normalize();
            }
        } else {
            forward.set(0.f, 0.f, -1.f);
            forward.applyQuaternion(pose.orientation);
        }

        pose.position.addScaledVector(forward, distance);

        if (pose.hasTarget) lookAtPose(pose, pose.target);
    }

    void CameraRig::pan(CameraPose& pose, const Vector2& delta) {

        Vector3 right(1.f, 0.f, 0.f);
        Vector3 up(0.f, 1.f, 0.f);
        right.applyQuaternion(pose.orientation);
        up.applyQuaternion(pose.orientation);

        pose.position.addScaledVector(right, delta.x);
        pose.position.addScaledVector(up, delta.y);
    }

    void CameraRig::tilt(CameraPose& pose, float radians) {

        Quaternion pitch;
        pitch.setFromAxisAngle(Vector3(1.f, 0.f, 0.f), radians);
        pose.orientation.multiply(pitch);
    }

    void CameraRig::orbit(CameraPose& pose, float yaw, float pitch) {

        if (pose.hasTarget) {
            orbitAround(pose, pose.target, yaw, pitch);
            return;
        }

        Quaternion yawRotation;
        yawRotation.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), yaw);
        pose.orientation.premultiply(yawRotation);

        Quaternion pitchRotation;
        pitchRotation.setFromAxisAngle(Vector3(1.f, 0.f, 0.f), pitch);
        pose.orientation.multiply(pitchRotation);
    }

    void CameraRig::orbitAround(CameraPose& pose, const Vector3& pivot, float yaw, float pitch) {

        Vector3 offset;
        offset.subVectors(pose.position, pivot);

        Quaternion yawRotation;
        yawRotation.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), yaw);
        offset.applyQuaternion(yawRotation);

        Vector3 right;
        right.crossVectors(Vector3(0.f, 1.f, 0.f), offset);
        if (right.lengthSq() > 1e-12f) {
            right.normalize();
            Quaternion pitchRotation;
            // Negative so positive pitch lifts the camera above the pivot.
            pitchRotation.setFromAxisAngle(right, -pitch);
            offset.applyQuaternion(pitchRotation);
        }

        pose.position.addVectors(pivot, offset);
        lookAtPose(pose, pivot);
    }

    void CameraRig::roll(CameraPose& pose, float radians) {

        Quaternion rollRotation;
        rollRotation.setFromAxisAngle(Vector3(0.f, 0.f, 1.f), radians);
        pose.orientation.multiply(rollRotation);
    }

    CameraPoseDerivative CameraRig::sampleDerivative(float seconds) const {

        CameraPoseDerivative d;

        if (!layer_.tracks.position.empty()) {
            d.linearVelocity = layer_.tracks.position.velocity(seconds);
        }
        if (!targetTrack_.empty()) {
            d.targetVelocity = targetTrack_.velocity(seconds);
        }
        if (!fovTrack_.empty()) {
            d.fovRate = fovTrack_.velocity(seconds);
        }
        if (!focusDistanceTrack_.empty()) {
            d.focusRate = focusDistanceTrack_.velocity(seconds);
        }
        if (!rollTrack_.empty()) {
            d.rollRate = rollTrack_.velocity(seconds);
        }

        // ω = 2·q·q̇·q⁻¹ : the world-space angular velocity of the orientation
        // channel. The track reports the rate as a quaternion delta in the
        // segment's local frame (q̇ ∝ q·Δq_local/2), so the world-space vector
        // needs one extra sandwich. This is the form a shutter/blur integrator consumes.
        if (!layer_.tracks.orientation.empty()) {
            const Quaternion q = layer_.tracks.orientation.sample(seconds);
            const Quaternion dq = layer_.tracks.orientation.velocity(seconds);
            Quaternion worldRate;
            worldRate.multiplyQuaternions(q, dq);
            Quaternion inv = q;
            inv.invert();
            worldRate.multiply(inv);
            // The track's rate quaternion is an axis-angle step (Track::
            // sampleScaled builds quat(axis, angle·rate)), so the angular
            // speed is its rotation angle θ = 2·atan2(|v|, w) and the axis is
            // its normalized vector part.
            const float vl = std::sqrt(worldRate.x * worldRate.x +
                                       worldRate.y * worldRate.y +
                                       worldRate.z * worldRate.z);
            if (vl > 1e-12f) {
                const float angle = 2.f * std::atan2(vl, worldRate.w);
                const float s = angle / vl;
                d.angularVelocity.set(worldRate.x * s, worldRate.y * s, worldRate.z * s);
            }
        }

        return d;
    }

    CameraPose CameraRig::sample(float seconds) const {

        const Transform transform = layer_.sample(seconds);

        CameraPose pose;
        pose.position = transform.position;
        pose.orientation = transform.localQuaternion();
        pose.hasTarget = hasTarget_ || !targetTrack_.empty();
        pose.target = targetTrack_.empty() ? target_ : targetTrack_.sample(seconds);
        pose.fov = fovTrack_.empty() ? fov_ : fovTrack_.sample(seconds);
        pose.focalLength = focalTrack_.empty() ? focalLength_ : focalTrack_.sample(seconds);
        pose.roll = rollTrack_.empty() ? roll_ : rollTrack_.sample(seconds);
        pose.aperture = apertureTrack_.empty() ? aperture_ : apertureTrack_.sample(seconds);
        pose.focusDistance = focusDistanceTrack_.empty() ? focusDistance_ : focusDistanceTrack_.sample(seconds);
        pose.maxBlur = maxBlurTrack_.empty() ? maxBlur_ : maxBlurTrack_.sample(seconds);
        pose.fStop = fStopTrack_.empty() ? fStop_ : fStopTrack_.sample(seconds);
        pose.irisBlades = irisBlades_;
        pose.highlightGain = highlightGain_;

        if (pose.hasTarget) {
            lookAtPose(pose, pose.target);
        }

        if (pose.roll != 0.f) {
            roll(pose, pose.roll);
        }

        return pose;
    }

    void CameraRig::apply(float seconds, PerspectiveCamera& camera, const Matrix4* parentWorld) const {

        apply(sample(seconds), camera, parentWorld);
    }

    void CameraRig::apply(const CameraPose& pose, PerspectiveCamera& camera, const Matrix4* parentWorld) const {

        Matrix4 local;
        local.compose(pose.position, pose.orientation, Vector3(1.f, 1.f, 1.f));

        Matrix4 world;
        if (parentWorld) {
            world.multiplyMatrices(*parentWorld, local);
        } else {
            world = local;
        }

        Vector3 scale(1.f, 1.f, 1.f);
        camera.matrixAutoUpdate = true;
        world.decompose(camera.position, camera.quaternion, scale);
        camera.scale.set(1.f, 1.f, 1.f);
        camera.updateMatrixWorld(true);

        if (pose.focalLength > 0.f) {
            camera.fov = pose.fov;
            camera.setFocalLength(pose.focalLength);
        } else {
            camera.fov = pose.fov;
        }

        camera.updateProjectionMatrix();
    }

}// namespace chrononmotion::motion
