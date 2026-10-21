#ifndef CHRONONMOTION_MOTION_CAMERAVALIDATION_HPP
#define CHRONONMOTION_MOTION_CAMERAVALIDATION_HPP

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/math/Vector2.hpp"

#include <string>
#include <vector>

namespace chrononmotion::motion {

struct CameraValidationBounds {
    std::string name;
    Vector3 center{0.f, 0.f, 0.f};
    Vector3 halfExtent{100.f, 100.f, 0.f};
};

struct CameraValidationLayerReport {
    std::string name;
    Vector2 minimum{0.f, 0.f};
    Vector2 maximum{0.f, 0.f};
    float visibleRatio{0.f};
    float opticalDepth{0.f};
    float centerErrorPixels{0.f};
    std::vector<std::string> failures;
};

struct CameraValidationReport {
    bool passed{true};
    float targetCenterErrorPixels{0.f};
    float estimatedVisibleRatio{0.f};
    std::vector<CameraValidationLayerReport> layers;
    std::vector<std::string> failures;
};

/// Geometric checks on an already-authored camera pose. This module does not
/// render pixels; it validates projected bounds, framing, and depth ordering.
class CameraValidator {
public:
    CameraValidator& addBounds(CameraValidationBounds bounds);
    CameraValidator& requireCentered(std::string name, float maxErrorPixels);
    CameraValidator& requireVisible(std::string name, float minVisibleRatio);
    CameraValidator& requireInsideSafeArea(std::string name, float margin01);
    CameraValidator& requireDepthOrder(std::vector<std::string> frontToBack);

    [[nodiscard]] CameraValidationReport validate(const CameraPose& pose,
                                                   const Vector2& viewport) const;

private:
    struct CenterRule { std::string name; float maxErrorPixels; };
    struct VisibleRule { std::string name; float minVisibleRatio; };
    struct SafeAreaRule { std::string name; float margin01; };

    std::vector<CameraValidationBounds> bounds_;
    std::vector<CenterRule> centered_;
    std::vector<VisibleRule> visible_;
    std::vector<SafeAreaRule> safeArea_;
    std::vector<std::vector<std::string>> depthOrders_;
};

} // namespace chrononmotion::motion

#endif
