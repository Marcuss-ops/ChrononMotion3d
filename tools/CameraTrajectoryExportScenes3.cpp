#include "CameraTrajectoryExportInternal.hpp"
namespace camera_trajectory_export {
void appendCameraScenes3(std::vector<SceneExport>& suite) {


    // =========================================================================
    // 17. camera_vertigo_dolly_zoom (Hitchcock Vertigo Effect)
    // =========================================================================
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


    // =========================================================================
    // 18. camera_bullet_time_orbit_freeze (360° Bullet Time)
    // =========================================================================
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


    // =========================================================================
    // 19. camera_fpv_drone_dive_roll (FPV Dive with Bank Roll)
    // =========================================================================
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


    // =========================================================================
    // 20. camera_split_diopter_dual_plane (Split Diopter Dual Plane)
    // =========================================================================
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


    // =========================================================================
    // 21. camera_spiral_helix_ascend (Spiral Helix Ascend)
    // =========================================================================
{
        SceneExport s;
        s.id = "21_camera_spiral_helix_ascend";
        s.title = "Camera Spiral Helix Ascend";
        s.recipeName = "camera_spiral_helix_ascend";
        s.description = "Double-helix orbital elevator ascending smoothly 10m upward around central monolithic vertical structure";
        s.durationFrames = 120;

        s.entities.push_back({"TOW", "CENTRAL SPIRE MONOLITH", "VERTICAL SPINE", "CORE AXIS", "#10B981", Vector3(0.f, 2.0f, 0.f), Vector3(0.f, 0.f, 0.f), 3.5f, 9.0f});
        s.entities.push_back({"L1", "LEVEL 1 FOUNDATION", "TIER ONE POD", "LOW ORBIT", "#38BDF8", Vector3(-4.f, -2.5f, 0.f), Vector3(0.f, 45.f, 0.f), 3.0f, 2.2f});
        s.entities.push_back({"L2", "LEVEL 2 PLATFORM", "TIER TWO POD", "MID ORBIT", "#F59E0B", Vector3(4.f, 2.0f, 0.f), Vector3(0.f, -45.f, 0.f), 3.2f, 2.3f});
        s.entities.push_back({"CRN", "CROWN APEX", "EXECUTIVE OBSERVATORY", "HIGH ORBIT", "#F43F5E", Vector3(0.f, 6.5f, -3.f), Vector3(0.f, 180.f, 0.f), 3.8f, 2.6f});

        CameraRig rig(21, 52.f, 16.f / 9.f);
        cameraSpiralHelixAscend(rig, ShotAnchor::world(Vector3(0.f, 2.0f, 0.f)), 0, 120, 10.f, 8.f, 1.25f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 22. camera_fly_under_reveal (Sub-Surface Under Skim)
    // =========================================================================
{
        SceneExport s;
        s.id = "22_camera_fly_under_reveal";
        s.title = "Camera Fly Under Reveal";
        s.recipeName = "camera_fly_under_reveal";
        s.description = "Camera ducks below floor level, skims beneath a massive foreground obstacle and pitches upward into hero sanctuary";
        s.durationFrames = 120;

        s.entities.push_back({"OBS", "SUSPENDED ARCHWAY", "OBSTACLE CANOPY", "CEILING BARRIER", "#64748B", Vector3(0.f, 2.2f, 4.0f), Vector3(0.f, 0.f, 0.f), 7.5f, 3.2f});
        s.entities.push_back({"HERO", "COMMAND SANCTUARY", "CORE REVELATION", "HERO STAGE", "#06B6D4", Vector3(0.f, 1.0f, -12.0f), Vector3(0.f, 0.f, 0.f), 6.2f, 4.2f});
        s.entities.push_back({"FLR", "LUMINESCENT GRID", "FLOOR GUIDES", "SUB-DECK", "#1E293B", Vector3(0.f, -3.8f, -4.0f), Vector3(90.f, 0.f, 0.f), 12.0f, 16.0f});

        CameraRig rig(22, 58.f, 16.f / 9.f);
        cameraFlyUnderReveal(rig, ShotAnchor::world(Vector3(0.f, 2.2f, 4.0f)), ShotAnchor::world(Vector3(0.f, 1.0f, -12.0f)), 0, 120, -3.5f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 23. camera_anamorphic_barrel_sweep (Anamorphic Barrel Sweep)
    // =========================================================================
{
        SceneExport s;
        s.id = "23_camera_anamorphic_barrel_sweep";
        s.title = "Camera Anamorphic Barrel Sweep";
        s.recipeName = "camera_anamorphic_barrel_sweep";
        s.description = "Cinematic 2.39:1 widescreen lateral barrel sweep across 16m with dynamic focal perspective expansion";
        s.durationFrames = 120;

        s.entities.push_back({"WST", "WESTERN TERMINAL", "LOGISTICS ALPHA", "LEFT HORIZON", "#3B82F6", Vector3(-9.f, 0.f, -2.f), Vector3(0.f, 15.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"EST", "EASTERN HEADQUARTERS", "ENTERPRISE OMEGA", "RIGHT HORIZON", "#EF4444", Vector3(9.f, 0.f, -2.f), Vector3(0.f, -15.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"CTR", "CENTRAL SKYBRIDGE", "INTERCONNECT", "MIDSPAN", "#8B5CF6", Vector3(0.f, 1.2f, -8.f), Vector3(0.f, 0.f, 0.f), 5.5f, 2.5f});
        s.entities.push_back({"HOR", "CONTINENTAL HORIZON", "BACKGROUND EXPANSE", "FAR STAGE", "#0F172A", Vector3(0.f, 0.f, -25.f), Vector3(0.f, 0.f, 0.f), 24.0f, 8.0f});

        CameraRig rig(23, 40.f, 16.f / 9.f);
        cameraAnamorphicBarrelSweep(rig, ShotAnchor::world(Vector3(-9.f, 0.f, -2.f)), ShotAnchor::world(Vector3(9.f, 0.f, -2.f)), 0, 120, 16.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 24. camera_corkscrew_through_ring (Axial Corkscrew Plunge)
    // =========================================================================
{
        SceneExport s;
        s.id = "24_camera_corkscrew_through_ring";
        s.title = "Camera Corkscrew Through Ring";
        s.recipeName = "camera_corkscrew_through_ring";
        s.description = "Continuous 360-degree axial roll while plunging forward through an orbital satellite ring straight into the quantum core";
        s.durationFrames = 120;

        s.entities.push_back({"RNG", "ORBITAL HALO RING", "COLLIDER APERTURE", "RING PORTAL", "#8B5CF6", Vector3(0.f, 0.f, 2.f), Vector3(0.f, 0.f, 0.f), 5.0f, 5.0f});
        s.entities.push_back({"S1", "RING SATELLITE 1", "ACCELERATOR NODE", "TOP CLUSTER", "#00F0FF", Vector3(0.f, 3.8f, 2.f), Vector3(0.f, 0.f, 0.f), 2.0f, 1.5f});
        s.entities.push_back({"S2", "RING SATELLITE 2", "ACCELERATOR NODE", "BOTTOM CLUSTER", "#00F0FF", Vector3(0.f, -3.8f, 2.f), Vector3(0.f, 0.f, 0.f), 2.0f, 1.5f});
        s.entities.push_back({"CORE", "QUANTUM SINGULARITY", "REACTOR INTERIOR", "TARGET DEEP", "#F43F5E", Vector3(0.f, 0.f, -16.f), Vector3(0.f, 0.f, 0.f), 6.5f, 4.5f});

        CameraRig rig(24, 60.f, 16.f / 9.f);
        cameraCorkscrewThroughRing(rig, ShotAnchor::world(Vector3(0.f, 0.f, 2.f)), ShotAnchor::world(Vector3(0.f, 0.f, -16.f)), 0, 120, 16.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }
}
} // namespace camera_trajectory_export
