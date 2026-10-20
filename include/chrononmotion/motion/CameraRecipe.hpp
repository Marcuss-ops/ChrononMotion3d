// Camera recipe evaluation owned by ChrononMotion3D.
#ifndef CHRONONMOTION_MOTION_CAMERARECIPE_HPP
#define CHRONONMOTION_MOTION_CAMERARECIPE_HPP

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Easing.hpp"

#include <string>

namespace chrononmotion::motion::camera {

    enum class RecipeIntensity { Subtle, Normal, Strong };

    struct RecipeSubject {
        Vector3 center{0.f, 0.f, 0.f};
        float halfWidth{480.f};
        float halfHeight{270.f};
    };

    struct CameraRecipeShot {
        RecipeSubject subject{};
        RecipeIntensity intensity{RecipeIntensity::Normal};
        float fov{50.f};
        float focusNear{800.f};
        float focusFar{1400.f};
        int inFrame{0};
        int durationOverride{0};
    };

    /// Numeric recipe data. Preset ids and editorial catalog labels stay with
    /// the client; all interpretation and application lives in CM3D.
    struct CameraMotionRecipe {
        int durationFrames{90};
        float distStart{1.f}, distMid{-1000.f}, distMidAt{0.5f}, distEnd{1.f};
        float xStart{0.f}, xMid{-1000.f}, xEnd{0.f};
        float yStart{0.f}, yMid{-1000.f}, yEnd{0.f};
        float posMidAt{0.5f};
        float travelStart{0.f}, travelEnd{1.f};
        float yawStart{0.f}, yawEnd{0.f};
        float pitchStart{0.f}, pitchEnd{0.f};
        float rollStart{0.f}, rollPeak{-1000.f}, rollEnd{0.f};
        float focusStart{-1.f}, focusEnd{-1.f};
        const char* easing{"smooth"};
        bool needsDof{false}, needs25D{false}, needsMultiPlane{false};
        bool twoShot{false}, needsOccluder{false}, highEnergy{false};
        float cutAt{0.5f};
        bool snapIn{false};
    };

    [[nodiscard]] float referenceDistance(const RecipeSubject& subject, float fovDegrees,
                                          const Vector2& viewport, float frameFill = 0.78f);
    [[nodiscard]] Easing easingFor(const std::string& name);
    [[nodiscard]] float intensityFactor(RecipeIntensity intensity) noexcept;
    void applyRecipe(CameraRig& rig, const CameraMotionRecipe& recipe,
                     const CameraRecipeShot& shot, float fps, const Vector2& viewport);

}// namespace chrononmotion::motion::camera

#endif//CHRONONMOTION_MOTION_CAMERARECIPE_HPP
