#ifndef CHRONONMOTION_MOTION_LIGHTING_HPP
#define CHRONONMOTION_MOTION_LIGHTING_HPP

#include "chrononmotion/math/Color.hpp"
#include "chrononmotion/motion/Layer.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace chrononmotion::motion {

    /// After Effects authors a spot's Cone Angle as the full aperture in
    /// degrees. The render-plan field is the half-angle in radians, so this is
    /// the one conversion an AE authoring adapter needs. Rejects values that
    /// would produce a degenerate or over-wide cone.
    [[nodiscard]] inline float aeSpotHalfAngleRadians(float totalDegrees) {
        if (!std::isfinite(totalDegrees) || totalDegrees <= 0.f || totalDegrees > 360.f) {
            throw std::invalid_argument("aeSpotHalfAngleRadians: cone angle must be finite and in (0, 360] degrees");
        }
        return totalDegrees * 0.5f * 3.14159265358979323846f / 180.f;
    }

    /// After Effects Cone Feather is a percentage of the cone's half-angle.
    /// The render-plan field is a normalized [0, 1] penumbra.
    [[nodiscard]] inline float aeSpotPenumbra(float featherPercent) {
        if (!std::isfinite(featherPercent) || featherPercent < 0.f || featherPercent > 100.f) {
            throw std::invalid_argument("aeSpotPenumbra: feather must be finite and in [0, 100] percent");
        }
        return featherPercent * 0.01f;
    }

    enum class LightKind {
        Ambient,
        Directional,
        Point,
        Spot
    };

    /// `SmoothInversePower` preserves the existing radius/decay curve.
    /// `AeInverseSquareClamped` is After Effects' Inverse Square Clamped:
    /// full strength inside `range`, then (range/d)^2.
    enum class LightFalloff {
        SmoothInversePower,
        AeInverseSquareClamped
    };

    struct LightSample {
        LightKind kind{LightKind::Ambient};
        Layer::Id id{0};
        Layer::Id parentId{Layer::kRoot};
        Layer::Id targetId{Layer::kRoot};
        Transform local{};
        Vector3 direction{0.f, 0.f, -1.f};
        Color color{};
        float intensity{1.f};
        float range{0.f};
        bool castsShadow{false};
        float shadowBias{0.f};
        float shadowNormalBias{0.f};
        float shadowRadius{1.f};
        float angle{0.f};
        float penumbra{0.f};
        float decay{1.f};
        LightFalloff falloff{LightFalloff::SmoothInversePower};
    };

    /// Renderer-independent light authoring state. It owns no Object3D and no
    /// shadow/render resources; a future renderer consumes LightSample/LightState.
    class Light {
    public:
        using Id = Layer::Id;

        Light(Id id, std::string name, LightKind kind);
        virtual ~Light() = default;

        Id id{0};
        std::string name{};
        Id parentId{Layer::kRoot};
        Color color{};
        float intensity{1.f};
        LightFalloff falloff{LightFalloff::SmoothInversePower};
        Transform transform{};
        Track<float> intensityTrack{};
        Track<Color> colorTrack{};
        Track<Vector3> positionTrack{};
        Track<Vector3> rotationTrack{};
        Track<Vector3> directionTrack{};
        bool castsShadow{false};
        float shadowBias{0.f};
        float shadowNormalBias{0.f};
        float shadowRadius{1.f};
        Track<float> shadowBiasTrack{};
        Track<float> shadowNormalBiasTrack{};
        Track<float> shadowRadiusTrack{};

        [[nodiscard]] Color sampleColor(float seconds) const;
        [[nodiscard]] Transform sampleTransform(float seconds) const;

        [[nodiscard]] LightKind kind() const { return kind_; }
        [[nodiscard]] float sampleIntensity(float seconds) const;
        [[nodiscard]] virtual LightSample sample(float seconds) const;

    protected:
        LightKind kind_;
    };

    class AmbientLight final: public Light {
    public:
        explicit AmbientLight(Id id, std::string name = "AmbientLight");
    };

    class DirectionalLight final: public Light {
    public:
        explicit DirectionalLight(Id id, std::string name = "DirectionalLight");

        [[nodiscard]] LightSample sample(float seconds) const override;
        Id targetId{Layer::kRoot};
    };

    class PointLight final: public Light {
    public:
        explicit PointLight(Id id, std::string name = "PointLight");

        [[nodiscard]] LightSample sample(float seconds) const override;
        float range{0.f};
        float decay{1.f};
        Track<float> rangeTrack{};
        Track<float> decayTrack{};
    };

    class SpotLight final: public Light {
    public:
        explicit SpotLight(Id id, std::string name = "SpotLight");

        [[nodiscard]] LightSample sample(float seconds) const override;

        /// Set the cone from AE's full Cone Angle in degrees.
        SpotLight& setAeConeAngleDegrees(float totalDegrees) {
            angle = aeSpotHalfAngleRadians(totalDegrees);
            return *this;
        }

        /// Set the penumbra from AE's Cone Feather percentage.
        SpotLight& setAeConeFeatherPercent(float featherPercent) {
            penumbra = aeSpotPenumbra(featherPercent);
            return *this;
        }

        Id targetId{Layer::kRoot};
        float range{0.f};
        float angle{0.7853981633974483f};
        float penumbra{0.f};
        float decay{1.f};
        Track<float> rangeTrack{};
        Track<float> angleTrack{};
        Track<float> penumbraTrack{};
        Track<float> decayTrack{};
    };

    struct LightState {
        LightSample sample{};
        Matrix4 world{};
        Vector3 position{};
        Vector3 direction{0.f, 0.f, -1.f};
        bool visible{true};
        bool evaluated{false};
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_LIGHTING_HPP
