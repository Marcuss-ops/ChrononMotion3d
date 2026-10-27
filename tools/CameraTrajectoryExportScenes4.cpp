#include "CameraTrajectoryExportInternal.hpp"
namespace camera_trajectory_export {
void appendCameraScenes4(std::vector<SceneExport>& suite) {


    // =========================================================================
    // 25. camera_pendulum_swing_reveal (Jib Arm Pendulum Arc)
    // =========================================================================
{
        SceneExport s;
        s.id = "25_camera_pendulum_swing_reveal";
        s.title = "Camera Pendulum Swing Reveal";
        s.recipeName = "camera_pendulum_swing_reveal";
        s.description = "Heavy camera jib pendulum swing: swoops from top-left, curves smoothly through nadir, and sweeps up to discover B";
        s.durationFrames = 120;

        s.entities.push_back({"OVL", "OVERLOOK ALPHA", "HIGH OBSERVATION", "TOP LEFT", "#EAB308", Vector3(-8.f, 4.5f, -3.f), Vector3(0.f, 25.f, 0.f), 4.6f, 3.2f});
        s.entities.push_back({"DISC", "DISCOVERY OMEGA", "FINAL REVELATION", "TOP RIGHT", "#10B981", Vector3(8.f, 4.0f, -4.f), Vector3(0.f, -25.f, 0.f), 4.8f, 3.3f});
        s.entities.push_back({"FLR", "THE CANYON FLOOR", "GEOLOGICAL ABYSS", "NADIR ZONE", "#475569", Vector3(0.f, -3.5f, -2.f), Vector3(0.f, 0.f, 0.f), 6.0f, 2.8f});

        CameraRig rig(25, 54.f, 16.f / 9.f);
        cameraPendulumSwingReveal(rig, ShotAnchor::world(Vector3(-8.f, 4.5f, -3.f)), ShotAnchor::world(Vector3(8.f, 4.0f, -4.f)), 0, 120, 4.5f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 26. camera_crash_zoom_snap (Tarantino / Edgar Wright Snap)
    // =========================================================================
{
        SceneExport s;
        s.id = "26_camera_crash_zoom_snap";
        s.title = "Camera Crash Zoom Snap";
        s.recipeName = "camera_crash_zoom_snap";
        s.description = "Tarantino/Edgar Wright hyper-fast snap push with explosive velocity curve and micro-spring settle";
        s.durationFrames = 90;

        s.entities.push_back({"CRIT", "CRITICAL THREAT TARGET", "RADICAL CLOSE-UP", "IMMEDIATE FOCUS", "#EF4444", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 4.5f, 3.0f});
        s.entities.push_back({"CITY", "BACKGROUND METROPOLIS", "URBAN CONTEXT", "BACKDROP", "#1E293B", Vector3(0.f, 0.f, -28.f), Vector3(0.f, 0.f, 0.f), 18.0f, 9.0f});
        s.entities.push_back({"LGT1", "WARNING BEACON L", "ALARM MATRIX", "LEFT FLANK", "#F59E0B", Vector3(-6.f, 0.f, -8.f), Vector3(0.f, 20.f, 0.f), 2.5f, 4.5f});
        s.entities.push_back({"LGT2", "WARNING BEACON R", "ALARM MATRIX", "RIGHT FLANK", "#F59E0B", Vector3(6.f, 0.f, -8.f), Vector3(0.f, -20.f, 0.f), 2.5f, 4.5f});

        CameraRig rig(26, 50.f, 16.f / 9.f);
        cameraCrashZoomSnap(rig, ShotAnchor::world(Vector3(0.f, 0.f, 0.f)), 0, 90, 15.f, 2.8f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 27. camera_infinity_mirror_dive (Exponential Portal Dive)
    // =========================================================================
{
        SceneExport s;
        s.id = "27_camera_infinity_mirror_dive";
        s.title = "Camera Infinity Mirror Dive";
        s.recipeName = "camera_infinity_mirror_dive";
        s.description = "Exponential acceleration plunge along Z axis slicing through nested translucent portal frames into infinity";
        s.durationFrames = 120;

        s.entities.push_back({"P1", "PORTAL GATE 01", "THRESHOLD ONE", "PORTAL ALPHA", "#38BDF8", Vector3(0.f, 0.f, 4.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"P2", "PORTAL GATE 02", "THRESHOLD TWO", "PORTAL BETA", "#818CF8", Vector3(0.f, 0.f, -2.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"P3", "PORTAL GATE 03", "THRESHOLD THREE", "PORTAL GAMMA", "#C084FC", Vector3(0.f, 0.f, -8.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"P4", "PORTAL GATE 04", "THRESHOLD FOUR", "PORTAL DELTA", "#F472B6", Vector3(0.f, 0.f, -14.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"P5", "PORTAL GATE 05", "THRESHOLD FIVE", "PORTAL EPSILON", "#FB7185", Vector3(0.f, 0.f, -20.f), Vector3(0.f, 0.f, 0.f), 4.8f, 3.2f});
        s.entities.push_back({"CORE", "INFINITY VOID SINGULARITY", "ORIGIN NODE", "INFINITE CORE", "#FBBF24", Vector3(0.f, 0.f, -26.f), Vector3(0.f, 0.f, 0.f), 6.5f, 4.5f});

        CameraRig rig(27, 56.f, 16.f / 9.f);
        std::vector<ShotAnchor> portals = {
            ShotAnchor::world(Vector3(0.f, 0.f, 4.f)),
            ShotAnchor::world(Vector3(0.f, 0.f, -2.f)),
            ShotAnchor::world(Vector3(0.f, 0.f, -8.f)),
            ShotAnchor::world(Vector3(0.f, 0.f, -14.f)),
            ShotAnchor::world(Vector3(0.f, 0.f, -20.f)),
            ShotAnchor::world(Vector3(0.f, 0.f, -26.f))
        };
        cameraInfinityMirrorDive(rig, portals, 0, 120, 26.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 28. camera_over_the_shoulder_dialogue (180° Dialogue Reverse-Angle)
    // =========================================================================
{
        SceneExport s;
        s.id = "28_camera_over_the_shoulder_dialogue";
        s.title = "Camera Over The Shoulder Dialogue";
        s.recipeName = "camera_over_the_shoulder_dialogue";
        s.description = "Cinematic 180° reverse-angle dialogue handoff: frames B past A's shoulder, then sweeps around to frame A past B's shoulder";
        s.durationFrames = 120;

        s.entities.push_back({"SPK1", "PROTAGONIST DIALOGUE A", "SPEAKER ALPHA", "LEFT SHOULDER", "#3B82F6", Vector3(-2.2f, 0.f, 1.2f), Vector3(0.f, 25.f, 0.f), 4.2f, 2.9f});
        s.entities.push_back({"SPK2", "ANTAGONIST DIALOGUE B", "SPEAKER BETA", "RIGHT SHOULDER", "#F97316", Vector3(2.2f, 0.f, -3.2f), Vector3(0.f, -25.f, 0.f), 4.2f, 2.9f});
        s.entities.push_back({"ROOM", "COUNCIL CHAMBER WALL", "BACKGROUND INTERIOR", "STAGE DEEP", "#1E293B", Vector3(0.f, 0.f, -16.f), Vector3(0.f, 0.f, 0.f), 15.0f, 7.5f});

        CameraRig rig(28, 48.f, 16.f / 9.f);
        cameraOverTheShoulderDialogue(rig, ShotAnchor::world(Vector3(-2.2f, 0.f, 1.2f)), ShotAnchor::world(Vector3(2.2f, 0.f, -3.2f)), 0, 120, 2.2f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 29. camera_birds_eye_god_view_descent (God View Descent to Eye-Level)
    // =========================================================================
{
        SceneExport s;
        s.id = "29_camera_birds_eye_god_view_descent";
        s.title = "Camera Bird's Eye God View Descent";
        s.recipeName = "camera_birds_eye_god_view_descent";
        s.description = "Starts at 90° top-down architectural nadir, dives rapidly downward while pitching smoothly up into eye-level hero perspective";
        s.durationFrames = 120;

        s.entities.push_back({"CAMP", "METROPOLIS CAMPUS", "CITY ARCHITECTURE", "GROUND CENTRAL", "#06B6D4", Vector3(0.f, 0.f, 0.f), Vector3(0.f, 0.f, 0.f), 5.5f, 3.6f});
        s.entities.push_back({"WNG1", "NORTH QUADRANT", "CIVIC DISTRICT", "NORTH FLANK", "#8B5CF6", Vector3(-6.f, 0.f, -4.f), Vector3(0.f, 30.f, 0.f), 4.2f, 2.8f});
        s.entities.push_back({"WNG2", "SOUTH QUADRANT", "COMMERCE DISTRICT", "SOUTH FLANK", "#10B981", Vector3(6.f, 0.f, 4.f), Vector3(0.f, -30.f, 0.f), 4.2f, 2.8f});

        CameraRig rig(29, 60.f, 16.f / 9.f);
        cameraBirdsEyeGodViewDescent(rig, ShotAnchor::world(Vector3(0.f, 0.f, 0.f)), 0, 120, 22.f, 7.5f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 30. camera_slalom_card_weave (High-Speed Slalom with Roll Banking)
    // =========================================================================
{
        SceneExport s;
        s.id = "30_camera_slalom_card_weave";
        s.title = "Camera Slalom Card Weave";
        s.recipeName = "camera_slalom_card_weave";
        s.description = "Dynamic high-speed slalom carving S-curves between alternating left/right monolithic cards with centrifugal banking roll";
        s.durationFrames = 120;

        s.entities.push_back({"G1", "SLALOM GATE 01", "NAV POINT LEFT", "GATE ONE", "#38BDF8", Vector3(-3.5f, 0.f, 8.f), Vector3(0.f, 15.f, 0.f), 4.0f, 2.8f});
        s.entities.push_back({"G2", "SLALOM GATE 02", "NAV POINT RIGHT", "GATE TWO", "#F59E0B", Vector3(3.5f, 0.f, 2.f), Vector3(0.f, -15.f, 0.f), 4.0f, 2.8f});
        s.entities.push_back({"G3", "SLALOM GATE 03", "NAV POINT LEFT", "GATE THREE", "#10B981", Vector3(-3.5f, 0.f, -4.f), Vector3(0.f, 15.f, 0.f), 4.0f, 2.8f});
        s.entities.push_back({"G4", "SLALOM GATE 04", "NAV POINT RIGHT", "GATE FOUR", "#EC4899", Vector3(3.5f, 0.f, -10.f), Vector3(0.f, -15.f, 0.f), 4.0f, 2.8f});
        s.entities.push_back({"G5", "FINAL FINISH LINE", "TERMINAL MARKER", "GATE FIVE", "#6366F1", Vector3(0.f, 0.f, -16.f), Vector3(0.f, 0.f, 0.f), 5.0f, 3.2f});

        CameraRig rig(30, 55.f, 16.f / 9.f);
        std::vector<ShotAnchor> gates = {
            ShotAnchor::world(Vector3(-3.5f, 0.f, 8.f)),
            ShotAnchor::world(Vector3(3.5f, 0.f, 2.f)),
            ShotAnchor::world(Vector3(-3.5f, 0.f, -4.f)),
            ShotAnchor::world(Vector3(3.5f, 0.f, -10.f)),
            ShotAnchor::world(Vector3(0.f, 0.f, -16.f))
        };
        cameraSlalomCardWeave(rig, gates, 0, 120, 4.5f, 18.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }


    // =========================================================================
    // 31. camera_hero_low_angle_monolith_orbit (Floor-Level Hero Orbit)
    // =========================================================================
{
        SceneExport s;
        s.id = "31_camera_hero_low_angle_monolith_orbit";
        s.title = "Camera Hero Low Angle Monolith Orbit";
        s.recipeName = "camera_hero_low_angle_monolith_orbit";
        s.description = "Dramatic floor-level perspective (Y=-2.8) pitched upward (+26°) orbiting an imposing monolith structure";
        s.durationFrames = 120;

        s.entities.push_back({"MONO", "SUPREME MONOLITH", "EXECUTIVE CITADEL", "HERO CENTER", "#F59E0B", Vector3(0.f, 2.0f, 0.f), Vector3(0.f, 0.f, 0.f), 4.5f, 7.5f});
        s.entities.push_back({"OBL1", "WEST WING OBELISK", "FLANK ARCHITECTURE", "LEFT DEFENSE", "#64748B", Vector3(-7.f, 0.f, -3.f), Vector3(0.f, 30.f, 0.f), 3.0f, 5.5f});
        s.entities.push_back({"OBL2", "EAST WING OBELISK", "FLANK ARCHITECTURE", "RIGHT DEFENSE", "#64748B", Vector3(7.f, 0.f, -3.f), Vector3(0.f, -30.f, 0.f), 3.0f, 5.5f});
        s.entities.push_back({"BG", "HORIZON NEBULA WALL", "ATMOSPHERE", "DEEP STAGE", "#1E293B", Vector3(0.f, 0.f, -24.f), Vector3(0.f, 0.f, 0.f), 20.0f, 10.0f});

        CameraRig rig(31, 55.f, 16.f / 9.f);
        cameraHeroLowAngleMonolithOrbit(rig, ShotAnchor::world(Vector3(0.f, 2.0f, 0.f)), 0, 120, 7.0f, -2.8f, 65.f, 26.f, kFps);

        for (int f = 0; f < s.durationFrames; ++f) {
            s.poses.push_back(rig.sample(atFrame(f)));
        }
        suite.push_back(s);
    }
}
} // namespace camera_trajectory_export
