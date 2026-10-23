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
        float sensorWidth{36.f}; ///< Sensor gate width in millimetres.
        Vector2 lensShift{0.f, 0.f}; ///< Normalized output viewport shift; +X right, +Y down.

        /// Lens data for depth of field. Each 0 means "not authored": the motion
        /// side carries the numbers and never interprets them, so a renderer
        /// applies what is present and leaves the rest to its own optics. The
        /// units are the RenderPlan Chronon3D consumer contract: `fStop` (f/N)
        /// is the single authored aperture truth and feeds the physical model.
        float focusDistance{0.f}; ///< Distance to the focal plane along the view axis, world units.
        float maxBlur{0.f};        ///< Blur clamp in pixels of the target canvas; 0 means not authored.
        float fStop{0.f};         ///< Physical aperture as f/N (0.7..32); 0 means not authored.
        int irisBlades{0};        ///< Bokeh iris blades; 0 means circular.
        float highlightGain{0.f}; ///< Gain for bright out-of-focus highlights.

        /// Roll about the view axis, after the aim is solved.
        float roll{0.f};
    };

    /// First-order motion of a sampled camera pose, in channel units per
    /// second. The exact analytic derivative of the rig's own keys (the same
    /// math as `Track::velocity`): no finite-difference sampling error, no
    /// dependence on neighboring frames, which is what makes shutter motion
    /// blur computable per frame in isolation.
    struct CameraPoseDerivative {
        Vector3 linearVelocity{0.f, 0.f, 0.f};  ///< d(position)/dt, world space.
        Vector3 angularVelocity{0.f, 0.f, 0.f}; ///< ω = 2·q̇·q⁻¹, world space, rad/s.
        Vector3 targetVelocity{0.f, 0.f, 0.f};  ///< d(target)/dt, world space.
        float fovRate{0.f};                     ///< d(fov)/dt, degrees per second.
        float focusRate{0.f};                   ///< d(focusDistance)/dt, units per second.
        float rollRate{0.f};                    ///< d(roll)/dt, radians per second.
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
        CameraRig& setAspect(float widthOverHeight);
        CameraRig& setFocalLength(float millimetres);
        CameraRig& setSensorWidth(float millimetres);
        CameraRig& setLensShift(const Vector2& normalizedViewportShift);
        CameraRig& setRoll(float radians);
        CameraRig& setFocusDistance(float distance);
        CameraRig& setMaxBlur(float pixels);
        CameraRig& setFStop(float fStop);
        CameraRig& setIrisBlades(int blades);
        CameraRig& setHighlightGain(float gain);
        CameraRig& setParent(Layer::Id parentId);

        [[nodiscard]] Vector3 position() const { return layer_.transform.position; }
        [[nodiscard]] Quaternion orientation() const { return layer_.transform.orientation; }
        [[nodiscard]] bool hasTarget() const { return hasTarget_; }
        [[nodiscard]] Vector3 target() const { return target_; }
        [[nodiscard]] float fov() const { return fov_; }
        [[nodiscard]] float roll() const { return roll_; }
        [[nodiscard]] float focusDistance() const { return focusDistance_; }
        [[nodiscard]] float maxBlur() const { return maxBlur_; }
        [[nodiscard]] float fStop() const { return fStop_; }
        [[nodiscard]] int irisBlades() const { return irisBlades_; }
        [[nodiscard]] float highlightGain() const { return highlightGain_; }
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
        [[nodiscard]] const Track<float>& focalLengthTrack() const { return focalTrack_; }
        [[nodiscard]] Track<float>& rollTrack() { return rollTrack_; }
        [[nodiscard]] Track<float>& focusDistanceTrack() { return focusDistanceTrack_; }
        [[nodiscard]] const Track<float>& focusDistanceTrack() const { return focusDistanceTrack_; }
        [[nodiscard]] Track<float>& maxBlurTrack() { return maxBlurTrack_; }
        [[nodiscard]] const Track<float>& maxBlurTrack() const { return maxBlurTrack_; }
        [[nodiscard]] Track<float>& fStopTrack() { return fStopTrack_; }
        [[nodiscard]] const Track<float>& fStopTrack() const { return fStopTrack_; }

        /// Sample the local pose at `seconds`.
        [[nodiscard]] CameraPose sample(float seconds) const;

        /// The analytic derivative of the pose at `seconds`. Reports zero for
        /// every channel without keys at the requested time (outside the keyed
        /// range a track holds its value, so its rate is zero). The angular
        /// velocity is the world-space ω a shutter/blur integrator consumes:
        /// with a target authored, that is the re-aim rotation ω = f × ḟ of the
        /// view axis itself (the look-at overrides the orientation channel and
        /// a dolly toward a fixed target still swings the camera), plus the
        /// roll rate about the backward view axis; without a target, it is the
        /// orientation channel's ω = 2·q̇·q⁻¹ plus the same roll term.
        [[nodiscard]] CameraPoseDerivative sampleDerivative(float seconds) const;

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
        float sensorWidth_{36.f};
        Vector2 lensShift_{0.f, 0.f};
        float roll_{0.f};
        Track<Vector3> targetTrack_;
        Track<float> fovTrack_;
        Track<float> focalTrack_;
        Track<float> rollTrack_;
        float focusDistance_{0.f};
        float maxBlur_{0.f};
        float fStop_{0.f};
        int irisBlades_{0};
        float highlightGain_{0.f};
        Track<float> focusDistanceTrack_;
        Track<float> maxBlurTrack_;
        Track<float> fStopTrack_;
        float aspect_{16.f / 9.f};
        float nearPlane_{0.1f};
        float farPlane_{2000.f};
    };

    /// Blur-gain bridge (DOF unification). The legacy normalized blur gain
    /// (blur in canvas pixels grows as focus_miss_world_units · gain, in
    /// [0, 1], clamped by `maxBlur` canvas pixels) is no longer authored or
    /// transported: the single authored aperture truth is `fStop` (f/N). The
    /// gain survives only as the internal unit of the GL bokeh preview,
    /// derived from f/stop through the sanctioned inverse relation
    /// gain = 0.672 / N (aperture diameter is proportional to gain), clamped
    /// to [0, 1]; f/stop 0 ("not authored") maps to gain 0.
    /// `fStopFromBlurGain` is its inverse, kept for one-time authoring
    /// migration of content that still speaks in gains.
    [[nodiscard]] inline float blurGainFromFStop(float fStop) {
        if (!(fStop > 0.f)) return 0.f;
        const float gain = 0.672f / fStop;
        return gain > 1.f ? 1.f : gain;
    }

    [[nodiscard]] inline float fStopFromBlurGain(float gain) {
        if (!(gain > 0.f)) return 0.f;
        const float fStop = 0.672f / gain;
        if (fStop < 0.7f) return 0.7f;
        return fStop > 32.f ? 32.f : fStop;
    }

    /// The GL bokeh preview keeps the linear blur model (blur in canvas
    /// pixels grows as focus_miss_world_units · gain, clamped by `maxBlur`),
    /// so the px→UV bridge divides the derived gain by the canvas height.
    /// A preview that sets the BokehPass uniforms from a rig pose goes
    /// through these helpers.
    ///
    /// `canvasHeight` is the height in pixels of the view that renders the
    /// rig — for RenderPlan-world scenes that is the plan canvas height; a
    /// physical-scale GL certification scene passes its own px-per-world
    /// aperture, so do not feed it the rig values raw.
    /// boundary-guardrail: allowed — pure optics up-to-unit arithmetic on
    /// authored lens numbers, no content tooling involved.
    [[nodiscard]] inline float bokehPreviewAperture(const CameraPose& pose,
                                                    float canvasHeight) {
        return blurGainFromFStop(pose.fStop) / canvasHeight;
    }

    /// `maxBlur` is authored in canvas pixels; the preview clamp is a UV
    /// fraction of the canvas height, so the bridge divides by that height.
    [[nodiscard]] inline float bokehPreviewMaxBlur(const CameraPose& pose,
                                                   float canvasHeight) {
        return pose.maxBlur / canvasHeight;
    }

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_CAMERARIG_HPP
