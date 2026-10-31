#include "RenderPlanLoweringInternal.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "chrononmotion/math/Finite.hpp"

#include "chrononmotion/math/Euler.hpp"
#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/Layer.hpp"

#include <chronon3d/core/types/time.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace chrononmotion::motion {
using namespace lowering_detail;
namespace {
using namespace chronon3d::render_plan;
ResolvedPose resolvePose(const Matrix4& matrix, float opacity, bool visible,
                         const Vector3& anchor, bool needsOriginPivot,
                         bool allowReflection) {
    if (!matrixFinite(matrix))
        throw std::invalid_argument("lowerToRenderPlan: world matrix contains non-finite values");
    const Vector3 origin = transformPoint(matrix, Vector3(0.f, 0.f, 0.f));
    const Vector3 pivot = transformPoint(matrix, anchor);
    if (needsOriginPivot && pivot.distanceTo(origin) > 1e-4f)
        throw std::invalid_argument("lowerToRenderPlan: RenderPlan has no anchor field; non-zero transformed anchor offsets are not representable");
    const Vector3 x = transformPoint(matrix, Vector3(1.f, 0.f, 0.f)) - origin;
    const Vector3 y = transformPoint(matrix, Vector3(0.f, 1.f, 0.f)) - origin;
    const Vector3 z = transformPoint(matrix, Vector3(0.f, 0.f, 1.f)) - origin;
    const float sx = x.length(), sy = y.length(), sz = z.length();
    if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz) ||
        sx <= 1e-8f || sy <= 1e-8f || sz <= 1e-8f)
        throw std::invalid_argument("lowerToRenderPlan: singular or non-finite world transform cannot be represented");
    if (!close(x.dot(y), 0.f) || !close(x.dot(z), 0.f) || !close(y.dot(z), 0.f))
        throw std::invalid_argument("lowerToRenderPlan: sheared world transform cannot be represented by RenderPlan TRS");
    const Vector3 normalizedX = x / sx;
    const Vector3 normalizedY = y / sy;
    const Vector3 normalizedZ = z / sz;
    if (!close(normalizedX.length(), 1.f) || !close(normalizedY.length(), 1.f) || !close(normalizedZ.length(), 1.f))
        throw std::invalid_argument("lowerToRenderPlan: invalid normalized world basis");
    const bool reflected = normalizedX.clone().cross(normalizedY).dot(normalizedZ) < 0.f;
    if (reflected && !allowReflection)
        throw std::invalid_argument("lowerToRenderPlan: reflected world transform cannot be represented without changing winding");
    Vector3 rotationX = normalizedX;
    if (reflected) rotationX.negate();
    Matrix4 rotation;
    rotation.makeBasis(rotationX, normalizedY, normalizedZ);
    Euler angles;
    angles.setFromRotationMatrix(rotation);
    ResolvedPose result;
    result.position = origin;
    result.scale.set(reflected ? -sx : sx, sy, sz);
    result.rotation.set(angles.x, angles.y, angles.z);
    if (!std::isfinite(opacity))
        throw std::invalid_argument("lowerToRenderPlan: world opacity is non-finite");
    result.opacity = visible ? std::clamp(opacity, 0.f, 1.f) : 0.f;
    ensureFinite(result.position.x, "position");
    ensureFinite(result.position.y, "position");
    ensureFinite(result.position.z, "position");
    ensureFinite(result.rotation.x, "rotation");
    ensureFinite(result.rotation.y, "rotation");
    ensureFinite(result.rotation.z, "rotation");
    return result;
}



std::vector<const Layer*> sceneLayers(const MotionScene& scene) {
    std::vector<const Layer*> result;
    result.reserve(scene.layerCount());
    for (const auto& layer : scene.layers()) result.push_back(&layer);
    return result;
}

} // namespace

RenderPlan lowerToRenderPlan(MotionScene& scene, const RenderPlanLoweringOptions& options) {
    if (scene.layerCount() > 1024)
        throw std::invalid_argument("lowerToRenderPlan: scene exceeds the RenderPlan v2 layer limit");
    const auto problems = scene.validate();
    if (!problems.empty())
        throw std::invalid_argument("lowerToRenderPlan: scene validation failed: " + problems.front());
    if (!options.cameraSampleFrames.empty() && (!options.cameraRig || !options.bakeResolvedCameraSamples))
        throw std::invalid_argument("lowerToRenderPlan: explicit camera sample frames require a baked CameraRig");
    if (options.cameraSampleFrames.size() > 1'000'000)
        throw std::invalid_argument("lowerToRenderPlan: explicit camera sample sequence exceeds 1000000 samples");
    double previousCameraFrame = -std::numeric_limits<double>::infinity();
    for (double frame : options.cameraSampleFrames) {
        if (!std::isfinite(frame) || frame <= previousCameraFrame || frame < -1'000'000.0 || frame > 1'000'000.0)
            throw std::invalid_argument("lowerToRenderPlan: explicit camera sample frames must be finite, strictly increasing, and within [-1000000, 1000000]");
        previousCameraFrame = frame;
    }
    const Layer::Id cameraParentId = options.cameraRig ? options.cameraRig->layer().parentId : Layer::kRoot;
    if (cameraParentId != Layer::kRoot && scene.findLayer(cameraParentId) == nullptr)
        throw std::invalid_argument("lowerToRenderPlan: CameraRig parentId does not reference an authored layer in MotionScene");
    int lastFrame = std::max(0, scene.contentEndFrame());
    if (options.cameraRig) {
        const float cameraEndTime = options.cameraRig->endTime();
        if (!std::isfinite(cameraEndTime) || cameraEndTime < 0.f)
            throw std::invalid_argument("lowerToRenderPlan: camera rig has an invalid animation end time");
        const double cameraFrame = static_cast<double>(cameraEndTime) * scene.fps();
        if (!std::isfinite(cameraFrame) || cameraFrame >= 1'000'000.)
            throw std::invalid_argument("lowerToRenderPlan: camera rig animation exceeds the offline frame budget");
        // Track times are float seconds (e.g. 2.f / 30.f); multiplication can
        // land a few ulps above the authored integer frame. Snap only values
        // within float-time precision, otherwise include the partially covered
        // output frame by rounding upward.
        const double nearestFrame = std::round(cameraFrame);
        const double frameTolerance = 4.0 * std::numeric_limits<float>::epsilon() *
                                      std::max(1.0, std::abs(cameraFrame));
        const double cameraLastFrame = std::abs(cameraFrame - nearestFrame) <= frameTolerance
            ? nearestFrame : std::ceil(cameraFrame);
        lastFrame = std::max(lastFrame, static_cast<int>(cameraLastFrame));
    }
    if (lastFrame >= 1'000'000)
        throw std::invalid_argument("lowerToRenderPlan: dense bake exceeds the offline sample budget");
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame) + 1;
    if (frameCount > kMaximumBakedTransformSamples ||
        (scene.layerCount() != 0 && frameCount > kMaximumBakedTransformSamples / scene.layerCount()) ||
        frameCount > 1'000'000)
        throw std::invalid_argument("lowerToRenderPlan: dense bake exceeds the offline sample budget");
    const auto layerPointers = sceneLayers(scene);
    std::vector<Layer> metadata;
    metadata.reserve(layerPointers.size());
    std::unordered_map<Layer::Id, std::size_t> indices;
    for (std::size_t index = 0; index < layerPointers.size(); ++index) {
        metadata.push_back(*layerPointers[index]);
        indices.emplace(layerPointers[index]->id, index);
    }
    std::vector<std::vector<ResolvedPose>> samples(scene.layerCount());
    for (auto& layerSamples : samples) layerSamples.reserve(frameCount);
    std::vector<Matrix4> cameraParentWorlds;
    if (cameraParentId != Layer::kRoot) cameraParentWorlds.reserve(frameCount);
    std::map<Layer::Id, std::vector<LightState>> lightSamples;
    const bool restoreEvaluation = scene.hasEvaluated();
    const MotionTime previousTime = scene.lastEvaluatedTime();
    struct SceneEvaluationRestore {
        MotionScene& scene;
        MotionTime time;
        bool restore;
        ~SceneEvaluationRestore() { if (restore) { try { scene.evaluate(time); } catch (...) { } } }
    } restore{scene, previousTime, restoreEvaluation};
    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        scene.evaluate(static_cast<int>(frame));
        if (cameraParentId != Layer::kRoot) {
            cameraParentWorlds.push_back(scene.worldMatrix(cameraParentId));
        }
        const CompiledScene compiled = scene.compile();
        for (const LightState& light : compiled.lights) {
            auto& frames = lightSamples[light.sample.id];
            if (frames.empty()) frames.reserve(frameCount);
            frames.push_back(light);
        }
        if (compiled.transforms.size() != layerPointers.size())
            throw std::invalid_argument("lowerToRenderPlan: compiled scene omitted authored layer transforms");
        for (const auto& transform : compiled.transforms) {
            const auto found = indices.find(transform.id);
            if (found == indices.end())
                throw std::invalid_argument("lowerToRenderPlan: compiled scene contains an unknown layer id");
            const std::size_t index = found->second;
            const auto supplied = options.contentLayers.find(std::to_string(transform.id));
            const Layer* authoredLayer = scene.findLayer(transform.id);
            if (authoredLayer == nullptr)
                throw std::invalid_argument("lowerToRenderPlan: compiled transform has no authored layer");
            if (authoredLayer->isNull() && supplied != options.contentLayers.end())
                throw std::invalid_argument("lowerToRenderPlan: null/controller layers cannot own RenderPlan content");
            const bool checkAnchor = supplied != options.contentLayers.end() &&
                (supplied->second.type == LayerType::Text || supplied->second.type == LayerType::Shape ||
                 supplied->second.type == LayerType::Particles || supplied->second.type == LayerType::Light);
            const bool allowReflection = supplied != options.contentLayers.end() &&
                supplied->second.type == LayerType::Image;
            const ResolvedPose pose = resolvePose(transform.world, transform.opacity, transform.visible,
                                                  scene.state(transform.id).local.anchor, checkAnchor,
                                                  allowReflection);
            samples[index].push_back(pose);
        }
    }
    RenderPlanLoweringOptions resolvedOptions = options;
    if (options.cameraRig) {
        if (options.camera && options.camera->type != "perspective")
            throw std::invalid_argument("lowerToRenderPlan: CameraRig only lowers to a perspective RenderPlan camera");
        const float nearPlane = options.camera ? options.camera->near : options.cameraRig->nearPlane();
        const float farPlane = options.camera ? options.camera->far : options.cameraRig->farPlane();
        CameraPose firstPose = cameraParentId != Layer::kRoot
            ? options.cameraRig->sampleWorld(0.f, cameraParentWorlds.front())
            : options.cameraRig->sample(0.f);
        // CameraRig::sample has already resolved these authoring channels into
        // orientation; clear only the redundant metadata before projection.
        firstPose.hasTarget = false;
        firstPose.roll = 0.f;
        resolvedOptions.camera = lowerCameraPose(firstPose, nearPlane, farPlane);
    } else if (!options.camera) {
        for (const auto& layer : metadata) {
            if (layer.isNull() && layer.name == "Camera")
                throw std::invalid_argument("lowerToRenderPlan: a MotionScene camera layer must be supplied through RenderPlanLoweringOptions::cameraRig");
        }
    }
    RenderPlan plan = lowering_detail::lowerSamplesToRenderPlan(
        scene.fps(), frameCount, resolvedOptions, metadata, samples);
    for (const auto& [lightId, frames] : lightSamples) {
        if (frames.size() != frameCount)
            throw std::invalid_argument("lowerToRenderPlan: light sample count changed during frame evaluation");
        const LightSample& firstSample = frames.front().sample;
        if (firstSample.kind == LightKind::Ambient) {
            plan.warnings.emplace_back("Ambient Motion lights are not representable as RenderPlan light layers");
            continue;
        }

        LayerPlan layer;
        layer.id = "motion_light_" + std::to_string(lightId);
        layer.type = LayerType::Light;
        layer.start_frame = chronon3d::Frame{0};
        layer.duration_frames = chronon3d::Frame{static_cast<std::int64_t>(frameCount)};
        layer.position = {frames.front().position.x, frames.front().position.y,
                          -frames.front().position.z};
        layer.position_dimensions = 3;
        layer.enable_3d = true;

        LightLayerPlan payload;
        payload.kind = firstSample.kind == LightKind::Point ? LightKindPlan::Point
                     : firstSample.kind == LightKind::Spot ? LightKindPlan::Spot
                     : LightKindPlan::Directional;
        payload.radius = 256.f;
        payload.color = {firstSample.color.r, firstSample.color.g, firstSample.color.b, 1.f};
        payload.intensity = firstSample.intensity;
        payload.direction = {frames.front().direction.x, frames.front().direction.y,
                            -frames.front().direction.z};
        payload.range = firstSample.range;
        payload.decay = firstSample.decay;
        payload.angle = firstSample.angle > 0.f ? firstSample.angle : 0.7853981634f;
        payload.penumbra = firstSample.penumbra;
        payload.cast_shadow = firstSample.castsShadow;
        payload.shadow_radius = firstSample.shadowRadius;
        payload.shadow_bias = firstSample.shadowBias;
        layer.light = payload;

        AnimationPlan animation;
        AnimationTrackPlan positionTrack;
        positionTrack.property = "position";
        AnimationTrackPlan intensityTrack;
        intensityTrack.property = "light_intensity";
        AnimationTrackPlan colorTrack;
        colorTrack.property = "light_color";
        AnimationTrackPlan directionTrack;
        directionTrack.property = "light_direction";
        bool positionChanges = false, intensityChanges = false, colorChanges = false, directionChanges = false;
        for (std::size_t frame = 0; frame < frames.size(); ++frame) {
            const LightState& state = frames[frame];
            const LightSample& value = state.sample;
            const auto key = chronon3d::Frame{static_cast<std::int64_t>(frame)};
            const std::array<float, 3> position{
                state.position.x - frames.front().position.x,
                state.position.y - frames.front().position.y,
                -(state.position.z - frames.front().position.z)};
            positionTrack.keyframes.push_back(makePlanKeyframe(key,
                {position[0], position[1], position[2]}));
            intensityTrack.keyframes.push_back(makePlanKeyframe(key, {value.intensity}));
            colorTrack.keyframes.push_back(makePlanKeyframe(key,
                {value.color.r, value.color.g, value.color.b, 1.f}));
            directionTrack.keyframes.push_back(makePlanKeyframe(key,
                {state.direction.x, state.direction.y, -state.direction.z}));
            positionChanges = positionChanges || position[0] != 0.f || position[1] != 0.f || position[2] != 0.f;
            intensityChanges = intensityChanges || value.intensity != firstSample.intensity;
            colorChanges = colorChanges || value.color.r != firstSample.color.r ||
                           value.color.g != firstSample.color.g || value.color.b != firstSample.color.b;
            directionChanges = directionChanges || state.direction.x != frames.front().direction.x ||
                               state.direction.y != frames.front().direction.y ||
                               state.direction.z != frames.front().direction.z;
        }
        if (positionChanges) animation.tracks.push_back(std::move(positionTrack));
        if (intensityChanges) animation.tracks.push_back(std::move(intensityTrack));
        if (colorChanges) animation.tracks.push_back(std::move(colorTrack));
        if (directionChanges && payload.kind != LightKindPlan::Point)
            animation.tracks.push_back(std::move(directionTrack));
        if (!animation.tracks.empty()) layer.animation = std::move(animation);
        plan.layers.push_back(std::move(layer));
    }
    // Full-scene shutter (docs/BOUNDARY_C3D.md C6): the scene authoring is the
    // single unit and the caller-supplied option is the programmatic override.
    // Authoring both is ambiguous and fails closed.
    if (options.temporalMotionBlur && scene.hasShutter())
        throw std::invalid_argument(
                "lowerToRenderPlan: temporal motion blur is authored both in the scene and in the lowering options");
    if (options.temporalMotionBlur || scene.hasShutter()) {
        const chronon3d::render_plan::MotionBlurPlan blur =
                options.temporalMotionBlur
                        ? *options.temporalMotionBlur
                        : chronon3d::render_plan::MotionBlurPlan{scene.shutter()->shutterAngleDegrees,
                                                                 scene.shutter()->samples};
        if (plan.schema != kRenderPlanSchemaV3)
            throw std::invalid_argument("lowerToRenderPlan: temporal motion blur requires RenderPlan V3");
        if (!std::isfinite(blur.shutter_angle) || blur.shutter_angle <= 0.f || blur.shutter_angle > 360.f ||
            blur.samples < 2 || blur.samples > 64)
            throw std::invalid_argument("lowerToRenderPlan: temporal motion blur requires shutter_angle in (0, 360] and samples in [2, 64]");
        plan.temporal_motion_blur = blur;
    }
    // The renderer owns shutter scheduling. Evaluate camera AND world layer
    // transforms at its exact timestamps instead of interpolating integer bakes.
    std::vector<double> sampleFrames = options.cameraSampleFrames;
    if (sampleFrames.empty() && plan.temporal_motion_blur)
        sampleFrames = chronon3d::render_plan::resolved_sample_frames(plan.canvas, plan.temporal_motion_blur);
    if (!sampleFrames.empty()) {
        if (plan.schema != kRenderPlanSchemaV3 ||
            (scene.layerCount() != 0 && sampleFrames.size() > kMaximumBakedTransformSamples / scene.layerCount()))
            throw std::invalid_argument("lowerToRenderPlan: resolved transform bake requires bounded V3 samples");
        for (auto& output : plan.layers) {
            const auto authored = std::find_if(metadata.begin(), metadata.end(), [&](const Layer& layer) {
                return std::to_string(layer.id) == output.id;
            });
            if (authored == metadata.end() || authored->isNull()) continue;
            if (output.type != LayerType::Image && output.type != LayerType::Color &&
                output.type != LayerType::Text && output.type != LayerType::Shape)
                throw std::invalid_argument("lowerToRenderPlan: exact shutter layer type is unsupported");
            if (output.animation) {
                for (const auto& track : output.animation->tracks)
                    if (track.property != "position" && track.property != "scale" &&
                        track.property != "rotation" && track.property != "opacity")
                        throw std::invalid_argument("lowerToRenderPlan: exact shutter cannot discard non-transform animation");
            }
            output.animation.reset();
            output.transform_samples.reserve(sampleFrames.size());
            for (double frame : sampleFrames) {
                const double wholeFrame = std::floor(frame);
                int whole = static_cast<int>(wholeFrame);
                float fraction = static_cast<float>(frame - wholeFrame);
                if (fraction >= 1.f) { ++whole; fraction = 0.f; }
                scene.evaluate(MotionTime{whole, scene.fps(), fraction});
                const auto compiled = scene.compile();
                const auto transform = std::find_if(compiled.transforms.begin(), compiled.transforms.end(),
                    [&](const auto& value) { return value.id == authored->id; });
                if (transform == compiled.transforms.end())
                    throw std::invalid_argument("lowerToRenderPlan: missing resolved shutter layer");
                const bool primitive = output.type == LayerType::Text || output.type == LayerType::Shape;
                const auto pose = resolvePose(transform->world, transform->opacity, transform->visible,
                    scene.state(authored->id).local.anchor, primitive, output.type == LayerType::Image);
                // Reflect the complete world rotation, not just Euler Z.
                // The consumer reconstructs qZ*qY*qX, so use its inverse.
                Matrix4 rotation;
                const float sx = pose.scale.x, sy = pose.scale.y, sz = pose.scale.z;
                rotation.makeBasis(
                    Vector3{transform->world.elements[0] / sx, transform->world.elements[1] / sx, -transform->world.elements[2] / sx},
                    Vector3{transform->world.elements[4] / sy, transform->world.elements[5] / sy, -transform->world.elements[6] / sy},
                    Vector3{-transform->world.elements[8] / sz, -transform->world.elements[9] / sz, transform->world.elements[10] / sz});
                const auto euler = chrononCameraEuler(rotation);
                output.transform_samples.push_back({frame,
                    {pose.position.x, pose.position.y, -pose.position.z},
                    {sx, sy, sz},
                    {math::radToDeg(euler[0]), math::radToDeg(euler[1]), math::radToDeg(euler[2])},
                    pose.opacity});
            }
        }
    }
    if (options.cameraRig && options.bakeResolvedCameraSamples) {
        const std::size_t frameCount = static_cast<std::size_t>(
            std::max<std::int64_t>(1, plan.canvas.duration.integral()));
        if (options.cameraSampleFrames.empty() && frameCount > 65536)
            throw std::invalid_argument("lowerToRenderPlan: resolved camera sample sequence exceeds 65536 frames");
        if (!sampleFrames.empty()) {
            std::vector<chronon3d::render_plan::CameraSamplePlan> explicitSamples;
            explicitSamples.reserve(sampleFrames.size());
            for (double frame : sampleFrames) {
                const double wholeFrame = std::floor(frame);
                int whole = static_cast<int>(wholeFrame);
                float fraction = static_cast<float>(frame - wholeFrame);
                if (fraction >= 1.f) { ++whole; fraction = 0.f; }
                const MotionTime time{whole, scene.fps(), fraction};
                CameraPose pose;
                if (cameraParentId != Layer::kRoot) {
                    scene.evaluate(time);
                    pose = options.cameraRig->sampleWorld(time.pts(), scene.worldMatrix(cameraParentId));
                } else {
                    pose = options.cameraRig->sample(static_cast<float>(frame / scene.fps()));
                }
                pose.hasTarget = false;
                pose.roll = 0.f;
                explicitSamples.push_back({frame, lowerCameraPose(
                    pose, resolvedOptions.camera->near, resolvedOptions.camera->far)});
            }
            plan.camera_samples = std::move(explicitSamples);
        } else if (cameraParentId != Layer::kRoot) {
            std::vector<chronon3d::render_plan::CameraSamplePlan> parentedSamples;
            parentedSamples.reserve(frameCount);
            for (std::size_t frame = 0; frame < frameCount; ++frame) {
                const Matrix4& parentWorld = frame < cameraParentWorlds.size()
                    ? cameraParentWorlds[frame]
                    : cameraParentWorlds.back();
                const CameraPose pose = options.cameraRig->sampleWorld(
                    static_cast<float>(frame) / scene.fps(), parentWorld);
                parentedSamples.push_back({
                    static_cast<double>(frame),
                    lowerCameraPose(pose, resolvedOptions.camera->near, resolvedOptions.camera->far)});
            }
            plan.camera_samples = std::move(parentedSamples);
        } else {
            plan.camera_samples = bakeResolvedCameraSamples(*options.cameraRig, scene.fps(), frameCount,
                resolvedOptions.camera->near, resolvedOptions.camera->far);
        }
        bool sequenceUsesDof = resolvedOptions.camera->dof_enabled;
        for (const auto& sample : *plan.camera_samples)
            sequenceUsesDof = sequenceUsesDof || sample.camera.dof_enabled;
        resolvedOptions.camera->dof_enabled = sequenceUsesDof;
        plan.camera = resolvedOptions.camera;
    }
    return plan;
}

} // namespace chrononmotion::motion
