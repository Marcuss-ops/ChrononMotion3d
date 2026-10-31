#include "CameraRigTrackBatch.hpp"
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

    // The shared applier writer must match direct Track::add for sampled
    // values, per-channel easing, and duplicate-time replacement.
    CameraRig batched(2), direct(2);
    CameraRigTrackBatch writes;
    const Easing smooth = Easing::easeInOut();
    writes.position(0.f, {0.f, 0.f, 1000.f}, smooth);
    writes.position(1.f, {100.f, 40.f, 900.f}, Easing::linear());
    writes.target(0.f, {0.f, 0.f, 0.f}, Easing::linear());
    writes.target(1.f, {10.f, 20.f, 30.f}, smooth);
    writes.fov(0.f, 50.f, smooth);
    writes.fov(1.f, 42.f, Easing::linear());
    writes.focusDistance(0.f, 800.f, smooth);
    writes.focusDistance(1.f, 1200.f, Easing::linear());
    writes.fStop(0.f, 2.8f, smooth);
    writes.fStop(1.f, 5.6f, Easing::linear());
    writes.fov(1.f, 44.f, Easing::linear()); // Track::add is last-write-wins.
    writes.apply(batched);
    direct.positionTrack().add(0.f, {0.f, 0.f, 1000.f}, smooth)
        .add(1.f, {100.f, 40.f, 900.f}, Easing::linear());
    direct.targetTrack().add(0.f, {0.f, 0.f, 0.f}, Easing::linear())
        .add(1.f, {10.f, 20.f, 30.f}, smooth);
    direct.fovTrack().add(0.f, 50.f, smooth)
        .add(1.f, 42.f, Easing::linear()).add(1.f, 44.f, Easing::linear());
    direct.focusDistanceTrack().add(0.f, 800.f, smooth).add(1.f, 1200.f, Easing::linear());
    direct.fStopTrack().add(0.f, 2.8f, smooth).add(1.f, 5.6f, Easing::linear());
    for (const float t : {0.f, 0.25f, 0.5f, 0.75f, 1.f}) {
        const CameraPose a = batched.sample(t), b = direct.sample(t);
        if (a.position.distanceTo(b.position) > 1e-6f ||
            a.target.distanceTo(b.target) > 1e-6f ||
            std::abs(a.fov - b.fov) > 1e-6f ||
            std::abs(a.focusDistance - b.focusDistance) > 1e-6f ||
            std::abs(a.fStop - b.fStop) > 1e-6f)
            throw std::runtime_error("shared CameraRig track writer must preserve direct track output");
    }

    CameraRig streamed(3), directStream(3);
    CameraRigTrackBatch streamedWrites(streamed);
    for (int i = 0; i < 5000; ++i) {
        const float t = static_cast<float>(i) / 60.f;
        const Vector3 value{static_cast<float>(i), 1.f, -static_cast<float>(i)};
        streamedWrites.position(t, value, Easing::linear());
        directStream.positionTrack().add(t, value, Easing::linear());
    }
    // Replace an already-flushed key to cover last-write-wins across chunks.
    streamedWrites.position(0.f, {7.f, 8.f, 9.f}, Easing::easeInOut());
    streamedWrites.apply(streamed);
    directStream.positionTrack().add(0.f, {7.f, 8.f, 9.f}, Easing::easeInOut());
    for (const float t : {0.f, 1.f, 10.f, 50.f, 4999.f / 60.f}) {
        if (streamed.sample(t).position.distanceTo(directStream.sample(t).position) > 1e-5f)
            throw std::runtime_error("streamed camera writes must preserve direct track output across flushes");
    }

    return 0;
} catch (const std::exception&) {
    return 1;
}
