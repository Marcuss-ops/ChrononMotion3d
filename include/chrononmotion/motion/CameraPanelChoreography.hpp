// Camera panel framing and timed push/pan choreography owned by ChrononMotion3D.
#ifndef CHRONONMOTION_MOTION_CAMERAPANELCHOREOGRAPHY_HPP
#define CHRONONMOTION_MOTION_CAMERAPANELCHOREOGRAPHY_HPP

#include "chrononmotion/motion/CameraRig.hpp"

namespace chrononmotion::motion::camera {

struct CameraPanelChoreography {
    float subjectWidth{0.f};
    float fov{50.f};
    float frameFill{0.78f};
    float safeMargin{1.12f};
    int totalFrames{90};
    int zoomInFrame{10};
    int zoomDuration{25};
    float zoomDolly{180.f};
    int panInFrame{28};
    int panDuration{37};
    Vector2 panOffset{-160.f, 0.f};
};

/// Distance needed for a width to span `frameFill` of the viewport width.
[[nodiscard]] float panelFramingDistance(float subjectWidth, float fovDegrees,
                                         const Vector2& viewport, float frameFill = 0.78f);

/// Resolve framing, overlapping push/pan tracks and the final settle hold.
/// Panel graphics and their animation remain the caller's responsibility.
void applyPanelChoreography(CameraRig& rig, const CameraPanelChoreography& choreography,
                            float fps, const Vector2& viewport);

} // namespace chrononmotion::motion::camera

#endif // CHRONONMOTION_MOTION_CAMERAPANELCHOREOGRAPHY_HPP
