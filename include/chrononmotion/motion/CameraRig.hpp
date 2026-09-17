// ChrononMotion — animated cameras.
//
// A deterministic camera: the classic cinematographic moves (dolly, pan, tilt,
// orbit, roll) as pure operations on a pose, plus the tracks that key them and a
// look-at target that keeps the aim while the position moves.
//
// The camera is backed by a null Layer, so it can itself be parented under a
// controller (a camera rig) and inherit that motion.

#ifndef CHRONONMOTION_MOTION_CAMERARIG_HPP
#define CHRONONMOTION_MOTION_CAMERARIG_HPP

#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/Track.hpp"

#include "chrononmotion/cameras/PerspectiveCamera.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <memory>

namespace chrononmotion::motion {

    /// A sampled camera pose.
    struct CameraPose {
        Vector3 position{0.f, 0.f, 0.f};
        Quaternion orientation{0.f, 0.f, 0.f, 1.f};

        /// Point of interest. When set, the pose is aimed at it (the "look-at"
        /// channel), so dolly/pan/orbit keep the subject framed.
        Vector3 target{0.f, 0.f, 0.f};
        bool hasTarget{false};

        float fov{60.f};

        /// Focal length in millimetres. 0 means "not authored": `fov` wins.
        float focalLength{0.f};

        /// Roll about the view axis, after the aim is solved.
        float roll{0.f};
    };

    /// An animated perspective camera.
    class CameraRig {
    public:
        using Id = Layer::Id;

        explicit CameraRig(Id id = 1,
                           float fov = 60.f,
                           float aspect = 16.f / 9.f,
                           float nearPlane = 0.1f,
                           float farPlane = 2000.f);

        [[nodiscard]] Id id() const { return layer_.id; }
        [[nodiscard]] Layer& layer() { return layer_; }
        [[nodiscard]] const Layer& layer() const { return layer_; }

        // --- static pose (used wherever the matching track has no keys)
        CameraRig& setPosition(const Vector3& value);
        CameraRig& setOrientation(const Quaternion& value);
        CameraRig& setTarget(const Vector3& value);
        CameraRig& clearTarget();
        CameraRig& setFov(float degrees);
        CameraRig& setFocalLength(float millimetres);
        CameraRig& setRoll(float radians);
        CameraRig& setParent(Layer::Id parentId);

        [[nodiscard]] Vector3 position() const { return layer_.transform.position; }
        [[nodiscard]] Quaternion orientation() const { return layer_.transform.orientation; }
        [[nodiscard]] bool hasTarget() const { return hasTarget_; }
        [[nodiscard]] Vector3 target() const { return target_; }
        [[nodiscard]] float fov() const { return fov_; }
        [[nodiscard]] float roll() const { return roll_; }
        [[nodiscard]] float aspect() const { return aspect_; }
        [[nodiscard]] float nearPlane() const { return nearPlane_; }
        [[nodiscard]] float farPlane() const { return farPlane_; }

        /// A camera node configured with this rig's projection, ready for `apply`.
        [[nodiscard]] std::shared_ptr<PerspectiveCamera> makeCamera() const;

        // --- animation channels
        [[nodiscard]] Track<Vector3>& positionTrack() { return layer_.tracks.position; }
        [[nodiscard]] Track<Quaternion>& orientationTrack() { return layer_.tracks.orientation; }
        [[nodiscard]] Track<float>& opacityTrack() { return layer_.tracks.opacity; }
        [[nodiscard]] Track<Vector3>& targetTrack() { return targetTrack_; }
        [[nodiscard]] Track<float>& fovTrack() { return fovTrack_; }
        [[nodiscard]] Track<float>& focalLengthTrack() { return focalTrack_; }
        [[nodiscard]] Track<float>& rollTrack() { return rollTrack_; }

        /// Sample the local pose at `seconds`.
        [[nodiscard]] CameraPose sample(float seconds) const;

        /// Sample and write into a camera node, optionally under a parent world
        /// matrix (the rig's own parented motion).
        void apply(float seconds, PerspectiveCamera& camera, const Matrix4* parentWorld = nullptr) const;
        void apply(const CameraPose& pose, PerspectiveCamera& camera, const Matrix4* parentWorld = nullptr) const;

        /// Latest key on any channel of the rig, in seconds.
        [[nodiscard]] float endTime() const;

        // --- the pure moves. Each is a function of the pose it is given, so it
        // can be applied at any time and then keyed.
        static void lookAtPose(CameraPose& pose, const Vector3& target);

        /// Move along the view axis: positive is forward, toward the target.
        static void dolly(CameraPose& pose, float distance);

        /// Translate in the camera's own plane (right = +x, up = +y).
        static void pan(CameraPose& pose, const Vector2& delta);

        /// Pitch about the camera's local X axis.
        static void tilt(CameraPose& pose, float radians);

        /// Yaw about world Y, then pitch about the camera's right axis.
        static void orbit(CameraPose& pose, float yaw, float pitch);

        /// Orbit the position around an explicit pivot and re-aim at it.
        static void orbitAround(CameraPose& pose, const Vector3& pivot, float yaw, float pitch);

        /// Roll about the local Z axis.
        static void roll(CameraPose& pose, float radians);

    private:
        Layer layer_;
        Vector3 target_{0.f, 0.f, 0.f};
        bool hasTarget_{false};
        float fov_{60.f};
        float focalLength_{0.f};
        float roll_{0.f};
        Track<Vector3> targetTrack_;
        Track<float> fovTrack_;
        Track<float> focalTrack_;
        Track<float> rollTrack_;
        float aspect_{16.f / 9.f};
        float nearPlane_{0.1f};
        float farPlane_{2000.f};
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_CAMERARIG_HPP
