#include "RenderPlanLoweringInternal.hpp"
#include "chrononmotion/math/Finite.hpp"
#include "chrononmotion/math/MathUtils.hpp"

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

namespace chrononmotion::motion::lowering_detail {
namespace {
using namespace chronon3d::render_plan;

bool samePose(const ResolvedPose& a, const ResolvedPose& b) {
    return a.position.x == b.position.x && a.position.y == b.position.y && a.position.z == b.position.z &&
           a.scale.x == b.scale.x && a.scale.y == b.scale.y && a.scale.z == b.scale.z &&
           a.rotation.x == b.rotation.x && a.rotation.y == b.rotation.y && a.rotation.z == b.rotation.z &&
           a.opacity == b.opacity;
}

std::array<float, 3> planPosition(const ResolvedPose& pose, LayerType type,
                                  int width, int height) {
    if (type == LayerType::Text || type == LayerType::Shape ||
        type == LayerType::Particles || type == LayerType::Light) {
        return {pose.position.x + static_cast<float>(width) * .5f,
                pose.position.y + static_cast<float>(height) * .5f, -pose.position.z};
    }
    // Motion cameras and 3D layers use the conventional forward -Z axis;
    // RenderPlan's camera-backed projection uses forward +Z. Mirror depth at
    // this boundary for all layer types, including canvas-centred primitives.
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


} // namespace

using namespace chronon3d::render_plan;

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
                   supplied.type != LayerType::Particles && supplied.type != LayerType::Light &&
                   supplied.type != LayerType::Adjustment) {
            throw std::invalid_argument("lowerToRenderPlan: unsupported content-layer type for motion content");
        }
        if (!layer.content.id.empty() && supplied.type == LayerType::Shape &&
            layer.content.kind != ContentKind::Image)
            throw std::invalid_argument("lowerToRenderPlan: shape content requires an explicitly supplied Shape LayerPlan");
        if (layer.content.kind == ContentKind::Image && supplied.type != LayerType::Image &&
            supplied.type != LayerType::Shape && supplied.type != LayerType::Particles &&
            supplied.type != LayerType::Adjustment)
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
        const bool depthOfFieldEnabled = camera.dof_enabled && camera.dof_aperture > 0.f;
        if (camera.fov_deg < 10.f || camera.fov_deg > 120.f || camera.near <= 0.f ||
            camera.far <= camera.near || camera.zoom <= 0.f || camera.type != "perspective" ||
            camera.dof_focus_distance < 0.f || (depthOfFieldEnabled && camera.dof_focus_distance <= 0.f) ||
            camera.dof_focus_distance > 10'000'000.f ||
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
            if (layer.content.isImage() && output.type != LayerType::Image &&
                output.type != LayerType::Shape && output.type != LayerType::Particles &&
                output.type != LayerType::Adjustment)
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
                inline_mesh.metallic_roughness_texture_asset = layer.mesh.metallicRoughnessTextureAsset;
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
                } else if (descriptor.kind == MaterialKind::Lambert ||
                           descriptor.kind == MaterialKind::PBR ||
                           descriptor.kind == MaterialKind::Emissive) {
                    chronon3d::render_plan::InlineMeshMaterialPlan material;
                    material.diffuse = descriptor.kind == MaterialKind::Emissive ? 0.f : 1.f;
                    material.specular = 1.f;
                    material.shininess = 1.f + (1.f - descriptor.roughness) * 1023.f;
                    material.rim_intensity = 0.f;
                    const bool emissive_only = descriptor.kind == MaterialKind::Emissive;
                    material.base_color = std::array<float, 4>{
                        emissive_only ? 0.f : renderPlanColor.r,
                        emissive_only ? 0.f : renderPlanColor.g,
                        emissive_only ? 0.f : renderPlanColor.b, descriptor.opacity};
                    material.metallic = descriptor.metalness;
                    material.roughness = descriptor.roughness;
                    material.emissive = std::array<float, 3>{
                        descriptor.emissiveColor.r * descriptor.emissiveStrength,
                        descriptor.emissiveColor.g * descriptor.emissiveStrength,
                        descriptor.emissiveColor.b * descriptor.emissiveStrength};
                    material.double_sided = descriptor.doubleSided;
                    if (descriptor.alphaCutoff > 0.f) material.alpha_cutoff = descriptor.alphaCutoff;
                    output.color = {1.f, 1.f, 1.f, 1.f};
                    inline_mesh.material = material;
                }
                output.inline_mesh = std::move(inline_mesh);
                output.asset.clear();
                if (!layer.mesh.baseColorTextureAsset.empty()) {
                    output.asset = layer.mesh.baseColorTextureAsset;
                    output.input_color_space = "srgb";
                    if (!suppliedMaterial && descriptor.kind == MaterialKind::Unlit) {
                        output.color = {renderPlanColor.r, renderPlanColor.g,
                                        renderPlanColor.b, descriptor.opacity};
                    }
                }
                if (!layer.mesh.metallicRoughnessTextureAsset.empty())
                    output.inline_mesh->metallic_roughness_texture_asset = layer.mesh.metallicRoughnessTextureAsset;
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
            if (measured.x > 0.f && measured.y > 0.f && math::isFinite(measured))
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

} // namespace chrononmotion::motion::lowering_detail
