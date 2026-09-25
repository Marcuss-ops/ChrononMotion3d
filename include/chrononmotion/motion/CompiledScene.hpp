#ifndef CHRONONMOTION_MOTION_COMPILED_SCENE_HPP
#define CHRONONMOTION_MOTION_COMPILED_SCENE_HPP

#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Lighting.hpp"

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace chrononmotion::motion {

    struct CompiledScene;

    struct CompiledPrecomp {
        Layer::Id layerId{Layer::kRoot};
        Matrix4 world{};
        float opacity{1.f};
        bool visible{true};
        std::shared_ptr<const CompiledScene> scene{};
    };

    struct CompiledTransform {
        Layer::Id id{Layer::kRoot};
        Matrix4 world{};
        Matrix4 previousWorld{};
        float opacity{1.f};
        float previousOpacity{1.f};
        bool visible{true};
        bool previousVisible{true};
        bool castsShadow{false};
        bool acceptsShadow{true};
        float shadowOpacity{1.f};
        float shadowBlur{0.f};
    };

    struct MeshDescriptor {
        Layer::Id layerId{Layer::kRoot};
        std::string name{};
        std::shared_ptr<BufferGeometry> geometry{};
        MaterialDescriptor material{MaterialDescriptor::unlit(0)};

        /// Opaque identity of the model asset when the geometry itself lives on
        /// the content side. Either this or `geometry` must be present; the
        /// descriptor is the whole of what crosses the boundary.
        std::string assetId{};
    };

    /// Immutable-by-convention data plane handed to a future renderer. The
    /// compiler owns no GPU state and does not resolve content bytes.
    struct CompiledScene {
        std::string name{};
        int frame{0};
        float fps{24.f};
        std::vector<CompiledTransform> transforms{};
        std::vector<CompiledPrecomp> precomps{};
        std::vector<LightState> lights{};
        std::vector<MeshDescriptor> meshes{};
        std::vector<MaterialDescriptor> materials{};
        std::shared_ptr<CameraPose> camera{};

        void addMaterial(MaterialDescriptor material) {
            material.validate();
            materials.push_back(std::move(material));
        }

        void addMesh(MeshDescriptor mesh) {
            if (!mesh.geometry && mesh.assetId.empty()) {
                throw std::invalid_argument("CompiledScene::addMesh: a geometry or an asset id is required");
            }
            mesh.material.validate();
            meshes.push_back(std::move(mesh));
        }
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_COMPILED_SCENE_HPP
