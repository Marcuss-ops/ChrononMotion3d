#include "chrononmotion/motion/Procedural.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {
    void check(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }

    bool near(float a, float b, float epsilon = 1e-6f) {
        return std::abs(a - b) <= epsilon;
    }
}

int main() {
    DriverSpec spring;
    spring.kind = DriverKind::Spring;
    spring.stiffness = 180.f;
    spring.damping = 12.f;
    spring.amplitude = 1.f;
    const Vector3 base(0.f, 0.f, 0.f);
    const Vector3 target(1.f, 0.f, 0.f);

    check(near(evaluateDriverOffset(spring, 0.f, base, target).x, 0.f),
          "spring begins at its initial position");
    const float peak = evaluateDriverOffset(spring, 0.262f, base, target).x;
    check(peak > 1.19f && peak < 1.23f,
          "underdamped unit-step spring has the analytic overshoot for k=180, c=12");
    check(std::abs(evaluateDriverOffset(spring, 2.f, base, target).x - 1.f) < 1e-4f,
          "spring settles to its target");
    check(near(evaluateDriverOffset(spring, 0.25f, base, target).x,
               evaluateDriverOffset(spring, 15.f / 60.f, base, target).x),
          "spring samples depend on absolute time, not frame rate");

    // Random access and sequential access invoke the same absolute-time
    // function; compare the whole transformed point at the final sample.
    Vector3 sequential;
    for (int frame = 0; frame <= 150; ++frame)
        sequential = base + evaluateDriverOffset(spring, frame / 30.f, base, target);
    const Vector3 jumped = base + evaluateDriverOffset(spring, 150.f / 30.f, base, target);
    check(sequential.distanceTo(jumped) == 0.f,
          "out-of-order frame sampling is bit-identical to sequential sampling");

    DriverSpec wiggle;
    wiggle.kind = DriverKind::Wiggle;
    wiggle.seed = 1729;
    wiggle.frequency = 0.5f;
    wiggle.amplitude = 100.f;
    wiggle.axis.set(1.f, 0.f, 0.f);
    constexpr float dt = 1.f / 60.f;
    float previous = evaluateDriverOffset(wiggle, 0.f, base).x;
    float current = evaluateDriverOffset(wiggle, dt, base).x;
    float previousAcceleration = 0.f;
    double sum = current;
    constexpr int samples = 6000;
    for (int i = 2; i < samples; ++i) {
        const float next = evaluateDriverOffset(wiggle, i * dt, base).x;
        const float acceleration = (next - 2.f * current + previous) / (dt * dt);
        if (i > 2)
            check(std::abs(acceleration - previousAcceleration) < 40.f,
                  "wiggle acceleration changes smoothly without lattice-boundary spikes");
        previousAcceleration = acceleration;
        previous = current;
        current = next;
        sum += next;
    }
    check(std::abs(sum / samples) < 2.f,
          "seeded wiggle has no long-term positional drift");
    check(near(proceduralNoise(12.375f, 1729), proceduralNoise(12.375f, 1729), 0.f),
          "noise sampling is deterministic at arbitrary times");

    std::cout << "Procedural motion certification passed\n";
    return 0;
}
