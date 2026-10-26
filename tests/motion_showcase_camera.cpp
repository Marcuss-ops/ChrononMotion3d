#include "motion_showcase_shared.hpp"

namespace chrononmotion_test::showcase {
void addCameraCases(std::vector<ShowcaseCase>& cases) {
    const auto add = [&](unsigned number, const char* id, const char* description, std::function<void(Checks&, Snapshot&)> run) {
        cases.push_back({number, id, description, {}, std::move(run)});
    };
    add(41, "camera_dolly_parallax", "camera move has depth-dependent projected parallax", [](Checks& c, Snapshot& s) {
        CameraRig rig(1, 55.f, 16.f / 9.f);
        rig.setPosition({0.f, 0.f, 12.f}).setTarget({0.f, 0.f, 0.f});
        presets::cameraPush(rig, 0, 60, 4.f, kFps);
        auto camera = rig.makeCamera();
        rig.apply(0.f, *camera);
        PerspectiveView view(*camera, {1920.f, 1080.f});
        presets::cameraPan(rig, 0, 60, {4.f, 0.f}, kFps);
        const Vector3 startPosition = rig.sample(0.f).position;
        Vector2 nearShift, farShift;
        for (const float time : {0.f, 1.f, 2.f}) {
            const auto pose = rig.sample(time);
            const Vector3 cameraDelta = pose.position - startPosition;
            nearShift = view.parallaxShift(cameraDelta, 4.f);
            farShift = view.parallaxShift(cameraDelta, 20.f);
            if (time > 0.f)
                c.that(std::fabs(nearShift.x) > std::fabs(farShift.x) && nearShift.x < 0.f,
                       "near plane receives a larger opposite-direction shift from the authored camera pan");
            s.vector(pose.position); s.number(pose.fov);
        }
        s.vector(nearShift); s.vector(farShift);
    });

    add(42, "camera_orbit", "camera orbits the look-at target at constant radius", [](Checks& c, Snapshot& s) {
        CameraRig rig(1);
        rig.setPosition({0.f, 0.f, 12.f}).setTarget({0.f, 0.f, 0.f});
        presets::cameraOrbit(rig, 0, 60, kPi, .3f, kFps);
        for (int frame = 0; frame <= 60; frame += 5) {
            const CameraPose pose = rig.sample(secondsAt(frame));
            c.near(pose.position.distanceTo(pose.target), 12.f, 2e-3f, "orbit holds the camera-target radius");
            s.vector(pose.position);
        }
    });

    add(43, "camera_whip", "overshooting pan settles exactly at its destination", [](Checks& c, Snapshot& s) {
        CameraRig rig(1);
        rig.positionTrack().add(0.f, {}, Easing::easeOut()).add(.8f, {12.f, 0.f, 0.f})
            .add(1.f, {10.f, 0.f, 0.f});
        float peak = -std::numeric_limits<float>::max();
        for (int sample = 0; sample <= 100; ++sample) {
            const float time = static_cast<float>(sample) / 100.f;
            peak = std::max(peak, rig.sample(time).position.x);
            if (sample % 10 == 0) s.vector(rig.sample(time).position);
        }
        c.that(peak > 10.f, "whip overshoots its destination before settling");
        c.near(rig.sample(1.f).position, Vector3(10.f, 0.f, 0.f), 1e-5f, "whip lands exactly on its destination");
    });

    add(44, "dolly_zoom", "camera distance and FOV preserve apparent subject size", [](Checks& c, Snapshot& s) {
        CameraRig rig(1, 60.f, 16.f / 9.f);
        rig.setPosition({0.f, 0.f, 10.f}).setTarget({0.f, 0.f, 0.f});
        const float startTangent = std::tan(math::degToRad(60.f) * .5f);
        for (int frame = 0; frame <= 100; ++frame) {
            const float time = static_cast<float>(frame) / 100.f;
            const float distance = 10.f + 10.f * time;
            const float fov = 2.f * std::atan(10.f * startTangent / distance);
            rig.positionTrack().add(time, {0.f, 0.f, distance});
            rig.fovTrack().add(time, math::radToDeg(fov));
        }
        const auto apparent = [&](float time) {
            const auto pose = rig.sample(time);
            const float distance = pose.position.distanceTo(pose.target);
            return 1.f / (distance * std::tan(math::degToRad(pose.fov) * .5f));
        };
        for (int frame = 0; frame <= 100; frame += 10) {
            const float time = static_cast<float>(frame) / 100.f;
            c.near(apparent(time), apparent(0.f), 3e-4f,
                   "dolly zoom holds normalized subject size through the move");
            s.number(apparent(time));
        }
    });

    add(45, "focus_pull", "camera focus-distance channel pulls between two subject depths", [](Checks& c, Snapshot& s) {
        CameraRig rig(1);
        rig.setFStop(2.8f).setFocusDistance(12.f);
        rig.focusDistanceTrack().add(0.f, 12.f, Easing::easeInOut()).add(2.f, 4.f);
        c.near(rig.sample(0.f).focusDistance, 12.f, 1e-5f, "focus starts at the far subject");
        c.near(rig.sample(2.f).focusDistance, 4.f, 1e-5f, "focus ends at the near subject");
        c.that(rig.sample(1.f).focusDistance < 12.f && rig.sample(1.f).focusDistance > 4.f,
               "focus plane moves continuously between subjects");
        s.number(rig.sample(0.f).focusDistance); s.number(rig.sample(1.f).focusDistance);
        s.number(rig.sample(2.f).focusDistance); s.number(rig.sample(1.f).fStop);
    });

    add(46, "depth_cascade", "thirty cards arrive from depth with staggered offsets", [](Checks& c, Snapshot& s) {
        MotionScene scene("46_depth_cascade", kFps);
        for (std::uint32_t i = 0; i < 30; ++i) {
            Layer& card = addCard(scene, i + 1, "depth_card_" + std::to_string(i), {static_cast<float>(i), 0.f, 0.f});
            presets::pushInZ(card, static_cast<int>(i) * 2, 30, -40.f, kFps);
        }
        c.that(scene.findLayer(1)->tracks.position.startTime() < scene.findLayer(30)->tracks.position.startTime(),
               "depth cards enter in deterministic sequence");
        for (std::uint32_t i = 0; i < 30; ++i) {
            const Layer* card = scene.findLayer(i + 1);
            const float start = secondsAt(static_cast<int>(i) * 2);
            c.near(card->tracks.position.startTime(), start, 1e-6f,
                   "each depth card receives its exact cascade offset");
            c.near(card->tracks.position.sample(start).z, -40.f, 1e-5f,
                   "each depth card starts at the authored depth offset");
            c.near(card->tracks.position.sample(start + 1.f).z, 0.f, 1e-5f,
                   "each depth card arrives at its own resting depth");
        }
        c.that(scene.findLayer(1)->tracks.position.sample(0.f).z != scene.findLayer(1)->transform.position.z,
               "first card begins offset in depth");
        capture(scene, s, {0.f, .5f, 1.f, 2.f, 3.f});
    });

    add(47, "perspective_deck", "nested card deck opens around animated pivots", [](Checks& c, Snapshot& s) {
        MotionScene scene("47_perspective_deck", kFps);
        Layer& root = scene.addNull(1, "deck_root");
        root.tracks.rotation.add(0.f, {}).add(1.f, {0.f, .3f, 0.f});
        for (std::uint32_t i = 0; i < 8; ++i) {
            Layer& card = addCard(scene, i + 2, "deck_card_" + std::to_string(i),
                                  {static_cast<float>(i) * 2.f, 0.f, -static_cast<float>(i)});
            card.parentId = 1;
            card.transform.anchor.set(-16.f, 0.f, 0.f);
            card.tracks.rotation.add(0.f, {}).add(1.f, {0.f, 0.f, (static_cast<float>(i) - 3.5f) * .08f});
        }
        c.that(scene.validate().empty(), "deck parent graph is valid");
        scene.evaluatePtsExact(0.f);
        const Matrix4 closedPose = scene.worldMatrix(5);
        scene.evaluatePtsExact(1.f);
        c.that(scene.state(5).evaluated && !scene.worldMatrix(5).equals(closedPose),
               "animated root and card pivot change the child world pose");
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(48, "time_reverse", "time remap reads a source animation in reverse", [](Checks& c, Snapshot& s) {
        MotionScene scene("48_time_reverse", 30.f);
        Layer& layer = addCard(scene, 1, "reversed_clip");
        layer.tracks.position.add(0.f, {}).add(1.f, {30.f, 0.f, 0.f});
        layer.timeRemap.offsetFrames = 30.f;
        layer.timeRemap.reverse = true;
        scene.evaluate(0); c.near(worldPosition(scene, 1).x, 30.f, 1e-5f, "reverse starts at the source end");
        scene.evaluate(15); c.near(worldPosition(scene, 1).x, 15.f, 1e-5f, "reverse samples the source midpoint");
        scene.evaluate(30); c.near(worldPosition(scene, 1).x, 0.f, 1e-5f, "reverse ends at the source start");
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f});
    });

    add(49, "freeze_and_ramp", "remap accelerates, freezes, then ramps onward", [](Checks& c, Snapshot& s) {
        MotionScene scene("49_freeze_and_ramp", 10.f);
        Layer& layer = addCard(scene, 1, "remapped_clip");
        layer.tracks.position.add(0.f, {}).add(3.f, {30.f, 0.f, 0.f});
        layer.timeRemapTrack.add(0.f, 0.f).add(1.f, 10.f).add(2.f, 10.f).add(3.f, 30.f);
        const std::array<float, 7> expected{{0.f, 5.f, 10.f, 10.f, 10.f, 20.f, 30.f}};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const float time = static_cast<float>(i) * .5f;
            scene.evaluatePtsExact(time);
            c.near(worldPosition(scene, 1).x, expected[i], 1e-5f, "freeze/ramp remap resolves its authored value");
            s.number(worldPosition(scene, 1).x);
        }
        capture(scene, s, {0.f, .5f, 1.f, 1.5f, 2.f, 2.5f, 3.f});
    });

    add(50, "posterize_motion", "continuous output samples a four-Hz held motion channel", [](Checks& c, Snapshot& s) {
        Layer layer(1, "posterized");
        layer.tracks.position.add(0.f, {}).add(1.f, {24.f, 0.f, 0.f}).posterize(4.f);
        c.near(layer.sample(.26f).position.x, 6.f, 1e-5f, "posterize holds the prior 0.25-second value");
        c.near(layer.sample(.49f).position.x, 6.f, 1e-5f, "posterized channel remains held within the interval");
        c.near(layer.sample(.51f).position.x, 12.f, 1e-5f, "next posterized sample advances at its boundary");
        for (int i = 0; i <= 12; ++i) s.number(layer.sample(static_cast<float>(i) / 24.f).position.x);
    });
}
} // namespace chrononmotion_test::showcase
