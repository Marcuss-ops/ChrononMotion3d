#include "chrononmotion/motion/OfflineBaking.hpp"
#include "OfflineBakingInternal.hpp"

#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace chrononmotion::motion {

FlowResult solveFlowLayout(const FlowSpec& spec, std::span<const FlowItem> items,
                           const Vector2& origin) {
    if (static_cast<unsigned>(spec.axis) > static_cast<unsigned>(FlowAxis::Column) ||
        static_cast<unsigned>(spec.mainAlignment) > static_cast<unsigned>(FlowMainAlignment::SpaceBetween) ||
        static_cast<unsigned>(spec.crossAlignment) > static_cast<unsigned>(FlowCrossAlignment::Stretch))
        throw std::invalid_argument("solveFlowLayout: invalid axis or alignment");
    const float padding[] = {spec.padding.top, spec.padding.right, spec.padding.bottom, spec.padding.left};
    if (!math::isFinite(origin) || !math::isFinite(spec.containerSize) || !std::isfinite(spec.gap) || spec.gap < 0.f)
        throw std::invalid_argument("solveFlowLayout: origin, container and gap must be finite; gap non-negative");
    for (float value : padding)
        if (!std::isfinite(value) || value < 0.f) throw std::invalid_argument("solveFlowLayout: padding must be finite and non-negative");
    std::unordered_set<std::uint32_t> ids;
    if (items.size() > 65536)
        throw std::invalid_argument("solveFlowLayout: item count exceeds 65536");
    float naturalMain = 0.f, naturalCross = 0.f;
    for (const auto& item : items) {
        if (!ids.insert(item.id).second || !math::isFinite(item.measuredSize) || item.measuredSize.x < 0.f || item.measuredSize.y < 0.f ||
            !math::isFinite(item.anchor) || item.anchor.x < 0.f || item.anchor.x > 1.f || item.anchor.y < 0.f || item.anchor.y > 1.f)
            throw std::invalid_argument("solveFlowLayout: duplicate ids or invalid measured item");
        const float main = spec.axis == FlowAxis::Row ? item.measuredSize.x : item.measuredSize.y;
        const float cross = spec.axis == FlowAxis::Row ? item.measuredSize.y : item.measuredSize.x;
        naturalMain += main;
        naturalCross = std::max(naturalCross, cross);
        if (!std::isfinite(naturalMain) || !std::isfinite(naturalCross))
            throw std::invalid_argument("solveFlowLayout: intrinsic item extent overflowed");
    }
    const float mainLeading = spec.axis == FlowAxis::Row ? spec.padding.left : spec.padding.top;
    const float mainTrailing = spec.axis == FlowAxis::Row ? spec.padding.right : spec.padding.bottom;
    const float crossLeading = spec.axis == FlowAxis::Row ? spec.padding.top : spec.padding.left;
    const float crossTrailing = spec.axis == FlowAxis::Row ? spec.padding.bottom : spec.padding.right;
    const float explicitMain = spec.axis == FlowAxis::Row ? spec.containerSize.x : spec.containerSize.y;
    const float explicitCross = spec.axis == FlowAxis::Row ? spec.containerSize.y : spec.containerSize.x;
    if (explicitMain < 0.f || explicitCross < 0.f)
        throw std::invalid_argument("solveFlowLayout: container dimensions must be non-negative");
    const float mainExtent = explicitMain > 0.f ? explicitMain : mainLeading + naturalMain + mainTrailing +
        (items.empty() ? 0.f : spec.gap * static_cast<float>(items.size() - 1));
    const float crossExtent = explicitCross > 0.f ? explicitCross : crossLeading + naturalCross + crossTrailing;
    if (!std::isfinite(mainExtent) || !std::isfinite(crossExtent))
        throw std::invalid_argument("solveFlowLayout: container extent overflowed");
    const float innerMain = std::max(0.f, mainExtent - mainLeading - mainTrailing);
    const float innerCross = std::max(0.f, crossExtent - crossLeading - crossTrailing);
    const float gaps = items.empty() ? 0.f : spec.gap * static_cast<float>(items.size() - 1);
    const float occupied = naturalMain + gaps;
    if (!std::isfinite(innerMain) || !std::isfinite(innerCross) ||
        !std::isfinite(gaps) || !std::isfinite(occupied))
        throw std::invalid_argument("solveFlowLayout: resolved inner extent or item spacing overflowed");
    float cursor = mainLeading;
    float effectiveGap = spec.gap;
    if (spec.mainAlignment == FlowMainAlignment::Center) cursor += (innerMain - occupied) * 0.5f;
    else if (spec.mainAlignment == FlowMainAlignment::End) cursor += innerMain - occupied;
    else if (spec.mainAlignment == FlowMainAlignment::SpaceBetween && items.size() > 1)
        effectiveGap += std::max(0.f, innerMain - naturalMain - gaps) / static_cast<float>(items.size() - 1);
    FlowResult result;
    result.items.reserve(items.size());
    for (const auto& item : items) {
        const float width = item.measuredSize.x, height = item.measuredSize.y;
        const float itemMain = spec.axis == FlowAxis::Row ? width : height;
        float itemCross = spec.axis == FlowAxis::Row ? height : width;
        float crossPosition = crossLeading;
        if (spec.crossAlignment == FlowCrossAlignment::Center) crossPosition += (innerCross - itemCross) * 0.5f;
        else if (spec.crossAlignment == FlowCrossAlignment::End) crossPosition += innerCross - itemCross;
        else if (spec.crossAlignment == FlowCrossAlignment::Stretch) itemCross = innerCross;
        const float x = origin.x + (spec.axis == FlowAxis::Row ? cursor : crossPosition);
        const float y = origin.y + (spec.axis == FlowAxis::Row ? crossPosition : cursor);
        if (spec.crossAlignment == FlowCrossAlignment::Stretch && itemCross < 0.f)
            throw std::invalid_argument("solveFlowLayout: invalid stretched cross extent");
        const float resolvedWidth = spec.axis == FlowAxis::Column && spec.crossAlignment == FlowCrossAlignment::Stretch ? itemCross : width;
        const float resolvedHeight = spec.axis == FlowAxis::Row && spec.crossAlignment == FlowCrossAlignment::Stretch ? itemCross : height;
        FlowItemResult itemResult;
        itemResult.id = item.id;
        itemResult.anchor = item.anchor;
        itemResult.size = Vector2(resolvedWidth, resolvedHeight);
        itemResult.position = Vector2(x + item.anchor.x * resolvedWidth, y + item.anchor.y * resolvedHeight);
        itemResult.bounds.set(Vector2(x, y), Vector2(x + resolvedWidth, y + resolvedHeight));
        if (!math::isFinite(itemResult.position) || !math::isFinite(itemResult.size) || !math::isFinite(Vector2(x + resolvedWidth, y + resolvedHeight)))
            throw std::invalid_argument("solveFlowLayout: resolved item overflows finite coordinates");
        result.items.push_back(itemResult);
        cursor += itemMain + effectiveGap;
    }
    const Vector2 maximum(origin.x + (spec.axis == FlowAxis::Row ? mainExtent : crossExtent),
                          origin.y + (spec.axis == FlowAxis::Row ? crossExtent : mainExtent));
    if (!math::isFinite(maximum)) throw std::invalid_argument("solveFlowLayout: resolved container bounds overflow");
    result.bounds.set(origin, maximum);
    return result;
}

FlowResult refreshFlowLayout(const FlowSpec& spec, std::span<const FlowItem> items,
                             MotionScene& scene, const Vector2& origin) {
    FlowResult result = solveFlowLayout(spec, items, origin);
    std::vector<Layer*> layers;
    layers.reserve(result.items.size());
    std::unordered_map<std::uint32_t, const FlowItem*> sources;
    sources.reserve(items.size());
    for (const auto& source : items) sources.emplace(source.id, &source);
    for (const auto& item : result.items) {
        Layer* layer = scene.findLayer(item.id);
        if (!layer || layer->isNull())
            throw std::invalid_argument("refreshFlowLayout: every item id must name a content layer in the scene");
        layers.push_back(layer);
    }
    // Validate every target before committing any layout changes: a late
    // animated layer must not leave earlier layers partially refreshed.
    for (const Layer* layer : layers) {
        if (!layer->tracks.position.empty() || !layer->tracks.anchor.empty())
            throw std::invalid_argument("refreshFlowLayout: layout cannot overwrite animated position or anchor tracks");
    }
    for (std::size_t index = 0; index < result.items.size(); ++index) {
        const auto& item = result.items[index];
        const FlowItem& source = *sources.at(item.id);
        Layer* layer = layers[index];
        layer->content.metrics.fingerprint = source.measurementFingerprint;
        layer->content.metrics.naturalSize = source.measuredSize;
        layer->content.metrics.anchor = item.anchor;
        layer->transform.position.x = item.position.x;
        layer->transform.position.y = item.position.y;
        layer->transform.anchor.set((item.anchor.x - 0.5f) * item.size.x,
                                    (item.anchor.y - 0.5f) * item.size.y,
                                    layer->transform.anchor.z);
    }
    return result;
}

} // namespace chrononmotion::motion
