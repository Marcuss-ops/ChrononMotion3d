#ifndef THREEPP_MOTION_CINEMATIC_SCENE_LOOK_HPP
#define THREEPP_MOTION_CINEMATIC_SCENE_LOOK_HPP

#include "threepp/math/Vector3.hpp"
#include "threepp/math/Color.hpp"

namespace threepp {

    class Renderer;
    class Scene;

    namespace motion {

        /// Reusable, renderer-facing look development for imported 3D scenes.
        /// Positions and distances are in the scene's world units; fog density
        /// is normalized to environmentRadius so presets scale across assets.
        struct CinematicSceneLookOptions {
            bool clayOverride{false};
            Color clayColor{0x3d444d};
            float clayRoughness{0.92f};

            bool enableShadows{true};
            int shadowResolution{1024};
            float ambientIntensity{0.12f};
            Color ambientColor{0xc5ceda};
            bool enableHemisphereFill{true};
            float hemisphereIntensity{0.8f};
            Color hemisphereSkyColor{0x9aaec4};
            Color hemisphereGroundColor{0x302d2a};
            float keyIntensity{40.f};
            Color keyColor{0xffdfc0};
            float keyAngleRadians{0.72f};
            float keyPenumbra{0.75f};
            float shadowBias{-0.0001f};
            float shadowNormalBias{0.02f};
            bool enableRimFill{true};
            float rimFillIntensity{1.8f};
            Color rimFillColor{0x8caed8};

            bool enableDepthFog{true};
            Color fogColor{0x172231};
            float fogDensityPerEnvironmentRadius{0.12f};
        };

        /// Applies a shared lighting/material/fog setup to a scene. The focus
        /// point and subject height define light placement; environmentRadius
        /// scales spotlight range and exponential fog for assets of different
        /// sizes. Existing imported materials are preserved unless clayOverride
        /// is explicitly selected.
        void applyCinematicSceneLook(
                Scene& scene,
                Renderer& renderer,
                const Vector3& focus,
                float subjectHeight,
                float environmentRadius,
                const CinematicSceneLookOptions& options = {});

    }// namespace motion
}// namespace threepp

#endif//THREEPP_MOTION_CINEMATIC_SCENE_LOOK_HPP
