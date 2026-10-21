#include "chrononmotion/motion/CameraSequence.hpp"

#include <cmath>
#include <stdexcept>

int main() try {
    using namespace chrononmotion;
    using namespace chrononmotion::motion;
    using namespace chrononmotion::motion::camera;

    if (cameraHandoffs().size() != 8 ||
        cameraHandoffId(Handoff::WhipReframe) != "scene_camera_whip_reframe")
        throw std::runtime_error("CM3D must own the stable camera handoff catalog");

    CameraSequence sequence;
    sequence.handoff = Handoff::OrbitHandoff;
    sequence.travelFrames = 24;
    sequence.beats = {
        {SequenceSubject{{0.f, 0.f, 0.f}, 240.f, 140.f, 50.f}, 30},
        {SequenceSubject{{400.f, 30.f, 0.f}, 220.f, 130.f, 45.f}, 30}
    };

    CameraRig rig;
    const Vector2 viewport{1920.f, 1080.f};
    applySequence(rig, sequence, 30.f, viewport);

    const CameraPose firstRest = rig.sample(0.f);
    const CameraPose secondRest = rig.sample(54.f / 30.f);
    if (!firstRest.hasTarget || std::abs(firstRest.target.x) > 1e-4f ||
        !secondRest.hasTarget || std::abs(secondRest.target.x - 400.f) > 1e-3f ||
        std::abs(secondRest.fov - 45.f) > 1e-4f)
        throw std::runtime_error("CM3D camera sequence must author both shot rest poses");

    const CameraPose randomAccess = rig.sample(2.5f);
    (void)rig.sample(0.4f);
    const CameraPose repeated = rig.sample(2.5f);
    if (randomAccess.position.distanceTo(repeated.position) > 1e-6f ||
        randomAccess.target.distanceTo(repeated.target) > 1e-6f ||
        std::abs(randomAccess.fov - repeated.fov) > 1e-6f)
        throw std::runtime_error("CM3D camera sequences must support deterministic random access");

    return 0;
} catch (const std::exception&) {
    return 1;
}
