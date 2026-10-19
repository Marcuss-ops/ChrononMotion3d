#include "chrononmotion/motion/Procedural.hpp"

#include <cmath>
#include <limits>

namespace chrononmotion::motion {

    float proceduralNoise(float time, std::uint32_t seed) {
        // Seeded value noise with a quintic fade.  Unlike the old sin/floor
        // hash, both the value and its first two derivatives meet at lattice
        // boundaries, so random access stays deterministic without introducing
        // a visible velocity/acceleration pop once per noise cell.
        if (!std::isfinite(time)) return 0.f;
        const double x = static_cast<double>(time);
        const double cell = std::floor(x);
        if (cell < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
            cell >= static_cast<double>(std::numeric_limits<std::int32_t>::max())) return 0.f;
        const auto lattice = [&](std::int32_t index) {
            std::uint32_t value = static_cast<std::uint32_t>(index) ^ seed;
            value += 0x9e3779b9u;
            value = (value ^ (value >> 16u)) * 0x85ebca6bu;
            value = (value ^ (value >> 13u)) * 0xc2b2ae35u;
            value ^= value >> 16u;
            return static_cast<double>(value) / 2147483647.5 - 1.0;
        };
        const auto left = static_cast<std::int32_t>(cell);
        const double u = x - cell;
        const double fade = u * u * u * (u * (u * 6.0 - 15.0) + 10.0);
        return static_cast<float>(lattice(left) + (lattice(left + 1) - lattice(left)) * fade);
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
                // Unit-step response of m*x'' + c*x' + k*x = k, with
                // x(0)=x'(0)=0.  Use double intermediates to keep long offline
                // timelines stable and make the result independent of fps.
                const double mass = 1.0;
                const double k = std::max(0.0, static_cast<double>(spec.stiffness));
                const double c = std::max(0.0, static_cast<double>(spec.damping));
                const double td = static_cast<double>(t);
                double response = 0.0;
                if (k > 0.0) {
                    const double alpha = c / (2.0 * mass);
                    const double omega0 = std::sqrt(k / mass);
                    if (alpha < omega0) {
                        const double omegaD = std::sqrt(omega0 * omega0 - alpha * alpha);
                        response = 1.0 - std::exp(-alpha * td) *
                            (std::cos(omegaD * td) + (alpha / omegaD) * std::sin(omegaD * td));
                    } else if (alpha == omega0) {
                        response = 1.0 - std::exp(-alpha * td) * (1.0 + alpha * td);
                    } else {
                        const double root = std::sqrt(alpha * alpha - omega0 * omega0);
                        const double r1 = -alpha + root;
                        const double r2 = -alpha - root;
                        response = 1.0 + (r2 * std::exp(r1 * td) - r1 * std::exp(r2 * td)) / (r1 - r2);
                    }
                }
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
