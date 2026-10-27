// ChrononMotion — Export Camera Motion V1 Trajectories and Scene Objects.
//
// Evaluates all 15 authoritative recipes + the 16s master canary using the
// real CameraRig and CameraMotion C++ implementations, and writes a structured
// JSON containing exact sampled poses and 3D card/anchor geometry.

#include "CameraTrajectoryExportInternal.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {

    using namespace camera_trajectory_export;

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
    appendCameraScenes1(suite);
    appendCameraScenes2(suite);
    appendCameraScenes3(suite);
    appendCameraScenes4(suite);

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
