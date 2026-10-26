#pragma once

#include "chrononmotion/chrononmotion.hpp"

#include <string>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace chrononmotion_test::camera_export {



    inline constexpr float kFps = 30.f;

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

    inline float atFrame(int f) {
        return static_cast<float>(f) / kFps;
    }


void appendScenes01To10(std::vector<SceneExport>& suite);
void appendScenes11To20(std::vector<SceneExport>& suite);
void appendScenes21To31(std::vector<SceneExport>& suite);

} // namespace chrononmotion_test::camera_export
