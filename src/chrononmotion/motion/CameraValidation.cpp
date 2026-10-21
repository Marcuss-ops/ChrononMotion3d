#include "chrononmotion/motion/CameraValidation.hpp"

#include "chrononmotion/motion/CameraScreenSpace.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace chrononmotion::motion {
namespace {

bool finite(const Vector3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

} // namespace

CameraValidator& CameraValidator::addBounds(CameraValidationBounds bounds) {
    if (bounds.name.empty() || !finite(bounds.center) || !finite(bounds.halfExtent) ||
        bounds.halfExtent.x < 0.f || bounds.halfExtent.y < 0.f || bounds.halfExtent.z < 0.f)
        throw std::invalid_argument("CameraValidator::addBounds: name and finite non-negative bounds required");
    bounds_.push_back(std::move(bounds));
    return *this;
}

CameraValidator& CameraValidator::requireCentered(std::string name, float maxErrorPixels) {
    if (name.empty() || !std::isfinite(maxErrorPixels) || maxErrorPixels < 0.f)
        throw std::invalid_argument("CameraValidator::requireCentered: invalid name or threshold");
    centered_.push_back({std::move(name), maxErrorPixels});
    return *this;
}

CameraValidator& CameraValidator::requireVisible(std::string name, float minVisibleRatio) {
    if (name.empty() || !std::isfinite(minVisibleRatio) || minVisibleRatio < 0.f || minVisibleRatio > 1.f)
        throw std::invalid_argument("CameraValidator::requireVisible: invalid name or ratio");
    visible_.push_back({std::move(name), minVisibleRatio});
    return *this;
}

CameraValidator& CameraValidator::requireInsideSafeArea(std::string name, float margin01) {
    if (name.empty() || !std::isfinite(margin01) || margin01 < 0.f || margin01 >= 0.5f)
        throw std::invalid_argument("CameraValidator::requireInsideSafeArea: invalid name or margin");
    safeArea_.push_back({std::move(name), margin01});
    return *this;
}

CameraValidator& CameraValidator::requireDepthOrder(std::vector<std::string> frontToBack) {
    if (frontToBack.size() < 2)
        throw std::invalid_argument("CameraValidator::requireDepthOrder: at least two names required");
    depthOrders_.push_back(std::move(frontToBack));
    return *this;
}

CameraValidator& CameraValidator::requireEstimatedFrameVisibility(float minVisibleRatio) {
    if (!std::isfinite(minVisibleRatio) || minVisibleRatio < 0.f || minVisibleRatio > 1.f)
        throw std::invalid_argument("CameraValidator::requireEstimatedFrameVisibility: ratio must be in [0, 1]");
    frameVisibilityEnabled_ = true;
    minFrameVisibleRatio_ = minVisibleRatio;
    return *this;
}

CameraValidationReport CameraValidator::validate(const CameraPose& pose, const Vector2& viewport) const {
    if (!finite(pose.position) || !std::isfinite(pose.fov) || pose.fov <= 0.f || pose.fov >= 180.f ||
        !std::isfinite(viewport.x) || !std::isfinite(viewport.y) || viewport.x <= 0.f || viewport.y <= 0.f)
        throw std::invalid_argument("CameraValidator::validate: finite pose, FOV and positive viewport required");

    CameraValidationReport report;
    std::unordered_map<std::string, std::size_t> byName;
    report.layers.reserve(bounds_.size());
    const ScreenCameraIntrinsics intrinsics{viewport, pose.fov};
    const float centerX = viewport.x * 0.5f;
    const float centerY = viewport.y * 0.5f;

    for (const auto& bound : bounds_) {
        CameraValidationLayerReport layer;
        layer.name = bound.name;
        layer.minimum = Vector2(std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity());
        layer.maximum = Vector2(-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity());
        layer.opticalDepth = std::numeric_limits<float>::infinity();
        bool anyInFront = false;
        for (int i = 0; i < 8; ++i) {
            const Vector3 corner(bound.center.x + ((i & 1) ? bound.halfExtent.x : -bound.halfExtent.x),
                                 bound.center.y + ((i & 2) ? bound.halfExtent.y : -bound.halfExtent.y),
                                 bound.center.z + ((i & 4) ? bound.halfExtent.z : -bound.halfExtent.z));
            const float depth = viewDepth(corner, pose);
            if (depth <= 1e-4f) continue;
            layer.opticalDepth = std::min(layer.opticalDepth, depth);
            anyInFront = true;
            const Vector2 pixel = projectToScreen(corner, pose, intrinsics);
            layer.minimum.x = std::min(layer.minimum.x, pixel.x);
            layer.minimum.y = std::min(layer.minimum.y, pixel.y);
            layer.maximum.x = std::max(layer.maximum.x, pixel.x);
            layer.maximum.y = std::max(layer.maximum.y, pixel.y);
        }
        if (!anyInFront) {
            layer.failures.push_back("bounds are behind the camera");
            layer.visibleRatio = 0.f;
            layer.centerErrorPixels = std::numeric_limits<float>::max();
            layer.minimum = Vector2(0.f, 0.f);
            layer.maximum = Vector2(0.f, 0.f);
        } else {
            const float width = std::max(0.f, layer.maximum.x - layer.minimum.x);
            const float height = std::max(0.f, layer.maximum.y - layer.minimum.y);
            const float area = width * height;
            const float clippedWidth = std::max(0.f, std::min(layer.maximum.x, viewport.x) -
                                                       std::max(layer.minimum.x, 0.f));
            const float clippedHeight = std::max(0.f, std::min(layer.maximum.y, viewport.y) -
                                                        std::max(layer.minimum.y, 0.f));
            layer.visibleRatio = area > 1e-6f ? std::clamp(clippedWidth * clippedHeight / area, 0.f, 1.f) : 0.f;
            layer.centerErrorPixels = std::hypot((layer.minimum.x + layer.maximum.x) * 0.5f - centerX,
                                                  (layer.minimum.y + layer.maximum.y) * 0.5f - centerY);
        }
        byName.emplace(layer.name, report.layers.size());
        report.layers.push_back(std::move(layer));
    }

    const auto findLayer = [&](const std::string& name) -> CameraValidationLayerReport* {
        const auto found = byName.find(name);
        if (found == byName.end()) {
            report.passed = false;
            report.failures.push_back("bounds not registered for '" + name + "'");
            return nullptr;
        }
        return &report.layers[found->second];
    };
    for (const auto& rule : centered_) {
        auto* layer = findLayer(rule.name);
        if (!layer) continue;
        report.targetCenterErrorPixels = std::max(report.targetCenterErrorPixels, layer->centerErrorPixels);
        if (layer->centerErrorPixels > rule.maxErrorPixels) {
            report.passed = false;
            report.failures.push_back("'" + rule.name + "' exceeds the center-error threshold");
        }
    }
    for (const auto& rule : visible_) {
        auto* layer = findLayer(rule.name);
        if (!layer) continue;
        if (layer->visibleRatio < rule.minVisibleRatio) {
            report.passed = false;
            layer->failures.push_back("visible ratio below threshold");
            report.failures.push_back("'" + rule.name + "' is not sufficiently visible");
        }
    }
    for (const auto& rule : safeArea_) {
        auto* layer = findLayer(rule.name);
        if (!layer) continue;
        if (layer->minimum.x < viewport.x * rule.margin01 || layer->maximum.x > viewport.x * (1.f - rule.margin01) ||
            layer->minimum.y < viewport.y * rule.margin01 || layer->maximum.y > viewport.y * (1.f - rule.margin01)) {
            report.passed = false;
            layer->failures.push_back("bounds outside safe area");
            report.failures.push_back("'" + rule.name + "' is outside the safe area");
        }
    }
    for (const auto& order : depthOrders_) {
        for (std::size_t i = 0; i + 1 < order.size(); ++i) {
            auto* front = findLayer(order[i]);
            auto* back = findLayer(order[i + 1]);
            if (!front || !back) continue;
            if (front->opticalDepth >= back->opticalDepth) {
                report.passed = false;
                front->failures.push_back("depth order violation");
                report.failures.push_back("depth order violation between '" + order[i] + "' and '" + order[i + 1] + "'");
            }
        }
    }

    float visibleArea = 0.f;
    for (const auto& layer : report.layers) {
        const float area = std::max(0.f, layer.maximum.x - layer.minimum.x) *
                           std::max(0.f, layer.maximum.y - layer.minimum.y);
        visibleArea += area * layer.visibleRatio;
        report.maxLayerVisibility = std::max(report.maxLayerVisibility, layer.visibleRatio);
    }
    report.estimatedVisibleArea = visibleArea;
    report.estimatedVisibleRatio = std::clamp(visibleArea / (viewport.x * viewport.y), 0.f, 1.f);
    report.isEstimatedBlackFrame = visibleArea <= 0.f;
    if (frameVisibilityEnabled_ && (report.isEstimatedBlackFrame ||
                                    report.estimatedVisibleRatio < minFrameVisibleRatio_)) {
        report.passed = false;
        report.failures.push_back(report.isEstimatedBlackFrame
            ? "estimated frame visibility is zero"
            : "estimated frame visibility is below the configured threshold");
    }
    return report;
}

} // namespace chrononmotion::motion
