#include "chrononmotion/motion/Presets.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace chrononmotion::motion::presets {

const std::vector<std::string_view>& catalog() {
    static const std::vector<std::string_view> names{
        "ambientDriftField", "anchorTo", "arcEntrance", "arcMotion", "areaReveal", "auraBlobMotion",
        "autoFocus", "bob", "bounceIn", "breathe",        "cameraAnamorphicBarrelSweep", "cameraBirdsEyeGodViewDescent", "cameraBulletTimeOrbitFreeze",
        "cameraCorkscrewThroughRing", "cameraCrashZoomSnap", "cameraCornerTurnReveal", "cameraCraneUpReveal", "cameraPull",
        "cameraDepthPeel", "cameraDetachTurnReacquire", "cameraDollyFocusHandoff", "cameraDrift",
        "cameraFlyUnderReveal", "cameraFocusOrbitSettle", "cameraFocusPush", "cameraForegroundPassReveal",
        "cameraFov", "cameraFovBreath", "cameraFpvDroneDiveRoll", "cameraHeroLowAngleMonolithOrbit",
        "cameraInfinityMirrorDive", "cameraLateralSubjectToSubject", "cameraOrbit", "cameraOrbit90Reveal",
        "cameraOverTheShoulderDialogue", "cameraPan", "cameraParallaxGallery", "cameraPendulumSwingReveal",
        "cameraArcBetweenSubjects",
        "cameraPush", "cameraPushThroughOverlay", "cameraRackFocusDuo", "cameraShake", "cameraSlalomCardWeave",
        "cameraSplitDiopterDualPlane", "cameraSpiralHelixAscend", "cameraVertigoDollyZoom", "cameraWhipPanHandoff",
        "CameraTransition", "cardTurn", "crosshairLock", "drift", "extrudeRise", "fadeIn", "fadeOut", "fadeScale", "fadeSlide",
        "flip", "flipX", "flipY", "floatDrift", "floatHover", "focusPush", "followThrough",
        "groundTravel", "handheldShake", "horizonGrid", "lifetimeSlide", "lineDraw", "orbitMild",
        "orbitalSlamZoom", "parallaxEntrance", "parentGroupReveal", "pathMotion", "pullOutZ", "pulse",
        "pulseOpacity", "pushInZ", "rackFocus", "radarPing", "rippleWave", "scaleOvershoot",
        "scalePop", "setupBackgroundPlane", "shakeMild", "slideIn", "slideOut", "spinIn", "spinXYZ",
        "spinZ", "springSettle", "staggeredEntrance", "tilt3D"
    };
    return names;
}

bool known(std::string_view name) {
    const auto& names = catalog();
    return std::find(names.begin(), names.end(), name) != names.end();
}

void author(MotionScene& scene, const std::string& presetName,
            const std::function<void()>& operation) {
    if (!operation) throw std::invalid_argument("presets::author: operation is required");
    scene.authorizePreset(presetName);
    operation();
}

}// namespace chrononmotion::motion::presets