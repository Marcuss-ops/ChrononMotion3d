#include "export_camera_trajectories_shared.hpp"

namespace chrononmotion_test::camera_export {
void appendScenes01To10(std::vector<SceneExport>& suite) {
{
        SceneExport s;
        s.id = "01_camera_focus_push";
        s.title = "Camera Focus Push";
        s.recipeName = "camera_focus_push";
        s.description = "Progressive dolly push onto subject with increasing aperture & background depth of field";
        s.durationFrames = 120; // 4.0s

        s.entities.push_back({"A", "SUBJECT ALPHA", "PRIMARY IDENTITY", "HERO 01", "#00F0FF", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"BG1", "ENVIRONMENT REF A", "BACKGROUND STAGE", "DEPTH -12M", "#38BDF8", Vector3(-6.f, 2.f, -12.f), Vector3(0.f, 15.f, 0.f), 4.0f, 2.8f});
        s.entities.push_back({"BG2", "ENVIRONMENT REF B", "BACKGROUND STAGE", "DEPTH -20M", "#818CF8", Vector3(6.f, -1.f, -20.f), Vector3(0.f, -15.f, 0.f), 4.5f, 3.0f});

        CameraRig rig(1, 55.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 9.f));
        cameraFocusPush(rig, ShotAnchor::world(Vector3(0.f, 0.f, 0.f)), 0, 120, 5.0f, 2.f, 18.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "02_camera_rack_focus_duo";
        s.title = "Camera Rack Focus Duo";
        s.recipeName = "camera_rack_focus_duo";
        s.description = "Pure cinematic rack focus shift from foreground Subject A to background Subject B with stationary camera";
        s.durationFrames = 120;

        s.entities.push_back({"A", "FOREGROUND ALPHA", "NEAR SUBJECT", "PLANE 01", "#00F0FF", Vector3(-2.8f, -0.4f, 0.f), Vector3(0.f, 12.f, 0.f), 4.6f, 3.1f});
        s.entities.push_back({"B", "BACKGROUND BETA", "FAR SUBJECT", "PLANE 02", "#F43F5E", Vector3(3.2f, 0.6f, -16.f), Vector3(0.f, -12.f, 0.f), 5.4f, 3.6f});

        CameraRig rig(2, 50.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 8.f));
        cameraRackFocusDuo(rig, ShotAnchor::world(Vector3(-2.8f, -0.4f, 0.f)), ShotAnchor::world(Vector3(3.2f, 0.6f, -16.f)), 0, 120, 20.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "03_camera_dolly_focus_handoff";
        s.title = "Camera Dolly Focus Handoff";
        s.recipeName = "camera_dolly_focus_handoff";
        s.description = "Sideways dolly glide combined with focus plane handoff from primary subject to incoming entity";
        s.durationFrames = 120;

        s.entities.push_back({"A", "PRIMARY CARD A", "INITIAL TARGET", "ACTIVE -> SECONDARY", "#38BDF8", Vector3(-3.5f, 0.f, 0.f), Vector3(0.f, 10.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"B", "SECONDARY CARD B", "INCOMING TARGET", "SECONDARY -> ACTIVE", "#A855F7", Vector3(3.5f, 0.f, -7.f), Vector3(0.f, -10.f, 0.f), 4.8f, 3.2f});

        CameraRig rig(3, 52.f, 16.f / 9.f);
        rig.setPosition(Vector3(-3.5f, 0.f, 8.f));
        cameraDollyFocusHandoff(rig, ShotAnchor::world(Vector3(-3.5f, 0.f, 0.f)), ShotAnchor::world(Vector3(3.5f, 0.f, -7.f)), 0, 120, Vector3(5.f, 0.f, -2.5f), 14.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "04_camera_lateral_subject_to_subject";
        s.title = "Camera Lateral Subject To Subject";
        s.recipeName = "camera_lateral_subject_to_subject";
        s.description = "Smooth 3D horizontal camera translation across space, transitioning look-at and focus anchors";
        s.durationFrames = 120;

        s.entities.push_back({"A", "METRIC CARD A", "FINANCIAL OVERVIEW", "Q3 REVENUE", "#10B981", Vector3(-6.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"B", "METRIC CARD B", "PERFORMANCE METRIC", "NET CONVERSION", "#F59E0B", Vector3(6.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});

        CameraRig rig(4, 55.f, 16.f / 9.f);
        rig.setPosition(Vector3(-6.f, 0.f, 8.f));
        cameraLateralSubjectToSubject(rig, ShotAnchor::world(Vector3(-6.f, 0.f, 0.f)), ShotAnchor::world(Vector3(6.f, 0.f, 0.f)), 0, 120, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "05_camera_orbit_90_reveal";
        s.title = "Camera Orbit 90° Reveal";
        s.recipeName = "camera_orbit_90_reveal";
        s.description = "Exact 90-degree orbital sweep around a 3D pivot, revealing a perpendicular second stage entity";
        s.durationFrames = 120;

        // Front wall card
        s.entities.push_back({"A", "STAGE 01 — FRONT", "MAIN PRESENTATION", "WALL 0°", "#06B6D4", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        // Right wall card (perpendicular at 90 deg yaw!)
        s.entities.push_back({"B", "STAGE 02 — RIGHT", "REVEALED ARCHITECTURE", "WALL 90°", "#EC4899", Vector3(7.f, 0.f, -7.f), Vector3(0.f, 90.f, 0.f), 4.8f, 3.2f});

        CameraRig rig(5, 54.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 8.f));
        cameraOrbit90Reveal(rig, ShotAnchor::world(Vector3(0.f, 0.f, 0.f)), ShotAnchor::world(Vector3(7.f, 0.f, -7.f)), Vector3(0.f, 0.f, -7.f), 0, 120, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "06_camera_corner_turn_reveal";
        s.title = "Camera Corner Turn Reveal";
        s.recipeName = "camera_corner_turn_reveal";
        s.description = "Forward corridor advance with continuous tangential banking around a smooth 90° corner into Subject B";
        s.durationFrames = 120;

        s.entities.push_back({"A", "CORRIDOR START", "INITIAL PERSPECTIVE", "ENTRANCE", "#6366F1", Vector3(0.f, 0.f, 2.f), Vector3(0.f, 0.f, 0.f), 4.4f, 3.0f});
        s.entities.push_back({"CORNER", "INTERSECTION", "TRANSITION JUNCTION", "CORNER POINT", "#64748B", Vector3(0.f, 0.f, -8.f), Vector3(0.f, 45.f, 0.f), 2.2f, 2.2f});
        s.entities.push_back({"B", "DESTINATION GALLERY", "REVEALED OBJECT", "EAST WING", "#14B8A6", Vector3(10.f, 0.f, -8.f), Vector3(0.f, 90.f, 0.f), 4.8f, 3.2f});

        CameraRig rig(6, 56.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 10.f));
        cameraCornerTurnReveal(rig, ShotAnchor::world(Vector3(0.f, 0.f, 2.f)), ShotAnchor::world(Vector3(10.f, 0.f, -8.f)), Vector3(0.f, 0.f, -8.f), 0, 120, 2.5f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "07_camera_detach_turn_reacquire";
        s.title = "Camera Detach Turn Reacquire";
        s.recipeName = "camera_detach_turn_reacquire";
        s.description = "Unlocks from primary anchor, sweeps freely through space, then smoothly acquires and locks Subject B";
        s.durationFrames = 120;

        s.entities.push_back({"A", "ORIGIN NODE", "INITIAL LOCK", "TRACK 01", "#3B82F6", Vector3(-5.5f, 0.f, 0.f), Vector3(0.f, 15.f, 0.f), 4.6f, 3.1f});
        s.entities.push_back({"B", "TARGET NODE", "ACQUIRED TARGET", "TRACK 02", "#F97316", Vector3(6.5f, 1.2f, -5.f), Vector3(0.f, -20.f, 0.f), 4.8f, 3.2f});

        CameraRig rig(7, 52.f, 16.f / 9.f);
        rig.setPosition(Vector3(-5.5f, 0.f, 8.f));
        cameraDetachTurnReacquire(rig, ShotAnchor::world(Vector3(-5.5f, 0.f, 0.f)), ShotAnchor::world(Vector3(6.5f, 1.2f, -5.f)), 0, 120, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "08_camera_foreground_pass_reveal";
        s.title = "Camera Foreground Pass Reveal";
        s.recipeName = "camera_foreground_pass_reveal";
        s.description = "Camera maneuvers beside a foreground blurred silhouette to reveal sharp hero content in background";
        s.durationFrames = 120;

        // Foreground element close to camera
        s.entities.push_back({"FG", "FOREGROUND OBSTACLE", "OCCLUDING LAYER", "DEFOCUSED SILHOUETTE", "#475569", Vector3(-1.8f, -0.6f, 3.5f), Vector3(0.f, 18.f, 0.f), 4.2f, 2.9f});
        s.entities.push_back({"B", "FEATURED REVELATION", "HERO DESTINATION", "PIN SHARP", "#8B5CF6", Vector3(3.2f, 0.4f, -7.f), Vector3(0.f, -12.f, 0.f), 5.0f, 3.4f});

        CameraRig rig(8, 54.f, 16.f / 9.f);
        rig.setPosition(Vector3(-1.8f, -0.6f, 10.f));
        cameraForegroundPassReveal(rig, ShotAnchor::world(Vector3(-1.8f, -0.6f, 3.5f)), ShotAnchor::world(Vector3(3.2f, 0.4f, -7.f)), 0, 120, Vector3(3.8f, 0.f, -4.f), 15.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "09_camera_depth_peel";
        s.title = "Camera Depth Peel";
        s.recipeName = "camera_depth_peel";
        s.description = "Camera pierces through a 3D corridor of stacked layers, sequential focus shifts and fly-throughs";
        s.durationFrames = 120;

        s.entities.push_back({"A", "LAYER 01 / CONCEPT", "FIRST REVELATION", "Z = 0.0M", "#06B6D4", Vector3(-1.5f, 0.f, 0.f), Vector3(0.f, 10.f, 0.f), 4.4f, 3.0f});
        s.entities.push_back({"B", "LAYER 02 / ANALYSIS", "SECOND REVELATION", "Z = -7.0M", "#8B5CF6", Vector3(1.8f, 0.4f, -7.f), Vector3(0.f, -10.f, 0.f), 4.6f, 3.1f});
        s.entities.push_back({"C", "LAYER 03 / DELIVERY", "FINAL DESTINATION", "Z = -14.0M", "#EC4899", Vector3(-0.5f, -0.2f, -14.f), Vector3(0.f, 5.f, 0.f), 5.0f, 3.4f});

        CameraRig rig(9, 56.f, 16.f / 9.f);
        rig.setPosition(Vector3(-1.5f, 0.f, 8.f));
        std::vector<ShotAnchor> anchors = {
            ShotAnchor::world(Vector3(-1.5f, 0.f, 0.f)),
            ShotAnchor::world(Vector3(1.8f, 0.4f, -7.f)),
            ShotAnchor::world(Vector3(-0.5f, -0.2f, -14.f))
        };
        cameraDepthPeel(rig, anchors, 0, 120, 5.5f, 14.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "10_camera_arc_between_subjects";
        s.title = "Camera Arc Between Subjects";
        s.recipeName = "camera_arc_between_subjects";
        s.description = "Cinematic curved arc flight path between two subjects with dynamic yaw and perspective banking";
        s.durationFrames = 120;

        s.entities.push_back({"A", "SUBJECT ALPHA", "DEPARTURE VECTOR", "POINT A", "#10B981", Vector3(-5.5f, 0.f, 0.f), Vector3(0.f, 15.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"B", "SUBJECT BETA", "ARRIVAL VECTOR", "POINT B", "#3B82F6", Vector3(5.5f, 0.f, 0.f), Vector3(0.f, -15.f, 0.f), 4.8f, 3.2f});

        CameraRig rig(10, 55.f, 16.f / 9.f);
        rig.setPosition(Vector3(-5.5f, 0.f, 8.f));
        cameraArcBetweenSubjects(rig, ShotAnchor::world(Vector3(-5.5f, 0.f, 0.f)), ShotAnchor::world(Vector3(5.5f, 0.f, 0.f)), 0, 120, 4.5f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }
}
} // namespace chrononmotion_test::camera_export
