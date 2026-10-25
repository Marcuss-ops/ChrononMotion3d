#include "chrononmotion/motion/MotionScene.hpp"
#include "MotionScene_internal.hpp"

#include "chrononmotion/motion/CompiledScene.hpp"
#include "chrononmotion/motion/SceneExport.hpp"
#include "chrononmotion/motion/Presets.hpp"
#include "chrononmotion/assets/GltfReader.hpp"
#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "JsonWriter.hpp"

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace chrononmotion::motion {

    namespace scene_detail {

        /// The value type a channel resolves to. A link only makes sense between
        /// channels of the same type — following position with an opacity is an
        /// authoring error, not a cast.
        [[nodiscard]] LinkType linkTypeOf(TrackChannel channel) {

            switch (channel) {
                case TrackChannel::Position:
                case TrackChannel::Anchor:
                case TrackChannel::Scale:
                case TrackChannel::Rotation: return LinkType::Vector3Type;
                case TrackChannel::Orientation: return LinkType::QuaternionType;
                case TrackChannel::Opacity:
                case TrackChannel::TimeRemap: return LinkType::ScalarType;
            }
            return LinkType::ScalarType;
        }

        [[nodiscard]] std::size_t saturatedAdd(std::size_t left, std::size_t right) {
            const auto maximum = std::numeric_limits<std::size_t>::max();
            return right > maximum - left ? maximum : left + right;
        }

        [[nodiscard]] std::size_t saturatedMultiply(std::size_t left, std::size_t right) {
            const auto maximum = std::numeric_limits<std::size_t>::max();
            return left != 0 && right > maximum / left ? maximum : left * right;
        }

        template<class... Tracks>
        [[nodiscard]] std::size_t trackKeyCount(const Tracks&... tracks) {
            std::size_t total = 0;
            ((total = saturatedAdd(total, tracks.size())), ...);
            return total;
        }

        [[nodiscard]] std::size_t keyframeCount(const Layer& layer) {
            std::size_t total = trackKeyCount(layer.tracks.position, layer.tracks.anchor, layer.tracks.scale,
                                              layer.tracks.rotation, layer.tracks.orientation, layer.tracks.opacity,
                                              layer.timeRemapTrack);
            for (const auto& constraint : layer.constraints)
                if (constraint.amountTrack) total = saturatedAdd(total, constraint.amountTrack->size());
            return total;
        }

        [[nodiscard]] std::size_t keyframeCount(const Light& light) {
            std::size_t total = trackKeyCount(light.intensityTrack, light.colorTrack, light.positionTrack,
                light.rotationTrack, light.directionTrack, light.shadowBiasTrack,
                light.shadowNormalBiasTrack, light.shadowRadiusTrack);
            if (const auto* point = dynamic_cast<const PointLight*>(&light))
                total = saturatedAdd(total, trackKeyCount(point->rangeTrack, point->decayTrack));
            if (const auto* spot = dynamic_cast<const SpotLight*>(&light))
                total = saturatedAdd(total, trackKeyCount(spot->rangeTrack, spot->angleTrack,
                                                         spot->penumbraTrack, spot->decayTrack));
            return total;
        }

        [[nodiscard]] std::size_t geometryVertexCount(const BufferGeometry& geometry) {
            const auto* positions = geometry.getAttribute<FloatBufferAttribute>("position");
            return positions ? positions->count() : 0;
        }

        [[nodiscard]] std::size_t primitiveSegmentCount(const BufferGeometry& geometry) {
            std::size_t first = 0;
            std::size_t second = 0;
            if (const auto* plane = dynamic_cast<const PlaneGeometry*>(&geometry)) {
                first = plane->widthSegments;
                second = plane->heightSegments;
            } else if (const auto* sphere = dynamic_cast<const SphereGeometry*>(&geometry)) {
                first = std::max(3u, sphere->parameters.widthSegments);
                second = std::max(2u, sphere->parameters.heightSegments);
            } else if (const auto* box = dynamic_cast<const BoxGeometry*>(&geometry)) {
                const auto& p = box->parameters;
                const std::size_t xy = saturatedMultiply(p.widthSegments, p.heightSegments);
                const std::size_t xz = saturatedMultiply(p.widthSegments, p.depthSegments);
                const std::size_t yz = saturatedMultiply(p.heightSegments, p.depthSegments);
                return saturatedAdd(saturatedAdd(xy, xy),
                                    saturatedAdd(saturatedAdd(xz, xz), saturatedAdd(yz, yz)));
            } else {
                return 0;
            }
            if (first == 0 || second == 0) return std::numeric_limits<std::size_t>::max();
            if (second > std::numeric_limits<std::size_t>::max() / first)
                return std::numeric_limits<std::size_t>::max();
            return first * second;
        }

} // namespace scene_detail

using scene_detail::keyframeCount;
using scene_detail::saturatedAdd;

    MotionScene::MotionScene(std::string name, float fps)
        : name_(std::move(name)), fps_(fps) {

        if (!std::isfinite(fps) || fps <= 0.f)
            throw std::invalid_argument("MotionScene: fps must be finite and positive");
    }

    MotionScene::MotionScene(MotionScene&& other)
        : name_(std::move(other.name_)),
          fps_(other.fps_),
          policy_(std::move(other.policy_)),
          sealed_(other.sealed_),
          auditLog_(std::move(other.auditLog_)),
          layers_(std::move(other.layers_)),
          index_(std::move(other.index_)),
          states_(std::move(other.states_)),
          lights_(std::move(other.lights_)),
          lightIndex_(std::move(other.lightIndex_)),
          lightStates_(std::move(other.lightStates_)),
          lastFrame_(other.lastFrame_),
          lastPts_(other.lastPts_),
          lastTime_(other.lastTime_),
          hasEvaluated_(other.hasEvaluated_),
          order_(std::move(other.order_)),
          orderParents_(std::move(other.orderParents_)),
          orderParentIds_(std::move(other.orderParentIds_)),
          orderSwitchParentIds_(std::move(other.orderSwitchParentIds_)),
          orderDirty_(other.orderDirty_),
          orderValid_(other.orderValid_) {
        for (auto& layer : layers_) {
            layer.authoringOwner = this;
            attachTrackGuards(layer);
        }
        for (auto& light : lights_) attachLightTrackGuards(*light);
    }

    void MotionScene::ensureMutable(const char* action) const {
        if (sealed_) {
            audit(action, "scene", false, "scene is sealed");
            throw std::logic_error(std::string("MotionScene: cannot ") + action + " after seal()");
        }
    }

    void MotionScene::audit(std::string action, std::string subject, bool accepted, std::string reason) const {
        auditLog_.push_back(AuthoringAuditEntry{std::move(action), std::move(subject), accepted, std::move(reason)});
    }

    GltfReadPolicy MotionScene::gltfReadPolicy() const {
        GltfReadPolicy result;
        result.enabled = true;
        result.allowStaticGltfImport = policy_.allowStaticGltfImport;
        result.allowMotionGltfImport = policy_.allowMotionGltfImport;
        result.maxVertices = policy_.maxGeometryVertices;
        result.audit = [this](bool motion, std::string_view format) {
            try {
                const_cast<MotionScene*>(this)->recordGltfImport(motion, std::string(format));
            } catch (const std::logic_error& error) {
                throw std::invalid_argument(error.what());
            }
        };
        return result;
    }

    MotionScene& MotionScene::setAuthoringPolicy(AuthoringPolicy policy) {
        ensureMutable("change authoring policy");
        for (const auto& name : policy.allowedPresets) {
            if (!presets::known(name)) {
                audit("setPolicy", name, false, "allowlist contains an unknown preset");
                throw std::invalid_argument("MotionScene::setAuthoringPolicy: unknown preset '" + name + "'");
            }
        }
        policy_ = std::move(policy);
        audit("setPolicy", name_, true);
        return *this;
    }

    void MotionScene::authorizePreset(const std::string& presetName) {
        authorizePresetName(presetName);
    }

    void MotionScene::authorizeCustomPreset(const std::string& presetName) {
        ensureMutable("create animation preset");
        if (!policy_.allowAnimations || !policy_.allowNewCustomPresets) {
            audit("customPreset", presetName, false, "custom presets are disabled by authoring policy");
            throw std::logic_error("MotionScene: custom presets are disabled by authoring policy");
        }
        audit("customPreset", presetName, true);
    }

    void MotionScene::authorizePresetName(const std::string& presetName) {
        if (!presets::known(presetName)) {
            audit("preset", presetName, false, "unknown built-in preset");
            throw std::invalid_argument("MotionScene: unknown built-in preset: " + presetName);
        }
        if (sealed_ || !policy_.allowAnimations) {
            audit("preset", presetName, false, sealed_ ? "scene is sealed" : "animations are disabled");
            throw std::logic_error("MotionScene: animation preset is not allowed: " + presetName);
        }
        if (!policy_.allowedPresets.empty() &&
            std::find(policy_.allowedPresets.begin(), policy_.allowedPresets.end(), presetName) == policy_.allowedPresets.end()) {
            audit("preset", presetName, false, "preset is not in the allowlist");
            throw std::invalid_argument("MotionScene: preset is not allowlisted: " + presetName);
        }
        audit("preset", presetName, true);
    }

    void MotionScene::authorizePresetFor(const Layer& layer, const std::string& presetName) {
        if (layer.authoringOwner) {
            layer.authoringOwner->authorizePresetName(presetName);
            return;
        }
        const Object3D* node = layer.parent;
        while (node) {
            if (const auto* scene = dynamic_cast<const MotionScene*>(node)) {
                const_cast<MotionScene*>(scene)->authorizePresetName(presetName);
                return;
            }
            node = node->parent;
        }
        if (!presets::known(presetName))
            throw std::invalid_argument("MotionScene: unknown built-in preset: " + presetName);
    }

    void MotionScene::seal() {
        if (sealed_) return;
        const auto problems = validate();
        if (!problems.empty()) {
            audit("seal", name_, false, problems.front());
            throw std::invalid_argument("MotionScene::seal: scene validation failed: " + problems.front());
        }
        sealed_ = true;
        audit("seal", name_, true);
    }

    void MotionScene::recordGltfImport(bool motion, std::string sourceName) {
        ensureMutable("import glTF");
        const bool permitted = motion ? policy_.allowMotionGltfImport : policy_.allowStaticGltfImport;
        if (!permitted) {
            audit("gltfImport", sourceName, false, motion ? "motion glTF import is disabled" : "static glTF import is disabled");
            throw std::logic_error("MotionScene: glTF import is disabled by authoring policy");
        }
        audit("gltfImport", sourceName, true, motion ? "motion" : "static");
    }

    std::string MotionScene::auditJson(int indentSpaces) const {
        if (indentSpaces < 0) throw std::invalid_argument("MotionScene::auditJson: indent must be non-negative");
        detail::Writer writer(indentSpaces);
        writer.beginArray();
        for (const auto& entry : auditLog_) {
            writer.item();
            writer.beginObject();
            writer.key("action"); writer.text(entry.action);
            writer.key("subject"); writer.text(entry.subject);
            writer.key("accepted"); writer.boolean(entry.accepted);
            writer.key("reason"); writer.text(entry.reason);
            writer.endObject();
        }
        writer.endArray();
        return writer.take();
    }



    void MotionScene::attachTrackGuards(Layer& layer) {
        Layer* const guardedLayer = &layer;
        const auto guard = [this, guardedLayer](std::size_t oldCount, std::size_t newCount) {
            ensureMutable("modify animation track");
            if (!policy_.allowAnimations) {
                audit("keyframe", guardedLayer->name, false, "animations are disabled");
                throw std::logic_error("MotionScene: animations are disabled by authoring policy");
            }
            if (policy_.maxKeyframes == std::numeric_limits<std::size_t>::max()) {
                audit("keyframe", guardedLayer->name, true);
                return;
            }
            std::size_t total = 0;
            for (const auto& candidate : layers_) {
                total = saturatedAdd(total, keyframeCount(candidate));
            }
            for (const auto& candidate : lights_) total = saturatedAdd(total, keyframeCount(*candidate));
            const std::size_t remaining = policy_.maxKeyframes - std::min(total, policy_.maxKeyframes);
            if (newCount > oldCount && newCount - oldCount > remaining) {
                audit("keyframe", guardedLayer->name, false, "keyframe budget exceeded");
                throw std::length_error("MotionScene: keyframe budget exceeded");
            }
            audit("keyframe", guardedLayer->name, true);
        };
        layer.tracks.position.setMutationGuard(guard);
        layer.tracks.anchor.setMutationGuard(guard);
        layer.tracks.scale.setMutationGuard(guard);
        layer.tracks.rotation.setMutationGuard(guard);
        layer.tracks.orientation.setMutationGuard(guard);
        layer.tracks.opacity.setMutationGuard(guard);
        layer.timeRemapTrack.setMutationGuard(guard);
        for (const auto& constraint : layer.constraints) {
            if (constraint.amountTrack) constraint.amountTrack->setMutationGuard(guard);
        }
        const auto readOnly = [this]() { ensureMutable("modify animation track"); };
        for (const auto& constraint : layer.constraints) {
            if (constraint.amountTrack) constraint.amountTrack->setReadOnlyGuard(readOnly);
        }
        layer.tracks.position.setReadOnlyGuard(readOnly);
        layer.tracks.anchor.setReadOnlyGuard(readOnly);
        layer.tracks.scale.setReadOnlyGuard(readOnly);
        layer.tracks.rotation.setReadOnlyGuard(readOnly);
        layer.tracks.orientation.setReadOnlyGuard(readOnly);
        layer.tracks.opacity.setReadOnlyGuard(readOnly);
        layer.timeRemapTrack.setReadOnlyGuard(readOnly);
    }

    void MotionScene::attachLightTrackGuards(Light& light) {
        const auto guard = [this, guarded = &light](std::size_t oldCount, std::size_t newCount) {
            ensureMutable("modify light animation track");
            if (!policy_.allowAnimations) {
                audit("keyframe", guarded->name, false, "animations are disabled");
                throw std::logic_error("MotionScene: animations are disabled by authoring policy");
            }
            if (policy_.maxKeyframes == std::numeric_limits<std::size_t>::max()) {
                audit("keyframe", guarded->name, true);
                return;
            }
            std::size_t total = 0;
            for (const auto& layer : layers_) total = saturatedAdd(total, keyframeCount(layer));
            for (const auto& candidate : lights_) total = saturatedAdd(total, keyframeCount(*candidate));
            const std::size_t remaining = policy_.maxKeyframes - std::min(total, policy_.maxKeyframes);
            if (newCount > oldCount && newCount - oldCount > remaining) {
                audit("keyframe", guarded->name, false, "keyframe budget exceeded");
                throw std::length_error("MotionScene: keyframe budget exceeded");
            }
            audit("keyframe", guarded->name, true);
        };
        const auto readOnly = [this]() { ensureMutable("modify light animation track"); };
        const auto bind = [&](auto& track) {
            track.setMutationGuard(guard);
            track.setReadOnlyGuard(readOnly);
        };
        bind(light.intensityTrack);
        bind(light.colorTrack);
        bind(light.positionTrack);
        bind(light.rotationTrack);
        bind(light.directionTrack);
        bind(light.shadowBiasTrack);
        bind(light.shadowNormalBiasTrack);
        bind(light.shadowRadiusTrack);
        if (auto* point = dynamic_cast<PointLight*>(&light)) {
            bind(point->rangeTrack);
            bind(point->decayTrack);
        }
        if (auto* spot = dynamic_cast<SpotLight*>(&light)) {
            bind(spot->rangeTrack);
            bind(spot->angleTrack);
            bind(spot->penumbraTrack);
            bind(spot->decayTrack);
        }
    }

}// namespace chrononmotion::motion
