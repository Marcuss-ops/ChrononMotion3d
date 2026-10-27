#pragma once
#include "chrononmotion/chrononmotion.hpp"
#include <string>
#include <vector>
namespace camera_trajectory_export {
using namespace chrononmotion;
using namespace chrononmotion::motion;
inline constexpr float kFps = 30.f;
struct Entity3D {
    std::string id, title, category, tag, colorHex;
    Vector3 position, rotationDeg;
    float width{4.8f}, height{3.2f};
};
struct SceneExport {
    std::string id, title, recipeName, description;
    int durationFrames{};
    std::vector<Entity3D> entities;
    std::vector<CameraPose> poses;
};
inline float atFrame(int frame) { return static_cast<float>(frame) / kFps; }
void appendCameraScenes1(std::vector<SceneExport>& suite);
void appendCameraScenes2(std::vector<SceneExport>& suite);
void appendCameraScenes3(std::vector<SceneExport>& suite);
void appendCameraScenes4(std::vector<SceneExport>& suite);
} // namespace camera_trajectory_export
