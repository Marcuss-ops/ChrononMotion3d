#ifndef CHRONONMOTION_MOTION_CAMERATRAJECTORY_HPP
#define CHRONONMOTION_MOTION_CAMERATRAJECTORY_HPP

#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"

#include <memory>
#include <optional>
#include <vector>

namespace chrononmotion::motion::camera {

enum class TrajectorySegmentKind { Linear, CubicBezier, CatmullRom, Hold };

struct CameraTrajectoryPoint {
    Vector3 position{};
    Vector3 handleIn{};
    Vector3 handleOut{};
    std::optional<Vector3> target{};
    float rollDegrees{0.f};
};

struct CameraTrajectorySegment {
    TrajectorySegmentKind kind{TrajectorySegmentKind::Linear};
    unsigned fromIndex{0};
    unsigned toIndex{0};
    float durationFrames{30.f};
};

struct CameraTrajectorySample {
    Vector3 position{};
    Vector3 tangent{};
    std::optional<Vector3> target{};
    float rollDegrees{0.f};
};

/// A camera path with continuous frame sampling, segment timing and optional
/// per-segment arc-length remapping. CameraRig remains the owner of evaluation
/// into renderer-ready camera tracks.
class CameraTrajectory {
public:
    [[nodiscard]] CameraTrajectorySample sample(float frame) const;
    [[nodiscard]] float totalFrames() const;
    [[nodiscard]] std::size_t size() const { return segments_.size(); }
    [[nodiscard]] const std::vector<CameraTrajectoryPoint>& points() const { return points_; }
    [[nodiscard]] const std::vector<CameraTrajectorySegment>& segments() const { return segments_; }

private:
    friend class CameraTrajectoryBuilder;
    struct ArcEntry { float distance; float parameter; };
    std::vector<CameraTrajectoryPoint> points_;
    std::vector<CameraTrajectorySegment> segments_;
    std::vector<std::vector<ArcEntry>> arcTables_;
};

class CameraTrajectoryBuilder {
public:
    CameraTrajectoryBuilder& moveTo(Vector3 position,
                                    std::optional<Vector3> target = std::nullopt,
                                    float rollDegrees = 0.f);
    CameraTrajectoryBuilder& bezierTo(Vector3 handleIn, Vector3 handleOut, Vector3 to);
    CameraTrajectoryBuilder& catmullRomTo(Vector3 to);
    CameraTrajectoryBuilder& holdFor(float frames);
    CameraTrajectoryBuilder& durationFrames(float frames);
    CameraTrajectoryBuilder& arcLengthParameterized(bool enabled = true);
    [[nodiscard]] std::shared_ptr<CameraTrajectory> build();

private:
    std::vector<CameraTrajectoryPoint> points_;
    std::vector<CameraTrajectorySegment> segments_;
    bool arcLength_{false};
};

/// Bake a continuous trajectory into CM3D CameraRig channels. Sub-frame
/// subdivision preserves the path shape for shutter sampling after lowering.
void authorCameraTrajectory(CameraRig& rig, const CameraTrajectory& trajectory,
                            int startFrame, float fps, unsigned samplesPerFrame = 4);

} // namespace chrononmotion::motion::camera

#endif
