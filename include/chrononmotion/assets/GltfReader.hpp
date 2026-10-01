#ifndef CHRONONMOTION_ASSETS_GLTF_READER_HPP
#define CHRONONMOTION_ASSETS_GLTF_READER_HPP

#include "chrononmotion/objects/Mesh.hpp"
#include "chrononmotion/scenes/Scene.hpp"
#include "chrononmotion/motion/RigBaking.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace chrononmotion {

    /// The resolver is called only for external buffer URIs. ChrononMotion does
    /// no filesystem or network I/O; the caller owns URI policy and byte loading.
    using GltfExternalBufferResolver =
        std::function<std::vector<std::byte>(std::string_view uri)>;

    struct GltfAsset {
        std::shared_ptr<Scene> scene;
        // All primitives in the selected glTF scene, in deterministic node,
        // mesh, primitive order. Meshes are also attached under their node groups.
        std::vector<std::shared_ptr<Mesh>> meshes;
    };

    struct GltfMotionClip {
        std::string name{};
        std::vector<motion::RigAnimatedNode> animation{};
        float duration{0.f};
        struct MorphChannel { std::uint32_t geometryId{0}; std::vector<motion::Track<float>> weights{}; };
        std::vector<MorphChannel> morphs{};
    };
    struct GltfMotionSkin {
        std::vector<motion::RigNodeId> joints{};
        std::vector<Matrix4> inverseBindMatrices{};
        motion::RigNodeId skeleton{0};
    };
    struct GltfMotionAsset {
        motion::RigGraph graph{};
        std::vector<GltfMotionSkin> skins{};
        std::vector<motion::RigGeometryBinding> geometry{};
        std::vector<GltfMotionClip> clips{};
    };
    namespace motion {
        // chrononmotion.rig.program.v1: strict authoring round trip for the
        // static rig program (graph, controllers/drivers, poses, geometry with
        // deformers). Schedules (animation/spaces/IK/weights/secondary) and import-
        // derived handles are deliberately rejected: they are re-derived at authoring
        // time from their own authorities, not stored here.
        [[nodiscard]] std::string serializeRigProgram(const RigProgram& program);
        [[nodiscard]] RigProgram deserializeRigProgram(std::string_view json);
    } // namespace motion

    // Explicit motion-only import. The original static reader remains strict.
    // Narrow TRS hierarchy, triangle POSITION, skins and linear/step clips;
    // no materials, extensions, compression, sparse or implicit I/O.
    [[nodiscard]] GltfMotionAsset loadGltfMotion(
        std::string_view json, const GltfExternalBufferResolver& resolver = {});

    /// Load static glTF 2.0 JSON. Buffers may be embedded base64 data URIs or
    /// supplied by the explicit resolver. Materials, animations, skins,
    /// morph targets, extensions, and non-triangle primitives fail closed.
    [[nodiscard]] GltfAsset loadGltf(
        std::string_view json,
        const GltfExternalBufferResolver& resolveExternalBuffer = {});

    /// Load a GLB 2.0 container. JSON and BIN chunks are parsed in memory;
    /// external buffer URIs still require the explicit caller resolver.
    [[nodiscard]] GltfAsset loadGlb(
        std::span<const std::byte> bytes,
        const GltfExternalBufferResolver& resolveExternalBuffer = {});

} // namespace chrononmotion

#endif // CHRONONMOTION_ASSETS_GLTF_READER_HPP
