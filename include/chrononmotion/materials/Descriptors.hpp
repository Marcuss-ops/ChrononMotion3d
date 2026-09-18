#ifndef CHRONONMOTION_MATERIAL_DESCRIPTORS_HPP
#define CHRONONMOTION_MATERIAL_DESCRIPTORS_HPP

#include "chrononmotion/math/Color.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace chrononmotion {

    enum class MaterialKind {
        Unlit,
        Lambert,
        Emissive
    };

    /// Data-only material contract. It contains no textures, uniforms, shaders,
    /// GPU handles, or renderer lifecycle state.
    struct MaterialDescriptor {
        std::uint32_t id{0};
        std::string name{};
        MaterialKind kind{MaterialKind::Unlit};
        Color baseColor{};
        Color emissiveColor{};
        float emissiveStrength{0.f};
        float roughness{1.f};
        float metalness{0.f};
        float opacity{1.f};
        bool transparent{false};

        void validate() const {
            if (!std::isfinite(emissiveStrength) || emissiveStrength < 0.f) {
                throw std::invalid_argument("MaterialDescriptor: emissiveStrength must be finite and non-negative");
            }
            if (!std::isfinite(roughness) || roughness < 0.f || roughness > 1.f) {
                throw std::invalid_argument("MaterialDescriptor: roughness must be in [0, 1]");
            }
            if (!std::isfinite(metalness) || metalness < 0.f || metalness > 1.f) {
                throw std::invalid_argument("MaterialDescriptor: metalness must be in [0, 1]");
            }
            if (!std::isfinite(opacity) || opacity < 0.f || opacity > 1.f) {
                throw std::invalid_argument("MaterialDescriptor: opacity must be in [0, 1]");
            }
        }

        [[nodiscard]] static MaterialDescriptor unlit(std::uint32_t id, Color color = Color(0xffffff)) {
            MaterialDescriptor descriptor;
            descriptor.id = id;
            descriptor.kind = MaterialKind::Unlit;
            descriptor.baseColor = color;
            return descriptor;
        }

        [[nodiscard]] static MaterialDescriptor lambert(std::uint32_t id, Color color = Color(0xffffff)) {
            MaterialDescriptor descriptor;
            descriptor.id = id;
            descriptor.kind = MaterialKind::Lambert;
            descriptor.baseColor = color;
            return descriptor;
        }

        [[nodiscard]] static MaterialDescriptor emissive(std::uint32_t id, Color color,
                                                          float strength = 1.f) {
            MaterialDescriptor descriptor;
            descriptor.id = id;
            descriptor.kind = MaterialKind::Emissive;
            descriptor.baseColor = color;
            descriptor.emissiveColor = color;
            descriptor.emissiveStrength = strength;
            descriptor.validate();
            return descriptor;
        }
    };

    struct UnlitMaterial {
        MaterialDescriptor descriptor{MaterialDescriptor::unlit(0)};

        explicit UnlitMaterial(std::uint32_t id, Color color = Color(0xffffff))
            : descriptor(MaterialDescriptor::unlit(id, color)) {}
    };

    struct LambertMaterial {
        MaterialDescriptor descriptor{MaterialDescriptor::lambert(0)};

        explicit LambertMaterial(std::uint32_t id, Color color = Color(0xffffff))
            : descriptor(MaterialDescriptor::lambert(id, color)) {}
    };

    struct EmissiveMaterial {
        MaterialDescriptor descriptor{MaterialDescriptor::emissive(0, Color(0xffffff), 1.f)};

        explicit EmissiveMaterial(std::uint32_t id, Color color, float strength = 1.f)
            : descriptor(MaterialDescriptor::emissive(id, color, strength)) {}
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_MATERIAL_DESCRIPTORS_HPP
