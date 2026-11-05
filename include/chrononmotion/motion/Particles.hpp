// ChrononMotion — particle authoring data.
//
// This is the Motion-side shape of a particle emitter: timing and placement
// live here, while Chronon owns the renderer-facing ParticleEmitterParams.
// Keeping this header free of Chronon3D includes preserves the authoring-only
// boundary: the optional RenderPlan lowering adapter performs the conversion.

#ifndef CHRONONMOTION_MOTION_PARTICLES_HPP
#define CHRONONMOTION_MOTION_PARTICLES_HPP

#include "chrononmotion/math/Vector2.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace chrononmotion::motion {

    enum class ParticleEmitterShape : std::uint8_t {
        Box,
        Ellipse,
        Edge,
        Path
    };

    enum class ParticleBlendMode : std::uint8_t {
        Normal,
        Add,
        Screen
    };

    /// One stop of the particle colour ramp. Alpha is explicit because the
    /// Motion Color type is RGB-only; a particle fade is usually alpha-driven.
    struct ParticleColorStop {
        float position{0.f};
        float r{1.f};
        float g{1.f};
        float b{1.f};
        float a{1.f};
    };

    /// Authored particle emitter. The fields intentionally mirror Chronon3D's
    /// renderer contract without including it; RenderPlanLowering maps them.
    struct ParticleEmitter {
        std::uint32_t count{0};
        std::uint32_t seed{0};
        Vector2 emitterSize{0.f, 0.f};
        Vector2 velocityMin{0.f, 0.f};
        Vector2 velocityMax{0.f, 0.f};
        float lifetimeMin{1.f};
        float lifetimeMax{1.f};
        float sizeMin{1.f};
        float sizeMax{1.f};
        float gravity{0.f};
        float turbulence{0.f};
        ParticleEmitterShape shape{ParticleEmitterShape::Box};
        std::string pathId{};
        std::vector<Vector2> pathPoints{};
        std::string spriteAsset{};
        float spriteScaleMin{1.f};
        float spriteScaleMax{1.f};
        float rotationMin{0.f};
        float rotationMax{0.f};
        float angularVelocityMin{0.f};
        float angularVelocityMax{0.f};
        ParticleBlendMode blendMode{ParticleBlendMode::Normal};
        bool environment{false};
        float depthMin{0.f};
        float depthMax{1.f};
        bool bokeh{false};
        std::vector<ParticleColorStop> colorRamp{};
    };

    /// Validate the authoring-side contract before it reaches the RenderPlan
    /// adapter. The renderer repeats the checks at its own boundary.
    [[nodiscard]] inline std::string particleEmitterProblem(const ParticleEmitter& emitter) {
        const auto finite2 = [](Vector2 value) {
            return std::isfinite(value.x) && std::isfinite(value.y);
        };
        if (emitter.count > 1'000'000U)
            return "particle count exceeds the 1,000,000 sampler limit";
        if (!finite2(emitter.emitterSize) || !finite2(emitter.velocityMin) ||
            !finite2(emitter.velocityMax))
            return "particle emitter size and velocity must be finite";
        if (emitter.emitterSize.x < 0.f || emitter.emitterSize.y < 0.f)
            return "particle emitter size must be non-negative";
        if (!std::isfinite(emitter.lifetimeMin) || !std::isfinite(emitter.lifetimeMax) ||
            emitter.lifetimeMin <= 0.f || emitter.lifetimeMax < emitter.lifetimeMin)
            return "particle lifetime must be finite and ordered";
        if (!std::isfinite(emitter.sizeMin) || !std::isfinite(emitter.sizeMax) ||
            emitter.sizeMin < 0.f || emitter.sizeMax < emitter.sizeMin)
            return "particle size must be finite, non-negative and ordered";
        if (!std::isfinite(emitter.gravity) || !std::isfinite(emitter.turbulence) ||
            emitter.turbulence < 0.f)
            return "particle gravity/turbulence must be finite and turbulence non-negative";
        if (emitter.velocityMax.x < emitter.velocityMin.x ||
            emitter.velocityMax.y < emitter.velocityMin.y)
            return "particle velocity range must be ordered";
        if (emitter.shape == ParticleEmitterShape::Path &&
            (emitter.pathId.empty() || emitter.pathPoints.size() < 2))
            return "path emitters need a path id and at least two points";
        for (const auto point : emitter.pathPoints)
            if (!finite2(point)) return "particle path points must be finite";
        if (!std::isfinite(emitter.spriteScaleMin) || !std::isfinite(emitter.spriteScaleMax) ||
            emitter.spriteScaleMin < 0.f || emitter.spriteScaleMax < emitter.spriteScaleMin)
            return "particle sprite scale must be finite, non-negative and ordered";
        if (!std::isfinite(emitter.rotationMin) || !std::isfinite(emitter.rotationMax) ||
            emitter.rotationMax < emitter.rotationMin)
            return "particle rotation range must be finite and ordered";
        if (!std::isfinite(emitter.angularVelocityMin) || !std::isfinite(emitter.angularVelocityMax) ||
            emitter.angularVelocityMax < emitter.angularVelocityMin)
            return "particle angular velocity range must be finite and ordered";
        if (emitter.environment && emitter.count > 4096U)
            return "environment particle count exceeds the 4,096 particle budget";
        if (!std::isfinite(emitter.depthMin) || !std::isfinite(emitter.depthMax) ||
            emitter.depthMin < 0.f || emitter.depthMax > 1.f ||
            emitter.depthMax < emitter.depthMin)
            return "particle depth range must be finite and inside [0, 1]";
        for (const auto& stop : emitter.colorRamp) {
            if (!std::isfinite(stop.position) || stop.position < 0.f || stop.position > 1.f)
                return "particle colour stop position must be finite and inside [0, 1]";
            if (!std::isfinite(stop.r) || !std::isfinite(stop.g) ||
                !std::isfinite(stop.b) || !std::isfinite(stop.a))
                return "particle colour stops must be finite";
        }
        return {};
    }

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_PARTICLES_HPP
