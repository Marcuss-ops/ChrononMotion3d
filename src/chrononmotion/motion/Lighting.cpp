#include "chrononmotion/motion/Lighting.hpp"

namespace chrononmotion::motion {

    Light::Light(Id id, std::string name, LightKind kind)
        : id(id), name(std::move(name)), kind_(kind) {}

    float Light::sampleIntensity(float seconds) const {
        return intensityTrack.empty() ? intensity : intensityTrack.sample(seconds);
    }

    Color Light::sampleColor(float seconds) const {
        return colorTrack.empty() ? color : colorTrack.sample(seconds);
    }

    Transform Light::sampleTransform(float seconds) const {
        Transform result = transform;
        if (!positionTrack.empty()) result.position = positionTrack.sample(seconds);
        if (!rotationTrack.empty()) result.rotation = rotationTrack.sample(seconds);
        return result;
    }

    LightSample Light::sample(float seconds) const {
        LightSample result;
        result.kind = kind_;
        result.id = id;
        result.parentId = parentId;
        result.local = sampleTransform(seconds);
        result.color = sampleColor(seconds);
        result.intensity = sampleIntensity(seconds);
        result.castsShadow = castsShadow;
        result.shadowBias = shadowBiasTrack.empty() ? shadowBias : shadowBiasTrack.sample(seconds);
        result.shadowNormalBias = shadowNormalBiasTrack.empty() ? shadowNormalBias : shadowNormalBiasTrack.sample(seconds);
        result.shadowRadius = shadowRadiusTrack.empty() ? shadowRadius : shadowRadiusTrack.sample(seconds);
        if (!directionTrack.empty()) {
            result.direction = directionTrack.sample(seconds);
        } else {
            result.direction.set(0.f, 0.f, -1.f).transformDirection(result.local.toMatrix());
        }
        return result;
    }

    AmbientLight::AmbientLight(Id id, std::string name)
        : Light(id, std::move(name), LightKind::Ambient) {}

    DirectionalLight::DirectionalLight(Id id, std::string name)
        : Light(id, std::move(name), LightKind::Directional) {}

    LightSample DirectionalLight::sample(float seconds) const {
        LightSample result = Light::sample(seconds);
        result.targetId = targetId;
        return result;
    }

    PointLight::PointLight(Id id, std::string name)
        : Light(id, std::move(name), LightKind::Point) {}

    LightSample PointLight::sample(float seconds) const {
        LightSample result = Light::sample(seconds);
        result.range = rangeTrack.empty() ? range : rangeTrack.sample(seconds);
        result.decay = decayTrack.empty() ? decay : decayTrack.sample(seconds);
        return result;
    }

    SpotLight::SpotLight(Id id, std::string name)
        : Light(id, std::move(name), LightKind::Spot) {}

    LightSample SpotLight::sample(float seconds) const {
        LightSample result = Light::sample(seconds);
        result.targetId = targetId;
        result.range = rangeTrack.empty() ? range : rangeTrack.sample(seconds);
        result.angle = angleTrack.empty() ? angle : angleTrack.sample(seconds);
        result.penumbra = penumbraTrack.empty() ? penumbra : penumbraTrack.sample(seconds);
        result.decay = decayTrack.empty() ? decay : decayTrack.sample(seconds);
        return result;
    }

}// namespace chrononmotion::motion
