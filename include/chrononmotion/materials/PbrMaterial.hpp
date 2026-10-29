#ifndef CHRONONMOTION_PBR_MATERIAL_HPP
#define CHRONONMOTION_PBR_MATERIAL_HPP

#include "chrononmotion/materials/Material.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/math/Color.hpp"

#include <string>

namespace chrononmotion {

/// Renderer-independent metallic/roughness material data imported from glTF.
class PbrMaterial final : public Material {
public:
    Color baseColor{};
    Color emissiveColor{0.f, 0.f, 0.f};
    float roughness{1.f};
    float metalness{0.f};
    float emissiveStrength{1.f};
    float alphaCutoff{0.5f};
    bool doubleSided{false};
    /// Mounted-root-relative external base-color image URI. Embedded images and
    /// non-default sampler behavior are rejected by the static glTF V1 reader.
    std::string baseColorTextureUri{};
    /// Mounted-root-relative linear metallic-roughness image URI.
    std::string metallicRoughnessTextureUri{};

    [[nodiscard]] std::string type() const override { return "PbrMaterial"; }
};

[[nodiscard]] inline MaterialDescriptor toMotionMaterialDescriptor(
    const PbrMaterial& source, std::uint32_t id) {
    auto descriptor = MaterialDescriptor::pbr(
        id, source.baseColor, source.roughness, source.metalness,
        source.emissiveColor, source.emissiveStrength);
    descriptor.name = source.name;
    descriptor.opacity = source.opacity;
    descriptor.transparent = source.transparent;
    descriptor.doubleSided = source.doubleSided;
    descriptor.alphaCutoff = source.alphaTest > 0.f ? source.alphaCutoff : 0.f;
    descriptor.validate();
    return descriptor;
}

} // namespace chrononmotion

#endif
