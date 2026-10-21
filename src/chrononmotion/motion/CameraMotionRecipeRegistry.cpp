#include "chrononmotion/motion/CameraRecipe.hpp"

#include <algorithm>
#include <array>

namespace chrononmotion::motion::camera {
namespace {

const std::array<NamedCameraMotionRecipe, 25> kRecipes = {{
#define CAMERA_MOTION_RECIPE(id, ...) \
    NamedCameraMotionRecipe{id, [] { CameraMotionRecipe recipe; __VA_ARGS__ return recipe; }()},
#include "CameraMotionRecipeRegistry.def"
#undef CAMERA_MOTION_RECIPE
}};

} // namespace

std::span<const NamedCameraMotionRecipe> cameraMotionRecipeRegistry() noexcept {
    return kRecipes;
}

const CameraMotionRecipe* findCameraMotionRecipe(std::string_view id) noexcept {
    const auto it = std::find_if(kRecipes.begin(), kRecipes.end(),
        [id](const auto& entry) { return entry.id == id; });
    return it == kRecipes.end() ? nullptr : &it->recipe;
}

} // namespace chrononmotion::motion::camera
