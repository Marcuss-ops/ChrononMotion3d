#include "chrononmotion/motion/RenderPlanLowering.hpp"

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
namespace {
using namespace chronon3d::render_plan;
constexpr std::size_t kMaximumBakedTransformSamples = 250'000;

void ensureFinite(float value, const char* name) {
    if (!std::isfinite(value))
        throw std::invalid_argument(std::string("lowerToRenderPlan: non-finite ") + name);
}

bool matrixFinite(const Matrix4& matrix) {
    return std::all_of(matrix.elements.begin(), matrix.elements.end(),
                       [](float value) { return std::isfinite(value); });
}

// Chronon3D reconstructs camera rotation with glm::quat(euler), whose order is
// qZ * qY * qX. The motion library's Euler extraction uses a different order;
// serialize the inverse of Chronon3D's actual constructor instead.
std::array<float, 3> chrononCameraEuler(const Matrix4& rotation) {
    const auto& m = rotation.elements; // column-major storage
    const float sinY = std::clamp(-m[2], -1.f, 1.f); // -row 2, column 0
    const float y = std::asin(sinY);
    const float cosY = std::cos(y);
    float x = 0.f;
    float z = 0.f;
    if (std::abs(cosY) > 1e-6f) {
        x = std::atan2(m[6], m[10]); // row 2, column 1 / row 2, column 2
        z = std::atan2(m[1], m[0]);  // row 1, column 0 / row 0, column 0
    } else {
        x = std::atan2(sinY * m[4], m[5]); // deterministic solution with z = 0
    }
    return {x, y, z};
}

bool close(float a, float b) {
    return std::abs(a - b) <= 1e-4f * std::max({1.f, std::abs(a), std::abs(b)});
}

Vector3 transformPoint(const Matrix4& matrix, const Vector3& point) {
    Vector3 result = point;
    result.applyMatrix4(matrix);
    return result;
}

inline chronon3d::render_plan::AnimationKeyframePlan makePlanKeyframe(chronon3d::Frame frame, std::vector<float> value) {
    chronon3d::render_plan::AnimationKeyframePlan kf;
    kf.frame = frame;
    kf.value = std::move(value);
    return kf;
}

struct ResolvedPose {
    Vector3 position{};
    Vector3 scale{1.f, 1.f, 1.f};
    Vector3 rotation{};
    float opacity{1.f};
};

bool samePose(const ResolvedPose& a, const ResolvedPose& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z &&
           a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.opacity == b.opacity;
}

ResolvedPose resolvePose(const Matrix4& matrix, float opacity, bool visible,
                         const Vector3& anchor, bool needsOriginPivot) {
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
    if (normalizedX.clone().cross(normalizedY).dot(normalizedZ) < 0.f)
        throw std::invalid_argument("lowerToRenderPlan: reflected world transform cannot be represented without changing winding");
    Matrix4 rotation;
    rotation.makeBasis(normalizedX, normalizedY, normalizedZ);
    Euler angles;
    angles.setFromRotationMatrix(rotation);
    ResolvedPose result;
    result.position = origin;
    result.scale.set(sx, sy, sz);
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

std::array<float, 3> planPosition(const ResolvedPose& pose, LayerType type,
                                  int width, int height) {
    if (type == LayerType::Text || type == LayerType::Shape ||
        type == LayerType::Particles || type == LayerType::Light) {
        return {pose.position.x + static_cast<float>(width) * .5f,
                pose.position.y + static_cast<float>(height) * .5f, pose.position.z};
    }
    // Motion cameras and 3D layers use the conventional forward -Z axis;
    // RenderPlan's camera-backed projection uses forward +Z. Mirror depth at
    // this boundary so authored anchors and camera targets remain coincident.
    return {pose.position.x, pose.position.y, -pose.position.z};
}

std::array<float, 3> planAnimationPosition(const ResolvedPose& pose, LayerType type,
                                          int width, int height) {
    // Primitive tracks are offsets relative to the RenderPlan layer position.
    // The authoring position is the top-left canvas origin for these types, so
    // output their exact world displacement without adding the canvas center.
    (void)type;
    (void)width;
    (void)height;
    return {pose.position.x, pose.position.y, -pose.position.z};
}

AnimationTrackPlan makeVectorTrack(const char* property,
                                   std::vector<AnimationKeyframePlan> keys) {
    AnimationTrackPlan result;
    result.property = property;
    result.keyframes = std::move(keys);
    result.easing = "linear";
    return result;
}

void preserveNonTransformTracks(LayerPlan& layer, AnimationPlan& baked) {
    if (!layer.animation) return;
    for (auto& track : layer.animation->tracks) {
        if (track.property == "position" || track.property == "position_x" || track.property == "position_y" ||
            track.property == "position_z" || track.property == "scale" || track.property == "scale_x" ||
            track.property == "scale_y" || track.property == "scale_z" || track.property == "rotation" ||
            track.property == "rotation_x" || track.property == "rotation_y" || track.property == "rotation_z" ||
            track.property == "opacity") continue;
        baked.tracks.push_back(std::move(track));
    }
}

void lowerTimeRemap(const Layer& source, LayerPlan& target, const std::string& schema,
                    float fps, std::size_t frameCount) {
    const TimeRemap& remap = source.timeRemap;
    const bool hasTrack = !source.timeRemapTrack.empty();
    const bool hasLoop = remap.loopEnd >= remap.loopStart;
    const bool configured = hasTrack || remap.offsetFrames != 0.f || remap.stretch != 1.f ||
                            remap.reverse || remap.freeze || remap.pingPong || hasLoop;
    if (!configured) return;
    if (schema != kRenderPlanSchemaV3)
        throw std::invalid_argument("lowerToRenderPlan: time remap requires RenderPlan v3");
    if (target.type != LayerType::Video)
        throw std::invalid_argument("lowerToRenderPlan: time remap is supported only for video layers");
    if (!hasTrack && !remap.valid())
        throw std::invalid_argument("lowerToRenderPlan: invalid Motion time remap");
    if (!hasTrack && remap.reverse)
        throw std::invalid_argument("lowerToRenderPlan: Motion reverse uses a different source-frame origin than RenderPlan V3 reverse");
    if (frameCount == 0)
        throw std::invalid_argument("lowerToRenderPlan: time remap requires a non-empty composition");

    TimeRemapPlan output;
    if (hasTrack) {
        if (frameCount > 65536)
            throw std::invalid_argument("lowerToRenderPlan: time-remap animation exceeds the 65536-key V3 contract");
        for (const auto& modifier : source.timeRemapTrack.modifiers()) {
            if (std::holds_alternative<LinkModifier>(modifier))
                throw std::invalid_argument("lowerToRenderPlan: linked time-remap tracks cannot be lowered without a resolved source curve");
        }
        AnimationTrackPlan track;
        track.property = "time_remap";
        track.easing = "linear";
        track.keyframes.reserve(frameCount);
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            const float mapped = source.mapFrame(static_cast<float>(frame),
                                                 static_cast<float>(frame) / fps);
            if (!std::isfinite(mapped) || mapped < 0.f || mapped > 1'000'000.f)
                throw std::invalid_argument("lowerToRenderPlan: time-remap source frame is outside V3's [0, 1000000] range");
            track.keyframes.push_back(makePlanKeyframe(
                chronon3d::Frame{static_cast<std::int64_t>(frame)}, {mapped}));
        }
        output.animation = std::move(track);
    } else if (remap.freeze) {
        if (remap.loopStart != 0 || remap.loopEnd >= remap.loopStart || remap.pingPong)
            throw std::invalid_argument("lowerToRenderPlan: freeze cannot be combined with looping or ping-pong remapping");
        if (!std::isfinite(remap.offsetFrames) || remap.offsetFrames < 0.f || remap.offsetFrames > 1'000'000.f)
            throw std::invalid_argument("lowerToRenderPlan: frozen source frame is outside V3's [0, 1000000] range");
        const float integral = std::floor(remap.offsetFrames);
        if (integral == remap.offsetFrames) {
            output.freeze_frame = chronon3d::Frame{static_cast<std::int64_t>(integral)};
        } else {
            AnimationTrackPlan track;
            track.property = "time_remap";
            track.easing = "linear";
            track.keyframes.push_back(makePlanKeyframe(chronon3d::Frame{0}, {remap.offsetFrames}));
            output.animation = std::move(track);
        }
    } else if (remap.pingPong) {
        throw std::invalid_argument("lowerToRenderPlan: ping-pong time remap is not representable by the V3 time-remap contract");
    } else if (hasLoop) {
        throw std::invalid_argument("lowerToRenderPlan: looping time remap is not representable by the V3 time-remap contract");
    } else if (remap.offsetFrames == 0.f) {
        const float speed = 1.f / remap.stretch;
        if (!std::isfinite(speed) || speed <= 0.f || speed > 1000.f)
            throw std::invalid_argument("lowerToRenderPlan: time-remap speed is outside V3's (0, 1000] range");
        output.speed = speed;
    } else {
        if (frameCount > 65536)
            throw std::invalid_argument("lowerToRenderPlan: time-remap animation exceeds the 65536-key V3 contract");
        AnimationTrackPlan track;
        track.property = "time_remap";
        track.easing = "linear";
        track.keyframes.reserve(frameCount);
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            const float mapped = remap.mapFrame(static_cast<float>(frame));
            if (!std::isfinite(mapped) || mapped < 0.f || mapped > 1'000'000.f)
                throw std::invalid_argument("lowerToRenderPlan: time-remap source frame is outside V3's [0, 1000000] range");
            track.keyframes.push_back(makePlanKeyframe(
                chronon3d::Frame{static_cast<std::int64_t>(frame)}, {mapped}));
        }
        output.animation = std::move(track);
    }
    target.time_remap = std::move(output);
}


RenderPlan lowerSamplesToRenderPlan(
    float fps, std::size_t frameCount, const RenderPlanLoweringOptions& options,
    const std::vector<Layer>& metadata,
    const std::vector<std::vector<ResolvedPose>>& samples) {
    if (options.width <= 0 || options.height <= 0 || options.jobId.empty() || options.outputPath.empty())
        throw std::invalid_argument("lowerToRenderPlan: dimensions, job id and output path are required");
    if (options.schema != kRenderPlanSchemaV2 && options.schema != kRenderPlanSchemaV3)
        throw std::invalid_argument("lowerToRenderPlan: schema must be Chronon3D RenderPlan v2 or v3");
    if (metadata.size() > 1024 || metadata.size() != samples.size())
        throw std::invalid_argument("lowerToRenderPlan: scene has an invalid layer count");
    if (!std::isfinite(fps) || fps <= 0.f || fps > 240.f)
        throw std::invalid_argument("lowerToRenderPlan: fps must be in (0, 240]");
    const auto fpsScaled = static_cast<std::int64_t>(std::llround(fps * 1000.f));
    if (fpsScaled <= 0 || fpsScaled > 240000 ||
        std::abs(fps - static_cast<float>(fpsScaled) / 1000.f) > 1e-4f)
        throw std::invalid_argument("lowerToRenderPlan: fps is not representable at 1/1000 fps");
    const auto divisor = std::gcd(fpsScaled, std::int64_t{1000});
    const auto frameRateNumerator = fpsScaled / divisor;
    const auto frameRateDenominator = std::int64_t{1000} / divisor;
    if (frameCount == 0 || frameCount > kMaximumBakedTransformSamples ||
        (metadata.size() != 0 && frameCount > kMaximumBakedTransformSamples / metadata.size()) ||
        frameCount > 1'000'000)
        throw std::invalid_argument("lowerToRenderPlan: dense bake exceeds the offline sample budget");

    std::unordered_map<std::string, std::size_t> layerIndexById;
    for (std::size_t index = 0; index < metadata.size(); ++index)
        layerIndexById.emplace(std::to_string(metadata[index].id), index);
    for (const auto& [id, unused] : options.contentLayers) {
        (void)unused;
        const auto found = layerIndexById.find(id);
        if (found == layerIndexById.end() || metadata[found->second].isNull())
            throw std::invalid_argument("lowerToRenderPlan: content mapping names no content layer '" + id + "'");
    }
    for (const auto& [id, supplied] : options.contentLayers) {
        const auto found = layerIndexById.find(id);
        if (found == layerIndexById.end() || metadata[found->second].isNull())
            throw std::invalid_argument("lowerToRenderPlan: content mapping names no content layer '" + id + "'");
        const Layer& layer = metadata[found->second];
        const bool mesh_layer = !layer.mesh.empty() || layer.content.isMesh();
        if (mesh_layer && (layer.content.isImage() || layer.content.isVideo() || layer.content.isText()))
            throw std::invalid_argument("lowerToRenderPlan: mesh attachment and non-mesh ContentRef cannot share one RenderPlan layer");
        if (!mesh_layer && layer.content.isMesh())
            throw std::invalid_argument("lowerToRenderPlan: mesh ContentRef requires an explicit MeshAttachment with geometry");
        if (mesh_layer && !layer.mesh.geometry)
            throw std::invalid_argument("lowerToRenderPlan: mesh geometry must be explicitly available inline; an opaque asset id alone cannot be rendered");
        if (mesh_layer) {
            if (supplied.type != LayerType::Mesh)
                throw std::invalid_argument("lowerToRenderPlan: mesh layers require a Mesh RenderPlan layer");
        } else if (supplied.type != LayerType::Image && supplied.type != LayerType::Video &&
                   supplied.type != LayerType::Text && supplied.type != LayerType::Shape &&
                   supplied.type != LayerType::Particles && supplied.type != LayerType::Light) {
            throw std::invalid_argument("lowerToRenderPlan: unsupported content-layer type for motion content");
        }
        if (!layer.content.id.empty() && supplied.type == LayerType::Shape &&
            layer.content.kind != ContentKind::Image)
            throw std::invalid_argument("lowerToRenderPlan: shape content requires an explicitly supplied Shape LayerPlan");
        if (layer.content.kind == ContentKind::Image && supplied.type != LayerType::Image &&
            supplied.type != LayerType::Shape)
            throw std::invalid_argument("lowerToRenderPlan: image content must map to an image or explicit shape plan");
        if (layer.content.kind == ContentKind::Video && supplied.type != LayerType::Video)
            throw std::invalid_argument("lowerToRenderPlan: video content must map to a video plan");
        if (layer.content.kind == ContentKind::Text && supplied.type != LayerType::Text)
            throw std::invalid_argument("lowerToRenderPlan: text content must map to a text plan");
    }

    RenderPlan plan;
    plan.schema = options.schema;
    plan.job_id = options.jobId;
    plan.camera = options.camera;
    plan.canvas = CanvasSpec{options.width, options.height,
        chronon3d::FrameRate{static_cast<std::int32_t>(frameRateNumerator),
                             static_cast<std::int32_t>(frameRateDenominator)},
        chronon3d::Frame{static_cast<std::int64_t>(frameCount)}};
    plan.output.path = options.outputPath;
    plan.output.format = OutputFormat::Png;
    plan.layers.reserve(metadata.size());
    if (plan.camera) {
        const auto& camera = *plan.camera;
        ensureFinite(camera.position[0], "camera position");
        ensureFinite(camera.position[1], "camera position");
        ensureFinite(camera.position[2], "camera position");
        ensureFinite(camera.rotation_deg[0], "camera rotation");
        ensureFinite(camera.rotation_deg[1], "camera rotation");
        ensureFinite(camera.rotation_deg[2], "camera rotation");
        ensureFinite(camera.fov_deg, "camera FOV");
        ensureFinite(camera.near, "camera near plane");
        ensureFinite(camera.far, "camera far plane");
        ensureFinite(camera.zoom, "camera zoom");
        ensureFinite(camera.dof_focus_distance, "camera depth-of-field focus distance");
        ensureFinite(camera.dof_aperture, "camera depth-of-field aperture");
        ensureFinite(camera.dof_max_blur, "camera depth-of-field maximum blur");
        if (camera.fov_deg < 10.f || camera.fov_deg > 120.f || camera.near <= 0.f ||
            camera.far <= camera.near || camera.zoom <= 0.f || camera.type != "perspective" ||
            camera.dof_focus_distance <= 0.f || camera.dof_focus_distance > 10'000'000.f ||
            camera.dof_aperture < 0.f || camera.dof_aperture > 1.f ||
            camera.dof_max_blur < 0.f || camera.dof_max_blur > 256.f)
            throw std::invalid_argument("lowerToRenderPlan: camera projection values are invalid");
    }

    for (std::size_t index = 0; index < metadata.size(); ++index) {
        const Layer& layer = metadata[index];
        const std::string layerId = std::to_string(layer.id);
        LayerPlan output;
        if (!layer.isNull()) {
            const auto supplied = options.contentLayers.find(layerId);
            if (supplied == options.contentLayers.end())
                throw std::invalid_argument("lowerToRenderPlan: caller must provide the content-owning LayerPlan for layer '" + layerId + "'");
            output = supplied->second;
            if (layer.content.isVideo() && output.type != LayerType::Video)
                throw std::invalid_argument("lowerToRenderPlan: supplied layer type does not match video content");
            if (layer.content.isText() && output.type != LayerType::Text)
                throw std::invalid_argument("lowerToRenderPlan: supplied layer type does not match text content");
            if (layer.content.isImage() && output.type != LayerType::Image && output.type != LayerType::Shape)
                throw std::invalid_argument("lowerToRenderPlan: supplied layer type does not match image content");
            if (layer.content.isMesh() && output.type != LayerType::Mesh)
                throw std::invalid_argument("lowerToRenderPlan: supplied layer type does not match mesh content");
            if (layer.mesh.empty() && layer.content.isMesh())
                throw std::invalid_argument("lowerToRenderPlan: mesh ContentRef requires an explicit MeshAttachment with geometry");
        if (!layer.mesh.empty() || layer.content.isMesh()) {
            if (output.type != LayerType::Mesh)
                    throw std::invalid_argument("lowerToRenderPlan: mesh attachment must map to a mesh plan");
                if (!layer.mesh.geometry)
                    throw std::invalid_argument("lowerToRenderPlan: mesh geometry is not available for inline lowering");
                const auto& geometry = *layer.mesh.geometry;
                const auto* positions = geometry.getAttribute<float>("position");
                if (!positions || positions->itemSize() != 3 || positions->normalized() || positions->count() < 3 ||
                    static_cast<std::size_t>(positions->count()) > chronon3d::render_plan::kInlineMeshMaxVertices)
                    throw std::invalid_argument("lowerToRenderPlan: inline mesh requires a bounded unnormalized float VEC3 position attribute");
                const auto* normals = geometry.getAttribute<float>("normal");
                const auto* uvs = geometry.getAttribute<float>("uv");
                if (normals && (normals->itemSize() != 3 || normals->normalized() || normals->count() != positions->count()))
                    throw std::invalid_argument("lowerToRenderPlan: inline mesh normal attribute must be unnormalized float VEC3 matching position count");
                if (uvs && (uvs->itemSize() != 2 || uvs->normalized() || uvs->count() != positions->count()))
                    throw std::invalid_argument("lowerToRenderPlan: inline mesh uv attribute must be unnormalized float VEC2 matching position count");
                const auto suppliedMaterial = output.inline_mesh
                    ? output.inline_mesh->material
                    : std::optional<chronon3d::render_plan::InlineMeshMaterialPlan>{};
                chronon3d::render_plan::InlineMeshPlan inline_mesh;
                inline_mesh.vertices.reserve(static_cast<std::size_t>(positions->count()));
                for (int vertex_index = 0; vertex_index < positions->count(); ++vertex_index) {
                    chronon3d::render_plan::InlineMeshVertex vertex;
                    const std::size_t index = static_cast<std::size_t>(vertex_index);
                    vertex.position = {positions->getX(index), positions->getY(index), positions->getZ(index)};
                    if (normals)
                        vertex.normal = {normals->getX(index), normals->getY(index), normals->getZ(index)};
                    if (uvs)
                        vertex.uv = {uvs->getX(index), uvs->getY(index)};
                    for (const float component : vertex.position)
                        if (!std::isfinite(component) || std::abs(component) > 1'000'000.0f)
                            throw std::invalid_argument("lowerToRenderPlan: inline mesh position component is outside the finite contract range");
                    for (const float component : vertex.normal)
                        if (!std::isfinite(component) || std::abs(component) > 1.0f)
                            throw std::invalid_argument("lowerToRenderPlan: inline mesh normal component is outside the finite contract range");
                    for (const float component : vertex.uv)
                        if (!std::isfinite(component) || std::abs(component) > 1'000'000.0f)
                            throw std::invalid_argument("lowerToRenderPlan: inline mesh UV component is outside the finite contract range");
                    inline_mesh.vertices.push_back(vertex);
                }
                if (geometry.hasIndex()) {
                    const auto* indices = geometry.getIndex();
                    if (!indices || indices->count() < 3 ||
                        static_cast<std::size_t>(indices->count()) > chronon3d::render_plan::kInlineMeshMaxIndices ||
                        indices->count() % 3 != 0)
                        throw std::invalid_argument("lowerToRenderPlan: inline mesh indices must be bounded indexed triangles");
                    inline_mesh.indices.reserve(static_cast<std::size_t>(indices->count()));
                    for (int index = 0; index < indices->count(); ++index)
                        inline_mesh.indices.push_back(indices->getX(static_cast<std::size_t>(index)));
                } else {
                    if (positions->count() % 3 != 0 ||
                        static_cast<std::size_t>(positions->count()) > chronon3d::render_plan::kInlineMeshMaxIndices)
                        throw std::invalid_argument("lowerToRenderPlan: non-indexed inline mesh vertex count must form bounded triangles");
                    inline_mesh.indices.reserve(static_cast<std::size_t>(positions->count()));
                    for (int index = 0; index < positions->count(); ++index)
                        inline_mesh.indices.push_back(static_cast<std::uint32_t>(index));
                }
                for (const auto index : inline_mesh.indices)
                    if (index >= inline_mesh.vertices.size())
                        throw std::invalid_argument("lowerToRenderPlan: inline mesh index exceeds its position vertex count");
                const auto& descriptor = layer.mesh.material;
                descriptor.validate();
                for (const float component : {descriptor.baseColor.r, descriptor.baseColor.g,
                                              descriptor.baseColor.b})
                    if (!std::isfinite(component) || component < 0.f || component > 1.f)
                        throw std::invalid_argument("lowerToRenderPlan: mesh material color must be finite and in [0, 1]");
                Color renderPlanColor = descriptor.baseColor;
                renderPlanColor.convertLinearToSRGB();
                if (suppliedMaterial) {
                    // A caller-supplied RenderPlan material is already expressed
                    // in the consumer's ABI and takes precedence over the native
                    // convenience descriptor on MeshAttachment.
                    inline_mesh.material = suppliedMaterial;
                } else if (descriptor.kind == MaterialKind::Unlit) {
                    // Omitting inline_mesh.material selects Chronon3D's flat
                    // software mesh path; preserve the authored native tint there.
                    output.color = {renderPlanColor.r, renderPlanColor.g,
                                    renderPlanColor.b, descriptor.opacity};
                } else if (descriptor.kind == MaterialKind::Lambert || descriptor.kind == MaterialKind::PBR) {
                    // RenderPlan has a compact Phong-like contract rather than
                    // ChrononMotion's material zoo. PBR roughness/metalness are
                    // approximated by shininess/specular here; the threepp bridge
                    // remains the physically based path. Reject emissive below.
                    chronon3d::render_plan::InlineMeshMaterialPlan material;
                    material.diffuse = 1.f;
                    material.specular = descriptor.metalness;
                    material.shininess = 1.f + (1.f - descriptor.roughness) * 1023.f;
                    material.rim_intensity = 0.f;
                    material.base_color = std::array<float, 4>{renderPlanColor.r,
                        renderPlanColor.g, renderPlanColor.b, descriptor.opacity};
                    output.color = {1.f, 1.f, 1.f, 1.f};
                    inline_mesh.material = material;
                } else {
                    throw std::invalid_argument("lowerToRenderPlan: emissive mesh materials are not expressible by the inline mesh RenderPlan material contract");
                }
                output.inline_mesh = std::move(inline_mesh);
                output.asset.clear();
            } else {
                if (layer.content.id.empty())
                    throw std::invalid_argument("lowerToRenderPlan: content layer has no id");
                const std::string& suppliedAsset = output.type == LayerType::Video ? output.source : output.asset;
                if (!suppliedAsset.empty() && suppliedAsset != layer.content.id)
                    throw std::invalid_argument("lowerToRenderPlan: supplied RenderPlan content identity does not match the motion layer");
                if (output.type == LayerType::Video) output.source = layer.content.id;
                else if (output.type == LayerType::Image) output.asset = layer.content.id;
            }
        } else {
            output.type = LayerType::Color;
            output.color = {0.f, 0.f, 0.f, 0.f};
            output.size = {1.f, 1.f};
            output.size_dimensions = 2;
        }
        output.id = layerId;
        output.parent.reset();
        const auto& poses = samples[index];
        if (layer.isNull() && options.contentLayers.contains(layerId))
            throw std::invalid_argument("lowerToRenderPlan: null/controller layers cannot own RenderPlan content");
        if (poses.size() != frameCount)
            throw std::invalid_argument("lowerToRenderPlan: inconsistent sample counts across layers");
        const ResolvedPose& first = poses.front();
        // A flat (2.5D) layer that never leaves the canvas plane keeps the
        // canonical two-component RenderPlan transform shape. RenderPlan reads
        // an explicit three-component transform vector as an opt-in to
        // camera-backed projection (`enable_3d`), and primitives composed on
        // that projection path use a Y-up axis convention, so they are
        // mirrored on Y relative to raster layers. Emitting the flat shape for
        // plane-bound layers keeps authored canvas coordinates (Y down)
        // identical for text, shapes and images.
        bool leavesCanvasPlane = false;
        if (!layer.is3D) {
            for (const auto& pose : poses)
                leavesCanvasPlane = leavesCanvasPlane ||
                    std::abs(pose.position.z) > 1e-5f || std::abs(pose.rotation.x) > 1e-5f ||
                    std::abs(pose.rotation.y) > 1e-5f || std::abs(pose.rotation.z) > 1e-5f;
        }
        const bool flatEmission = !layer.is3D && !leavesCanvasPlane;
        output.position = planPosition(first, output.type, options.width, options.height);
        output.position_dimensions = flatEmission ? 2 : 3;
        const bool staticPlanWasProvided = options.contentLayers.contains(layerId);
        if (!staticPlanWasProvided &&
            (output.type == LayerType::Text || output.type == LayerType::Shape ||
             output.type == LayerType::Particles || output.type == LayerType::Light)) {
            output.position[0] = static_cast<float>(options.width) * 0.5f;
            output.position[1] = static_cast<float>(options.height) * 0.5f;
        }
        if (output.type == LayerType::Video && layer.content.isVideo() && output.size_dimensions == 0 &&
            layer.content.metrics.naturalSize.x > 0.f && layer.content.metrics.naturalSize.y > 0.f) {
            output.size = {layer.content.metrics.naturalSize.x, layer.content.metrics.naturalSize.y};
            output.size_dimensions = 2;
        }
        if (output.size_dimensions == 0 &&
            (output.type == LayerType::Image || output.type == LayerType::Video ||
             output.type == LayerType::Text || output.type == LayerType::Shape)) {
            const Vector2 measured = layer.content.metrics.naturalSize;
            if (measured.x > 0.f && measured.y > 0.f && std::isfinite(measured.x) && std::isfinite(measured.y))
                output.size = {measured.x, measured.y};
            else if (output.type == LayerType::Video)
                output.size = {static_cast<float>(options.width), static_cast<float>(options.height)};
            if ((measured.x > 0.f && measured.y > 0.f) || output.type == LayerType::Video)
                output.size_dimensions = 2;
        }
        output.scale = {first.scale.x, first.scale.y, first.scale.z};
        output.scale_dimensions = flatEmission ? 2 : 3;
        output.rotation = {math::radToDeg(first.rotation.x), math::radToDeg(first.rotation.y),
                           math::radToDeg(first.rotation.z)};
        output.rotation_dimensions = flatEmission ? 0 : 3;
        // RenderPlan's canonical decoder treats an explicit 3D vector as an
        // opt-in to camera-backed projection, even when its Z/tilt components
        // are zero. The lowerer always emits three-component TRS vectors, so
        // mark those layers explicitly rather than producing undecodable plans.
        output.enable_3d = output.position_dimensions >= 3 || output.scale_dimensions >= 3 ||
                           output.rotation_dimensions >= 3 ||
                           std::abs(first.rotation.x) > 1e-5f || std::abs(first.rotation.y) > 1e-5f ||
                           std::abs(first.position.z) > 1e-5f;
        for (const auto& pose : poses)
            output.enable_3d = output.enable_3d || std::abs(pose.rotation.x) > 1e-5f ||
                std::abs(pose.rotation.y) > 1e-5f || std::abs(pose.position.z) > 1e-5f;
        output.opacity = first.opacity;
        if (layer.isContent() && layer.is3D &&
            (output.type == LayerType::Image || output.type == LayerType::Video || output.type == LayerType::Shape))
            output.enable_3d = true;
        output.start_frame = chronon3d::Frame{0};
        output.duration_frames = chronon3d::Frame{static_cast<std::int64_t>(frameCount)};
        const TimeRemap& authoredTimeRemap = layer.timeRemap;
        const bool hasMotionTimeRemap = !layer.timeRemapTrack.empty() || authoredTimeRemap.offsetFrames != 0.f ||
            authoredTimeRemap.stretch != 1.f || authoredTimeRemap.reverse || authoredTimeRemap.freeze ||
            authoredTimeRemap.pingPong || authoredTimeRemap.loopEnd >= authoredTimeRemap.loopStart;
        if (hasMotionTimeRemap && output.time_remap)
            throw std::invalid_argument("lowerToRenderPlan: caller-supplied RenderPlan time remap conflicts with Motion layer timing");
        if (hasMotionTimeRemap && !layer.content.isVideo())
            throw std::invalid_argument("lowerToRenderPlan: Motion time remap is supported only for video content");
        lowerTimeRemap(layer, output, options.schema, fps, frameCount);

        AnimationPlan baked;
        if (output.type == LayerType::Mesh) {
            if (output.animation && !output.animation->tracks.empty())
                throw std::invalid_argument("lowerToRenderPlan: inline mesh layers cannot carry RenderPlan animation tracks");
            for (std::size_t frame = 1; frame < poses.size(); ++frame)
                if (!samePose(poses.front(), poses[frame]))
                    throw std::invalid_argument("lowerToRenderPlan: animated inline mesh transforms are not expressible by the RenderPlan contract");
        } else {
            preserveNonTransformTracks(output, baked);
        }
        if (frameCount > 1 && output.type != LayerType::Mesh) {
            std::vector<AnimationKeyframePlan> positions, scales, rotations, opacities;
            positions.reserve(frameCount); scales.reserve(frameCount);
            rotations.reserve(frameCount); opacities.reserve(frameCount);
            for (std::size_t frame = 0; frame < frameCount; ++frame) {
                const auto& pose = poses[frame];
                const auto point = planAnimationPosition(pose, output.type, options.width, options.height);
                const chronon3d::Frame keyFrame{static_cast<std::int64_t>(frame)};
                positions.push_back(flatEmission
                    ? makePlanKeyframe(keyFrame, {point[0], point[1]})
                    : makePlanKeyframe(keyFrame, {point[0], point[1], point[2]}));
                scales.push_back(flatEmission
                    ? makePlanKeyframe(keyFrame, {pose.scale.x, pose.scale.y})
                    : makePlanKeyframe(keyFrame, {pose.scale.x, pose.scale.y, pose.scale.z}));
                if (!flatEmission)
                    rotations.push_back(makePlanKeyframe(keyFrame, {math::radToDeg(pose.rotation.x),
                                                                    math::radToDeg(pose.rotation.y),
                                                                    math::radToDeg(pose.rotation.z)}));
                opacities.push_back(makePlanKeyframe(keyFrame, {pose.opacity}));
            }
            baked.tracks.push_back(makeVectorTrack("position", std::move(positions)));
            baked.tracks.push_back(makeVectorTrack("scale", std::move(scales)));
            if (!flatEmission)
                baked.tracks.push_back(makeVectorTrack("rotation", std::move(rotations)));
            AnimationTrackPlan opacity;
            opacity.property = "opacity";
            opacity.keyframes = std::move(opacities);
            baked.tracks.push_back(std::move(opacity));
        }
        if (!baked.tracks.empty()) output.animation = std::move(baked);
        else output.animation.reset();
        plan.layers.push_back(std::move(output));
    }
    return plan;
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
    if (options.cameraRig && options.cameraRig->layer().parentId != Layer::kRoot)
        throw std::invalid_argument("lowerToRenderPlan: parented CameraRig layers must be resolved to world space before lowering");
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
            const ResolvedPose pose = resolvePose(transform.world, transform.opacity, transform.visible,
                                                  scene.state(transform.id).local.anchor, checkAnchor);
            samples[index].push_back(pose);
        }
    }
    RenderPlanLoweringOptions resolvedOptions = options;
    if (options.cameraRig) {
        if (options.camera && options.camera->type != "perspective")
            throw std::invalid_argument("lowerToRenderPlan: CameraRig only lowers to a perspective RenderPlan camera");
        const float nearPlane = options.camera ? options.camera->near : options.cameraRig->nearPlane();
        const float farPlane = options.camera ? options.camera->far : options.cameraRig->farPlane();
        CameraPose firstPose = options.cameraRig->sample(0.f);
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
    RenderPlan plan = lowerSamplesToRenderPlan(scene.fps(), frameCount, resolvedOptions, metadata, samples);
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
    if (options.temporalMotionBlur) {
        const auto& blur = *options.temporalMotionBlur;
        if (plan.schema != kRenderPlanSchemaV3)
            throw std::invalid_argument("lowerToRenderPlan: temporal motion blur requires RenderPlan V3");
        if (!std::isfinite(blur.shutter_angle) || blur.shutter_angle <= 0.f || blur.shutter_angle > 360.f ||
            blur.samples < 2 || blur.samples > 64)
            throw std::invalid_argument("lowerToRenderPlan: temporal motion blur requires shutter_angle in (0, 360] and samples in [2, 64]");
        plan.temporal_motion_blur = blur;
    }
    if (options.cameraRig) {
        const std::size_t last = plan.canvas.duration.integral() > 0
            ? static_cast<std::size_t>(plan.canvas.duration.integral() - 1) : 0;
        if (last >= 65536)
            throw std::invalid_argument("lowerToRenderPlan: camera track exceeds the 65536 keyframe contract");
        std::vector<chronon3d::render_plan::AnimationKeyframePlan> positionX, positionY, positionZ;
        std::vector<chronon3d::render_plan::AnimationKeyframePlan> rotationX, rotationY, rotationZ;
        std::vector<AnimationKeyframePlan> focusDistance, aperture, maxBlur;
        std::vector<AnimationKeyframePlan> focalLength, fStop;
        positionX.reserve(last + 1); positionY.reserve(last + 1); positionZ.reserve(last + 1);
        rotationX.reserve(last + 1); rotationY.reserve(last + 1); rotationZ.reserve(last + 1);
        const bool animateFocus = !options.cameraRig->focusDistanceTrack().empty();
        const bool animateAperture = !options.cameraRig->apertureTrack().empty();
        const bool animateMaxBlur = !options.cameraRig->maxBlurTrack().empty();
        const bool animateFocalLength = !options.cameraRig->focalLengthTrack().empty();
        const bool animateFStop = !options.cameraRig->fStopTrack().empty();
        if (animateFocus) focusDistance.reserve(last + 1);
        if (animateAperture) aperture.reserve(last + 1);
        if (animateMaxBlur) maxBlur.reserve(last + 1);
        if (animateFocalLength) focalLength.reserve(last + 1);
        if (animateFStop) fStop.reserve(last + 1);
        bool dofEnabled = resolvedOptions.camera->dof_enabled;
        for (std::size_t frame = 0; frame <= last; ++frame) {
            CameraPose pose = options.cameraRig->sample(static_cast<float>(frame) / scene.fps());
            pose.hasTarget = false;
            pose.roll = 0.f;
            const auto camera = lowerCameraPose(pose, resolvedOptions.camera->near, resolvedOptions.camera->far);
            dofEnabled = dofEnabled || camera.dof_enabled;
            const chronon3d::Frame keyFrame{static_cast<std::int64_t>(frame)};
            positionX.push_back(makePlanKeyframe(keyFrame, {camera.position[0]}));
            positionY.push_back(makePlanKeyframe(keyFrame, {camera.position[1]}));
            positionZ.push_back(makePlanKeyframe(keyFrame, {camera.position[2]}));
            rotationX.push_back(makePlanKeyframe(keyFrame, {camera.rotation_deg[0]}));
            rotationY.push_back(makePlanKeyframe(keyFrame, {camera.rotation_deg[1]}));
            rotationZ.push_back(makePlanKeyframe(keyFrame, {camera.rotation_deg[2]}));
            if (animateFocus)
                focusDistance.push_back(makePlanKeyframe(keyFrame, {camera.dof_focus_distance}));
            if (animateAperture)
                aperture.push_back(makePlanKeyframe(keyFrame, {camera.dof_aperture}));
            if (animateMaxBlur)
                maxBlur.push_back(makePlanKeyframe(keyFrame, {camera.dof_max_blur}));
            if (animateFocalLength)
                focalLength.push_back(makePlanKeyframe(keyFrame, {camera.focal_length_mm}));
            if (animateFStop)
                fStop.push_back(makePlanKeyframe(keyFrame, {camera.f_stop}));
        }
        const bool sequenceUsesDof = dofEnabled || animateFocus || animateAperture || animateMaxBlur || animateFStop;
        if (plan.camera) plan.camera->dof_enabled = sequenceUsesDof;
        plan.camera_animation = std::vector<chronon3d::render_plan::CameraTrackPlan>{};
        auto& tracks = *plan.camera_animation;
        tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::PositionX, std::move(positionX), "linear", std::nullopt});
        tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::PositionY, std::move(positionY), "linear", std::nullopt});
        tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::PositionZ, std::move(positionZ), "linear", std::nullopt});
        tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::RotationX, std::move(rotationX), "linear", std::nullopt});
        tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::RotationY, std::move(rotationY), "linear", std::nullopt});
        tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::RotationZ, std::move(rotationZ), "linear", std::nullopt});
        std::vector<chronon3d::render_plan::AnimationKeyframePlan> fov;
        fov.reserve(last + 1);
        for (std::size_t frame = 0; frame <= last; ++frame)
            fov.push_back(makePlanKeyframe(chronon3d::Frame{static_cast<std::int64_t>(frame)},
                                           {options.cameraRig->sample(static_cast<float>(frame) / scene.fps()).fov}));
        tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::FovDeg, std::move(fov), "linear", std::nullopt});
        if (animateFocus)
            tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::FocusDistance, std::move(focusDistance), "linear", std::nullopt});
        if (animateAperture)
            tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::Aperture, std::move(aperture), "linear", std::nullopt});
        if (animateMaxBlur)
            tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::MaxBlur, std::move(maxBlur), "linear", std::nullopt});
        if (animateFocalLength)
            tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::FocalLengthMM, std::move(focalLength), "linear", std::nullopt});
        if (animateFStop)
            tracks.push_back({chronon3d::render_plan::CameraPropertyPlan::FStop, std::move(fStop), "linear", std::nullopt});
    }
    return plan;
}

void applyRigGeometryFrame(chronon3d::render_plan::LayerPlan& layer,
                           const RigFrameGeometry& geometry,
                           std::span<const std::uint32_t> indices) {
    using namespace chronon3d::render_plan;
    if (layer.type != LayerType::Mesh || !layer.inline_mesh)
        throw std::invalid_argument("applyRigGeometryFrame: target must be an inline mesh layer");
    auto& mesh = *layer.inline_mesh;
    if (geometry.positions.size() != mesh.vertices.size() || geometry.positions.size() < 3 ||
        geometry.positions.size() > kInlineMeshMaxVertices || indices.size() != mesh.indices.size() ||
        indices.size() < 3 || indices.size() > kInlineMeshMaxIndices || indices.size() % 3 != 0 ||
        !std::equal(indices.begin(), indices.end(), mesh.indices.begin()))
        throw std::invalid_argument("applyRigGeometryFrame: baked geometry topology does not match inline mesh");

    for (const auto& position : geometry.positions) {
        for (const float component : {position.x, position.y, position.z})
            if (!std::isfinite(component) || std::abs(component) > 1'000'000.f)
                throw std::invalid_argument("applyRigGeometryFrame: baked position exceeds the finite mesh contract");
    }
    for (const auto index : indices)
        if (index >= geometry.positions.size())
            throw std::invalid_argument("applyRigGeometryFrame: triangle index is outside baked geometry");

    // Preserve authored UV/material data, replace positions, and regenerate
    // normals from the deformed triangles so lit meshes do not retain stale
    // rest-pose shading.
    std::vector<Vector3> normals(geometry.positions.size());
    for (std::size_t triangle = 0; triangle < indices.size(); triangle += 3) {
        const auto a = indices[triangle];
        const auto b = indices[triangle + 1];
        const auto c = indices[triangle + 2];
        const Vector3 edge1 = geometry.positions[b] - geometry.positions[a];
        const Vector3 edge2 = geometry.positions[c] - geometry.positions[a];
        const Vector3 faceNormal = edge1.clone().cross(edge2);
        if (!std::isfinite(faceNormal.lengthSq()))
            throw std::invalid_argument("applyRigGeometryFrame: deformed triangle normal overflow");
        normals[a] += faceNormal;
        normals[b] += faceNormal;
        normals[c] += faceNormal;
    }
    for (std::size_t vertex = 0; vertex < mesh.vertices.size(); ++vertex) {
        const float lengthSq = normals[vertex].lengthSq();
        if (!std::isfinite(lengthSq))
            throw std::invalid_argument("applyRigGeometryFrame: deformed vertex normal overflow");
        if (lengthSq > 1e-20f) normals[vertex] /= std::sqrt(lengthSq);
        else normals[vertex].set(0.f, 0.f, 1.f);
        mesh.vertices[vertex].position = {geometry.positions[vertex].x,
                                          geometry.positions[vertex].y,
                                          geometry.positions[vertex].z};
        mesh.vertices[vertex].normal = {normals[vertex].x, normals[vertex].y,
                                        normals[vertex].z};
    }
}

void appendRigGeometrySample(chronon3d::render_plan::LayerPlan& layer,
                             const RigFrameGeometry& geometry,
                             std::span<const std::uint32_t> indices,
                             std::int64_t frame) {
    using namespace chronon3d::render_plan;
    if (frame < 0 || frame > 1'000'000)
        throw std::invalid_argument("appendRigGeometrySample: frame is outside [0, 1000000]");
    if (layer.type != LayerType::Mesh || !layer.inline_mesh)
        throw std::invalid_argument("appendRigGeometrySample: target must be an inline mesh layer");
    auto& mesh = *layer.inline_mesh;
    if (!mesh.animation.empty() && frame <= mesh.animation.back().frame)
        throw std::invalid_argument("appendRigGeometrySample: frames must be strictly increasing");
    if (geometry.positions.size() != mesh.vertices.size() || indices.size() != mesh.indices.size() ||
        !std::equal(indices.begin(), indices.end(), mesh.indices.begin()))
        throw std::invalid_argument("appendRigGeometrySample: baked geometry topology does not match inline mesh");
    if (mesh.animation.size() >= kInlineMeshMaxAnimationSamples)
        throw std::invalid_argument("appendRigGeometrySample: sample count exceeds 4096");
    InlineMeshFrameSamplePlan sample;
    sample.frame = frame;
    sample.positions.reserve(geometry.positions.size());
    for (const auto& position : geometry.positions) {
        for (const float component : {position.x, position.y, position.z})
            if (!std::isfinite(component) || std::abs(component) > 1'000'000.f)
                throw std::invalid_argument("appendRigGeometrySample: position exceeds finite mesh bounds");
        sample.positions.push_back({position.x, position.y, position.z});
    }
    mesh.animation.push_back(std::move(sample));
}

void applyRigGeometryBake(chronon3d::render_plan::LayerPlan& layer,
                          const BakedRigProgram& bake,
                          std::uint32_t geometryId,
                          std::span<const std::uint32_t> indices,
                          std::int64_t firstFrame) {
    if (bake.frames.empty())
        throw std::invalid_argument("applyRigGeometryBake: bake has no frames");
    if (layer.type != chronon3d::render_plan::LayerType::Mesh || !layer.inline_mesh)
        throw std::invalid_argument("applyRigGeometryBake: target must be an inline mesh layer");
    if (firstFrame < 0 || firstFrame > 1'000'000 ||
        bake.frames.size() > static_cast<std::size_t>(1'000'001 - firstFrame))
        throw std::invalid_argument("applyRigGeometryBake: output frame range exceeds [0, 1000000]");
    auto candidate = layer;
    candidate.inline_mesh->animation.clear();
    for (std::size_t sampleIndex = 0; sampleIndex < bake.frames.size(); ++sampleIndex) {
        const auto& frame = bake.frames[sampleIndex];
        const auto geometry = std::find_if(frame.geometry.begin(), frame.geometry.end(),
            [geometryId](const RigFrameGeometry& item) { return item.id == geometryId; });
        if (geometry == frame.geometry.end() ||
            std::find_if(std::next(geometry), frame.geometry.end(),
                [geometryId](const RigFrameGeometry& item) { return item.id == geometryId; }) != frame.geometry.end())
            throw std::invalid_argument("applyRigGeometryBake: geometry id is missing or duplicated in a bake frame");
        appendRigGeometrySample(candidate, *geometry, indices,
            firstFrame + static_cast<std::int64_t>(sampleIndex));
    }
    layer = std::move(candidate);
}

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
    ensureFinite(pose.aperture, "camera aperture");
    ensureFinite(pose.focusDistance, "camera focus distance");
    ensureFinite(pose.maxBlur, "camera maximum blur");
    ensureFinite(pose.fStop, "camera f-stop");
    ensureFinite(pose.highlightGain, "camera highlight gain");
    ensureFinite(pose.roll, "camera roll");
    if (pose.hasTarget || pose.roll != 0.f)
        throw std::invalid_argument(
            "lowerCameraPose: resolve look-at and roll into orientation before lowering; RenderPlan has no target/roll metadata channels");
    if ((pose.focalLength != 0.f && (pose.focalLength < 12.f || pose.focalLength > 200.f)) ||
        (pose.fStop != 0.f && (pose.fStop < 0.7f || pose.fStop > 32.f)) ||
        pose.irisBlades < 0 || pose.irisBlades > 12 ||
        pose.highlightGain < 0.f || pose.highlightGain > 8.f)
        throw std::invalid_argument("lowerCameraPose: physical lens/iris settings are outside the RenderPlan contract");
    if (!std::isfinite(pose.aperture) || pose.aperture < 0.f || pose.aperture > 1.f ||
        !std::isfinite(pose.focusDistance) || pose.focusDistance < 0.f || pose.focusDistance > 10'000'000.f ||
        pose.maxBlur < 0.f || pose.maxBlur > 256.f)
        throw std::invalid_argument(
            "lowerCameraPose: depth-of-field values are outside the RenderPlan contract");
    const bool depthOfFieldAuthored = pose.aperture != 0.f || pose.focusDistance != 0.f || pose.maxBlur != 0.f ||
                                      pose.fStop != 0.f || pose.irisBlades != 0 || pose.highlightGain != 0.f;
    const bool depthOfFieldEnabled = pose.aperture > 0.f || pose.fStop > 0.f;
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
    orientation.invert(); // RenderPlan camera forward is +Z; Motion camera forward is -Z.
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
    camera.f_stop = pose.fStop;
    camera.iris_blades = pose.irisBlades;
    if (depthOfFieldAuthored) {
        camera.dof_enabled = depthOfFieldEnabled;
        camera.dof_aperture = pose.aperture;
        if (pose.focusDistance > 0.f) camera.dof_focus_distance = pose.focusDistance;
        if (pose.maxBlur > 0.f) camera.dof_max_blur = pose.maxBlur;
        camera.dof_f_stop = pose.fStop;
        camera.dof_iris_blades = pose.irisBlades;
        camera.dof_highlight_gain = pose.highlightGain;
    }
    return camera;
}

std::vector<ShapePathCommandPlan> lowerBakedPath(const BakedPath& path) {
    if (path.empty() || path.size() > 65536 || path.front().type != BakedPathCommandType::MoveTo)
        throw std::invalid_argument("lowerBakedPath: path must begin with moveto and contain 1..65536 commands");
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (path[index].type == BakedPathCommandType::Close &&
            (index == 0 || path[index - 1].type == BakedPathCommandType::Close))
            throw std::invalid_argument("lowerBakedPath: close command has no active drawable subpath");
    }
    std::vector<ShapePathCommandPlan> output;
    output.reserve(path.size());
    for (const auto& command : path) {
        ShapePathCommandPlan value;
        if (command.type != BakedPathCommandType::Close &&
            (!std::isfinite(command.point.x) || !std::isfinite(command.point.y)))
            throw std::invalid_argument("lowerBakedPath: path coordinates must be finite");
        switch (command.type) {
            case BakedPathCommandType::MoveTo:
                value.type = ShapePathCommandTypePlan::MoveTo;
                value.point = {command.point.x, command.point.y};
                break;
            case BakedPathCommandType::CubicTo:
                value.type = ShapePathCommandTypePlan::CubicTo;
                value.point = {command.point.x, command.point.y};
                value.control1 = {command.control1.x, command.control1.y};
                value.control2 = {command.control2.x, command.control2.y};
                if (!std::isfinite(command.control1.x) || !std::isfinite(command.control1.y) ||
                    !std::isfinite(command.control2.x) || !std::isfinite(command.control2.y))
                    throw std::invalid_argument("lowerBakedPath: cubic controls must be finite");
                break;
            case BakedPathCommandType::Close:
                value.type = ShapePathCommandTypePlan::Close;
                break;
            default:
                throw std::invalid_argument("lowerBakedPath: unsupported command type");
        }
        output.push_back(value);
    }
    return output;
}

void applyBakedPathGeometry(chronon3d::render_plan::LayerPlan& layer,
                            const BakedPath& path,
                            std::span<const BakedPath> repeaterCopies) {
    using namespace chronon3d::render_plan;
    if (layer.type != LayerType::Shape || !layer.shape ||
        layer.shape->type != ShapeContentTypePlan::Path)
        throw std::invalid_argument("applyBakedPathGeometry: target layer must own a V3 path shape plan");
    const auto source = lowerBakedPath(path);
    if (repeaterCopies.empty()) {
        layer.shape->path = source;
        return;
    }
    if (repeaterCopies.size() > 1025)
        throw std::invalid_argument("applyBakedPathGeometry: V3 Merge accepts at most 1024 additional paths");

    // expandPathRepeater returns copy 0 as the unmodified source path. Merge
    // appends paths to the source, so store only the additional copies.
    const auto sameCommand = [](const BakedPathCommand& a, const BakedPathCommand& b) {
        if (a.type != b.type) return false;
        if (a.type == BakedPathCommandType::Close) return true;
        if (a.point != b.point) return false;
        return a.type != BakedPathCommandType::CubicTo ||
               (a.control1 == b.control1 && a.control2 == b.control2);
    };
    const auto& firstCopy = repeaterCopies.front();
    const bool firstIsSource = firstCopy.size() == path.size() &&
        std::equal(firstCopy.begin(), firstCopy.end(), path.begin(), sameCommand);
    if (!firstIsSource)
        throw std::invalid_argument(
            "applyBakedPathGeometry: repeater copies must start with the exact source path");

    PathOperatorPlan merge;
    merge.kind = PathOperatorKindPlan::Merge;
    merge.paths.reserve(repeaterCopies.size() - 1);
    std::size_t totalCommands = 0;
    for (std::size_t index = 1; index < repeaterCopies.size(); ++index) {
        auto lowered = lowerBakedPath(repeaterCopies[index]);
        if (lowered.size() > 1'000'000 - totalCommands)
            throw std::invalid_argument("applyBakedPathGeometry: V3 Merge command budget exceeded");
        totalCommands += lowered.size();
        merge.paths.push_back(std::move(lowered));
    }
    if (!merge.paths.empty() && layer.shape->operators.size() >= 16)
        throw std::invalid_argument("applyBakedPathGeometry: V3 path operator limit exceeded");

    layer.shape->path = source;
    if (!merge.paths.empty()) layer.shape->operators.push_back(std::move(merge));
}

void applyBakedPathMorph(chronon3d::render_plan::LayerPlan& layer,
                         std::span<const BakedPath> morphFrames,
                         int firstFrame) {
    using namespace chronon3d::render_plan;
    if (layer.type != LayerType::Shape || !layer.shape ||
        layer.shape->type != ShapeContentTypePlan::Path)
        throw std::invalid_argument("applyBakedPathMorph: target layer must own a V3 path shape plan");
    if (!layer.shape->operators.empty())
        throw std::invalid_argument("applyBakedPathMorph: target path must not already contain operators");
    if (morphFrames.size() < 2 || morphFrames.size() > 65536 || firstFrame < 0 ||
        static_cast<std::int64_t>(firstFrame) + static_cast<std::int64_t>(morphFrames.size()) - 1 > 1'000'000)
        throw std::invalid_argument("applyBakedPathMorph: frame range must contain 2..65536 samples within V3 limits");
    const auto source = lowerBakedPath(morphFrames.front());
    const auto target = lowerBakedPath(morphFrames.back());
    if (source.size() != target.size())
        throw std::invalid_argument("applyBakedPathMorph: morph endpoints must have identical topology");
    for (std::size_t index = 0; index < source.size(); ++index)
        if (source[index].type != target[index].type)
            throw std::invalid_argument("applyBakedPathMorph: morph endpoints must have identical topology");
    for (std::size_t frame = 0; frame < morphFrames.size(); ++frame) {
        const auto commands = lowerBakedPath(morphFrames[frame]);
        if (commands.size() != source.size())
            throw std::invalid_argument("applyBakedPathMorph: every baked frame must preserve path topology");
        const float amount = static_cast<float>(frame) / static_cast<float>(morphFrames.size() - 1);
        const auto closePoint = [](const auto& a, const auto& b) {
            return std::abs(a[0] - b[0]) <= 1e-4f * std::max({1.f, std::abs(a[0]), std::abs(b[0])}) &&
                   std::abs(a[1] - b[1]) <= 1e-4f * std::max({1.f, std::abs(a[1]), std::abs(b[1])});
        };
        for (std::size_t index = 0; index < commands.size(); ++index) {
            if (commands[index].type != source[index].type)
                throw std::invalid_argument("applyBakedPathMorph: every baked frame must preserve path topology");
            const auto lerpPoint = [&](const auto& from, const auto& to) {
                return std::array<float, 2>{from[0] + (to[0] - from[0]) * amount,
                                            from[1] + (to[1] - from[1]) * amount};
            };
            if (commands[index].type == ShapePathCommandTypePlan::Close) continue;
            if (!closePoint(commands[index].point, lerpPoint(source[index].point, target[index].point)) ||
                (commands[index].type == ShapePathCommandTypePlan::CubicTo &&
                 (!closePoint(commands[index].control1, lerpPoint(source[index].control1, target[index].control1)) ||
                  !closePoint(commands[index].control2, lerpPoint(source[index].control2, target[index].control2)))))
                throw std::invalid_argument(
                    "applyBakedPathMorph: Chronon3D Morph can represent only linear interpolation between the endpoint paths");
        }
    }

    PathOperatorPlan morph;
    morph.kind = PathOperatorKindPlan::Morph;
    morph.morph_target = target;
    AnimationTrackPlan progress;
    progress.property = "morph_progress";
    progress.easing = "linear";
    progress.keyframes.reserve(morphFrames.size());
    for (std::size_t frame = 0; frame < morphFrames.size(); ++frame) {
        const float amount = static_cast<float>(frame) / static_cast<float>(morphFrames.size() - 1);
        progress.keyframes.push_back(makePlanKeyframe(chronon3d::Frame{static_cast<std::int64_t>(firstFrame) +
                                                                       static_cast<std::int64_t>(frame)}, {amount}));
    }
    morph.morph_animation = std::move(progress);
    layer.shape->path = source;
    layer.shape->operators.push_back(std::move(morph));
}

} // namespace chrononmotion::motion
