#include "chrononmotion/motion/CameraShake.hpp"
#include "chrononmotion/math/MathUtils.hpp"

#include <cmath>
#include <stdexcept>

int main() {
    using namespace chrononmotion::motion::camera;
    auto spec = handheldShake();
    const auto a = sampleCameraShake(spec, 30.f, 120.f);
    const auto b = sampleCameraShake(spec, 30.f, 120.f);
    if (a.position.x != b.position.x || a.rotationRadians.x != b.rotationRadians.x)
        throw std::runtime_error("camera shake must be deterministic for random-access sampling");
    if (!(std::fabs(a.position.x) <= spec.positionAmplitude) ||
        !(std::fabs(a.rotationRadians.x) <= chrononmotion::math::degToRad(spec.rotationAmplitudeDegrees)))
        throw std::runtime_error("camera shake output must stay within its configured bounds");
    const auto explosion = explosionShake();
    const auto ended = sampleCameraShake(explosion, 20.f, 20.f);
    if (ended.position.x != 0.f || ended.zoom != 0.f)
        throw std::runtime_error("shake fade-out must reach zero at the duration boundary");
    if (earthquakeShake().positionAmplitude <= handheldShake().positionAmplitude)
        throw std::runtime_error("shake presets must retain distinct intensities");
    return 0;
}
