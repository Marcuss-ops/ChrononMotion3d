// ChrononMotion acceptance — suspended dust and volumetric bokeh
// (six-mechanics Test 6).
//
// 350 particles live in a 3-D camera volume and integrate the curl of a
// vector potential, so the flow is divergence-free. Sprites grow into hollow
// bokeh rings away from the focal plane, fade to transparent at the lens and at
// the volume boundary, and wrap around the frustum edges only where they are
// already invisible.

#include "chrononmotion/chrononmotion.hpp"
#include "motion_check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace chrononmotion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr int kParticleCount = 350;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kFocusDepth = 400.f;
    constexpr float kBoxMinX = -300.f;
    constexpr float kBoxMaxX = 300.f;
    constexpr float kBoxMinY = -200.f;
    constexpr float kBoxMaxY = 200.f;
    constexpr float kBoxMinZ = 40.f;
    constexpr float kBoxMaxZ = 900.f;
    constexpr float kFadeMargin = 35.f;
    constexpr float kFlowScale = 900.f;
    constexpr float kFrameStep = 1.f / 60.f;
    constexpr int kFrames = 900;

    struct alignas(16) Particle {
        Vector3 position;
        float radiusPx;
        float opacity;
        float phase;
    };

    static_assert(sizeof(Particle) == 32, "SSBO particle stride is 32 bytes");
    static_assert(offsetof(Particle, position) == 0, "position occupies the first 12 bytes");
    static_assert(offsetof(Particle, radiusPx) == 12, "radius follows the vec3");
    static_assert(offsetof(Particle, opacity) == 16, "opacity follows the radius");
    static_assert(offsetof(Particle, phase) == 20, "phase follows the opacity");

    std::uint32_t mix(std::uint32_t value) {

        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float random01(std::uint32_t& state) {

        state = mix(state);
        return static_cast<float>(state & 0x00ffffffu) /
               static_cast<float>(0x01000000u);
    }

    std::vector<Particle> seedParticles() {

        std::vector<Particle> particles(static_cast<std::size_t>(kParticleCount));
        std::uint32_t state = 0x51ed270bu;
        for (Particle& particle : particles) {
            particle.position.set(kBoxMinX + (kBoxMaxX - kBoxMinX) * random01(state),
                                  kBoxMinY + (kBoxMaxY - kBoxMinY) * random01(state),
                                  kBoxMinZ + (kBoxMaxZ - kBoxMinZ) * random01(state));
            particle.radiusPx = 0.8f + 2.2f * random01(state);
            particle.opacity = 0.08f + 0.42f * random01(state);
            particle.phase = 2.f * kPi * random01(state);
        }
        return particles;
    }

    Vector3 potential(const Vector3& point) {

        return Vector3(std::sin(point.y * 0.011f + 1.3f) *
                               std::cos(point.z * 0.007f + 0.4f),
                       std::sin(point.z * 0.009f + 2.1f) *
                               std::cos(point.x * 0.008f + 1.1f),
                       std::sin(point.x * 0.010f + 0.7f) *
                               std::cos(point.y * 0.012f + 2.4f));
    }

    Vector3 curlVelocity(const Vector3& point) {

        constexpr float step = 0.5f;
        const Vector3 dx(step, 0.f, 0.f);
        const Vector3 dy(0.f, step, 0.f);
        const Vector3 dz(0.f, 0.f, step);
        const Vector3 px = (potential(point + dx) - potential(point - dx)) / (2.f * step);
        const Vector3 py = (potential(point + dy) - potential(point - dy)) / (2.f * step);
        const Vector3 pz = (potential(point + dz) - potential(point - dz)) / (2.f * step);

        return Vector3(pz.y - py.z, px.z - pz.x, py.x - px.y) * kFlowScale;
    }

    float divergenceAt(const Vector3& point) {

        constexpr float step = 0.5f;
        const Vector3 dx(step, 0.f, 0.f);
        const Vector3 dy(0.f, step, 0.f);
        const Vector3 dz(0.f, 0.f, step);
        const Vector3 vx = (curlVelocity(point + dx) - curlVelocity(point - dx)) / (2.f * step);
        const Vector3 vy = (curlVelocity(point + dy) - curlVelocity(point - dy)) / (2.f * step);
        const Vector3 vz = (curlVelocity(point + dz) - curlVelocity(point - dz)) / (2.f * step);
        return vx.x + vy.y + vz.z;
    }

    float edgeFade(const Vector3& point) {

        const float x = math::smoothstep(0.f, kFadeMargin,
                                         std::min(point.x - kBoxMinX, kBoxMaxX - point.x));
        const float y = math::smoothstep(0.f, kFadeMargin,
                                         std::min(point.y - kBoxMinY, kBoxMaxY - point.y));
        const float z = math::smoothstep(0.f, kFadeMargin,
                                         std::min(point.z - kBoxMinZ, kBoxMaxZ - point.z));
        return x * y * z;
    }

    float nearLensFade(float depth) {

        // Particles in front of Z=50 must not cover the subject with opaque discs.
        return math::smoothstep(50.f, 180.f, depth);
    }

    float effectiveOpacity(const Particle& particle) {

        return particle.opacity * edgeFade(particle.position) *
               nearLensFade(particle.position.z);
    }

    float circleOfConfusion(float depth) {

        if (std::fabs(depth - kFocusDepth) < 1e-4f) return 0.f;
        const float radius = 26.f * std::fabs(depth - kFocusDepth) /
                             (std::fabs(depth) + 100.f);
        return std::min(radius, 64.f);
    }

    float bokehProfile(float radialDistance, float radius) {

        if (radius <= 0.f || radialDistance > radius * 1.02f) return 0.f;
        const float rise = math::smoothstep(radius * 0.58f, radius * 0.94f, radialDistance);
        const float fall = 1.f - math::smoothstep(radius * 0.96f, radius * 1.02f,
                                                  radialDistance);
        return rise * fall;
    }

    bool wrap(Vector3& position) {

        bool wrapped = false;
        if (position.x < kBoxMinX) {
            position.x += kBoxMaxX - kBoxMinX;
            wrapped = true;
        } else if (position.x > kBoxMaxX) {
            position.x -= kBoxMaxX - kBoxMinX;
            wrapped = true;
        }
        if (position.y < kBoxMinY) {
            position.y += kBoxMaxY - kBoxMinY;
            wrapped = true;
        } else if (position.y > kBoxMaxY) {
            position.y -= kBoxMaxY - kBoxMinY;
            wrapped = true;
        }
        if (position.z < kBoxMinZ) {
            position.z += kBoxMaxZ - kBoxMinZ;
            wrapped = true;
        } else if (position.z > kBoxMaxZ) {
            position.z -= kBoxMaxZ - kBoxMinZ;
            wrapped = true;
        }
        return wrapped;
    }

    struct SimulationResult {
        std::vector<Particle> particles;
        int wraps{0};
        int invisibleWraps{0};
        float maxSpeed{0.f};
        float meanSpeed{0.f};
        float maxCocStep{0.f};
        float maxOpacityStep{0.f};
        double checksum{0.0};
    };

    SimulationResult simulate() {

        SimulationResult result;
        result.particles = seedParticles();

        double speedSum = 0.0;
        int speedSamples = 0;
        std::vector<float> previousCoc(result.particles.size(), 0.f);
        std::vector<float> previousOpacity(result.particles.size(), 0.f);
        for (std::size_t i = 0; i < result.particles.size(); ++i) {
            previousCoc[i] = circleOfConfusion(result.particles[i].position.z);
            previousOpacity[i] = effectiveOpacity(result.particles[i]);
        }

        for (int frame = 0; frame < kFrames; ++frame) {
            for (std::size_t i = 0; i < result.particles.size(); ++i) {
                Particle& particle = result.particles[i];
                const Vector3 velocity = curlVelocity(particle.position);
                const float speed = velocity.length();
                result.maxSpeed = std::max(result.maxSpeed, speed);
                speedSum += static_cast<double>(speed);
                ++speedSamples;

                const float beforeOpacity = effectiveOpacity(particle);
                particle.position += velocity * kFrameStep;
                const bool wrapped = wrap(particle.position);
                if (wrapped) {
                    ++result.wraps;
                    const float afterOpacity = effectiveOpacity(particle);
                    if (beforeOpacity < 0.03f && afterOpacity < 0.03f) {
                        ++result.invisibleWraps;
                    }
                }

                const float coc = circleOfConfusion(particle.position.z);
                const float opacity = effectiveOpacity(particle);
                // A wrap is a teleport of an already invisible particle; it is
                // not a focus/opacity pop and is excluded from the flicker gate.
                if (!wrapped) {
                    result.maxCocStep = std::max(result.maxCocStep,
                                                 std::fabs(coc - previousCoc[i]));
                    result.maxOpacityStep = std::max(result.maxOpacityStep,
                                                     std::fabs(opacity - previousOpacity[i]));
                }
                previousCoc[i] = coc;
                previousOpacity[i] = opacity;
            }
        }

        result.meanSpeed = static_cast<float>(speedSum / std::max(1, speedSamples));
        for (const Particle& particle : result.particles) {
            result.checksum += std::sin(static_cast<double>(particle.position.x) * 0.37) +
                               std::cos(static_cast<double>(particle.position.y) * 0.41) +
                               std::sin(static_cast<double>(particle.position.z) * 0.53);
        }
        return result;
    }

    // ------------------------------------------------------------------------

    void particleBufferLayout() {

        section("Test 6 — 350-particle SSBO layout");

        check(kParticleCount == 350, "the field carries exactly 350 particles");
        check(sizeof(Particle) == 32, "the particle stride is the aligned 32 bytes");
        check(alignof(Particle) == 16, "the particle buffer is 16-byte aligned");
        check(offsetof(Particle, position) == 0, "position starts at byte 0");
        check(offsetof(Particle, radiusPx) == 12, "radiusPx starts at byte 12");
        check(offsetof(Particle, opacity) == 16, "opacity starts at byte 16");
        check(offsetof(Particle, phase) == 20, "phase starts at byte 20");

        const std::vector<Particle> particles = seedParticles();
        check(particles.size() == 350, "the seeded buffer has 350 elements");
        bool bounded = true;
        bool distinctPhases = true;
        for (std::size_t i = 0; i < particles.size(); ++i) {
            const Particle& particle = particles[i];
            if (particle.position.x < kBoxMinX || particle.position.x > kBoxMaxX ||
                particle.position.y < kBoxMinY || particle.position.y > kBoxMaxY ||
                particle.position.z < kBoxMinZ || particle.position.z > kBoxMaxZ) {
                bounded = false;
            }
            if (i > 0 && std::fabs(particle.phase - particles[i - 1].phase) < 1e-6f) {
                distinctPhases = false;
            }
        }
        check(bounded, "seed particles start inside the camera volume");
        check(distinctPhases, "each particle carries its own time phase");
    }

    void curlFieldIsDivergenceFree() {

        section("Test 6 — curl-noise velocity is incompressible");

        float maximumDivergence = 0.f;
        double sumDivergence = 0.0;
        float maximumSpeed = 0.f;
        int samples = 0;

        for (int z = 0; z < 10; ++z) {
            for (int y = 0; y < 10; ++y) {
                for (int x = 0; x < 10; ++x) {
                    const Vector3 point(kBoxMinX + 20.f + (kBoxMaxX - kBoxMinX - 40.f) *
                                                               static_cast<float>(x) / 9.f,
                                        kBoxMinY + 20.f + (kBoxMaxY - kBoxMinY - 40.f) *
                                                               static_cast<float>(y) / 9.f,
                                        kBoxMinZ + 20.f + (kBoxMaxZ - kBoxMinZ - 40.f) *
                                                               static_cast<float>(z) / 9.f);
                    const float divergence = std::fabs(divergenceAt(point));
                    maximumDivergence = std::max(maximumDivergence, divergence);
                    sumDivergence += static_cast<double>(divergence);
                    maximumSpeed = std::max(maximumSpeed, curlVelocity(point).length());
                    ++samples;
                }
            }
        }

        check(samples == 1000, "the divergence probe covers the whole volume");
        check(maximumDivergence < 0.02f,
              "the curl field is divergence-free at every probe");
        check(sumDivergence / samples < 0.002,
              "the mean divergence is numerical noise");
        check(maximumSpeed > 2.f, "the flow carries a visible non-zero velocity");
        check(maximumSpeed < 60.f, "the flow remains a slow theatrical drift");
    }

    void motionIsSlowSmoothAndDeterministic() {

        section("Test 6 — slow fluid motion, no flicker, deterministic wrap");

        const SimulationResult first = simulate();
        const SimulationResult second = simulate();

        checkNear(first.meanSpeed, first.meanSpeed, 0.f, "mean speed is finite");
        check(first.meanSpeed > 0.5f && first.meanSpeed < 20.f,
              "the dust drifts slowly rather than racing or standing still");
        check(first.maxSpeed < 80.f, "the turbulence stays bounded");
        check(first.maxCocStep < 0.5f,
              "no particle changes bokeh size abruptly across a frame");
        check(first.maxOpacityStep < 0.04f,
              "no particle flickers in opacity as it crosses focus");
        check(first.wraps > 0, "the volume recycles particles across the frustum");
        check(first.invisibleWraps == first.wraps,
              "every wrap happens while the particle is transparent");
        checkNear(first.checksum, second.checksum, 0.0,
                  "the particle field is deterministic under identical stepping");

        bool allInside = true;
        for (const Particle& particle : first.particles) {
            allInside = allInside &&
                        particle.position.x >= kBoxMinX - 1e-3f &&
                        particle.position.x <= kBoxMaxX + 1e-3f &&
                        particle.position.y >= kBoxMinY - 1e-3f &&
                        particle.position.y <= kBoxMaxY + 1e-3f &&
                        particle.position.z >= kBoxMinZ - 1e-3f &&
                        particle.position.z <= kBoxMaxZ + 1e-3f;
        }
        check(allInside, "wrap-around keeps every particle inside the working volume");
    }

    void defocusAndBokehShape() {

        section("Test 6 — depth-dependent defocus and hollow bokeh rings");

        checkNear(circleOfConfusion(kFocusDepth), 0.f, 0.f,
                  "the nominal focus plane renders a sharp point");
        check(circleOfConfusion(kFocusDepth + 0.01f) < 0.01f,
              "defocus is continuous through the focus plane");
        check(circleOfConfusion(kFocusDepth - 0.01f) < 0.01f,
              "the focus plane has no flicker from either side");
        check(circleOfConfusion(80.f) > circleOfConfusion(300.f),
              "near-lens particles grow larger than mildly defocused ones");
        check(circleOfConfusion(900.f) > 5.f,
              "far background particles expand into bokeh as well");

        const float radius = circleOfConfusion(80.f);
        check(radius > 20.f, "a near-lens particle becomes a large bokeh disc");
        checkNear(bokehProfile(0.f, radius), 0.f, 1e-6f,
                  "the bokeh core is hollow");
        check(bokehProfile(radius * 0.94f, radius) > 0.9f,
              "the bokeh perimeter is the bright ring");
        checkNear(bokehProfile(radius * 1.1f, radius), 0.f, 1e-6f,
                  "nothing renders outside the bokeh radius");
        check(bokehProfile(radius * 0.5f, radius) < bokehProfile(radius * 0.9f, radius) * 0.4f,
              "the ring is brighter than the disc interior");

        // Very near particles are large but hidden by their near-lens opacity.
        int nearVisible = 0;
        int nearInvisible = 0;
        const std::vector<Particle> particles = seedParticles();
        for (const Particle& particle : particles) {
            if (particle.position.z < 50.f) {
                ++nearVisible;
                if (effectiveOpacity(particle) < 0.03f) ++nearInvisible;
            }
        }
        check(nearVisible > 0, "the seed includes particles inside the near-lens band");
        check(nearInvisible == nearVisible,
              "near-lens bokeh is transparent, never an opaque disc over the subject");
    }

}// namespace

int main() {

    particleBufferLayout();
    curlFieldIsDivergenceFree();
    motionIsSlowSmoothAndDeterministic();
    defocusAndBokehShape();

    return chrononmotion_test::report();
}
