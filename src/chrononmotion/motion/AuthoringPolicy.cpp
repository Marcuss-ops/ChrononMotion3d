#include "chrononmotion/motion/Presets.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace chrononmotion::motion::presets {

const std::vector<std::string_view>& catalog() {
    static const std::vector<std::string_view> names{
        "ambientDriftField", "anchorTo", "arcEntrance", "arcMotion", "areaReveal", "auraBlobMotion",
        "autoFocus", "bob", "bounceIn", "breathe",
        "cameraAnamorphicBarrelSweep", "cameraBirdsEyeGodViewDescent", "cameraBulletTimeOrbitFreeze",
        "cameraCorkscrewThroughRing", "cameraCrashZoomSnap", "cameraCornerTurnReveal", "cameraCraneUpReveal", "cameraPull",
        "cameraDepthPeel", "cameraDetachTurnReacquire", "cameraDollyFocusHandoff", "cameraDrift",
        "cameraFlyUnderReveal", "cameraFocusOrbitSettle", "cameraFocusPush", "cameraForegroundPassReveal",
        "cameraFov", "cameraFovBreath", "cameraFpvDroneDiveRoll", "cameraHeroLowAngleMonolithOrbit",
        "cameraInfinityMirrorDive", "cameraLateralSubjectToSubject", "cameraOrbit", "cameraOrbit90Reveal",
        "cameraOverTheShoulderDialogue", "cameraPan", "cameraParallaxGallery", "cameraPendulumSwingReveal",
        "cameraArcBetweenSubjects",
        "cameraPush", "cameraPushThroughOverlay", "cameraRackFocusDuo", "cameraShake", "cameraSlalomCardWeave",
        "cameraSplitDiopterDualPlane", "cameraSpiralHelixAscend", "cameraVertigoDollyZoom", "cameraWhipPanHandoff",
        "CameraTransition",
        "camera_motion_soft_push_in", "camera_motion_soft_pull_back",
        "camera_motion_editorial_drift_left", "camera_motion_editorial_drift_right",
        "camera_motion_title_rise", "camera_motion_title_descend",
        "camera_motion_quiet_diagonal_drift", "camera_motion_type_depth_focus",
        "camera_motion_type_micro_orbit", "camera_motion_impact_punch_settle",
        "camera_motion_ken_burns_in", "camera_motion_ken_burns_out",
        "camera_motion_portrait_upward_track", "camera_motion_portrait_downward_track",
        "camera_motion_photo_reveal_left", "camera_motion_photo_reveal_right",
        "camera_motion_low_angle_photo_reveal", "camera_motion_portrait_hero_arc",
        "camera_motion_documentary_parallax", "camera_motion_photo_rack_focus",
        "camera_motion_map_region_push", "camera_motion_map_region_pull",
        "camera_motion_map_eastward_track", "camera_motion_map_westward_track",
        "camera_motion_map_northward_track", "camera_motion_map_southward_track",
        "camera_motion_route_camera_sweep", "camera_motion_country_focus_lock",
        "camera_motion_country_comparison_pan", "camera_motion_geopolitical_tilt_reveal",
        "camera_motion_ui_panel_push", "camera_motion_dashboard_pan",
        "camera_motion_card_stack_parallax", "camera_motion_corner_detail_inspect",
        "camera_motion_data_detail_zoom", "camera_motion_ui_overview_pull",
        "camera_motion_side_by_side_compare", "camera_motion_panel_angled_peek",
        "camera_motion_chart_traverse", "camera_motion_product_ui_micro_float",
        "camera_motion_whip_pan_left", "camera_motion_whip_pan_right",
        "camera_motion_whip_pan_up", "camera_motion_whip_pan_down",
        "camera_motion_fast_dolly_through", "camera_motion_reverse_dolly_reveal",
        "camera_motion_roll_match_cut", "camera_motion_orbit_handoff",
        "camera_motion_foreground_occlusion_pass", "camera_motion_focus_handoff",
        "title_camera_slow_push", "title_camera_slow_pull_out",
        "title_camera_left_drift", "title_camera_right_drift",
        "title_camera_vertical_rise", "title_camera_vertical_descend",
        "title_camera_micro_orbit_left", "title_camera_micro_orbit_right",
        "title_camera_arc_push", "title_camera_arc_pull",
        "title_camera_low_angle_push", "title_camera_high_angle_settle",
        "title_camera_roll_settle", "title_camera_roll_pass",
        "title_camera_dolly_zoom_subtle", "title_camera_focus_push",
        "title_camera_parallax_side", "title_camera_parallax_push",
        "title_camera_whip_settle", "title_camera_corner_reveal",
        "title_camera_crash_push", "title_camera_top_down_settle",
        "title_camera_handheld_micro", "title_camera_orbit_wide",
        "title_camera_floor_rise",
        "scene_camera_push_through", "scene_camera_lateral_swipe",
        "scene_camera_arc_carry", "scene_camera_orbit_handoff",
        "scene_camera_rise_and_land", "scene_camera_focus_rack",
        "scene_camera_pull_back_reveal", "scene_camera_whip_reframe",
        "cardTurn", "crosshairLock", "drift", "extrudeRise", "fadeIn", "fadeOut", "fadeScale", "fadeSlide",
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