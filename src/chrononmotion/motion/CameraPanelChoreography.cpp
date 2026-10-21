#include "chrononmotion/motion/CameraPanelChoreography.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace chrononmotion::motion::camera {
namespace {

void checkWindow(int inFrame, int duration, int totalFrames, const char* name) {
    if (duration <= 0 || inFrame < 0 || inFrame + duration > totalFrames)
        throw std::invalid_argument(std::string("applyPanelChoreography: invalid ") + name + " window");
}

} // namespace

float panelFramingDistance(float subjectWidth, float fovDegrees,
                           const Vector2& viewport, float frameFill) {
    if (!std::isfinite(subjectWidth) || subjectWidth <= 0.f ||
        !std::isfinite(viewport.x) || viewport.x <= 0.f ||
        !std::isfinite(viewport.y) || viewport.y <= 0.f ||
        !std::isfinite(fovDegrees) || fovDegrees <= 0.f || fovDegrees >= 180.f ||
        !std::isfinite(frameFill) || frameFill <= 0.f || frameFill > 1.f)
        throw std::invalid_argument("panelFramingDistance: width, FOV, viewport and fill must be valid");
    const float verticalHalf = fovDegrees * 0.5f * 0.01745329251994329577f;
    const float horizontalTan = std::tan(verticalHalf) * (viewport.x / viewport.y);
    if (!(horizontalTan > 0.f) || !std::isfinite(horizontalTan))
        throw std::invalid_argument("panelFramingDistance: framing geometry is degenerate");
    return (subjectWidth * 0.5f / frameFill) / horizontalTan;
}

void applyPanelChoreography(CameraRig& rig, const CameraPanelChoreography& move,
                            float fps, const Vector2& viewport) {
    if (!std::isfinite(fps) || fps <= 0.f ||
        !std::isfinite(viewport.x) || viewport.x <= 0.f ||
        !std::isfinite(viewport.y) || viewport.y <= 0.f ||
        !std::isfinite(move.zoomDolly) || !std::isfinite(move.panOffset.x) ||
        !std::isfinite(move.panOffset.y) || !std::isfinite(move.safeMargin) || move.safeMargin < 1.f)
        throw std::invalid_argument("applyPanelChoreography: invalid motion values");
    checkWindow(move.zoomInFrame, move.zoomDuration, move.totalFrames, "zoom");
    checkWindow(move.panInFrame, move.panDuration, move.totalFrames, "pan");

    const float required = panelFramingDistance(move.subjectWidth, move.fov, viewport, move.frameFill) * move.safeMargin;
    rig.setFov(move.fov).setAspect(viewport.x / viewport.y);
    const Vector3 eye = rig.sample(0.f).position;
    if (eye.z < required) rig.setPosition(Vector3(eye.x, eye.y, required));

    const auto at = [fps](int frame) { return static_cast<float>(frame) / fps; };
    const Easing smooth = Easing::easeInOut();
    const float zoomT0 = at(move.zoomInFrame), zoomT1 = at(move.zoomInFrame + move.zoomDuration);
    const Vector3 zoomStart = rig.sample(zoomT0).position;
    Vector3 zoomEnd = zoomStart;
    zoomEnd.z -= move.zoomDolly;
    rig.positionTrack().add(zoomT0, zoomStart, smooth);
    rig.positionTrack().add(zoomT1, zoomEnd, smooth);

    const float panT0 = at(move.panInFrame), panT1 = at(move.panInFrame + move.panDuration);
    const Vector3 panStart = rig.sample(panT0).position;
    const Vector3 panEnd = panStart + Vector3(move.panOffset.x, move.panOffset.y, 0.f);
    rig.positionTrack().add(panT0, panStart, smooth);
    rig.positionTrack().add(panT1, panEnd, smooth);
    const Vector3 targetStart = rig.sample(panT0).target;
    const Vector3 targetEnd = targetStart + Vector3(move.panOffset.x, move.panOffset.y, 0.f);
    rig.targetTrack().add(panT0, targetStart, smooth);
    rig.targetTrack().add(panT1, targetEnd, smooth);

    const float endT = at(move.totalFrames);
    rig.positionTrack().add(endT, panEnd, smooth);
    rig.targetTrack().add(endT, targetEnd, smooth);
}

} // namespace chrononmotion::motion::camera
