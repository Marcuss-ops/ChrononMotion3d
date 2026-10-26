#include "export_camera_trajectories_shared.hpp"

namespace chrononmotion_test::camera_export {
void appendScenes11To20(std::vector<SceneExport>& suite) {
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

{
        SceneExport s;
        s.id = "17_camera_vertigo_dolly_zoom";
        s.title = "Camera Vertigo Dolly Zoom";
        s.recipeName = "camera_vertigo_dolly_zoom";
        s.description = "Optical Hitchcock vertigo effect: subject screen size is invariant while background perspective distorts dramatically";
        s.durationFrames = 120;

        s.entities.push_back({"A", "VERTIGO MONOLITH", "OPTICAL CORE", "SUBJECT HERO", "#00F0FF", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"BG1", "BACKGROUND PILLAR L", "DEPTH ARCHITECTURE", "STAGE LEFT", "#6366F1", Vector3(-8.f, 1.f, -18.f), Vector3(0.f, 15.f, 0.f), 3.5f, 6.5f});
        s.entities.push_back({"BG2", "BACKGROUND PILLAR R", "DEPTH ARCHITECTURE", "STAGE RIGHT", "#EC4899", Vector3(8.f, -1.f, -22.f), Vector3(0.f, -15.f, 0.f), 3.5f, 6.5f});
        s.entities.push_back({"BG3", "HORIZON WALL", "DISTANT CALIBRATION", "INFINITY PLANE", "#334155", Vector3(0.f, 0.f, -36.f), Vector3(0.f, 0.f, 0.f), 18.0f, 9.0f});

        CameraRig rig(17, 22.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 14.f));
        cameraVertigoDollyZoom(rig, ShotAnchor::world(Vector3(0.f, 0.f, 0.f)), 0, 120, 14.f, 3.6f, 22.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "18_camera_bullet_time_orbit_freeze";
        s.title = "Camera Bullet Time Orbit Freeze";
        s.recipeName = "camera_bullet_time_orbit_freeze";
        s.description = "Full 360-degree cylindrical orbital sweep around frozen suspended entities in mid-air with cinematic DOF";
        s.durationFrames = 120;

        s.entities.push_back({"A", "FROZEN MATRIX CORE", "SUSPENDED ENTITY", "FOCAL CENTER", "#EC4899", Vector3(0.f, 0.5f, 0.f), Vector3(15.f, 25.f, 10.f), 4.2f, 3.0f});
        s.entities.push_back({"D1", "ORBITAL SHARD ALPHA", "FROZEN DEBRIS", "NEAR CLUSTER", "#00F0FF", Vector3(-3.2f, 2.0f, -2.5f), Vector3(-10.f, 40.f, 20.f), 2.2f, 1.8f});
        s.entities.push_back({"D2", "ORBITAL SHARD BETA", "FROZEN DEBRIS", "FAR CLUSTER", "#F59E0B", Vector3(3.5f, -1.2f, 2.8f), Vector3(20.f, -30.f, -15.f), 2.4f, 2.0f});
        s.entities.push_back({"CIT", "PERIMETER CITADEL", "ENVIRONMENT BOUNDARY", "BACKGROUND", "#1E293B", Vector3(0.f, 0.f, -20.f), Vector3(0.f, 0.f, 0.f), 16.0f, 8.0f});

        CameraRig rig(18, 55.f, 16.f / 9.f);
        cameraBulletTimeOrbitFreeze(rig, ShotAnchor::world(Vector3(0.f, 0.5f, 0.f)), 0, 120, 7.5f, 1.2f, 360.f, 16.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "19_camera_fpv_drone_dive_roll";
        s.title = "Camera FPV Drone Dive Roll";
        s.recipeName = "camera_fpv_drone_dive_roll";
        s.description = "Acrobatic FPV drone steep dive with 55° Dutch banking roll, pulling out skimming millimeter above the floor";
        s.durationFrames = 120;

        s.entities.push_back({"SKY", "HIGHWAY OVERPASS", "HIGHWAY STRUCTURE", "HIGH ELEVATION", "#38BDF8", Vector3(-10.f, 10.f, 14.f), Vector3(-15.f, 20.f, 0.f), 5.0f, 3.2f});
        s.entities.push_back({"GATE", "STREET LEVEL GATE", "URBAN PORTAL", "TARGET ZERO", "#F59E0B", Vector3(0.f, -1.0f, 0.f), Vector3(0.f, 0.f, 0.f), 5.4f, 3.5f});
        s.entities.push_back({"TUN", "SUBWAY TUNNEL ENTRY", "TRANSIT NEXUS", "EXTRA DEPTH", "#EF4444", Vector3(0.f, -1.0f, -15.f), Vector3(0.f, 0.f, 0.f), 6.0f, 4.0f});

        CameraRig rig(19, 65.f, 16.f / 9.f);
        cameraFpvDroneDiveRoll(rig, ShotAnchor::world(Vector3(-10.f, 10.f, 14.f)), ShotAnchor::world(Vector3(0.f, -1.0f, 0.f)), 0, 120, 10.f, 55.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }

{
        SceneExport s;
        s.id = "20_camera_split_diopter_dual_plane";
        s.title = "Camera Split Diopter Dual Plane";
        s.recipeName = "camera_split_diopter_dual_plane";
        s.description = "Optical split-diopter framing: foreground left and deep background right simultaneously in cinematic focus handoff";
        s.durationFrames = 120;

        s.entities.push_back({"FG", "DIOPTER FOREGROUND", "OPTICAL FRONT", "STAGE LEFT", "#38BDF8", Vector3(-2.8f, -0.2f, 3.0f), Vector3(0.f, 12.f, 0.f), 3.8f, 2.6f});
        s.entities.push_back({"BG", "DEEP BACKGROUND CORE", "STRATEGIC SYSTEM", "STAGE RIGHT", "#A855F7", Vector3(3.2f, 0.5f, -18.0f), Vector3(0.f, -10.f, 0.f), 6.0f, 4.0f});
        s.entities.push_back({"MID", "TRANSIT CORRIDOR", "NEUTRAL MIDDLE", "MIDGROUND", "#475569", Vector3(0.f, 0.f, -7.0f), Vector3(0.f, 0.f, 0.f), 3.0f, 5.0f});

        CameraRig rig(20, 48.f, 16.f / 9.f);
        cameraSplitDiopterDualPlane(rig, ShotAnchor::world(Vector3(-2.8f, -0.2f, 3.0f)), ShotAnchor::world(Vector3(3.2f, 0.5f, -18.0f)), 0, 120, 20.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }
}
} // namespace chrononmotion_test::camera_export
