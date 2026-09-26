#ifndef CHRONONMOTION_ASSETS_GLTF_READER_HPP
#define CHRONONMOTION_ASSETS_GLTF_READER_HPP

#include "chrononmotion/objects/Mesh.hpp"
#include "chrononmotion/scenes/Scene.hpp"

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
