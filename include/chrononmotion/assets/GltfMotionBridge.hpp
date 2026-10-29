#ifndef CHRONONMOTION_ASSETS_GLTF_MOTION_BRIDGE_HPP
#define CHRONONMOTION_ASSETS_GLTF_MOTION_BRIDGE_HPP

#include "chrononmotion/materials/PbrMaterial.hpp"
#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/objects/Mesh.hpp"

namespace chrononmotion::motion {

/// Explicitly transfer one static glTF primitive's CPU geometry and supported
/// PBR material into MotionScene authoring. Unsupported Material subclasses
/// fail closed instead of silently dropping render intent.
[[nodiscard]] inline MeshAttachment meshAttachmentFromGltf(
    Mesh& source, std::uint32_t materialId) {
    MeshAttachment attachment;
    attachment.geometry = source.geometry();
    if (const auto material = source.material()) {
        const auto* pbr = material->as<PbrMaterial>();
        if (!pbr)
            throw std::invalid_argument("meshAttachmentFromGltf: only PbrMaterial is supported");
        attachment.material = toMotionMaterialDescriptor(*pbr, materialId);
        attachment.baseColorTextureAsset = pbr->baseColorTextureUri;
        attachment.metallicRoughnessTextureAsset = pbr->metallicRoughnessTextureUri;
    }
    return attachment;
}

} // namespace chrononmotion::motion

#endif
