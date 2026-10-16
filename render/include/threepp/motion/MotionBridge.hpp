#ifndef THREEPP_MOTION_BRIDGE_HPP
#define THREEPP_MOTION_BRIDGE_HPP

#include "chrononmotion/core/BufferGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/math/Color.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/Lighting.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RigBaking.hpp"

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/core/Object3D.hpp"
#include "threepp/lights/Light.hpp"
#include "threepp/materials/Material.hpp"
#include "threepp/math/Color.hpp"
#include "threepp/math/Matrix4.hpp"
#include "threepp/math/Quaternion.hpp"
#include "threepp/math/Vector3.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace threepp::motion {

    [[nodiscard]] Vector3 toThreeppVector3(const chrononmotion::Vector3& v);
    [[nodiscard]] Quaternion toThreeppQuaternion(const chrononmotion::Quaternion& q);
    [[nodiscard]] Matrix4 toThreeppMatrix4(const chrononmotion::Matrix4& m);
    [[nodiscard]] Color toThreeppColor(const chrononmotion::Color& c);

    /// Extract the rest-pose 3D vertex positions from a ChrononMotion
    /// BufferGeometry (e.g. a subdivided PlaneGeometry for an image mesh) so
    /// they can be bound to a RigGeometryBinding or DeformerStack.
    [[nodiscard]] std::vector<chrononmotion::Vector3> extractGeometryPositions(
        const chrononmotion::BufferGeometry& source);

    /// Convert a data-only ChrononMotion BufferGeometry into a GPU-uploadable
    /// threepp::BufferGeometry. Copies vertex attributes (position, normal, uv,
    /// color) and optional triangle indices/groups.
    [[nodiscard]] std::shared_ptr<BufferGeometry> toThreeppGeometry(
        const chrononmotion::BufferGeometry& source);

    /// Update the vertex positions of a threepp::BufferGeometry in-place from a
    /// deformed vertex array (e.g. RigFrameGeometry::positions or
    /// DeformerStack::evaluate), flagging the GPU buffer for glBufferSubData
    /// upload and optionally recomputing vertex normals.
    void updateGeometryPositions(
        BufferGeometry& geometry,
        std::span<const chrononmotion::Vector3> positions,
        bool recomputeNormals = true);

    /// Convert a data-only ChrononMotion MaterialDescriptor into a concrete
    /// threepp::Material (MeshBasicMaterial for Unlit, MeshLambertMaterial for
    /// Lambert/Emissive), combining descriptor opacity with evaluated world
    /// opacity and optional bound texture map.
    [[nodiscard]] std::shared_ptr<Material> toThreeppMaterial(
        const chrononmotion::MaterialDescriptor& descriptor,
        float worldOpacity = 1.f,
        const std::shared_ptr<Texture>& map = nullptr);

    /// Apply a sampled ChrononMotion CameraPose (and optional parent world
    /// matrix) onto a threepp::PerspectiveCamera.
    void applyCameraPose(
        const chrononmotion::motion::CameraPose& pose,
        PerspectiveCamera& camera,
        const chrononmotion::Matrix4* parentWorld = nullptr,
        float nearPlane = 0.1f,
        float farPlane = 2000.f);

    /// Sample and apply a ChrononMotion CameraRig at the scene's last evaluated
    /// timestamp, respecting parent layer world matrices when the rig's layer is
    /// parented inside `scene`.
    void applyCameraRig(
        const chrononmotion::motion::CameraRig& rig,
        const chrononmotion::motion::MotionScene& scene,
        PerspectiveCamera& camera);

    /// Stateful bridge from ChrononMotion's evaluated `CompiledScene` /
    /// `MotionScene` into a renderable `threepp::Scene` + `PerspectiveCamera`.
    ///
    /// Keeps GPU geometries, materials, textures and imported GLB subtrees alive
    /// across frames and updates world transforms, deformed mesh vertices,
    /// visibility, opacity, lights and camera pose deterministically on each frame.
    class MotionSceneBridge {
    public:
        explicit MotionSceneBridge(
            float defaultFov = 45.f,
            float aspect = 1.f,
            float nearPlane = 0.1f,
            float farPlane = 2000.f);

        [[nodiscard]] std::shared_ptr<Scene> scene() const { return scene_; }
        [[nodiscard]] std::shared_ptr<PerspectiveCamera> camera() const { return camera_; }

        /// Bind a pre-loaded threepp Object3D subtree (e.g. a GLTFResult::scene
        /// loaded via GLTFLoader) to a Motion `assetId`. Any mesh layer carrying
        /// that `assetId` will attach a clone of `node` under its Motion-driven
        /// layer wrapper.
        void bindAsset(const std::string& assetId, std::shared_ptr<Object3D> node);

        /// Bind a pre-built threepp Object3D directly to a Motion layer id.
        void bindLayerObject(chrononmotion::motion::Layer::Id layerId, std::shared_ptr<Object3D> node);

        /// Bind a threepp Texture to a ChrononMotion MaterialDescriptor id.
        void bindMaterialTexture(std::uint32_t materialId, std::shared_ptr<Texture> texture);

        /// Bind a threepp Texture (e.g. an image loaded via TextureLoader) directly
        /// to a Motion mesh layer id.
        void bindLayerTexture(chrononmotion::motion::Layer::Id layerId, std::shared_ptr<Texture> texture);

        /// Synchronize the threepp Scene and Camera from an evaluated CompiledScene.
        void sync(const chrononmotion::motion::CompiledScene& compiled);

        /// Evaluate `motionScene` at `frame`, compile its snapshot, synchronize
        /// all layers and lights, and optionally apply `cameraRig`.
        void evaluateAndSync(
            chrononmotion::motion::MotionScene& motionScene,
            int frame,
            const chrononmotion::motion::CameraRig* cameraRig = nullptr);

        /// Apply deformed vertex positions (from DeformerStack or RigProgram) to
        /// the materialized mesh of `layerId` for the current frame.
        void applyDeformedGeometry(
            chrononmotion::motion::Layer::Id layerId,
            std::span<const chrononmotion::Vector3> positions,
            bool recomputeNormals = true);

        /// Apply a baked/evaluated RigFrameGeometry to `layerId`.
        void applyRigFrame(
            chrononmotion::motion::Layer::Id layerId,
            const chrononmotion::motion::RigFrameGeometry& frameGeometry,
            bool recomputeNormals = true);

        /// Retrieve the threepp node driven by `layerId` (nullptr if not materialized).
        [[nodiscard]] std::shared_ptr<Object3D> layerNode(chrononmotion::motion::Layer::Id layerId) const;

    private:
        struct LightEntry {
            chrononmotion::motion::LightKind kind{chrononmotion::motion::LightKind::Ambient};
            std::shared_ptr<Light> light{};
            std::shared_ptr<Object3D> target{};
        };

        void syncSceneGraph(
            const chrononmotion::motion::CompiledScene& compiled,
            const Matrix4& rootTransform,
            float rootOpacity,
            bool rootVisible,
            const std::string& scopePrefix);

        std::shared_ptr<Scene> scene_;
        std::shared_ptr<PerspectiveCamera> camera_;

        std::unordered_map<std::string, std::shared_ptr<Object3D>> assetPrototypes_;
        std::unordered_map<chrononmotion::motion::Layer::Id, std::shared_ptr<Object3D>> boundLayerObjects_;
        std::unordered_map<std::uint32_t, std::shared_ptr<Texture>> materialTextures_;
        std::unordered_map<chrononmotion::motion::Layer::Id, std::shared_ptr<Texture>> layerTextures_;

        std::unordered_map<std::string, std::shared_ptr<Object3D>> layerNodes_;
        std::unordered_map<unsigned int, std::shared_ptr<BufferGeometry>> geometryCache_;
        std::unordered_map<std::string, LightEntry> lightNodes_;
    };

} // namespace threepp::motion

#endif // THREEPP_MOTION_BRIDGE_HPP
