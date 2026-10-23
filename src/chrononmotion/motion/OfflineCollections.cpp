#include "chrononmotion/motion/OfflineBaking.hpp"
#include "OfflineBakingInternal.hpp"

#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace chrononmotion::motion {
using detail::kMaximumBakedFrames;
using detail::kMaximumBakedPathCommands;
using detail::transformed;
using detail::validateBakedPath;

std::vector<const Layer*> selectCollection(const MotionScene& scene,
                                           const CollectionSelector& selector) {
    constexpr std::size_t kMaximumSelectorIds = 100000;
    if (selector.includeIds.size() > kMaximumSelectorIds ||
        selector.excludeIds.size() > kMaximumSelectorIds || selector.namePrefix.size() > 4096 ||
        (selector.visibleAtFrame && *selector.visibleAtFrame < 0) ||
        static_cast<unsigned>(selector.layerKind) >
                static_cast<unsigned>(CollectionSelector::LayerKind::Mesh) ||
        (selector.contentKind &&
         static_cast<unsigned>(*selector.contentKind) > static_cast<unsigned>(ContentKind::Mesh))) {
        throw std::invalid_argument("selectCollection: invalid selector criteria or query budget");
    }

    const auto makeIdSet = [](const std::vector<Layer::Id>& ids, const char* name) {
        std::unordered_set<Layer::Id> result;
        result.reserve(ids.size());
        for (const Layer::Id id : ids) {
            if (id == Layer::kRoot || !result.insert(id).second)
                throw std::invalid_argument(std::string("selectCollection: ") + name +
                                            " IDs must be positive and unique");
        }
        return result;
    };
    const auto included = makeIdSet(selector.includeIds, "included");
    const auto excluded = makeIdSet(selector.excludeIds, "excluded");

    std::vector<const Layer*> result;
    result.reserve(scene.layerCount());
    for (const Layer& layer : scene.layers()) {
        if (!included.empty() && !included.contains(layer.id)) continue;
        if (excluded.contains(layer.id)) continue;
        if (selector.parentId && layer.parentId != *selector.parentId) continue;
        if (!selector.namePrefix.empty() &&
            (layer.name.size() < selector.namePrefix.size() ||
             layer.name.compare(0, selector.namePrefix.size(), selector.namePrefix) != 0)) continue;
        if (selector.contentKind && layer.content.kind != *selector.contentKind) continue;
        switch (selector.layerKind) {
            case CollectionSelector::LayerKind::Any: break;
            case CollectionSelector::LayerKind::Null:
                if (!layer.isNull()) continue;
                break;
            case CollectionSelector::LayerKind::Content:
                if (!layer.isContent() || layer.isMesh()) continue;
                break;
            case CollectionSelector::LayerKind::Mesh:
                if (!layer.isMesh()) continue;
                break;
        }
        if (selector.is3D && layer.is3D != *selector.is3D) continue;
        if (selector.visibleAtFrame && !layer.visibleAt(*selector.visibleAtFrame)) continue;
        result.push_back(&layer);
    }
    return result;
}

std::vector<Layer*> instantiateCloner(MotionScene& scene, const Layer& prototype,
                                     const ClonerSpec& spec, std::string_view idPrefix) {
    if (spec.count == 0 || spec.count > 10000 || idPrefix.empty() || idPrefix.size() > 4096 ||
        !math::isFinite(spec.offsetStep) || !math::isFinite(spec.scaleStep) || !std::isfinite(spec.rotationStepRad) ||
        !std::isfinite(spec.spiralRadiusStep) || spec.spiralRadiusStep < 0.f ||
        !std::isfinite(spec.spiralAngleStepRad) || !std::isfinite(spec.timeOffsetSeconds) ||
        spec.timeOffsetSeconds < 0.f ||
        !std::isfinite(spec.count > 1 ? spec.timeOffsetSeconds * static_cast<float>(spec.count - 1) : 0.f) ||
        (spec.pattern == ClonerSpec::Pattern::Grid &&
         (spec.gridCols <= 0 || static_cast<unsigned>(spec.gridCols) > spec.count)) ||
        static_cast<unsigned>(spec.pattern) > static_cast<unsigned>(ClonerSpec::Pattern::Spiral))
        throw std::invalid_argument("instantiateCloner: invalid count, pattern, or transform settings");
    if (scene.layerCount() + spec.count > 100000)
        throw std::invalid_argument("instantiateCloner: scene clone budget exceeded");
    const std::size_t requestedKeys = prototype.tracks.position.size() + prototype.tracks.anchor.size() +
        prototype.tracks.scale.size() + prototype.tracks.rotation.size() +
        prototype.tracks.orientation.size() + prototype.tracks.opacity.size();
    if (requestedKeys != 0 && requestedKeys > kMaximumBakedFrames / spec.count)
        throw std::invalid_argument("instantiateCloner: requested clone tracks exceed the offline key budget");

    const auto shiftTimes = [&](auto& track, float shift) {
        for (auto& key : track.keys()) key.time += shift;
    };
    const auto scaleValue = [&](Vector3& value, const Vector3& factor) {
        value.x *= factor.x; value.y *= factor.y; value.z *= factor.z;
    };
    std::vector<Layer> pending;
    pending.reserve(spec.count);
    std::vector<Layer*> result;
    result.reserve(spec.count);
    Layer::Id nextId = 1;
    for (unsigned index = 0; index < spec.count; ++index) {
        const float i = static_cast<float>(index);
        Vector3 offset{};
        float angle = spec.rotationStepRad * i;
        switch (spec.pattern) {
            case ClonerSpec::Pattern::Linear:
                offset = spec.offsetStep * i;
                break;
            case ClonerSpec::Pattern::Radial:
                angle = spec.rotationStepRad * i;
                offset.set(spec.offsetStep.x * std::cos(angle), spec.offsetStep.x * std::sin(angle),
                           spec.offsetStep.z * i);
                break;
            case ClonerSpec::Pattern::Grid: {
                const unsigned cols = static_cast<unsigned>(spec.gridCols);
                offset.set(static_cast<float>(index % cols) * spec.offsetStep.x,
                           static_cast<float>(index / cols) * spec.offsetStep.y,
                           static_cast<float>(index / cols) * spec.offsetStep.z);
                angle = spec.rotationStepRad * i;
                break;
            }
            case ClonerSpec::Pattern::Spiral: {
                const float spiralAngle = spec.spiralAngleStepRad * i;
                const float radius = spec.spiralRadiusStep * i;
                offset.set(radius * std::cos(spiralAngle), radius * std::sin(spiralAngle),
                           spec.offsetStep.z * i);
                angle = spiralAngle;
                break;
            }
        }
        const Vector3 scaleFactor(std::pow(spec.scaleStep.x, i), std::pow(spec.scaleStep.y, i),
                                  std::pow(spec.scaleStep.z, i));
        if (!math::isFinite(offset) || !math::isFinite(scaleFactor) || !std::isfinite(angle))
            throw std::invalid_argument("instantiateCloner: clone transform overflowed");
        Layer clone(prototype);
        for (auto& constraint : clone.constraints) {
            if (constraint.amountTrack)
                constraint.amountTrack = std::make_shared<Track<float>>(*constraint.amountTrack);
        }
        while (scene.hasLayer(nextId)) {
            if (nextId == std::numeric_limits<Layer::Id>::max())
                throw std::invalid_argument("instantiateCloner: no positive layer IDs remain");
            ++nextId;
        }
        clone.id = nextId++;
        clone.name = std::string(idPrefix) + std::to_string(index);
        clone.transform.position.add(offset);
        scaleValue(clone.transform.scale, scaleFactor);
        clone.transform.rotation.z += angle;
        Quaternion rotation;
        rotation.setFromAxisAngle(Vector3(0.f, 0.f, 1.f), angle);
        clone.transform.orientation.multiplyQuaternions(rotation, clone.transform.orientation);
        for (auto& key : clone.tracks.position.keys()) key.value.add(offset);
        for (auto& key : clone.tracks.scale.keys()) scaleValue(key.value, scaleFactor);
        for (auto& key : clone.tracks.rotation.keys()) key.value.z += angle;
        for (auto& key : clone.tracks.orientation.keys())
            key.value.multiplyQuaternions(rotation, key.value);
        const float timeShift = spec.timeOffsetSeconds * i;
        shiftTimes(clone.tracks.position, timeShift);
        shiftTimes(clone.tracks.anchor, timeShift);
        shiftTimes(clone.tracks.scale, timeShift);
        shiftTimes(clone.tracks.rotation, timeShift);
        shiftTimes(clone.tracks.orientation, timeShift);
        shiftTimes(clone.tracks.opacity, timeShift);
        const auto finiteTrackTimes = [](const auto& track) {
            for (const auto& key : track.keys()) if (!std::isfinite(key.time)) return false;
            return true;
        };
        const auto finiteVectorTrack = [&](const Track<Vector3>& track) {
            for (const auto& key : track.keys())
                if (!math::isFinite(key.value)) return false;
            return finiteTrackTimes(track);
        };
        const auto finiteOrientationTrack = [&](const Track<Quaternion>& track) {
            for (const auto& key : track.keys())
                if (!std::isfinite(static_cast<float>(key.value.x)) ||
                    !std::isfinite(static_cast<float>(key.value.y)) ||
                    !std::isfinite(static_cast<float>(key.value.z)) ||
                    !std::isfinite(static_cast<float>(key.value.w))) return false;
            return finiteTrackTimes(track);
        };
        const auto finiteOpacityTrack = [&](const Track<float>& track) {
            for (const auto& key : track.keys())
                if (!std::isfinite(key.value)) return false;
            return finiteTrackTimes(track);
        };
        if (!math::isFinite(clone.transform.position) || !math::isFinite(clone.transform.anchor) ||
            !math::isFinite(clone.transform.scale) || !math::isFinite(clone.transform.rotation) ||
            !std::isfinite(clone.transform.opacity) ||
            !std::isfinite(static_cast<float>(clone.transform.orientation.x)) ||
            !std::isfinite(static_cast<float>(clone.transform.orientation.y)) ||
            !std::isfinite(static_cast<float>(clone.transform.orientation.z)) ||
            !std::isfinite(static_cast<float>(clone.transform.orientation.w)) ||
            !finiteVectorTrack(clone.tracks.position) || !finiteVectorTrack(clone.tracks.anchor) ||
            !finiteVectorTrack(clone.tracks.scale) || !finiteVectorTrack(clone.tracks.rotation) ||
            !finiteOrientationTrack(clone.tracks.orientation) || !finiteOpacityTrack(clone.tracks.opacity))
            throw std::invalid_argument("instantiateCloner: transformed clone data exceeds finite range");
        pending.push_back(std::move(clone));
    }
    for (Layer& clone : pending) result.push_back(&scene.addLayer(std::move(clone)));
    return result;
}

std::vector<BakedPath> expandPathRepeater(const BakedPath& path,
                                           const RepeaterSpec& repeater,
                                           std::uint32_t maximumCopies) {
    validateBakedPath(path, "expandPathRepeater");
    if (path.empty() || path.size() > 65536 || path.front().type != BakedPathCommandType::MoveTo ||
        repeater.copies == 0 || repeater.copies > maximumCopies || maximumCopies > 100000 ||
        repeater.copies > kMaximumBakedPathCommands / path.size() ||
        !math::isFinite(repeater.position) || !math::isFinite(repeater.scale) || !math::isFinite(repeater.anchor) ||
        !std::isfinite(repeater.rotationRadians))
        throw std::invalid_argument("expandPathRepeater: invalid path or repeater settings");
    std::vector<BakedPath> result;
    result.reserve(repeater.copies);
    for (std::uint32_t copy = 0; copy < repeater.copies; ++copy) {
        const float power = static_cast<float>(copy);
        const Vector2 scale(std::pow(repeater.scale.x, power), std::pow(repeater.scale.y, power));
        if (!math::isFinite(scale)) throw std::invalid_argument("expandPathRepeater: scale overflow");
        const float angle = repeater.rotationRadians * power;
        const Vector2 translation = repeater.position * power;
        BakedPath transformedPath = path;
        for (auto& command : transformedPath) {
            if (command.type == BakedPathCommandType::Close) continue;
            command.point = transformed(command.point, repeater.anchor, scale, angle, translation);
            if (command.type == BakedPathCommandType::CubicTo) {
                command.control1 = transformed(command.control1, repeater.anchor, scale, angle, translation);
                command.control2 = transformed(command.control2, repeater.anchor, scale, angle, translation);
            }
            if (!math::isFinite(command.point) ||
                (command.type == BakedPathCommandType::CubicTo &&
                 (!math::isFinite(command.control1) || !math::isFinite(command.control2))))
                throw std::invalid_argument("expandPathRepeater: transformed command overflows finite coordinates");
        }
        result.push_back(std::move(transformedPath));
    }
    return result;
}

} // namespace chrononmotion::motion
