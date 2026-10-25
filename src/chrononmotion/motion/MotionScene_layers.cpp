#include "chrononmotion/motion/MotionScene.hpp"
#include "MotionScene_internal.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::motion {
using scene_detail::geometryVertexCount;
using scene_detail::keyframeCount;
using scene_detail::primitiveSegmentCount;
using scene_detail::saturatedAdd;

    Layer& MotionScene::addLayer(Layer layer) {

        ensureMutable("add or replace layer");
        const bool replaces = index_.contains(layer.id);
        if (!replaces && layers_.size() >= policy_.maxLayers) {
            audit("addLayer", layer.name, false, "layer budget exceeded");
            throw std::length_error("MotionScene: layer budget exceeded");
        }
        if (layer.mesh.geometry) {
            const std::string geometryType = layer.mesh.geometry->type();
            if (!policy_.allowedGeometryTypes.empty() &&
                std::find(policy_.allowedGeometryTypes.begin(), policy_.allowedGeometryTypes.end(), geometryType) ==
                    policy_.allowedGeometryTypes.end()) {
                audit("addGeometry", geometryType, false, "geometry type is not in the allowlist");
                throw std::invalid_argument("MotionScene: geometry type is not allowlisted: " + geometryType);
            }
            const std::size_t vertices = geometryVertexCount(*layer.mesh.geometry);
            std::size_t totalVertices = 0;
            for (const auto& candidate : layers_) {
                if (replaces && candidate.id == layer.id) continue;
                if (candidate.mesh.geometry)
                    totalVertices = saturatedAdd(totalVertices, geometryVertexCount(*candidate.mesh.geometry));
            }
            if (vertices > policy_.maxGeometryVertices - std::min(totalVertices, policy_.maxGeometryVertices)) {
                audit("addGeometry", geometryType, false, "geometry vertex budget exceeded");
                throw std::length_error("MotionScene: geometry vertex budget exceeded");
            }
        }
        std::size_t existingKeyframes = 0;
        std::size_t existingVertices = 0;
        std::size_t existingSegments = 0;
        for (const auto& candidate : layers_) {
            if (replaces && candidate.id == layer.id) continue;
            existingKeyframes = saturatedAdd(existingKeyframes, keyframeCount(candidate));
            if (candidate.mesh.geometry) {
                existingVertices = saturatedAdd(existingVertices, geometryVertexCount(*candidate.mesh.geometry));
                existingSegments = saturatedAdd(existingSegments, primitiveSegmentCount(*candidate.mesh.geometry));
            }
        }
        for (const auto& candidate : lights_)
            existingKeyframes = saturatedAdd(existingKeyframes, keyframeCount(*candidate));
        if (policy_.maxKeyframes != std::numeric_limits<std::size_t>::max() &&
            keyframeCount(layer) > policy_.maxKeyframes - std::min(existingKeyframes, policy_.maxKeyframes)) {
            audit("addLayer", layer.name, false, "keyframe budget exceeded");
            throw std::length_error("MotionScene: keyframe budget exceeded");
        }
        if (layer.mesh.geometry &&
            geometryVertexCount(*layer.mesh.geometry) >
                policy_.maxGeometryVertices - std::min(existingVertices, policy_.maxGeometryVertices)) {
            audit("addGeometry", layer.mesh.geometry->type(), false, "geometry vertex budget exceeded");
            throw std::length_error("MotionScene: geometry vertex budget exceeded");
        }
        if (layer.mesh.geometry &&
            primitiveSegmentCount(*layer.mesh.geometry) >
                policy_.maxPrimitiveSegments - std::min(existingSegments, policy_.maxPrimitiveSegments)) {
            audit("addGeometry", layer.mesh.geometry->type(), false, "primitive segment budget exceeded");
            throw std::length_error("MotionScene: primitive segment budget exceeded");
        }
        if (!policy_.allowAnimations && keyframeCount(layer) != 0) {
            audit("addLayer", layer.name, false, "animations are disabled");
            throw std::logic_error("MotionScene: animations are disabled by authoring policy");
        }
        audit(replaces ? "replaceLayer" : "addLayer", layer.name, true);
        if (layer.mesh.geometry) {
            layer.mesh.geometry->freeze();
            for (const auto& [unused, attribute] : layer.mesh.geometry->getAttributes()) {
                (void)unused;
                if (attribute) attribute->freeze();
            }
            if (auto* index = layer.mesh.geometry->getIndex()) index->freeze();
            audit("addGeometry", layer.mesh.geometry->type(), true);
        }
        layer.authoringOwner = this;
        const Layer::Id id = layer.id;
        const auto existing = index_.find(id);

        if (existing != index_.end()) {
            Layer& stored = layers_[existing->second];
            stored.replaceAuthoring(std::move(layer));
            stored.authoringOwner = this;
            attachTrackGuards(stored);
            states_[existing->second] = LayerState{};
            orderDirty_ = true;
            return stored;
        }

        index_.emplace(id, layers_.size());
        layers_.emplace_back();
        Layer& stored = layers_.back();
        stored.replaceAuthoring(std::move(layer));
        stored.authoringOwner = this;
        attachTrackGuards(stored);
        states_.emplace_back();
        orderDirty_ = true;
        return stored;
    }

    Layer& MotionScene::addLayer(Layer::Id id, std::string name) {

        return addLayer(Layer(id, std::move(name)));
    }

    Layer& MotionScene::addNull(Layer::Id id, std::string name, Layer::Id parentId) {

        Layer layer(id, std::move(name));
        layer.parentId = parentId;
        return addLayer(std::move(layer));
    }

    Layer& MotionScene::addContent(Layer::Id id, std::string name, ContentRef content, Layer::Id parentId) {

        Layer layer(id, std::move(name));
        layer.parentId = parentId;
        layer.content = std::move(content);
        return addLayer(std::move(layer));
    }

    Layer& MotionScene::addMesh(Layer::Id id, std::string name, MeshAttachment mesh, Layer::Id parentId) {

        Layer layer(id, std::move(name));
        layer.parentId = parentId;
        layer.mesh = std::move(mesh);
        return addLayer(std::move(layer));
    }

    Layer::Id MotionScene::allocateLayerId() {
        ensureMutable("allocate layer id");
        Layer::Id candidate = 1;
        while (index_.contains(candidate)) {
            if (candidate == std::numeric_limits<Layer::Id>::max())
                throw std::overflow_error("MotionScene::allocateLayerId: no layer ids remain");
            ++candidate;
        }
        return candidate;
    }

    Light& MotionScene::addLight(std::unique_ptr<Light> light) {
        ensureMutable("add light");
        if (!light) throw std::invalid_argument("MotionScene::addLight: light is required");
        if (lightIndex_.size() >= policy_.maxLights) {
            audit("addLight", light->name, false, "light budget exceeded");
            throw std::length_error("MotionScene: light budget exceeded");
        }
        if (lightIndex_.contains(light->id)) {
            audit("addLight", light->name, false, "duplicate light id");
            throw std::invalid_argument("MotionScene::addLight: duplicate light id");
        }
        if (!policy_.allowAnimations && keyframeCount(*light) != 0) {
            audit("addLight", light->name, false, "animations are disabled");
            throw std::logic_error("MotionScene: animations are disabled by authoring policy");
        }
        std::size_t existingKeyframes = 0;
        for (const auto& layer : layers_) existingKeyframes = saturatedAdd(existingKeyframes, keyframeCount(layer));
        for (const auto& candidate : lights_) existingKeyframes = saturatedAdd(existingKeyframes, keyframeCount(*candidate));
        if (policy_.maxKeyframes != std::numeric_limits<std::size_t>::max() &&
            keyframeCount(*light) > policy_.maxKeyframes - std::min(existingKeyframes, policy_.maxKeyframes)) {
            audit("addLight", light->name, false, "keyframe budget exceeded");
            throw std::length_error("MotionScene: keyframe budget exceeded");
        }
        audit("addLight", light->name, true);
        const Light::Id id = light->id;
        lightIndex_.emplace(id, lights_.size());
        lights_.push_back(std::move(light));
        attachLightTrackGuards(*lights_.back());
        lightStates_.emplace_back();
        return *lights_.back();
    }

    AmbientLight& MotionScene::addAmbientLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<AmbientLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<AmbientLight&>(addLight(std::move(light)));
    }

    DirectionalLight& MotionScene::addDirectionalLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<DirectionalLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<DirectionalLight&>(addLight(std::move(light)));
    }

    PointLight& MotionScene::addPointLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<PointLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<PointLight&>(addLight(std::move(light)));
    }

    SpotLight& MotionScene::addSpotLight(Layer::Id id, std::string name, Layer::Id parentId) {
        auto light = std::make_unique<SpotLight>(id, std::move(name));
        light->parentId = parentId;
        return static_cast<SpotLight&>(addLight(std::move(light)));
    }

    std::size_t MotionScene::indexOf(Layer::Id id) const {

        const auto it = index_.find(id);
        return it == index_.end() ? npos : it->second;
    }

    std::size_t MotionScene::lightIndexOf(Light::Id id) const {

        const auto it = lightIndex_.find(id);
        return it == lightIndex_.end() ? npos : it->second;
    }

    Layer* MotionScene::findLayer(Layer::Id id) {

        ensureMutable("mutable layer access");
        const std::size_t index = indexOf(id);
        return index == npos ? nullptr : &layers_[index];
    }

    const Layer* MotionScene::findLayer(Layer::Id id) const {

        const std::size_t index = indexOf(id);
        return index == npos ? nullptr : &layers_[index];
    }

    Layer* MotionScene::findLayer(const std::string& name) {

        ensureMutable("mutable layer access");
        for (auto& layer : layers_) {
            if (layer.name == name) return &layer;
        }
        return nullptr;
    }

    const Layer* MotionScene::findLayer(const std::string& name) const {

        for (const auto& layer : layers_) {
            if (layer.name == name) return &layer;
        }
        return nullptr;
    }

    bool MotionScene::hasLayer(Layer::Id id) const {

        return index_.find(id) != index_.end();
    }

} // namespace chrononmotion::motion
