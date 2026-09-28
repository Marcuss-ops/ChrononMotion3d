#include "chrononmotion/motion/Procedural.hpp"

#include <cmath>

namespace chrononmotion::motion {

    float proceduralNoise(float time, std::uint32_t seed) {
        const float value = std::sin(time * 12.9898f + static_cast<float>(seed) * 78.233f) * 43758.5453f;
        return (value - std::floor(value)) * 2.f - 1.f;
    }

    Vector3 evaluateDriverOffset(const DriverSpec& spec, float seconds,
                                 const Vector3& base, const Vector3& target) {
        if (!spec.active(seconds)) return Vector3{};
        const float t = seconds - spec.startTime;
        const float envelope = spec.endTime < spec.startTime
            ? 1.f : std::clamp((spec.endTime - seconds) / std::max(1e-6f, spec.endTime - spec.startTime), 0.f, 1.f);
        const float omega = 2.f * 3.14159265358979323846f * spec.frequency;
        Vector3 out;
        switch (spec.kind) {
            case DriverKind::Wiggle:
                out.set(proceduralNoise(t * spec.frequency, spec.seed),
                        proceduralNoise(t * spec.frequency + 17.f, spec.seed + 1),
                        proceduralNoise(t * spec.frequency + 31.f, spec.seed + 2));
                out.multiply(spec.axis).multiplyScalar(spec.amplitude * envelope);
                return out;
            case DriverKind::Shake:
                out.set(std::sin(omega * t + spec.phase),
                        std::sin(omega * 1.37f * t + spec.phase + 1.f),
                        std::sin(omega * 1.73f * t + spec.phase + 2.f));
                out.multiply(spec.axis).multiplyScalar(spec.amplitude * envelope);
                return out;
            case DriverKind::Float:
                out.set(0.f, std::sin(omega * t + spec.phase) * spec.amplitude * envelope, 0.f);
                return out;
            case DriverKind::Pulse: {
                const float pulseVal = (0.5f + 0.5f * std::sin(omega * t + spec.phase)) * spec.amplitude * envelope;
                out.set(pulseVal, pulseVal, pulseVal);
                out.multiply(spec.axis);
                return out;
            }
            case DriverKind::Orbit:
                out.set(std::cos(omega * t + spec.phase) * spec.radius,
                        0.f,
                        std::sin(omega * t + spec.phase) * spec.radius);
                out.sub(base).add(spec.center);
                return out;
            case DriverKind::Spring: {
                const float decay = std::exp(-spec.damping * t);
                const float response = 1.f - decay * std::cos(std::sqrt(std::max(0.f, spec.stiffness)) * t);
                out.subVectors(target, base).multiplyScalar(response * spec.amplitude);
                return out;
            }
            case DriverKind::DampedFollow:
                out.subVectors(target, base).multiplyScalar(
                    (1.f - std::exp(-std::max(0.f, spec.damping) * t)) * spec.amplitude);
                return out;
        }
        return Vector3{};
    }

}// namespace chrononmotion::motion
