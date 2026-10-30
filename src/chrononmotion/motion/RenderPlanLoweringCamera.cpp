#include "RenderPlanLoweringInternal.hpp"
#include <chronon3d/render_plan/camera_sample_provider.hpp>
#include "chrononmotion/math/Finite.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/Layer.hpp"

#include <chronon3d/core/types/time.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

using namespace chrononmotion::motion::lowering_detail;

namespace chrononmotion::motion {

CameraPlan lowerCameraPose(const CameraPose& pose, float nearPlane, float farPlane) {
    ensureFinite(pose.position.x, "camera position");
    ensureFinite(pose.position.y, "camera position");
    ensureFinite(pose.position.z, "camera position");
    ensureFinite(pose.fov, "camera FOV");
    ensureFinite(nearPlane, "camera near plane");
    ensureFinite(farPlane, "camera far plane");
    Quaternion orientation = pose.orientation;
    const float orientationLengthSq = orientation.lengthSq();
    if (pose.fov < 10.f || pose.fov > 120.f || nearPlane <= 0.f || farPlane <= nearPlane ||
        !std::isfinite(orientationLengthSq) || orientationLengthSq <= 1e-12f)
        throw std::invalid_argument("lowerCameraPose: FOV, clipping planes, and orientation must be valid");
    ensureFinite(pose.target.x, "camera target");
    ensureFinite(pose.target.y, "camera target");
    ensureFinite(pose.target.z, "camera target");
    ensureFinite(pose.focalLength, "camera focal length");
    ensureFinite(pose.sensorWidth, "camera sensor width");
    ensureFinite(pose.lensShift.x, "camera lens shift x");
    ensureFinite(pose.lensShift.y, "camera lens shift y");
    ensureFinite(pose.focusDistance, "camera focus distance");
    ensureFinite(pose.maxBlur, "camera maximum blur");
    ensureFinite(pose.fStop, "camera f-stop");
    ensureFinite(pose.highlightGain, "camera highlight gain");
    ensureFinite(pose.roll, "camera roll");
    if (pose.hasTarget || pose.roll != 0.f)
        throw std::invalid_argument(
            "lowerCameraPose: resolve look-at and roll into orientation before lowering; RenderPlan has no target/roll metadata channels");
    if (pose.sensorWidth < 1.f || pose.sensorWidth > 100.f ||
        std::abs(pose.lensShift.x) > 1.f || std::abs(pose.lensShift.y) > 1.f ||
        (pose.focalLength != 0.f && (pose.focalLength < 12.f || pose.focalLength > 200.f)) ||
        (pose.fStop != 0.f && (pose.fStop < 0.7f || pose.fStop > 32.f)) ||
        pose.irisBlades < 0 || pose.irisBlades > 12 ||
        pose.highlightGain < 0.f || pose.highlightGain > 8.f)
        throw std::invalid_argument("lowerCameraPose: physical lens/iris settings are outside the RenderPlan contract");
    if (!std::isfinite(pose.focusDistance) || pose.focusDistance < 0.f || pose.focusDistance > 10'000'000.f ||
        pose.maxBlur < 0.f || pose.maxBlur > 256.f)
        throw std::invalid_argument(
            "lowerCameraPose: depth-of-field values are outside the RenderPlan contract");
    const bool depthOfFieldAuthored = pose.focusDistance != 0.f || pose.maxBlur != 0.f ||
                                      pose.fStop != 0.f || pose.irisBlades != 0 || pose.highlightGain != 0.f;
    const bool depthOfFieldEnabled = pose.fStop > 0.f;
    if (depthOfFieldEnabled && pose.focusDistance <= 0.f)
        throw std::invalid_argument(
            "lowerCameraPose: depth of field with an aperture requires a positive focus distance");
    // CameraRig sampling resolves look-at and roll into orientation. The public
    // pose helper rejects unresolved authoring metadata so direct calls cannot
    // silently drop those fields.
    if (std::abs(orientationLengthSq - 1.f) > 1e-3f)
        throw std::invalid_argument("lowerCameraPose: orientation must be finite and normalized");
    if (!std::isfinite(orientation.x) || !std::isfinite(orientation.y) ||
        !std::isfinite(orientation.z) || !std::isfinite(orientation.w))
        throw std::invalid_argument("lowerCameraPose: orientation must be finite and normalized");
    orientation.normalize();
    // RenderPlan dof.focus_distance carries the camera-space distance along
    // the optical axis exactly as authored (CameraRig focusDistance contract).
    // World-space focus planes are derived downstream with the camera pose,
    // so the lowering passes the distance through unchanged.
    // RenderPlan mirrors world Z and its camera looks along +Z, while Motion
    // uses -Z for forward. Reflecting both world and camera Z gives
    // R_plan = S * R_motion * S, where S = diag(1, 1, -1). In quaternion
    // form this negates X/Y and preserves Z/W; a plain inverse is not the
    // same conversion for combined pan, tilt, and roll.
    orientation.set(-orientation.x, -orientation.y, orientation.z, orientation.w);
    Matrix4 rotation;
    rotation.makeRotationFromQuaternion(orientation);
    const auto euler = chrononCameraEuler(rotation);
    CameraPlan camera;
    camera.position = {pose.position.x, pose.position.y, -pose.position.z};
    camera.rotation_deg = {math::radToDeg(euler[0]), math::radToDeg(euler[1]),
                           math::radToDeg(euler[2])};
    camera.fov_deg = pose.fov;
    camera.near = nearPlane;
    camera.far = farPlane;
    camera.focal_length_mm = pose.focalLength;
    camera.sensor_width_mm = pose.sensorWidth;
    camera.lens_shift = {pose.lensShift.x, pose.lensShift.y};
    camera.f_stop = pose.fStop;
    camera.iris_blades = pose.irisBlades;
    if (depthOfFieldAuthored) {
        camera.dof_enabled = depthOfFieldEnabled;
        // RenderPlan legacy transport field, gain domain: derived from the
        // authored f/stop through the sanctioned bridge, never authored here.
        camera.dof_aperture = blurGainFromFStop(pose.fStop);
        if (pose.focusDistance > 0.f) camera.dof_focus_distance = pose.focusDistance;
        if (pose.maxBlur > 0.f) camera.dof_max_blur = pose.maxBlur;
        camera.dof_f_stop = pose.fStop;
        camera.dof_iris_blades = pose.irisBlades;
        camera.dof_highlight_gain = pose.highlightGain;
    }
    return camera;
}

std::vector<chronon3d::render_plan::CameraSamplePlan> bakeResolvedCameraSamples(
    const CameraRig& rig, float fps, std::size_t frameCount, float nearPlane, float farPlane,
    const Vector3& cameraPositionOffset) {
    if (rig.layer().parentId != Layer::kRoot)
        throw std::invalid_argument("bakeResolvedCameraSamples: parented rigs require MotionScene world transforms");
    if (!std::isfinite(fps) || fps <= 0.f)
        throw std::invalid_argument("bakeResolvedCameraSamples: fps must be finite and positive");
    if (frameCount == 0 || frameCount > 65536)
        throw std::invalid_argument("bakeResolvedCameraSamples: frameCount must be in [1, 65536]");
    std::vector<chronon3d::render_plan::CameraSamplePlan> samples;
    samples.reserve(frameCount);
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        CameraPose pose = rig.sample(static_cast<float>(frame) / fps);
        pose.position.add(cameraPositionOffset);
        pose.hasTarget = false;
        pose.roll = 0.f;
        samples.push_back({static_cast<double>(frame), lowerCameraPose(pose, nearPlane, farPlane)});
    }
    return samples;
}

nlohmann::json cameraSamplesToJson(
    const std::vector<chronon3d::render_plan::CameraSamplePlan>& samples) {
    if (samples.empty())
        throw std::invalid_argument("cameraSamplesToJson: at least one resolved sample is required");
    nlohmann::json result = nlohmann::json::array();
    for (const auto& sample : samples) {
        const auto& camera = sample.camera;
        nlohmann::json value = {
            {"type", camera.type}, {"position", camera.position},
            {"rotation_deg", camera.rotation_deg}, {"fov_deg", camera.fov_deg},
            {"near", camera.near}, {"far", camera.far}, {"zoom", camera.zoom},
            {"sensor_width_mm", camera.sensor_width_mm}, {"lens_shift", camera.lens_shift},
            {"dof", {{"enabled", camera.dof_enabled},
                     {"focus_distance", camera.dof_focus_distance},
                     {"aperture", camera.dof_aperture}, {"max_blur", camera.dof_max_blur}}}};
        if (camera.focal_length_mm > 0.f) value["focal_length_mm"] = camera.focal_length_mm;
        if (camera.f_stop > 0.f) value["f_stop"] = camera.f_stop;
        value["iris_blades"] = camera.iris_blades;
        if (camera.dof_f_stop > 0.f) value["dof"]["f_stop"] = camera.dof_f_stop;
        value["dof"]["iris_blades"] = camera.dof_iris_blades;
        value["dof"]["highlight_gain"] = camera.dof_highlight_gain;
        result.push_back({{"frame", sample.frame}, {"camera", std::move(value)}});
    }
    return result;
}

chronon3d::render_plan::RuntimeCameraSampleProvider makeCameraSampleProvider(
    std::shared_ptr<const CameraRig> rig, float fps, std::string fingerprint,
    float nearPlane, float farPlane) {
    if (!rig || !std::isfinite(fps) || fps <= 0.f || fingerprint.empty())
        throw std::invalid_argument(
            "makeCameraSampleProvider: rig, positive finite fps, and stable fingerprint are required");
    if (rig->layer().parentId != Layer::kRoot)
        throw std::invalid_argument(
            "makeCameraSampleProvider: parented rig requires a MotionScene");
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        nearPlane <= 0.f || farPlane <= nearPlane)
        throw std::invalid_argument(
            "makeCameraSampleProvider: clipping planes are invalid");

    chronon3d::render_plan::RuntimeCameraSampleProvider provider;
    provider.fingerprint = std::move(fingerprint);
    provider.sample = [rig = std::move(rig), fps, nearPlane, farPlane](double frame) {
        if (!std::isfinite(frame) || frame < -1'000'000.0 || frame > 1'000'000.0)
            throw std::invalid_argument(
                "CameraSampleProvider: frame must be finite and in [-1000000, 1000000]");
        const float seconds = static_cast<float>(frame / static_cast<double>(fps));
        if (!std::isfinite(seconds))
            throw std::invalid_argument("CameraSampleProvider: sample time is not representable");
        // CameraRig::sample() already bakes look-at and roll into the
        // orientation. Clear the authoring metadata before lowering: the
        // RenderPlan has no target/roll channels, and lowerCameraPose() must
        // keep rejecting unresolved direct calls. This mirrors the offline
        // bake path above instead of letting the runtime provider diverge.
        CameraPose pose = rig->sample(seconds);
        pose.hasTarget = false;
        pose.roll = 0.f;
        return lowerCameraPose(pose, nearPlane, farPlane);
    };
    return provider;
}

chronon3d::render_plan::RuntimeCameraSampleProvider makeCameraSampleProvider(
    std::shared_ptr<const CameraRig> rig, std::shared_ptr<MotionScene> scene,
    std::string fingerprint, float nearPlane, float farPlane) {
    if (!rig || !scene || fingerprint.empty() || !std::isfinite(scene->fps()) || scene->fps() <= 0.f)
        throw std::invalid_argument(
            "makeCameraSampleProvider: rig, scene, positive fps and stable fingerprint are required");
    const auto parentId = rig->layer().parentId;
    if (parentId == Layer::kRoot || !scene->findLayer(parentId))
        throw std::invalid_argument(
            "makeCameraSampleProvider: parent must reference a layer in the MotionScene");
    const auto problems = scene->validate();
    if (!problems.empty())
        throw std::invalid_argument("makeCameraSampleProvider: scene validation failed: " + problems.front());
    if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) ||
        nearPlane <= 0.f || farPlane <= nearPlane)
        throw std::invalid_argument("makeCameraSampleProvider: clipping planes are invalid");

    const float fps = scene->fps();
    auto mutex = std::make_shared<std::mutex>();
    chronon3d::render_plan::RuntimeCameraSampleProvider provider;
    provider.fingerprint = std::move(fingerprint);
    provider.sample = [rig = std::move(rig), scene = std::move(scene), mutex = std::move(mutex),
                       fps, parentId, nearPlane, farPlane](double frame) {
        if (!std::isfinite(frame) || frame < -1'000'000.0 || frame > 1'000'000.0)
            throw std::invalid_argument(
                "CameraSampleProvider: frame must be finite and in [-1000000, 1000000]");
        const double floorFrame = std::floor(frame);
        int whole = static_cast<int>(floorFrame);
        float fraction = static_cast<float>(frame - floorFrame);
        if (fraction >= 1.f) { ++whole; fraction = 0.f; }
        const MotionTime time{whole, fps, fraction};
        std::lock_guard lock(*mutex);
        if (rig->layer().parentId != parentId || scene->fps() != fps)
            throw std::invalid_argument("CameraSampleProvider: rig parent or scene fps changed");
        scene->evaluate(time);
        CameraPose pose = rig->sampleWorld(time.pts(), scene->worldMatrix(parentId));
        pose.hasTarget = false;
        pose.roll = 0.f;
        return lowerCameraPose(pose, nearPlane, farPlane);
    };
    return provider;
}
} // namespace chrononmotion::motion
