// ChrononMotion — deterministic multi-target camera framing.
#ifndef CHRONONMOTION_MOTION_CAMERAFRAMING_HPP
#define CHRONONMOTION_MOTION_CAMERAFRAMING_HPP

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/math/Vector2.hpp"

#include <cstdint>
#include <vector>

namespace chrononmotion::motion {

struct CameraFramingBounds {
    Vector3 min{0.f, 0.f, 0.f};
    Vector3 max{0.f, 0.f, 0.f};
    float weight{1.f};
};

struct CameraSafeArea {
    float left{0.12f};
    float right{0.12f};
    float top{0.12f};
    float bottom{0.12f};

    [[nodiscard]] static CameraSafeArea full() { return {0.f, 0.f, 0.f, 0.f}; }
    [[nodiscard]] static CameraSafeArea tiktokVertical() { return {0.04f, 0.18f, 0.06f, 0.20f}; }
};

enum class CameraFramingStrategy : std::uint8_t {
    FitAll,
    FitHighest,
    RuleOfThirds,
    DollyOnly,
    AimOnly,
    DollyAndAim
};

struct CameraFramingRequest {
    std::vector<CameraFramingBounds> targets;
    CameraSafeArea safeArea{};
    CameraFramingStrategy strategy{CameraFramingStrategy::FitAll};
    Vector2 viewport{1920.f, 1080.f};
    Vector2 compositionPoint{0.5f, 0.5f};
    Vector3 targetVelocity{0.f, 0.f, 0.f};
    float lookAheadSeconds{0.f};
    float minDistance{10.f};
    float maxDistance{50000.f};
    float dollyDeadZone{0.02f};
    float aimDeadZoneDegrees{2.f};
    float aimErrorDegrees{0.f};
    float hysteresis{1.f};
};

struct CameraFramingSession {
    CameraPose previous{};
    bool hasPrevious{false};
    void reset() { previous = {}; hasPrevious = false; }
};

struct CameraFramingResult {
    CameraPose pose{};
    bool ok{true};
    float requiredDistance{0.f};
};

/// Resolves target bounds to a camera pose. It is pure with respect to scene
/// contents and can be evaluated in random access; the optional session only
/// applies deterministic hysteresis between caller-provided evaluations.
class CameraFramingSolver {
public:
    /// Optical distance required for a centered axis-aligned bound under the
    /// supplied safe-area contract. Used by shot and sequence authoring so
    /// they share the same framing law as multi-target solving.
    [[nodiscard]] static float distanceForBounds(float halfWidth, float halfHeight,
                                                 float halfDepth, float fovDegrees,
                                                 const Vector2& viewport,
                                                 const CameraSafeArea& safeArea);

    /// Maximum eye distance at which the centered bound still covers the
    /// complete viewport. Used for camera moves that pass through a surface.
    [[nodiscard]] static float coverDistanceForBounds(float halfWidth, float halfHeight,
                                                       float fovDegrees,
                                                       const Vector2& viewport);

    [[nodiscard]] CameraFramingResult solve(const CameraFramingRequest& request,
                                            const CameraPose& base,
                                            CameraFramingSession* session = nullptr) const;
};

} // namespace chrononmotion::motion

#endif
