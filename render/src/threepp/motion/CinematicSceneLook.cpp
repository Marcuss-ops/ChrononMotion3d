#include "threepp/motion/CinematicSceneLook.hpp"

#include "threepp/core/Object3D.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/lights/HemisphereLight.hpp"
#include "threepp/lights/SpotLight.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/Renderer.hpp"
#include "threepp/scenes/FogExp2.hpp"
#include "threepp/scenes/Scene.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace threepp::motion {

    namespace {

        bool finiteColor(const Color& color) {
            return std::isfinite(color.r) && std::isfinite(color.g) && std::isfinite(color.b);
        }

        void validate(const Vector3& focus, float subjectHeight, float environmentRadius,
                      const CinematicSceneLookOptions& options) {
            if (!std::isfinite(focus.x) || !std::isfinite(focus.y) || !std::isfinite(focus.z) ||
                !std::isfinite(subjectHeight) || subjectHeight <= 0.f ||
                !std::isfinite(environmentRadius) || environmentRadius <= 0.f) {
                throw std::invalid_argument("applyCinematicSceneLook: focus and scene dimensions must be finite; dimensions must be positive");
            }
            if (!std::isfinite(options.clayRoughness) || options.clayRoughness < 0.f || options.clayRoughness > 1.f ||
                !std::isfinite(options.ambientIntensity) || options.ambientIntensity < 0.f ||
                !std::isfinite(options.hemisphereIntensity) || options.hemisphereIntensity < 0.f ||
                !std::isfinite(options.keyIntensity) || options.keyIntensity < 0.f ||
                !std::isfinite(options.rimFillIntensity) || options.rimFillIntensity < 0.f ||
                !std::isfinite(options.keyAngleRadians) || options.keyAngleRadians <= 0.f ||
                options.keyAngleRadians >= 1.57079632679f ||
                !std::isfinite(options.keyPenumbra) || options.keyPenumbra < 0.f || options.keyPenumbra > 1.f ||
                !std::isfinite(options.fogDensityPerEnvironmentRadius) || options.fogDensityPerEnvironmentRadius < 0.f ||
                options.shadowResolution < 64 || options.shadowResolution > 8192 ||
                !std::isfinite(options.shadowBias) || !std::isfinite(options.shadowNormalBias) ||
                !finiteColor(options.clayColor) || !finiteColor(options.ambientColor) ||
                !finiteColor(options.hemisphereSkyColor) || !finiteColor(options.hemisphereGroundColor) ||
                !finiteColor(options.keyColor) || !finiteColor(options.rimFillColor) || !finiteColor(options.fogColor)) {
                throw std::invalid_argument("applyCinematicSceneLook: invalid look option");
            }
        }

    }// namespace

    void applyCinematicSceneLook(Scene& scene, Renderer& renderer, const Vector3& focus,
                                 float subjectHeight, float environmentRadius,
                                 const CinematicSceneLookOptions& options) {
        validate(focus, subjectHeight, environmentRadius, options);

        scene.add(AmbientLight::create(options.ambientColor, options.ambientIntensity));
        if (options.enableHemisphereFill) {
            scene.add(HemisphereLight::create(options.hemisphereSkyColor,
                                              options.hemisphereGroundColor,
                                              options.hemisphereIntensity));
        }

        auto key = SpotLight::create(options.keyColor, options.keyIntensity,
                                     environmentRadius * 3.f,
                                     options.keyAngleRadians, options.keyPenumbra, 2.f);
        key->position.set(focus.x - subjectHeight * 0.45f,
                          focus.y + subjectHeight * 0.92f,
                          focus.z + subjectHeight * 0.60f);
        key->castShadow = options.enableShadows;
        key->shadow->mapSize.set(static_cast<float>(options.shadowResolution),
                                 static_cast<float>(options.shadowResolution));
        key->shadow->bias = options.shadowBias;
        key->shadow->normalBias = options.shadowNormalBias;
        key->shadow->camera->nearPlane = std::max(0.01f, subjectHeight * 0.02f);
        key->shadow->camera->farPlane = environmentRadius * 3.f;
        auto keyTarget = Group::create();
        keyTarget->position.copy(focus);
        scene.add(keyTarget);
        key->setTarget(*keyTarget);
        scene.add(key);

        if (options.enableRimFill) {
            auto rim = DirectionalLight::create(options.rimFillColor, options.rimFillIntensity);
            rim->position.set(focus.x + subjectHeight * 1.2f,
                              focus.y + subjectHeight * 0.45f,
                              focus.z - subjectHeight * 1.1f);
            auto rimTarget = Group::create();
            rimTarget->position.copy(focus);
            scene.add(rimTarget);
            rim->setTarget(*rimTarget);
            scene.add(rim);
        }

        scene.traverse([&](Object3D& node) {
            if (dynamic_cast<Mesh*>(&node)) {
                node.castShadow = options.enableShadows;
                node.receiveShadow = options.enableShadows;
            }
        });

        renderer.shadowMap().enabled = options.enableShadows;
        if (options.enableShadows) {
            renderer.shadowMap().type = ShadowMap::PFCSoft;
        }

        if (options.clayOverride) {
            auto clay = MeshStandardMaterial::create(
                    MeshStandardMaterial::Params()
                            .color(options.clayColor)
                            .roughness(options.clayRoughness)
                            .metalness(0.f));
            scene.overrideMaterial = clay;
        }

        if (options.enableDepthFog) {
            const float density = options.fogDensityPerEnvironmentRadius / environmentRadius;
            scene.fog = FogExp2(options.fogColor, density);
        }
    }

}// namespace threepp::motion
