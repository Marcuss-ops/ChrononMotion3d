#include "threepp/motion/MotionBridge.hpp"

#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/lights/PointLight.hpp"
#include "threepp/lights/SpotLight.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/materials/MeshLambertMaterial.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/objects/SkinnedMesh.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace threepp::motion {

    namespace {

        std::shared_ptr<Object3D> cloneAssetTree(const std::shared_ptr<Object3D>& source) {
            if (!source) return nullptr;
            std::unordered_map<const Object3D*, std::shared_ptr<Object3D>> clones;
            std::function<std::shared_ptr<Object3D>(Object3D&)> cloneNode;
            cloneNode = [&](Object3D& original) {
                auto clone = original.clone(false);
                if (!clone) throw std::runtime_error("MotionSceneBridge: failed to clone asset node");
                clones.emplace(&original, clone);
                for (const auto& child : original.children) {
                    clone->add(cloneNode(*child));
                }
                return clone;
            };
            auto root = cloneNode(*source);

            // Skin joints are often siblings of the SkinnedMesh in a glTF scene.
            // Reconnect each cloned skin after the complete node map exists;
            // copying only the mesh would leave it bound to the prototype bones.
            for (const auto& [original, clone] : clones) {
                const auto* sourceSkin = dynamic_cast<const SkinnedMesh*>(original);
                auto* clonedSkin = dynamic_cast<SkinnedMesh*>(clone.get());
                if (!sourceSkin || !clonedSkin || !sourceSkin->skeleton) continue;
                std::vector<std::shared_ptr<Bone>> bones;
                bones.reserve(sourceSkin->skeleton->bones.size());
                for (const auto& sourceBone : sourceSkin->skeleton->bones) {
                    const auto it = clones.find(sourceBone.get());
                    if (it == clones.end()) {
                        throw std::runtime_error("MotionSceneBridge: skin joint is outside the cloned asset subtree");
                    }
                    auto clonedBone = std::dynamic_pointer_cast<Bone>(it->second);
                    if (!clonedBone) throw std::runtime_error("MotionSceneBridge: cloned skin joint lost its Bone type");
                    bones.push_back(std::move(clonedBone));
                }
                auto skeleton = Skeleton::create(bones, sourceSkin->skeleton->boneInverses);
                clonedSkin->bind(skeleton, sourceSkin->bindMatrix);
            }
            return root;
        }

    }// namespace

    Vector3 toThreeppVector3(const chrononmotion::Vector3& v) {
        return Vector3(v.x, v.y, v.z);
    }

    Quaternion toThreeppQuaternion(const chrononmotion::Quaternion& q) {
        return Quaternion(q.x, q.y, q.z, q.w);
    }

    Matrix4 toThreeppMatrix4(const chrononmotion::Matrix4& m) {
        Matrix4 out;
        out.elements = m.elements;
        return out;
    }

    Color toThreeppColor(const chrononmotion::Color& c) {
        return Color(c.r, c.g, c.b);
    }

    std::vector<chrononmotion::Vector3> extractGeometryPositions(
            const chrononmotion::BufferGeometry& source) {

        const auto* posAttr = source.getAttribute<float>("position");
        if (!posAttr || posAttr->itemSize() < 3) {
            throw std::invalid_argument("extractGeometryPositions: source geometry lacks a 3D float 'position' attribute");
        }
        const int count = posAttr->count();
        std::vector<chrononmotion::Vector3> out;
        out.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            out.emplace_back(posAttr->getX(i), posAttr->getY(i), posAttr->getZ(i));
        }
        return out;
    }

    std::shared_ptr<BufferGeometry> toThreeppGeometry(const chrononmotion::BufferGeometry& source) {
        auto dst = BufferGeometry::create();
        dst->name = source.name;

        for (const auto& [name, attrPtr] : source.getAttributes()) {
            if (!attrPtr) continue;
            if (const auto* floatAttr = source.getAttribute<float>(name)) {
                auto copied = FloatBufferAttribute::create(
                        floatAttr->array(), floatAttr->itemSize(), floatAttr->normalized());
                dst->setAttribute(name, std::move(copied));
            }
        }

        if (source.hasIndex() && source.getIndex()) {
            dst->setIndex(source.getIndex()->array());
        }

        for (const auto& g : source.groups) {
            dst->addGroup(g.start, g.count, g.materialIndex);
        }

        if (dst->hasAttribute("position")) {
            dst->computeBoundingBox();
            dst->computeBoundingSphere();
        }
        return dst;
    }

    void updateGeometryPositions(
            BufferGeometry& geometry,
            std::span<const chrononmotion::Vector3> positions,
            bool recomputeNormals) {

        auto* posAttr = geometry.getAttribute<float>("position");
        if (!posAttr || posAttr->itemSize() != 3) {
            throw std::invalid_argument("updateGeometryPositions: target geometry lacks a 3-component float 'position' attribute");
        }
        if (static_cast<std::size_t>(posAttr->count()) != positions.size()) {
            throw std::invalid_argument("updateGeometryPositions: vertex count mismatch");
        }

        auto& arr = posAttr->array();
        for (std::size_t i = 0; i < positions.size(); ++i) {
            const auto& v = positions[i];
            if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
                throw std::invalid_argument("updateGeometryPositions: non-finite vertex position");
            }
            arr[i * 3 + 0] = v.x;
            arr[i * 3 + 1] = v.y;
            arr[i * 3 + 2] = v.z;
        }
        posAttr->needsUpdate();

        if (recomputeNormals) {
            geometry.computeVertexNormals();
            if (auto* normAttr = geometry.getAttribute<float>("normal")) {
                normAttr->needsUpdate();
            }
        }
        geometry.computeBoundingBox();
        geometry.computeBoundingSphere();
    }

    std::shared_ptr<Material> toThreeppMaterial(
            const chrononmotion::MaterialDescriptor& descriptor,
            float worldOpacity,
            const std::shared_ptr<Texture>& map,
            const std::shared_ptr<Texture>& normalMap = nullptr) {

        descriptor.validate();
        const float effectiveOpacity = std::clamp(descriptor.opacity * worldOpacity, 0.f, 1.f);
        const bool isTransparent = descriptor.transparent || (effectiveOpacity < 1.f);

        switch (descriptor.kind) {
            case chrononmotion::MaterialKind::Unlit: {
                auto mat = MeshBasicMaterial::create();
                mat->name = descriptor.name;
                mat->color = toThreeppColor(descriptor.baseColor);
                mat->map = map;
                mat->opacity = effectiveOpacity;
                mat->transparent = isTransparent;
                return mat;
            }
            case chrononmotion::MaterialKind::Lambert: {
                auto mat = MeshLambertMaterial::create();
                mat->name = descriptor.name;
                mat->color = toThreeppColor(descriptor.baseColor);
                mat->map = map;
                mat->opacity = effectiveOpacity;
                mat->transparent = isTransparent;
                return mat;
            }
            case chrononmotion::MaterialKind::PBR: {
                auto mat = MeshStandardMaterial::create();
                mat->name = descriptor.name;
                mat->color = toThreeppColor(descriptor.baseColor);
                mat->map = map;
                mat->normalMap = normalMap;
                mat->roughness = descriptor.roughness;
                mat->metalness = descriptor.metalness;
                mat->emissive = toThreeppColor(descriptor.emissiveColor);
                mat->emissiveIntensity = descriptor.emissiveStrength;
                mat->opacity = effectiveOpacity;
                mat->transparent = isTransparent;
                return mat;
            }
            case chrononmotion::MaterialKind::Emissive: {
                auto mat = MeshLambertMaterial::create();
                mat->name = descriptor.name;
                mat->color = toThreeppColor(descriptor.baseColor);
                mat->emissive = Color(
                        descriptor.emissiveColor.r * descriptor.emissiveStrength,
                        descriptor.emissiveColor.g * descriptor.emissiveStrength,
                        descriptor.emissiveColor.b * descriptor.emissiveStrength);
                mat->map = map;
                mat->opacity = effectiveOpacity;
                mat->transparent = isTransparent;
                return mat;
            }
        }
        return MeshBasicMaterial::create();
    }

    void applyCameraPose(
            const chrononmotion::motion::CameraPose& pose,
            PerspectiveCamera& camera,
            const chrononmotion::Matrix4* parentWorld,
            float nearPlane,
            float farPlane) {

        chrononmotion::Matrix4 local;
        local.compose(pose.position, pose.orientation, chrononmotion::Vector3(1.f, 1.f, 1.f));

        chrononmotion::Matrix4 world = local;
        if (parentWorld) {
            world.multiplyMatrices(*parentWorld, local);
        }

        chrononmotion::Vector3 pos;
        chrononmotion::Quaternion quat;
        chrononmotion::Vector3 scale(1.f, 1.f, 1.f);
        world.decompose(pos, quat, scale);

        camera.matrixAutoUpdate = true;
        camera.position = toThreeppVector3(pos);
        camera.quaternion = toThreeppQuaternion(quat);
        camera.scale.set(1.f, 1.f, 1.f);
        camera.nearPlane = nearPlane;
        camera.farPlane = farPlane;

        if (pose.focalLength > 0.f) {
            camera.fov = pose.fov;
            camera.setFocalLength(pose.focalLength);
        } else {
            camera.fov = pose.fov;
        }

        camera.updateMatrixWorld(true);
        camera.updateProjectionMatrix();
    }

    void applyCameraRig(
            const chrononmotion::motion::CameraRig& rig,
            const chrononmotion::motion::MotionScene& scene,
            PerspectiveCamera& camera) {

        const float seconds = scene.hasEvaluated() ? scene.lastEvaluatedPts() : 0.f;
        const auto pose = rig.sample(seconds);
        const chrononmotion::Matrix4* parentWorld = nullptr;
        const auto parentId = rig.layer().parentId;
        if (parentId != chrononmotion::motion::Layer::kRoot &&
            scene.hasEvaluated() &&
            scene.hasLayer(parentId)) {
            parentWorld = &scene.worldMatrix(parentId);
        }
        camera.aspect = rig.aspect();
        applyCameraPose(pose, camera, parentWorld, rig.nearPlane(), rig.farPlane());
    }

    MotionSceneBridge::MotionSceneBridge(
            float defaultFov,
            float aspect,
            float nearPlane,
            float farPlane)
        : scene_(Scene::create()),
          camera_(PerspectiveCamera::create(defaultFov, aspect, nearPlane, farPlane)) {}

    void MotionSceneBridge::bindAsset(const std::string& assetId, std::shared_ptr<Object3D> node) {
        if (assetId.empty() || !node) return;
        assetPrototypes_[assetId] = std::move(node);
    }

    void MotionSceneBridge::bindLayerObject(
            chrononmotion::motion::Layer::Id layerId,
            std::shared_ptr<Object3D> node) {
        if (layerId == chrononmotion::motion::Layer::kRoot || !node) return;
        boundLayerObjects_[layerId] = std::move(node);
    }

    void MotionSceneBridge::bindLayerAnimation(
            chrononmotion::motion::Layer::Id layerId,
            std::shared_ptr<AnimationClip> clip) {
        if (layerId == chrononmotion::motion::Layer::kRoot || !clip) {
            throw std::invalid_argument("MotionSceneBridge::bindLayerAnimation: valid layer id and clip required");
        }
        auto& entry = animations_[layerId];
        entry.clip = std::move(clip);
        entry.root.reset();
        entry.mixer.reset();
    }

    float MotionSceneBridge::clipSequenceWeight(
            const std::vector<ClipSegment>& segments,
            std::size_t index,
            float t) {
        const std::size_t n = segments.size();
        if (index >= n) return 0.f;
        const auto& seg = segments[index];

        // Fade in over [start, start + fadeIn], linear.
        float in = 0.f;
        if (t >= seg.startSeconds) {
            in = seg.fadeInSeconds > 0.f ? std::min(1.f, (t - seg.startSeconds) / seg.fadeInSeconds) : 1.f;
        }

        // Fade out is driven by the next segment's fade-in window; the final
        // segment fades out at its endSeconds.
        float out = 1.f;
        if (index + 1 < n) {
            const auto& next = segments[index + 1];
            if (t >= next.startSeconds) {
                out = next.fadeInSeconds > 0.f
                              ? std::max(0.f, 1.f - (t - next.startSeconds) / next.fadeInSeconds)
                              : 0.f;
            }
        } else if (t >= seg.endSeconds) {
            out = 0.f;
        }
        return in * out;
    }

    void MotionSceneBridge::bindLayerClipSequence(
            chrononmotion::motion::Layer::Id layerId,
            std::vector<ClipSegment> segments) {
        if (layerId == chrononmotion::motion::Layer::kRoot || segments.empty()) {
            throw std::invalid_argument("MotionSceneBridge::bindLayerClipSequence: valid layer id and segments required");
        }
        for (std::size_t i = 0; i < segments.size(); ++i) {
            const auto& seg = segments[i];
            if (!seg.clip || !std::isfinite(seg.startSeconds) || !std::isfinite(seg.fadeInSeconds) || seg.fadeInSeconds < 0.f) {
                throw std::invalid_argument("MotionSceneBridge::bindLayerClipSequence: segment needs clip and finite start/non-negative fade");
            }
            if (i > 0 && seg.startSeconds < segments[i - 1].startSeconds) {
                throw std::invalid_argument("MotionSceneBridge::bindLayerClipSequence: segments must be sorted by start");
            }
        }
        if (!(segments.back().endSeconds > segments.back().startSeconds)) {
            throw std::invalid_argument("MotionSceneBridge::bindLayerClipSequence: last segment needs endSeconds > startSeconds");
        }
        auto& entry = sequences_[layerId];
        entry.segments = std::move(segments);
        entry.root.reset();
        entry.mixer.reset();
        entry.actions.clear();
    }

    void MotionSceneBridge::bindMaterialTexture(
            std::uint32_t materialId,
            std::shared_ptr<Texture> texture) {
        if (!texture) {
            materialTextures_.erase(materialId);
            return;
        }
        materialTextures_[materialId] = std::move(texture);
    }

    void MotionSceneBridge::bindMaterialNormalMap(
            std::uint32_t materialId,
            std::shared_ptr<Texture> texture) {
        if (!texture) {
            materialNormalMaps_.erase(materialId);
            return;
        }
        materialNormalMaps_[materialId] = std::move(texture);
    }

    void MotionSceneBridge::bindLayerTexture(
            chrononmotion::motion::Layer::Id layerId,
            std::shared_ptr<Texture> texture) {
        if (!texture) {
            layerTextures_.erase(layerId);
            return;
        }
        layerTextures_[layerId] = std::move(texture);
    }

    std::shared_ptr<Object3D> MotionSceneBridge::layerNode(
            chrononmotion::motion::Layer::Id layerId) const {
        const auto it = layerNodes_.find(std::to_string(layerId));
        return it != layerNodes_.end() ? it->second : nullptr;
    }

    void MotionSceneBridge::applyDeformedGeometry(
            chrononmotion::motion::Layer::Id layerId,
            std::span<const chrononmotion::Vector3> positions,
            bool recomputeNormals) {

        auto node = layerNode(layerId);
        if (!node) {
            throw std::invalid_argument("MotionSceneBridge::applyDeformedGeometry: layer node not materialized");
        }
        bool updated = false;
        node->traverse([&](Object3D& obj) {
            if (updated) return;
            if (auto* mesh = dynamic_cast<Mesh*>(&obj)) {
                if (mesh->geometry()) {
                    updateGeometryPositions(*mesh->geometry(), positions, recomputeNormals);
                    updated = true;
                }
            }
        });
        if (!updated) {
            throw std::invalid_argument("MotionSceneBridge::applyDeformedGeometry: layer has no Mesh geometry");
        }
    }

    void MotionSceneBridge::applyRigFrame(
            chrononmotion::motion::Layer::Id layerId,
            const chrononmotion::motion::RigFrameGeometry& frameGeometry,
            bool recomputeNormals) {
        applyDeformedGeometry(layerId, frameGeometry.positions, recomputeNormals);
    }

    namespace {

        void applySubtreeOpacityAndShadows(
                Object3D& root,
                float opacity,
                bool castShadow,
                bool receiveShadow) {

            const float clamped = std::clamp(opacity, 0.f, 1.f);
            root.traverse([&](Object3D& node) {
                node.castShadow = castShadow;
                node.receiveShadow = receiveShadow;
                if (auto* mesh = dynamic_cast<Mesh*>(&node)) {
                    for (auto& mat : mesh->materials()) {
                        if (!mat) continue;
                        mat->opacity = clamped;
                        if (clamped < 1.f) {
                            mat->transparent = true;
                        }
                    }
                }
            });
        }

    }// namespace

    void MotionSceneBridge::sync(const chrononmotion::motion::CompiledScene& compiled) {
        for (auto& [key, node] : layerNodes_) {
            (void) key;
            if (node) node->visible = false;
        }
        for (auto& [key, entry] : lightNodes_) {
            (void) key;
            if (entry.light) entry.light->visible = false;
        }

        Matrix4 identity;
        syncSceneGraph(compiled, identity, 1.f, true, "");

        if (compiled.camera) {
            applyCameraPose(
                    *compiled.camera,
                    *camera_,
                    nullptr,
                    compiled.cameraNearPlane,
                    compiled.cameraFarPlane);
        }

        if (!std::isfinite(compiled.fps) || compiled.fps <= 0.f) {
            throw std::invalid_argument("MotionSceneBridge::sync: compiled fps must be finite and positive");
        }
        const float timelineSeconds = std::max(0.f, static_cast<float>(compiled.frame) / compiled.fps);
        for (auto& [layerId, animation] : animations_) {
            auto root = layerNode(layerId);
            if (!root || !animation.clip) continue;
            if (animation.root != root || !animation.mixer) {
                animation.mixer = std::make_unique<AnimationMixer>(*root);
                animation.mixer->clipAction(animation.clip)->play();
                animation.root = std::move(root);
            }
            animation.mixer->setTime(timelineSeconds);
        }

        // Clip sequences: each segment's weight is a linear crossfade. Segment i
        // fades in over [start_i, start_i + fadeIn_i]; the previous segment fades
        // out over the same window, so the weights always sum to 1 there.
        for (auto& [layerId, seq] : sequences_) {
            auto root = layerNode(layerId);
            if (!root || seq.segments.empty()) continue;
            if (seq.root != root || !seq.mixer) {
                seq.mixer = std::make_unique<AnimationMixer>(*root);
                seq.actions.clear();
                for (const auto& seg : seq.segments) {
                    AnimationAction* action = seq.mixer->clipAction(seg.clip);
                    action->setEffectiveWeight(0.f);
                    action->play();
                    seq.actions.push_back(action);
                }
                seq.root = std::move(root);
            }

            const auto n = seq.segments.size();
            const float t = timelineSeconds;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& seg = seq.segments[i];
                AnimationAction* action = seq.actions[i];
                const float weight = clipSequenceWeight(seq.segments, i, t);
                action->setEffectiveWeight(weight);
                if (weight > 0.f) {
                    // Clips loop (Loop::Repeat default): wrap local time into [0, duration).
                    const float duration = seg.clip->getDuration();
                    const float elapsed = std::max(0.f, t - seg.startSeconds);
                    const float local = duration > 0.f ? std::fmod(elapsed, duration) : 0.f;
                    action->setLocalTime(local);
                }
            }
            seq.mixer->update(0.f);
        }

        scene_->updateMatrixWorld(true);
    }

    void MotionSceneBridge::evaluateAndSync(
            chrononmotion::motion::MotionScene& motionScene,
            int frame,
            const chrononmotion::motion::CameraRig* cameraRig) {

        motionScene.evaluate(frame);
        auto compiled = motionScene.compile();
        sync(compiled);
        if (cameraRig) {
            applyCameraRig(*cameraRig, motionScene, *camera_);
            scene_->updateMatrixWorld(true);
        }
    }

    void MotionSceneBridge::syncSceneGraph(
            const chrononmotion::motion::CompiledScene& compiled,
            const Matrix4& rootTransform,
            float rootOpacity,
            bool rootVisible,
            const std::string& scopePrefix) {

        std::unordered_map<chrononmotion::motion::Layer::Id, const chrononmotion::motion::CompiledTransform*> transformsById;
        transformsById.reserve(compiled.transforms.size());
        for (const auto& t : compiled.transforms) {
            transformsById[t.id] = &t;
        }

        auto applyTransformToWrapper = [&](Object3D& wrapper, const chrononmotion::motion::CompiledTransform& t, float baseMatOpacity) {
            Matrix4 world = toThreeppMatrix4(t.world);
            Matrix4 composed;
            composed.multiplyMatrices(rootTransform, world);

            wrapper.matrixAutoUpdate = false;
            wrapper.matrix->copy(composed);
            wrapper.matrixWorldNeedsUpdate = true;

            const float effectiveOpacity = std::clamp(rootOpacity * t.opacity * baseMatOpacity, 0.f, 1.f);
            wrapper.visible = rootVisible && t.visible && (effectiveOpacity > 0.f);
            applySubtreeOpacityAndShadows(wrapper, effectiveOpacity, t.castsShadow, t.acceptsShadow);
        };

        // 1. Materialize / update MeshDescriptors
        for (const auto& meshDesc : compiled.meshes) {
            const auto tIt = transformsById.find(meshDesc.layerId);
            if (tIt == transformsById.end()) continue;
            const auto& t = *tIt->second;

            const std::string nodeKey = scopePrefix + std::to_string(meshDesc.layerId);
            auto& wrapper = layerNodes_[nodeKey];

            std::shared_ptr<Texture> boundTex;
            if (const auto layerTexIt = layerTextures_.find(meshDesc.layerId); layerTexIt != layerTextures_.end()) {
                boundTex = layerTexIt->second;
            } else if (const auto matTexIt = materialTextures_.find(meshDesc.material.id); matTexIt != materialTextures_.end()) {
                boundTex = matTexIt->second;
            }
            std::shared_ptr<Texture> boundNormalMap;
            if (const auto normalMapIt = materialNormalMaps_.find(meshDesc.material.id);
                normalMapIt != materialNormalMaps_.end()) {
                boundNormalMap = normalMapIt->second;
            }

            if (!wrapper) {
                if (const auto boundIt = boundLayerObjects_.find(meshDesc.layerId);
                    boundIt != boundLayerObjects_.end() && boundIt->second) {
                    auto group = Group::create();
                    group->name = meshDesc.name;
                    group->add(boundIt->second);
                    wrapper = std::move(group);
                } else if (!meshDesc.assetId.empty() && assetPrototypes_.contains(meshDesc.assetId)) {
                    auto group = Group::create();
                    group->name = meshDesc.name;
                    group->add(cloneAssetTree(assetPrototypes_.at(meshDesc.assetId)));
                    wrapper = std::move(group);
                } else if (meshDesc.geometry) {
                    auto& cachedGeom = geometryCache_[meshDesc.geometry->id];
                    if (!cachedGeom) {
                        cachedGeom = toThreeppGeometry(*meshDesc.geometry);
                    }
                    auto mat = toThreeppMaterial(meshDesc.material, 1.f, boundTex, boundNormalMap);
                    auto meshNode = Mesh::create(cachedGeom, mat);
                    meshNode->name = meshDesc.name;
                    wrapper = std::move(meshNode);
                }
                if (wrapper) {
                    scene_->add(wrapper);
                }
            }

            if (wrapper) {
                applyTransformToWrapper(*wrapper, t, meshDesc.material.opacity);
            }
        }

        // 2. Materialize / update bound layer objects without MeshDescriptor
        if (scopePrefix.empty()) {
            for (const auto& [layerId, boundObj] : boundLayerObjects_) {
                if (!boundObj) continue;
                const std::string nodeKey = std::to_string(layerId);
                if (layerNodes_.contains(nodeKey)) continue;
                const auto tIt = transformsById.find(layerId);
                if (tIt == transformsById.end()) continue;

                auto group = Group::create();
                group->add(boundObj);
                scene_->add(group);
                layerNodes_[nodeKey] = group;
                applyTransformToWrapper(*group, *tIt->second, 1.f);
            }
        }

        // 3. Synchronize lights
        for (const auto& lightState : compiled.lights) {
            const std::string lightKey = scopePrefix + "light_" + std::to_string(lightState.sample.id);
            auto& entry = lightNodes_[lightKey];

            const Color lightColor = toThreeppColor(lightState.sample.color);
            const float intensity = lightState.sample.intensity;

            if (!entry.light || entry.kind != lightState.sample.kind) {
                if (entry.light) scene_->remove(*entry.light);
                if (entry.target) scene_->remove(*entry.target);
                entry = LightEntry{};
                entry.kind = lightState.sample.kind;

                switch (lightState.sample.kind) {
                    case chrononmotion::motion::LightKind::Ambient: {
                        entry.light = AmbientLight::create(lightColor, intensity);
                        break;
                    }
                    case chrononmotion::motion::LightKind::Directional: {
                        auto dir = DirectionalLight::create(lightColor, intensity);
                        entry.target = Object3D::create();
                        dir->setTarget(*entry.target);
                        scene_->add(entry.target);
                        entry.light = std::move(dir);
                        break;
                    }
                    case chrononmotion::motion::LightKind::Point: {
                        entry.light = PointLight::create(
                                lightColor, intensity, lightState.sample.range, lightState.sample.decay);
                        break;
                    }
                    case chrononmotion::motion::LightKind::Spot: {
                        auto spot = SpotLight::create(
                                lightColor,
                                intensity,
                                lightState.sample.range,
                                lightState.sample.angle,
                                lightState.sample.penumbra,
                                lightState.sample.decay);
                        entry.target = Object3D::create();
                        spot->setTarget(*entry.target);
                        scene_->add(entry.target);
                        entry.light = std::move(spot);
                        break;
                    }
                }
                if (entry.light) {
                    scene_->add(entry.light);
                }
            }

            if (!entry.light) continue;

            entry.light->color = lightColor;
            entry.light->intensity = intensity;
            entry.light->visible = rootVisible && lightState.visible && (intensity > 0.f);
            entry.light->castShadow = lightState.sample.castsShadow;
            if (auto* shadowedLight = dynamic_cast<LightWithShadow*>(entry.light.get());
                shadowedLight && shadowedLight->shadow) {
                shadowedLight->shadow->bias = lightState.sample.shadowBias;
                shadowedLight->shadow->normalBias = lightState.sample.shadowNormalBias;
                shadowedLight->shadow->radius = lightState.sample.shadowRadius;
            }

            Vector3 worldPos = toThreeppVector3(lightState.position);
            worldPos.applyMatrix4(rootTransform);
            entry.light->position.copy(worldPos);
            entry.light->matrixAutoUpdate = true;

            if (entry.target) {
                Vector3 worldDir = toThreeppVector3(lightState.direction);
                worldDir.transformDirection(rootTransform);
                entry.target->position.copy(worldPos).add(worldDir);
                entry.target->matrixAutoUpdate = true;
            }

            if (auto* pt = dynamic_cast<PointLight*>(entry.light.get())) {
                pt->distance = lightState.sample.range;
                pt->decay = lightState.sample.decay;
            } else if (auto* sp = dynamic_cast<SpotLight*>(entry.light.get())) {
                sp->distance = lightState.sample.range;
                sp->angle = lightState.sample.angle;
                sp->penumbra = lightState.sample.penumbra;
                sp->decay = lightState.sample.decay;
            }
        }

        // 4. Synchronize nested precomps
        for (const auto& precomp : compiled.precomps) {
            if (!precomp.scene) continue;
            Matrix4 precompWorld = toThreeppMatrix4(precomp.world);
            Matrix4 nextRoot;
            nextRoot.multiplyMatrices(rootTransform, precompWorld);
            const float nextOpacity = std::clamp(rootOpacity * precomp.opacity, 0.f, 1.f);
            const bool nextVisible = rootVisible && precomp.visible && (nextOpacity > 0.f);
            const std::string nextScope = scopePrefix + "p" + std::to_string(precomp.layerId) + "_";
            syncSceneGraph(*precomp.scene, nextRoot, nextOpacity, nextVisible, nextScope);
        }
    }

}// namespace threepp::motion
