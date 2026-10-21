#include "chrononmotion/motion/CameraTrajectory.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void check(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
}

int main() try {
    using namespace chrononmotion;
    using namespace chrononmotion::motion::camera;

    CameraTrajectoryBuilder line;
    auto path = line.moveTo({0.f, 0.f, 0.f})
        .moveTo({10.f, 0.f, 0.f}).durationFrames(30.f)
        .moveTo({20.f, 0.f, 0.f}).durationFrames(30.f)
        .build();
    check(path->size() == 2, "two authored legs are retained");
    check(near(path->sample(0.f).position.x, 0.f), "path starts at first point");
    check(near(path->sample(30.f).position.x, 10.f), "segment timing reaches its boundary");
    check(near(path->sample(60.f).position.x, 20.f), "path reaches its final point");
    check(path->sample(15.f).tangent.x > 0.99f, "linear path exposes a unit tangent");

    CameraTrajectoryBuilder bezier;
    auto curve = bezier.moveTo({0.f, 0.f, 0.f})
        .bezierTo({0.f, 8.f, 0.f}, {0.f, -8.f, 0.f}, {10.f, 0.f, 0.f})
        .durationFrames(60.f).arcLengthParameterized().build();
    const auto middle = curve->sample(30.f);
    check(near(curve->sample(0.f).position.x, 0.f) && near(curve->sample(60.f).position.x, 10.f),
          "arc-length remapping preserves Bézier endpoints");
    check(std::isfinite(middle.tangent.y) && middle.position.y > 0.f,
          "Bézier path provides finite tangent and curved position");
    const auto ahead = curve->sample(30.06f);
    check(middle.tangent.distanceTo(ahead.tangent) < 0.02f,
          "Bézier tangent stays consistent under sub-frame sampling");

    CameraTrajectoryBuilder spline;
    auto catmullPath = spline.moveTo({0.f, 0.f, 0.f})
        .moveTo({10.f, 5.f, 0.f}).durationFrames(20.f)
        .catmullRomTo({20.f, 0.f, 0.f}).durationFrames(20.f)
        .catmullRomTo({30.f, -5.f, 0.f}).durationFrames(20.f)
        .build();
    check(near(catmullPath->sample(20.f).position.x, 10.f) &&
          near(catmullPath->sample(40.f).position.x, 20.f),
          "Catmull-Rom path passes through every authored waypoint");

    CameraTrajectoryBuilder uniform;
    auto uniformPath = uniform.moveTo({0.f, 0.f, 0.f})
        .moveTo({10.f, 0.f, 0.f}).durationFrames(120.f)
        .bezierTo({0.f, 12.f, 0.f}, {0.f, -12.f, 0.f}, {30.f, 0.f, 0.f})
        .durationFrames(120.f).arcLengthParameterized().build();
    std::vector<float> steps;
    Vector3 prior = uniformPath->sample(120.f).position;
    for (int i = 1; i <= 24; ++i) {
        const Vector3 current = uniformPath->sample(120.f + 5.f * i).position;
        steps.push_back(current.distanceTo(prior));
        prior = current;
    }
    float mean = 0.f;
    for (float step : steps) mean += step;
    mean /= static_cast<float>(steps.size());
    float variance = 0.f;
    for (float step : steps) variance += (step - mean) * (step - mean);
    variance /= static_cast<float>(steps.size());
    check(std::sqrt(variance) / mean < 0.04f,
          "arc-length mode keeps near-uniform spacing within each segment");

    CameraTrajectoryBuilder hold;
    auto held = hold.moveTo({1.f, 2.f, 3.f}, Vector3{4.f, 5.f, 6.f}, 12.f)
        .holdFor(20.f).build();
    const auto heldSample = held->sample(10.f);
    check(near(heldSample.position.x, 1.f) && heldSample.tangent.lengthSq() == 0.f,
          "hold segment keeps position and has no tangent");
    check(heldSample.target && near(heldSample.target->x, 4.f) && near(heldSample.rollDegrees, 12.f),
          "hold segment preserves target and roll metadata");

    CameraTrajectoryBuilder zeroLength;
    auto degenerate = zeroLength.moveTo({2.f, 3.f, 4.f})
        .moveTo({2.f, 3.f, 4.f}).durationFrames(30.f)
        .arcLengthParameterized().build();
    const auto degenerateSample = degenerate->sample(12.5f);
    check(std::isfinite(degenerateSample.position.x) && std::isfinite(degenerateSample.tangent.x),
          "zero-length path segment remains finite in arc-length mode");

    chrononmotion::motion::CameraRig rig;
    CameraTrajectoryBuilder authored;
    auto cameraPath = authored.moveTo({0.f, 0.f, 10.f}, Vector3{0.f, 0.f, 0.f}, 0.f)
        .moveTo({10.f, 0.f, 10.f}, Vector3{0.f, 0.f, 0.f}, 90.f)
        .durationFrames(12.f).build();
    authorCameraTrajectory(rig, *cameraPath, 6, 24.f, 4);
    check(near(rig.sample(6.f / 24.f).position.x, 0.f) &&
          near(rig.sample(18.f / 24.f).position.x, 10.f),
          "CM3D authors path samples into CameraRig at the requested frame range");
    check(near(rig.sample(18.f / 24.f).roll, 1.5707963f, 1e-4f) &&
          near(rig.sample(18.f / 24.f).target.x, 0.f),
          "camera path authoring resolves roll units and target channels");

    std::cout << "Camera trajectory checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
