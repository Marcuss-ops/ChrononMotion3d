#include "chrononmotion/motion/CameraRig.hpp"

#include <algorithm>
#include <utility>

namespace chrononmotion::motion {

    CameraRig::CameraRig(Id id, float fov, float aspect, float nearPlane, float farPlane)
        : layer_(id, "Camera"), fov_(fov), aspect_(aspect), nearPlane_(nearPlane), farPlane_(farPlane) {}

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

        fov_ = degrees;
        return *this;
    }

    CameraRig& CameraRig::setFocalLength(float millimetres) {

        focalLength_ = millimetres;
        return *this;
    }

    CameraRig& CameraRig::setRoll(float radians) {

        roll_ = radians;
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

    CameraPose CameraRig::sample(float seconds) const {

        const Transform transform = layer_.sample(seconds);

        CameraPose pose;
        pose.position = transform.position;
        pose.orientation = transform.localQuaternion();
        pose.hasTarget = hasTarget_;
        pose.target = targetTrack_.empty() ? target_ : targetTrack_.sample(seconds);
        pose.fov = fovTrack_.empty() ? fov_ : fovTrack_.sample(seconds);
        pose.focalLength = focalTrack_.empty() ? focalLength_ : focalTrack_.sample(seconds);
        pose.roll = rollTrack_.empty() ? roll_ : rollTrack_.sample(seconds);

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
