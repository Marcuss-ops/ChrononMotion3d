#include "RenderPlanLoweringInternal.hpp"
#include "chrononmotion/math/Finite.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/Layer.hpp"

#include <chronon3d/core/types/time.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace chrononmotion::motion::lowering_detail;

namespace chrononmotion::motion {

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
            (!math::isFinite(command.point)))
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
