#include "chrononmotion/motion/Lighting.hpp"

namespace chrononmotion::motion {

    Light::Light(Id id, std::string name, LightKind kind)
        : id(id), name(std::move(name)), kind_(kind) {}

    float Light::sampleIntensity(float seconds) const {
        return intensityTrack.empty() ? intensity : intensityTrack.sample(seconds);
    }

    LightSample Light::sample(float seconds) const {
        LightSample result;
        result.kind = kind_;
        result.id = id;
        result.parentId = parentId;
        result.local = transform;
        result.color = color;
        result.intensity = sampleIntensity(seconds);
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
        result.range = range;
        result.decay = decay;
        return result;
    }

    SpotLight::SpotLight(Id id, std::string name)
        : Light(id, std::move(name), LightKind::Spot) {}

    LightSample SpotLight::sample(float seconds) const {
        LightSample result = Light::sample(seconds);
        result.targetId = targetId;
        result.range = range;
        result.angle = angle;
        result.penumbra = penumbra;
        result.decay = decay;
        return result;
    }

}// namespace chrononmotion::motion
