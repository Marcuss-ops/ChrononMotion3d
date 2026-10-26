#include "export_camera_trajectories_shared.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace chrononmotion_test::camera_export {
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

} // namespace chrononmotion_test::camera_export

int main(int argc, char** argv) {
    using namespace chrononmotion_test::camera_export;
    std::string outPath = "camera_motion_trajectories.json";
    if (argc > 1) outPath = argv[1];
    std::vector<SceneExport> suite;
    appendScenes01To10(suite);
    appendScenes11To20(suite);
    appendScenes21To31(suite);
    writeJson(outPath, suite);
    return 0;
}
