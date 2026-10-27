#include "CameraTrajectoryExportInternal.hpp"
namespace camera_trajectory_export {
void appendCameraScenes2(std::vector<SceneExport>& suite) {


    // =========================================================================
    // 9. camera_depth_peel
    // =========================================================================
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


    // =========================================================================
    // 10. camera_arc_between_subjects
    // =========================================================================
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


    // =========================================================================
    // 11. camera_whip_pan_handoff
    // =========================================================================
{
        SceneExport s;
        s.id = "11_camera_whip_pan_handoff";
        s.title = "Camera Whip Pan Handoff";
        s.recipeName = "camera_whip_pan_handoff";
        s.description = "High-energy fast rotational whip snap from A to B with rock-solid zero residual drift settle";
        s.durationFrames = 120;

        s.entities.push_back({"A", "SCENE ALPHA", "INITIAL FRAME", "HOLD 0-35F", "#E11D48", Vector3(-7.f, 0.f, 0.f), Vector3(0.f, 15.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"B", "SCENE BETA", "TARGET SNAP", "SNAP & LOCK", "#22C55E", Vector3(7.f, 0.f, 0.f), Vector3(0.f, -15.f, 0.f), 4.8f, 3.2f});

        CameraRig rig(11, 55.f, 16.f / 9.f);
        rig.setPosition(Vector3(-7.f, 0.f, 8.f));
        rig.setTarget(Vector3(-7.f, 0.f, 0.f));
        cameraWhipPanHandoff(rig, ShotAnchor::world(Vector3(-7.f, 0.f, 0.f)), ShotAnchor::world(Vector3(7.f, 0.f, 0.f)), 30, 40, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 12. camera_crane_up_reveal
    // =========================================================================
{
        SceneExport s;
        s.id = "12_camera_crane_up_reveal";
        s.title = "Camera Crane Up Reveal";
        s.recipeName = "camera_crane_up_reveal";
        s.description = "Close-up on detailed element elevations cranes up and pulls back with widening FOV to reveal entire landscape";
        s.durationFrames = 120;

        s.entities.push_back({"A", "CORE METRIC", "PRIMARY DETAIL", "CENTER", "#00F0FF", Vector3(0.f, -1.5f, 0.f), Vector3(0.f, 0.f, 0.f), 4.4f, 3.0f});
        s.entities.push_back({"B1", "DISTRIBUTED CLUSTER L", "CONTEXT PERIPHERY", "WEST SATELLITE", "#6366F1", Vector3(-8.f, -0.5f, -6.f), Vector3(0.f, 25.f, 0.f), 4.0f, 2.7f});
        s.entities.push_back({"B2", "DISTRIBUTED CLUSTER R", "CONTEXT PERIPHERY", "EAST SATELLITE", "#EC4899", Vector3(8.f, -0.5f, -6.f), Vector3(0.f, -25.f, 0.f), 4.0f, 2.7f});
        s.entities.push_back({"B3", "ARCHITECTURAL HORIZON", "CONTEXT PERIPHERY", "NORTH BACKBONE", "#10B981", Vector3(0.f, 2.5f, -15.f), Vector3(0.f, 0.f, 0.f), 6.0f, 3.2f});

        CameraRig rig(12, 42.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, -1.5f, 5.f));
        cameraCraneUpReveal(rig, ShotAnchor::world(Vector3(0.f, -1.5f, 0.f)), ShotAnchor::world(Vector3(0.f, 0.5f, -6.f)), 0, 120, 7.5f, 12.f, 38.f, 68.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 13. camera_push_through_overlay
    // =========================================================================
{
        SceneExport s;
        s.id = "13_camera_push_through_overlay";
        s.title = "Camera Push Through Overlay";
        s.recipeName = "camera_push_through_overlay";
        s.description = "Camera glides straight through an opening portal card into an expansive interior 3D world behind it";
        s.durationFrames = 120;

        s.entities.push_back({"A", "PORTAL OVERLAY", "ENTRY INTERFACE", "PORTAL PLANE", "#38BDF8", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 5.2f, 3.6f});
        s.entities.push_back({"B", "INTERIOR WORLD", "INNER SYSTEM", "INTERIOR CORE", "#F59E0B", Vector3(0.f, 0.f, -14.f), Vector3(0.f, 0.f, 0.f), 6.5f, 4.2f});
        s.entities.push_back({"P1", "PILLAR LEFT", "INTERIOR ARCHITECTURE", "LEFT WING", "#64748B", Vector3(-6.f, 0.f, -8.f), Vector3(0.f, 30.f, 0.f), 3.0f, 6.0f});
        s.entities.push_back({"P2", "PILLAR RIGHT", "INTERIOR ARCHITECTURE", "RIGHT WING", "#64748B", Vector3(6.f, 0.f, -8.f), Vector3(0.f, -30.f, 0.f), 3.0f, 6.0f});

        CameraRig rig(13, 55.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 8.f));
        cameraPushThroughOverlay(rig, ShotAnchor::world(Vector3(0.f, 0.f, 0.f)), ShotAnchor::world(Vector3(0.f, 0.f, -14.f)), 0, 120, 16.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 14. camera_parallax_gallery
    // =========================================================================
{
        SceneExport s;
        s.id = "14_camera_parallax_gallery";
        s.title = "Camera Parallax Gallery";
        s.recipeName = "camera_parallax_gallery";
        s.description = "Staggered multi-card lateral travel showcasing natural multi-plane 3D parallax depth separation";
        s.durationFrames = 120;

        s.entities.push_back({"C1", "PORTFOLIO ALPHA", "EXECUTIVE CARD", "NEAR LAYER", "#00F0FF", Vector3(-8.f, 0.f, 2.5f), Vector3(0.f, 10.f, 0.f), 4.2f, 2.9f});
        s.entities.push_back({"C2", "PORTFOLIO BETA", "TECH STACK", "MID DEPTH", "#8B5CF6", Vector3(-2.5f, 0.2f, -3.5f), Vector3(0.f, -8.f, 0.f), 4.5f, 3.1f});
        s.entities.push_back({"C3", "PORTFOLIO GAMMA", "CLOUD ARCHITECTURE", "FOREGROUND", "#10B981", Vector3(3.0f, -0.2f, 1.5f), Vector3(0.f, 12.f, 0.f), 4.3f, 3.0f});
        s.entities.push_back({"C4", "PORTFOLIO DELTA", "FINANCIAL CORE", "DEEP STAGE", "#F43F5E", Vector3(8.5f, 0.3f, -5.5f), Vector3(0.f, -14.f, 0.f), 4.8f, 3.3f});

        CameraRig rig(14, 54.f, 16.f / 9.f);
        rig.setPosition(Vector3(-8.f, 0.f, 8.f));
        std::vector<ShotAnchor> cards = {
            ShotAnchor::world(Vector3(-8.f, 0.f, 2.5f)),
            ShotAnchor::world(Vector3(-2.5f, 0.2f, -3.5f)),
            ShotAnchor::world(Vector3(3.0f, -0.2f, 1.5f)),
            ShotAnchor::world(Vector3(8.5f, 0.3f, -5.5f))
        };
        cameraParallaxGallery(rig, cards, 0, 120, 8.5f, 12.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 15. camera_focus_orbit_settle
    // =========================================================================
{
        SceneExport s;
        s.id = "15_camera_focus_orbit_settle";
        s.title = "Camera Focus Orbit Settle";
        s.recipeName = "camera_focus_orbit_settle";
        s.description = "Frontal presentation with gentle sweeping orbit (-10° to +15°) and dolly settle on core entity";
        s.durationFrames = 120;

        s.entities.push_back({"A", "HERO ENTERPRISE CARD", "EXECUTIVE SHOWCASE", "MONOLITH", "#00F0FF", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 5.2f, 3.5f});
        s.entities.push_back({"BG", "HORIZON GRID REFERENCE", "CALIBRATION PLANE", "STAGE BG", "#334155", Vector3(0.f, 0.f, -10.f), Vector3(0.f, 0.f, 0.f), 12.0f, 6.0f});

        CameraRig rig(15, 50.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 8.f));
        cameraFocusOrbitSettle(rig, ShotAnchor::world(Vector3(0.f, 0.f, 0.f)), 0, 120, 18.f, 7.f, 2.5f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 16. camera_motion_torture_master_16s (Master Canary Showcase)
    // =========================================================================
{
        SceneExport s;
        s.id = "16_camera_motion_torture_master_16s";
        s.title = "Camera Motion V1 Master Canary (16s Continuous)";
        s.recipeName = "camera_motion_torture_v1";
        s.description = "Full 16-second continuous multi-entity torture suite verifying continuity, framing locks, DOF handoffs & crane finish";
        s.durationFrames = 480; // 16.0s @ 30fps

        s.entities.push_back({"A", "IMAGE ALPHA", "PRIMARY PORTRAIT", "SECTOR A", "#00F0FF", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 4.6f, 3.1f});
        s.entities.push_back({"B", "IMAGE BETA", "SECONDARY ARCHITECTURE", "SECTOR B", "#F43F5E", Vector3(5.f, 0.f, -40.f), Vector3(0.f, 15.f, 0.f), 6.5f, 4.4f});
        s.entities.push_back({"C", "WEB CARD GAMMA", "INTERACTIVE INTERFACE", "SECTOR C", "#10B981", Vector3(25.f, 0.f, -10.f), Vector3(0.f, -20.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"D", "MAP POINT DELTA", "GEOLOCATION MARKER", "SECTOR D", "#F59E0B", Vector3(40.f, -2.f, -30.f), Vector3(0.f, 45.f, 0.f), 5.2f, 3.5f});

        CameraRig rig(16, 60.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 8.f));

        const ShotAnchor anchorA = ShotAnchor::image(1, Vector3(0.f, 0.f, 0.f));
        const ShotAnchor anchorB = ShotAnchor::image(2, Vector3(5.f, 0.f, -40.f));
        const ShotAnchor anchorC = ShotAnchor::webCard(3, Vector3(25.f, 0.f, -10.f));
        const ShotAnchor anchorD = ShotAnchor::mapPoint(4, Vector3(40.f, -2.f, -30.f));
        const ShotAnchor overviewAnchor = ShotAnchor::world(Vector3(15.f, 5.f, -20.f));

        cameraFocusPush(rig, anchorA, 0, 60, 3.f, 4.f, 14.f, kFps);
        cameraRackFocusDuo(rig, anchorA, anchorB, 60, 60, 14.f, kFps);
        cameraLateralSubjectToSubject(rig, anchorA, anchorC, 120, 60, kFps);
        cameraOrbit90Reveal(rig, anchorC, anchorD, Vector3(25.f, 0.f, -20.f), 180, 60, kFps);
        cameraPushThroughOverlay(rig, anchorC, anchorD, 240, 60, 12.f, kFps);
        cameraArcBetweenSubjects(rig, anchorC, anchorD, 300, 60, 5.f, kFps);
        cameraCraneUpReveal(rig, anchorD, overviewAnchor, 360, 120, 12.f, 25.f, 50.f, 75.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }
}
} // namespace camera_trajectory_export
