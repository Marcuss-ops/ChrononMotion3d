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
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    result.world = matrix;
    return result;
}

constexpr std::size_t kMaximumPrecompDepth = 64;

struct PrecompStep {
    MotionScene* parentScene{};
    Layer::Id precompId{Layer::kRoot};
    MotionScene* childScene{};
};

struct FlattenedLeafInfo {
    Layer::Id sourceId{Layer::kRoot};
    MotionScene* scene{};
    std::vector<PrecompStep> path{};
};

struct FlattenedLightInfo {
    Light::Id sourceId{Layer::kRoot};
    MotionScene* scene{};
    std::string outputId{};
};

struct FlattenedInstance {
    MotionScene* scene{};
    Layer::Id precompId{Layer::kRoot};
    std::vector<std::size_t> leaves{};
    std::vector<std::size_t> lights{};
    std::vector<FlattenedInstance> children{};
};

struct FlattenedScene {
    FlattenedInstance root{};
    std::vector<FlattenedLayer> layers{};
    std::vector<FlattenedLeafInfo> leaves{};
    std::vector<FlattenedLightInfo> lights{};
};

MotionTime motionTimeFromDoubleFrame(double frame, float fps) {
    if (!std::isfinite(frame) || !std::isfinite(fps) || fps <= 0.f)
        throw std::invalid_argument("lowerToRenderPlan: invalid nested composition timestamp");
    const double whole = std::floor(frame);
    if (!std::isfinite(whole) || whole < std::numeric_limits<int>::min() ||
        whole > std::numeric_limits<int>::max())
        throw std::invalid_argument("lowerToRenderPlan: nested composition timestamp exceeds the frame range");
    int frameIndex = static_cast<int>(whole);
    float fraction = static_cast<float>(frame - whole);
    if (fraction >= 1.f - 1e-6f) {
        if (frameIndex == std::numeric_limits<int>::max())
            throw std::invalid_argument("lowerToRenderPlan: nested composition timestamp exceeds the frame range");
        ++frameIndex;
        fraction = 0.f;
    }
    if (fraction < 0.f) fraction = 0.f;
    return MotionTime{frameIndex, fps, fraction};
}

MotionTime motionTimeFromFrame(float frame, float fps) {
    return motionTimeFromDoubleFrame(static_cast<double>(frame), fps);
}

FlattenedScene flattenScene(MotionScene& scene) {
    FlattenedScene flat;
    flat.root.scene = &scene;
    std::unordered_set<std::string> topLevelIds;
    for (const auto& layer : scene.layers())
        topLevelIds.insert(std::to_string(layer.id));

    std::unordered_set<const MotionScene*> activeScenes;
    activeScenes.insert(&scene);
    std::function<void(FlattenedInstance&, MotionScene&, const std::string&,
                       const std::vector<PrecompStep>&, bool, std::size_t)> build;
    build = [&](FlattenedInstance& instance, MotionScene& owner, const std::string& prefix,
                const std::vector<PrecompStep>& path, bool ancestorRemapped, std::size_t depth) {
        if (depth > kMaximumPrecompDepth)
            throw std::invalid_argument("lowerToRenderPlan: nested composition depth exceeds the offline lowering limit");
        instance.scene = &owner;
        for (const auto& light : owner.lights()) {
            FlattenedLightInfo info;
            info.sourceId = light->id;
            info.scene = &owner;
            std::string path = prefix;
            std::replace(path.begin(), path.end(), '/', '_');
            info.outputId = "motion_light_" + path + std::to_string(light->id);
            flat.lights.push_back(std::move(info));
            instance.lights.push_back(flat.lights.size() - 1);
        }
        // Preserve the historical numeric light order for every scene instance;
        // nested instances are new, but top-level plans must not reorder lights.
        std::sort(instance.lights.begin(), instance.lights.end(),
                  [&](std::size_t left, std::size_t right) {
                      return flat.lights[left].sourceId < flat.lights[right].sourceId;
                  });
        for (const Layer& layer : owner.layers()) {
            if (!layer.precomp.empty()) {
                if (!layer.precomp.valid())
                    throw std::invalid_argument("lowerToRenderPlan: layer " + std::to_string(layer.id) +
                                                " has an invalid nested composition");
                if (!layer.isNull())
                    throw std::invalid_argument("lowerToRenderPlan: layer " + std::to_string(layer.id) +
                                                " cannot own renderable content and a nested composition at the same time");
                if (!std::isfinite(layer.precomp.localFps) || layer.precomp.localFps <= 0.f)
                    throw std::invalid_argument("lowerToRenderPlan: nested composition local fps must be finite and positive");
                MotionScene* child = layer.precomp.scene.get();
                if (child == nullptr)
                    throw std::invalid_argument("lowerToRenderPlan: nested composition has no scene");
                if (activeScenes.contains(child))
                    throw std::invalid_argument("lowerToRenderPlan: nested composition cycle detected at layer " +
                                                std::to_string(layer.id));
                if (child->hasShutter())
                    throw std::invalid_argument("lowerToRenderPlan: nested compositions cannot author a second shutter");
                const auto problems = child->validate();
                if (!problems.empty())
                    throw std::invalid_argument("lowerToRenderPlan: nested composition validation failed: " + problems.front());
                activeScenes.insert(child);
                const PrecompStep step{&owner, layer.id, child};
                std::vector<PrecompStep> childPath = path;
                childPath.push_back(step);
                FlattenedInstance childInstance;
                childInstance.precompId = layer.id;
                build(childInstance, *child, prefix + std::to_string(layer.id) + "/", childPath,
                      ancestorRemapped || hasMotionTimeRemap(layer), depth + 1);
                activeScenes.erase(child);
                instance.children.push_back(std::move(childInstance));
                continue;
            }
            if (layer.isNull()) {
                if (!prefix.empty() && layer.name == "Camera")
                    throw std::invalid_argument("lowerToRenderPlan: a camera controller inside a nested composition is not representable; author the camera on the outer scene");
                continue;
            }
            if (ancestorRemapped && hasMotionTimeRemap(layer))
                throw std::invalid_argument("lowerToRenderPlan: time remap on layer " + std::to_string(layer.id) +
                                            " inside a time-remapped nested composition is not representable");
            if (ancestorRemapped && (layer.content.isVideo() || layer.content.isParticles()))
                throw std::invalid_argument("lowerToRenderPlan: time-dependent content on layer " +
                                            std::to_string(layer.id) +
                                            " inside a time-remapped nested composition is not representable");
            FlattenedLayer meta(layer);
            meta.layer.precomp = {};
            meta.layer.parentId = Layer::kRoot;
            meta.outputId = prefix.empty() ? std::to_string(layer.id) : prefix + std::to_string(layer.id);
            meta.sourceId = std::to_string(layer.id);
            meta.pathPrefix = prefix;
            meta.fromPrecomp = !prefix.empty();
            meta.fallbackToSource = !meta.fromPrecomp || !topLevelIds.contains(meta.sourceId);
            if (flat.layers.size() >= 1024)
                throw std::invalid_argument("lowerToRenderPlan: flattened composition exceeds the RenderPlan layer limit");
            const std::size_t index = flat.layers.size();
            flat.layers.push_back(std::move(meta));
            flat.leaves.push_back({layer.id, &owner, path});
            instance.leaves.push_back(index);
        }
    };
    build(flat.root, scene, {}, {}, false, 0);
    return flat;
}

LightState composeLightState(const FlattenedLightInfo& info, const Matrix4& parentWorld,
                             float parentOpacity, bool parentVisible, bool applyParent) {
    LightState state = info.scene->lightState(info.sourceId);
    if (applyParent) {
        state.position = transformPoint(parentWorld, state.position);
        state.direction.transformDirection(parentWorld);
        if (state.direction.length() > 1e-8f) state.direction.normalize();
        Matrix4 world;
        world.multiplyMatrices(parentWorld, state.world);
        state.world = world;
    }
    state.sample.intensity *= parentOpacity;
    state.visible = parentVisible && state.visible;
    if (!state.visible) state.sample.intensity = 0.f;
    return state;
}

void resolveInstance(const FlattenedScene& flat, const FlattenedInstance& instance,
                     const Matrix4& parentWorld, float parentOpacity, bool parentVisible,
                     const RenderPlanLoweringOptions& options,
                     std::vector<std::vector<ResolvedPose>>& samples,
                     std::vector<std::vector<LightState>>* lightSamples = nullptr) {
    if (lightSamples != nullptr) {
        const bool applyParent = instance.precompId != Layer::kRoot;
        for (const std::size_t index : instance.lights)
            lightSamples->at(index).push_back(composeLightState(
                flat.lights[index], parentWorld, parentOpacity, parentVisible, applyParent));
    }
    for (const std::size_t index : instance.leaves) {
        const FlattenedLayer& meta = flat.layers[index];
        const FlattenedLeafInfo& leaf = flat.leaves[index];
        const LayerState& state = instance.scene->state(leaf.sourceId);
        Matrix4 world;
        world.multiplyMatrices(parentWorld, state.world);
        const LayerPlan* supplied = findContentLayer(options, meta);
        const bool checkAnchor = meta.layer.content.isParticles() ||
            (supplied != nullptr &&
             (supplied->type == LayerType::Text || supplied->type == LayerType::Shape ||
              supplied->type == LayerType::Particles || supplied->type == LayerType::Light));
        const bool allowReflection = supplied != nullptr && supplied->type == LayerType::Image;
        samples[index].push_back(resolvePose(world, parentOpacity * state.worldOpacity,
                                             parentVisible && state.visible, state.local.anchor,
                                             checkAnchor, allowReflection));
    }
    for (const FlattenedInstance& child : instance.children) {
        const LayerState& state = instance.scene->state(child.precompId);
        Matrix4 world;
        world.multiplyMatrices(parentWorld, state.world);
        const Layer* precompLayer = instance.scene->findLayer(child.precompId);
        if (precompLayer == nullptr)
            throw std::invalid_argument("lowerToRenderPlan: nested composition source layer disappeared during evaluation");
        const MotionTime& parentTime = instance.scene->lastEvaluatedTime();
        const float localFrame = precompLayer->mapFrame(
            static_cast<float>(parentTime.frame) + parentTime.subFrame, parentTime.pts());
        const MotionTime childTime = motionTimeFromFrame(localFrame, child.scene->fps());
        child.scene->evaluate(childTime);
        resolveInstance(flat, child, world, parentOpacity * state.worldOpacity,
                        parentVisible && state.visible, options, samples, lightSamples);
    }
}

} // namespace

RenderPlan lowerToRenderPlan(MotionScene& scene, const RenderPlanLoweringOptions& options) {
    if (scene.layerCount() > 1024)
        throw std::invalid_argument("lowerToRenderPlan: scene exceeds the RenderPlan v2 layer limit");
    const auto problems = scene.validate();
    if (!problems.empty())
        throw std::invalid_argument("lowerToRenderPlan: scene validation failed: " + problems.front());
    if (options.cameraRig && !options.activeCameraShots.empty())
        throw std::invalid_argument("lowerToRenderPlan: use either cameraRig or activeCameraShots, not both");
    if (!options.activeCameraShots.empty()) {
        if (!options.bakeResolvedCameraSamples || options.activeCameraShots.front().cutFrame != 0)
            throw std::invalid_argument("lowerToRenderPlan: active camera shots must be baked and begin at frame zero");
        std::int64_t previousCut = -1;
        for (const auto& shot : options.activeCameraShots) {
            if (!shot.rig || shot.cutFrame <= previousCut || shot.cutFrame < 0 || shot.cutFrame >= 1'000'000)
                throw std::invalid_argument("lowerToRenderPlan: active camera shots require rigs and strictly increasing non-negative cut frames");
            previousCut = shot.cutFrame;
        }
        if (!options.cameraCutFrames.empty())
            throw std::invalid_argument("lowerToRenderPlan: cut frames are authored by activeCameraShots");
    }
    const CameraRig* primaryCameraRig = options.cameraRig ? options.cameraRig
        : options.activeCameraShots.empty() ? nullptr : options.activeCameraShots.front().rig;
    if (!options.cameraSampleFrames.empty() && (!primaryCameraRig || !options.bakeResolvedCameraSamples))
        throw std::invalid_argument("lowerToRenderPlan: explicit camera sample frames require a baked CameraRig");
    if (!options.cameraCutFrames.empty() && (!options.cameraRig || !options.bakeResolvedCameraSamples))
        throw std::invalid_argument("lowerToRenderPlan: camera cuts require baked CameraRig samples");
    if (options.cameraSampleFrames.size() > 1'000'000)
        throw std::invalid_argument("lowerToRenderPlan: explicit camera sample sequence exceeds 1000000 samples");
    double previousCameraFrame = -std::numeric_limits<double>::infinity();
    for (double frame : options.cameraSampleFrames) {
        if (!std::isfinite(frame) || frame <= previousCameraFrame || frame < -1'000'000.0 || frame > 1'000'000.0)
            throw std::invalid_argument("lowerToRenderPlan: explicit camera sample frames must be finite, strictly increasing, and within [-1000000, 1000000]");
        previousCameraFrame = frame;
    }
    double previousCutFrame = -std::numeric_limits<double>::infinity();
    for (double frame : options.cameraCutFrames) {
        if (!std::isfinite(frame) || frame <= previousCutFrame || frame < -1'000'000.0 || frame > 1'000'000.0)
            throw std::invalid_argument("lowerToRenderPlan: camera cut frames must be finite, strictly increasing, and within [-1000000, 1000000]");
        previousCutFrame = frame;
    }
    const Layer::Id cameraParentId = primaryCameraRig ? primaryCameraRig->layer().parentId : Layer::kRoot;
    if (cameraParentId != Layer::kRoot && scene.findLayer(cameraParentId) == nullptr)
        throw std::invalid_argument("lowerToRenderPlan: CameraRig parentId does not reference an authored layer in MotionScene");
    for (const auto& shot : options.activeCameraShots) {
        const Layer::Id parentId = shot.rig->layer().parentId;
        if (parentId != Layer::kRoot && scene.findLayer(parentId) == nullptr)
            throw std::invalid_argument("lowerToRenderPlan: active CameraRig parentId does not reference an authored layer in MotionScene");
    }
    int lastFrame = std::max(0, scene.contentEndFrame());
    if (primaryCameraRig) {
        const float cameraEndTime = primaryCameraRig->endTime();
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
    for (const auto& shot : options.activeCameraShots)
        lastFrame = std::max(lastFrame, static_cast<int>(shot.cutFrame));
    for (const auto& shot : options.activeCameraShots) {
        const float endTime = shot.rig->endTime();
        if (!std::isfinite(endTime) || endTime < 0.f ||
            static_cast<double>(endTime) * scene.fps() >= 1'000'000.)
            throw std::invalid_argument("lowerToRenderPlan: active camera rig has an invalid or over-budget animation end time");
        lastFrame = std::max(lastFrame, static_cast<int>(std::ceil(static_cast<double>(endTime) * scene.fps())));
    }
    for (double cutFrame : options.cameraCutFrames) {
        if (cutFrame >= 1'000'000.0)
            throw std::invalid_argument("lowerToRenderPlan: camera cut exceeds the offline frame budget");
        lastFrame = std::max(lastFrame, static_cast<int>(std::ceil(cutFrame)));
    }
    if (lastFrame >= 1'000'000)
        throw std::invalid_argument("lowerToRenderPlan: dense bake exceeds the offline sample budget");
    const std::size_t frameCount = static_cast<std::size_t>(lastFrame) + 1;
    if (frameCount > kMaximumBakedTransformSamples || frameCount > 1'000'000)
        throw std::invalid_argument("lowerToRenderPlan: dense bake exceeds the offline sample budget");
    const FlattenedScene flattened = flattenScene(scene);
    const std::vector<FlattenedLayer>& metadata = flattened.layers;
    if (metadata.size() > 1024 ||
        (metadata.size() != 0 && frameCount > kMaximumBakedTransformSamples / metadata.size()))
        throw std::invalid_argument("lowerToRenderPlan: dense bake exceeds the offline sample budget");
    std::vector<std::vector<ResolvedPose>> samples(metadata.size());
    for (auto& layerSamples : samples) layerSamples.reserve(frameCount);
    std::vector<Matrix4> cameraParentWorlds;
    if (cameraParentId != Layer::kRoot) cameraParentWorlds.reserve(frameCount);
    std::vector<std::vector<LightState>> lightSamples(flattened.lights.size());
    for (auto& frames : lightSamples) frames.reserve(frameCount);
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
        resolveInstance(flattened, flattened.root, Matrix4{}, 1.f, true, options, samples,
                        &lightSamples);
    }
    for (const auto& layerSamples : samples)
        if (layerSamples.size() != frameCount)
            throw std::invalid_argument("lowerToRenderPlan: flattened composition sample count changed during evaluation");
    RenderPlanLoweringOptions resolvedOptions = options;
    if (primaryCameraRig) {
        if (options.camera && options.camera->type != "perspective")
            throw std::invalid_argument("lowerToRenderPlan: CameraRig only lowers to a perspective RenderPlan camera");
        const float nearPlane = options.camera ? options.camera->near : primaryCameraRig->nearPlane();
        const float farPlane = options.camera ? options.camera->far : primaryCameraRig->farPlane();
        CameraPose firstPose = cameraParentId != Layer::kRoot
            ? primaryCameraRig->sampleWorld(0.f, cameraParentWorlds.front())
            : primaryCameraRig->sample(0.f);
        // CameraRig::sample has already resolved these authoring channels into
        // orientation; clear only the redundant metadata before projection.
        firstPose.hasTarget = false;
        firstPose.roll = 0.f;
        resolvedOptions.camera = lowerCameraPose(firstPose, nearPlane, farPlane);
    } else if (!options.camera) {
        for (const auto& layer : scene.layers()) {
            if (layer.isNull() && layer.name == "Camera")
                throw std::invalid_argument("lowerToRenderPlan: a MotionScene camera layer must be supplied through RenderPlanLoweringOptions::cameraRig");
        }
    }
    RenderPlan plan = lowering_detail::lowerSamplesToRenderPlan(
        scene.fps(), frameCount, resolvedOptions, metadata, samples);
    for (std::size_t lightIndex = 0; lightIndex < lightSamples.size(); ++lightIndex) {
        const auto& frames = lightSamples[lightIndex];
        if (frames.size() != frameCount)
            throw std::invalid_argument("lowerToRenderPlan: light sample count changed during frame evaluation");
        const LightSample& firstSample = frames.front().sample;
        if (firstSample.kind == LightKind::Ambient) {
            plan.warnings.emplace_back("Ambient Motion lights are not representable as RenderPlan light layers");
            continue;
        }

        LayerPlan layer;
        layer.id = flattened.lights[lightIndex].outputId;
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
    if (plan.temporal_motion_blur) {
        const auto requiredFrames = chronon3d::render_plan::resolved_sample_frames(plan.canvas, plan.temporal_motion_blur);
        if (sampleFrames.empty()) {
            sampleFrames = requiredFrames;
        } else if (!std::includes(sampleFrames.begin(), sampleFrames.end(),
                                  requiredFrames.begin(), requiredFrames.end())) {
            throw std::invalid_argument("lowerToRenderPlan: explicit sample frames must include every shutter and presentation timestamp");
        }
    }
    // V3 has no legacy integer-TRS path: even when the caller omits the
    // migration flag, presentation frames are materialized as resolved
    // transform samples. V2 remains the explicit legacy schema.
    if ((options.requireResolvedLayerSamples || plan.schema == kRenderPlanSchemaV3) &&
        sampleFrames.empty()) {
        sampleFrames.reserve(frameCount);
        for (std::size_t frame = 0; frame < frameCount; ++frame)
            sampleFrames.push_back(static_cast<double>(frame));
    }
    if (!options.activeCameraShots.empty() && sampleFrames.empty()) {
        sampleFrames.reserve(frameCount);
        for (std::size_t frame = 0; frame < frameCount; ++frame)
            sampleFrames.push_back(static_cast<double>(frame));
    }
    if (!options.activeCameraShots.empty()) {
        for (const auto& shot : options.activeCameraShots)
            if (shot.cutFrame != 0) sampleFrames.push_back(static_cast<double>(shot.cutFrame));
        std::sort(sampleFrames.begin(), sampleFrames.end());
        sampleFrames.erase(std::unique(sampleFrames.begin(), sampleFrames.end()), sampleFrames.end());
    }
    if (!sampleFrames.empty()) {
        if (plan.schema != kRenderPlanSchemaV3 ||
            (metadata.size() != 0 && sampleFrames.size() > kMaximumBakedTransformSamples / metadata.size()))
            throw std::invalid_argument("lowerToRenderPlan: resolved transform bake requires bounded V3 samples");
        std::unordered_map<std::string, std::size_t> outputIndexById;
        for (std::size_t index = 0; index < metadata.size(); ++index)
            outputIndexById.emplace(metadata[index].outputId, index);
        std::vector<std::vector<ResolvedPose>> resolvedSamples(metadata.size());
        for (auto& layerSamples : resolvedSamples) layerSamples.reserve(sampleFrames.size());
        for (double frame : sampleFrames) {
            scene.evaluate(motionTimeFromDoubleFrame(frame, scene.fps()));
            resolveInstance(flattened, flattened.root, Matrix4{}, 1.f, true, options, resolvedSamples);
        }
        for (const auto& layerSamples : resolvedSamples)
            if (layerSamples.size() != sampleFrames.size())
                throw std::invalid_argument("lowerToRenderPlan: flattened resolved sample count changed during evaluation");
        for (auto& output : plan.layers) {
            const auto found = outputIndexById.find(output.id);
            if (found == outputIndexById.end()) {
                if (output.type == LayerType::Light || output.type == LayerType::Mesh)
                    continue;
                if (output.parent || output.animation || output.follow_path || output.auto_orient ||
                    output.transition_in || output.transition_out)
                    throw std::invalid_argument(
                        "lowerToRenderPlan: a visual plan layer without a Motion transform source must be static");
                output.transform_samples.reserve(sampleFrames.size());
                for (double frame : sampleFrames) {
                    chronon3d::render_plan::LayerPlan::ResolvedTransformSample sample;
                    sample.frame = frame;
                    sample.position = output.position;
                    sample.scale = output.scale;
                    sample.rotation_deg = output.rotation;
                    sample.opacity = output.opacity.value_or(1.f);
                    output.transform_samples.push_back(std::move(sample));
                }
                continue;
            }
            if (output.type == LayerType::Light)
                throw std::invalid_argument(
                    "lowerToRenderPlan: resolved layer samples support 2.5D visual layers, not light layers");
            if (output.type == LayerType::Mesh) {
                const auto& poses = resolvedSamples[found->second];
                const ResolvedPose& firstPose = poses.front();
                for (const ResolvedPose& pose : poses)
                    if (pose.position != firstPose.position || pose.scale != firstPose.scale ||
                        pose.rotation != firstPose.rotation || pose.opacity != firstPose.opacity)
                        throw std::invalid_argument(
                            "lowerToRenderPlan: animated mesh transforms cannot be represented as resolved layer samples");
                continue;
            }
            if (output.animation) {
                for (const auto& track : output.animation->tracks)
                    if (track.property != "position" && track.property != "scale" &&
                        track.property != "rotation" && track.property != "opacity")
                        throw std::invalid_argument("lowerToRenderPlan: resolved transform samples cannot discard non-transform animation");
            }
            output.animation.reset();
            output.transform_samples.reserve(sampleFrames.size());
            const auto& poses = resolvedSamples[found->second];
            for (std::size_t sampleIndex = 0; sampleIndex < sampleFrames.size(); ++sampleIndex) {
                const ResolvedPose& pose = poses[sampleIndex];
                // Reflect the complete world rotation, not just Euler Z.
                // The consumer reconstructs qZ*qY*qX, so use its inverse.
                Matrix4 rotation;
                const float sx = pose.scale.x, sy = pose.scale.y, sz = pose.scale.z;
                rotation.makeBasis(
                    Vector3{pose.world.elements[0] / sx, pose.world.elements[1] / sx, -pose.world.elements[2] / sx},
                    Vector3{pose.world.elements[4] / sy, pose.world.elements[5] / sy, -pose.world.elements[6] / sy},
                    Vector3{-pose.world.elements[8] / sz, -pose.world.elements[9] / sz, pose.world.elements[10] / sz});
                const auto euler = chrononCameraEuler(rotation);
                output.transform_samples.push_back({sampleFrames[sampleIndex],
                    {pose.position.x, pose.position.y, -pose.position.z},
                    {sx, sy, sz},
                    {math::radToDeg(euler[0]), math::radToDeg(euler[1]), math::radToDeg(euler[2])},
                    pose.opacity});
            }
        }
    }
    if (primaryCameraRig && options.bakeResolvedCameraSamples) {
        const std::size_t frameCount = static_cast<std::size_t>(
            std::max<std::int64_t>(1, plan.canvas.duration.integral()));
        if (options.cameraSampleFrames.empty() && frameCount > 65536)
            throw std::invalid_argument("lowerToRenderPlan: resolved camera sample sequence exceeds 65536 frames");
        std::vector<double> cameraFrames = sampleFrames;
        if (cameraFrames.empty() && !options.cameraCutFrames.empty()) {
            cameraFrames.reserve(frameCount + options.cameraCutFrames.size());
            for (std::size_t frame = 0; frame < frameCount; ++frame)
                cameraFrames.push_back(static_cast<double>(frame));
        }
        cameraFrames.insert(cameraFrames.end(), options.cameraCutFrames.begin(), options.cameraCutFrames.end());
        std::sort(cameraFrames.begin(), cameraFrames.end());
        cameraFrames.erase(std::unique(cameraFrames.begin(), cameraFrames.end()), cameraFrames.end());
        if (!cameraFrames.empty()) {
            std::vector<chronon3d::render_plan::CameraSamplePlan> explicitSamples;
            explicitSamples.reserve(cameraFrames.size());
            for (double frame : cameraFrames) {
                const double wholeFrame = std::floor(frame);
                int whole = static_cast<int>(wholeFrame);
                float fraction = static_cast<float>(frame - wholeFrame);
                if (fraction >= 1.f) { ++whole; fraction = 0.f; }
                const MotionTime time{whole, scene.fps(), fraction};
                const auto activeIt = options.activeCameraShots.empty()
                    ? options.activeCameraShots.end()
                    : std::upper_bound(options.activeCameraShots.begin(), options.activeCameraShots.end(), frame,
                        [](double value, const auto& shot) { return value < static_cast<double>(shot.cutFrame); });
                const auto* rig = options.activeCameraShots.empty() ? primaryCameraRig : (activeIt - 1)->rig;
                const auto parentId = rig->layer().parentId;
                CameraPose pose;
                if (parentId != Layer::kRoot) {
                    scene.evaluate(time);
                    pose = rig->sampleWorld(time.pts(), scene.worldMatrix(parentId));
                } else {
                    pose = rig->sample(static_cast<float>(frame / scene.fps()));
                }
                pose.hasTarget = false;
                pose.roll = 0.f;
                chronon3d::render_plan::CameraSamplePlan sample{frame, lowerCameraPose(
                    pose, options.camera ? options.camera->near : rig->nearPlane(),
                    options.camera ? options.camera->far : rig->farPlane())};
                sample.cut = !options.activeCameraShots.empty() &&
                    std::any_of(options.activeCameraShots.begin() + 1, options.activeCameraShots.end(),
                        [frame](const auto& shot) { return static_cast<double>(shot.cutFrame) == frame; });
                explicitSamples.push_back(std::move(sample));
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
        for (double cutFrame : options.cameraCutFrames) {
            const auto cut = std::lower_bound(plan.camera_samples->begin(), plan.camera_samples->end(), cutFrame,
                [](const auto& sample, double frame) { return sample.frame < frame; });
            if (cut == plan.camera_samples->end() || cut->frame != cutFrame || cut == plan.camera_samples->begin())
                throw std::invalid_argument("lowerToRenderPlan: camera cut frame " + std::to_string(cutFrame) +
                    " has no matching non-initial sample in a " +
                    std::to_string(plan.camera_samples->size()) + "-sample camera bake");
            cut->cut = true;
        }
    }
    return plan;
}

} // namespace chrononmotion::motion
