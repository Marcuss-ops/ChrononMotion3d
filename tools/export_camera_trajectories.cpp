// ChrononMotion — Export Camera Motion V1 Trajectories and Scene Objects.
//
// Evaluates all 15 authoritative recipes + the 16s master canary using the
// real CameraRig and CameraMotion C++ implementations, and writes a structured
// JSON containing exact sampled poses and 3D card/anchor geometry.

#include "chrononmotion/chrononmotion.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {

    constexpr float kFps = 30.f;

    struct Entity3D {
        std::string id;
        std::string title;
        std::string category;
        std::string tag;
        std::string colorHex;
        Vector3 position;
        Vector3 rotationDeg;
        float width{4.8f};
        float height{3.2f};
    };

    struct SceneExport {
        std::string id;
        std::string title;
        std::string recipeName;
        std::string description;
        int durationFrames;
        std::vector<Entity3D> entities;
        std::vector<CameraPose> poses;
    };

    float atFrame(int f) {
        return static_cast<float>(f) / kFps;
    }

    void writeJson(const std::string& outputPath, const std::vector<SceneExport>& scenes) {
        std::ofstream out(outputPath);
        if (!out) {
            std::cerr << "Failed to open output file: " << outputPath << "\n";
            return;
        }

        out << "{\n  \"version\": 1,\n  \"fps\": 30,\n  \"width\": 1920,\n  \"height\": 1080,\n  \"scenes\": [\n";

        for (size_t s = 0; s < scenes.size(); ++s) {
            const auto& sc = scenes[s];
            out << "    {\n";
            out << "      \"id\": \"" << sc.id << "\",\n";
            out << "      \"title\": \"" << sc.title << "\",\n";
            out << "      \"recipe\": \"" << sc.recipeName << "\",\n";
            out << "      \"description\": \"" << sc.description << "\",\n";
            out << "      \"duration_frames\": " << sc.durationFrames << ",\n";

            // Entities
            out << "      \"entities\": [\n";
            for (size_t e = 0; e < sc.entities.size(); ++e) {
                const auto& ent = sc.entities[e];
                out << "        {\n";
                out << "          \"id\": \"" << ent.id << "\",\n";
                out << "          \"title\": \"" << ent.title << "\",\n";
                out << "          \"category\": \"" << ent.category << "\",\n";
                out << "          \"tag\": \"" << ent.tag << "\",\n";
                out << "          \"color\": \"" << ent.colorHex << "\",\n";
                out << "          \"position\": [" << ent.position.x << ", " << ent.position.y << ", " << ent.position.z << "],\n";
                out << "          \"rotation_deg\": [" << ent.rotationDeg.x << ", " << ent.rotationDeg.y << ", " << ent.rotationDeg.z << "],\n";
                out << "          \"size\": [" << ent.width << ", " << ent.height << "]\n";
                out << "        }" << (e + 1 < sc.entities.size() ? "," : "") << "\n";
            }
            out << "      ],\n";

            // Poses
            out << "      \"frames\": [\n";
            CameraRig helperRig(999, 60.f, 16.f / 9.f);
            for (size_t f = 0; f < sc.poses.size(); ++f) {
                const auto& p = sc.poses[f];

                // Calculate screen projected target
                auto cam = helperRig.makeCamera();
                helperRig.apply(p, *cam);

                float tgtScreenX = 960.f;
                float tgtScreenY = 540.f;
                if (p.position.distanceTo(p.target) > 0.01f) {
                    Vector3 tgtNdc = p.target;
                    tgtNdc.project(*cam);
                    if (std::isfinite(tgtNdc.x) && std::isfinite(tgtNdc.y)) {
                        tgtScreenX = (tgtNdc.x + 1.f) * 0.5f * 1920.f;
                        tgtScreenY = (1.f - tgtNdc.y) * 0.5f * 1080.f;
                    }
                }

                out << "        {\n";
                out << "          \"frame\": " << f << ",\n";
                out << "          \"time\": " << (static_cast<float>(f) / kFps) << ",\n";
                out << "          \"position\": [" << p.position.x << ", " << p.position.y << ", " << p.position.z << "],\n";
                out << "          \"quaternion\": [" << p.orientation.x << ", " << p.orientation.y << ", " << p.orientation.z << ", " << p.orientation.w << "],\n";
                out << "          \"target\": [" << p.target.x << ", " << p.target.y << ", " << p.target.z << "],\n";
                out << "          \"target_screen\": [" << tgtScreenX << ", " << tgtScreenY << "],\n";
                out << "          \"fov\": " << p.fov << ",\n";
                out << "          \"focus_distance\": " << p.focusDistance << ",\n";
                out << "          \"f_stop\": " << p.fStop << ",\n";
                out << "          \"roll\": " << p.roll << "\n";
                out << "        }" << (f + 1 < sc.poses.size() ? "," : "") << "\n";
            }
            out << "      ]\n";
            out << "    }" << (s + 1 < scenes.size() ? "," : "") << "\n";
        }

        out << "  ]\n}\n";
        std::cout << "Successfully exported " << scenes.size() << " scene trajectories to " << outputPath << "\n";
    }

} // namespace

int main(int argc, char** argv) {
    std::string outPath = "camera_motion_trajectories.json";
    std::string selectedScene;
    bool listScenes = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--list") {
            listScenes = true;
        } else if (argument == "--all") {
            selectedScene.clear();
        } else if (argument == "--scene" && i + 1 < argc) {
            selectedScene = argv[++i];
        } else if (!argument.starts_with("--") && outPath == "camera_motion_trajectories.json") {
            outPath = argument;
        } else {
            std::cerr << "usage: chrononmotion_export_camera_v1 [--all | --scene ID | --list] [output.json]\n";
            return 2;
        }
    }
    if (listScenes && !selectedScene.empty()) {
        std::cerr << "--list cannot be combined with --scene\n";
        return 2;
    }

    std::vector<SceneExport> suite;

    // =========================================================================
    // 1. camera_focus_push
    // =========================================================================
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

    // =========================================================================
    // 2. camera_rack_focus_duo
    // =========================================================================
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

    // =========================================================================
    // 3. camera_dolly_focus_handoff
    // =========================================================================
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

    // =========================================================================
    // 4. camera_lateral_subject_to_subject
    // =========================================================================
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

    // =========================================================================
    // 5. camera_orbit_90_reveal
    // =========================================================================
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

    // =========================================================================
    // 6. camera_corner_turn_reveal
    // =========================================================================
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

    // =========================================================================
    // 7. camera_detach_turn_reacquire
    // =========================================================================
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

    // =========================================================================
    // 8. camera_foreground_pass_reveal
    // =========================================================================
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

    if (listScenes) {
        for (const auto& scene : suite) std::cout << scene.id << "\t" << scene.title << '\n';
        return 0;
    }
    if (!selectedScene.empty()) {
        const auto selected = std::find_if(suite.begin(), suite.end(), [&](const SceneExport& scene) {
            return scene.id == selectedScene;
        });
        if (selected == suite.end()) {
            std::cerr << "unknown scene id: " << selectedScene << "\n";
            return 2;
        }
        SceneExport scene = std::move(*selected);
        suite = {std::move(scene)};
    }
    writeJson(outPath, suite);
    return 0;
}
