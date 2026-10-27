#include "motion_presets_internal.hpp"
namespace motion_presets_test {


    void namedPresetAliases() {

        section("named preset aliases");
        Layer layer(1, "Aliases");
        presets::flipX(layer, 0, 10, kFps);
        presets::flipY(layer, 12, 10, kFps);
        presets::arcEntrance(layer, Vector3(4.f, 0.f, 0.f), Vector3(2.f, 3.f, 0.f), 24, 12, kFps);
        checkNear(layer.sample(at(0)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                  "flipX is a stable named alias");
        checkNear(layer.sample(at(36)).position, Vector3(4.f, 0.f, 0.f), 1e-3f,
                  "arcEntrance is a stable named alias");

        CameraRig rig(30, 60.f, 16.f / 9.f, 0.1f, 2000.f);
        rig.setPosition(Vector3(0.f, 0.f, 12.f)).setTarget(Vector3(0.f, 0.f, 0.f));
        presets::orbitMild(rig, 0, 30, kFps);
        presets::cameraFovBreath(rig, 0, 30, 60.f, 54.f, kFps);
        presets::shakeMild(rig, 30, 15, 0.2f, kFps);
        presets::focusPush(rig, 50, 10, 1.f, kFps);
        check(rig.endTime() >= at(60), "named camera aliases compose on the same timeline");
    }


    void presetTimingUsesFrames() {

        section("presets are authored in frames");

        checkNear(frameToTime(30, 30.f), 1.f, 1e-6f, "frameToTime converts at the given fps");
        checkNear(frameToTime(45, 30.f), 1.5f, 1e-6f, "frameToTime converts a mid-timeline frame");

        // The same preset at a different fps must land on the same *frame*.
        Layer slow(1, "Slow");
        Layer fast(2, "Fast");
        presets::slideIn(slow, presets::Direction::Left, 0, 24, 4.f, 24.f);
        presets::slideIn(fast, presets::Direction::Left, 0, 24, 4.f, 24.f);
        checkNear(slow.sample(at(24)).position.x, fast.sample(at(24)).position.x, 1e-5f,
                  "a preset's end frame does not depend on the fps used to sample it");
    }


    void settleAndWavePresets() {

        section("settle, wave and secondary-motion presets");

        // spinIn: a half turn about Y lands exactly on the resting orientation.
        {
            Layer spinner(1, "Spinner");
            spinner.transform.orientation = aboutY(30.f);

            presets::spinIn(spinner, presets::Axis::Y, 0, 30, 0.5f, kFps);

            checkNear(spinner.sample(at(0)).orientation.angleTo(aboutY(30.f)), math::PI, 1e-2f,
                      "spinIn starts a half turn away from the resting orientation");
            checkNear(spinner.sample(at(30)).orientation.angleTo(aboutY(30.f)), 0.f, 1e-3f,
                      "spinIn lands exactly on the resting orientation");
        }

        // bounceIn: an offset entrance whose rebounds are the segment's easing.
        {
            Layer bouncer(2, "Bouncer");
            bouncer.transform.position.set(0.f, 5.f, 0.f);

            presets::bounceIn(bouncer, presets::Axis::Y, 0, 30, 2.f, kFps);

            checkNear(bouncer.sample(at(0)).position, Vector3(0.f, 7.f, 0.f), 1e-4f,
                      "bounceIn starts the offset away along the axis");
            checkNear(bouncer.sample(at(30)).position, Vector3(0.f, 5.f, 0.f), 1e-5f,
                      "bounceIn lands on the resting position");
            check(bouncer.sample(at(15)).position.y < 6.f,
                  "bounceIn is ahead of a linear move at the midpoint");
        }

        // springSettle: the same entrance, arriving with one soft overshoot.
        {
            Layer springy(3, "Springy");
            springy.transform.position.set(0.f, 0.f, 0.f);

            presets::springSettle(springy, presets::Axis::Y, 0, 30, 3.f, kFps);

            checkNear(springy.sample(at(0)).position, Vector3(0.f, 3.f, 0.f), 1e-4f,
                      "springSettle starts the amplitude away along the axis");
            checkNear(springy.sample(at(30)).position, Vector3(0.f, 0.f, 0.f), 1e-5f,
                      "springSettle lands on the resting position");

            float lowest = springy.sample(at(0)).position.y;
            for (int frame = 0; frame <= 30; ++frame) {
                lowest = std::min(lowest, springy.sample(at(frame)).position.y);
            }
            check(lowest < 0.f, "springSettle overshoots past the resting position before settling");
        }

        // pulseOpacity: the resting opacity is the top of every cycle.
        {
            Layer pumper(4, "Pumper");
            pumper.transform.opacity = 0.8f;

            presets::pulseOpacity(pumper, 0, 60, 3, kFps, 0.1f);

            checkNear(pumper.sample(at(0)).opacity, 0.8f, 1e-5f,
                      "pulseOpacity starts at the resting opacity");
            checkNear(pumper.sample(at(60)).opacity, 0.8f, 1e-5f,
                      "pulseOpacity ends at the resting opacity");

            bool dipsEveryCycle = true;
            for (int cycle = 0; cycle < 3; ++cycle) {
                const float mid = at(60) * ((static_cast<float>(cycle) + 0.5f) / 3.f);
                if (std::fabs(pumper.sample(mid).opacity - 0.1f) > 1e-4f) dipsEveryCycle = false;
            }
            check(dipsEveryCycle, "pulseOpacity dips to the floor once per cycle");
            check(pumper.sample(at(15)).opacity < 0.8f,
                  "pulseOpacity is below the top between cycles");
        }

        // rippleWave: one wave sampled across the group, so opposite lanes move in
        // opposite directions at the same instant.
        {
            Layer first(10, "First");
            Layer middle(11, "Middle");
            Layer third(12, "Third");
            Layer last(13, "Last");
            const std::vector<Layer*> group{&first, &middle, &third, &last};

            presets::rippleWave(group, presets::Direction::Up, 0, 30, 1.f, 2.f, kFps);

            // A quarter of the way into the wave: lanes 0 and 0.5 are out of phase.
            constexpr float sampleTime = 0.125f;
            const float lead = 2.f * std::sin(math::TWO_PI * sampleTime);
            checkNear(first.sample(sampleTime).position.y, lead, 1e-4f,
                      "rippleWave leads the first lane with the wave's own phase");
            checkNear(third.sample(sampleTime).position.y, -lead, 1e-4f,
                      "rippleWave is half a cycle behind at the middle lane");
            check(std::fabs(last.sample(sampleTime).position.y) <= 2.0001f,
                  "rippleWave keeps every lane within the travel distance");
            check(first.sample(sampleTime).position.x == 0.f,
                  "rippleWave travels only along the requested direction");
        }

        // followThrough: the follower is where the source was one lag earlier.
        {
            MotionScene scene("follow", kFps);

            Layer& source = scene.addNull(20, "Source");
            source.transform.position.set(0.f, 0.f, 0.f);
            source.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
            source.tracks.position.add(1.f, Vector3(10.f, 0.f, 0.f), Easing::linear());

            Layer& follower = scene.addNull(21, "Follower");
            follower.transform.position.set(100.f, 0.f, 0.f);

            presets::followThrough(follower, source, 0.5f);

            scene.evaluate(15);
            checkNear(scene.state(21).local.position.x, 100.f, 1e-3f,
                      "the follower has not moved while the source is inside its lag");

            scene.evaluate(30);
            checkNear(scene.state(21).local.position.x, 105.f, 1e-3f,
                      "the follower is where the source was one lag earlier");

            const float once = scene.state(21).local.position.x;
            scene.evaluate(30);
            checkNear(scene.state(21).local.position.x, once, 0.f,
                      "followThrough is a pure function of the frame");
        }
    }


    void documentaryPresets() {

        section("documentary map presets");

        // radarPing: an expanding ring that resets at each pulse boundary.
        {
            Layer ping(1, "Ping");
            ping.transform.scale.set(2.f, 2.f, 2.f);
            ping.transform.opacity = 0.9f;

            presets::radarPing(ping, 0, 60, 3, 0.2f, 1.0f, kFps);

            // One pulse is 60/3 frames; the ring is authored in the layer's own
            // scale, so the factors compose with the size it was handed in.
            const float ringEnd = at(20);
            checkNear(ping.sample(0.f).scale.x, 2.f * 0.2f, 1e-3f,
                      "radarPing starts the ring at fromScale");
            checkNear(ping.sample(0.f).opacity, 0.9f, 1e-4f,
                      "radarPing starts the ring at the layer's own opacity");
            check(ping.sample(ringEnd - 1e-3f).scale.x > 1.9f,
                  "radarPing expands the ring to toScale across one pulse");
            check(ping.sample(ringEnd - 1e-3f).opacity < 0.05f,
                  "radarPing fades the ring out across the pulse");
            check(ping.sample(ringEnd + 1e-3f).scale.x < 0.6f,
                  "radarPing resets the ring at the pulse boundary");
            check(ping.sample(ringEnd + 1e-3f).opacity > 0.85f,
                  "radarPing restarts the next ring opaque");
        }

        // areaReveal: the fill fades up while a slight settle closes the loop.
        {
            Layer area(2, "Area");
            presets::areaReveal(area, 0, 30, kFps);

            checkNear(area.sample(at(0)).opacity, 0.f, 1e-4f, "areaReveal starts transparent");
            checkNear(area.sample(at(30)).opacity, 1.f, 1e-4f,
                      "areaReveal lands on the layer's own opacity");
            checkNear(area.sample(at(0)).scale.x, 1.04f, 1e-3f, "areaReveal opens slightly larger");
            checkNear(area.sample(at(30)).scale.x, 1.f, 1e-3f, "areaReveal settles onto the resting scale");

            float lowest = area.sample(at(0)).scale.x;
            for (int frame = 0; frame <= 30; ++frame)
                lowest = std::min(lowest, area.sample(at(frame)).scale.x);
            check(lowest < 1.f, "areaReveal overshoots past the resting scale before it settles");
        }

        // extrudeRise: the block lifts off the plane and lands on its height.
        {
            Layer block(3, "Block");
            presets::extrudeRise(block, 0, 30, 5.f, kFps);

            checkNear(block.sample(at(0)).position.z, 0.f, 1e-4f, "extrudeRise starts flat on the plane");
            checkNear(block.sample(at(0)).opacity, 0.f, 1e-4f, "extrudeRise starts transparent");
            checkNear(block.sample(at(30)).position.z, 5.f, 1e-4f,
                      "extrudeRise lands exactly on its height");
            checkNear(block.sample(at(30)).opacity, 1.f, 1e-4f, "extrudeRise lands opaque");

            float peak = 0.f;
            for (int frame = 0; frame <= 30; ++frame)
                peak = std::max(peak, block.sample(at(frame)).position.z);
            check(peak > 5.f, "extrudeRise overshoots its height before settling");
            checkNear(block.sample(at(15)).position.x, 0.f, 1e-4f,
                      "extrudeRise lifts without shearing off the plane");
        }

        // lineDraw: uncovered along its own width, keeping its depth.
        {
            Layer rule(4, "Rule");
            rule.transform.scale.set(4.f, 1.f, 0.5f);
            presets::lineDraw(rule, 0, 30, kFps);

            check(rule.sample(at(0)).scale.x > 0.f && rule.sample(at(0)).scale.x < 0.01f,
                  "lineDraw starts from a hairline, not a singular zero scale");
            checkNear(rule.sample(at(0)).opacity, 0.f, 1e-4f, "lineDraw starts transparent");
            checkNear(rule.sample(at(30)).scale.x, 4.f, 1e-4f, "lineDraw reaches the resting width");
            checkNear(rule.sample(at(30)).scale.y, 1.f, 1e-4f, "lineDraw leaves the height alone");
            checkNear(rule.sample(at(30)).scale.z, 0.5f, 1e-4f, "lineDraw leaves the depth alone");
        }

        // crosshairLock: every segment collapses from its outward offset onto its
        // own resting place, and lands exactly there.
        {
            Layer top(10, "Top"), right(11, "Right"), bottom(12, "Bottom"), left(13, "Left");
            top.transform.position.set(0.f, 2.f, 0.f);
            right.transform.position.set(2.f, 0.f, 0.f);
            bottom.transform.position.set(0.f, -2.f, 0.f);
            left.transform.position.set(-2.f, 0.f, 0.f);
            const std::vector<Layer*> segments{&top, &right, &bottom, &left};
            const std::vector<Vector3> outward{Vector3(0.f, 3.f, 0.f), Vector3(3.f, 0.f, 0.f),
                                               Vector3(0.f, -3.f, 0.f), Vector3(-3.f, 0.f, 0.f)};

            presets::crosshairLock(segments, outward, 0, 30, kFps);

            checkNear(top.sample(at(0)).position, Vector3(0.f, 5.f, 0.f), 1e-4f,
                      "crosshairLock starts each segment at its outward offset");
            checkNear(top.sample(at(0)).opacity, 0.f, 1e-4f, "crosshairLock fades the segments in");
            checkNear(top.sample(at(30)).position, Vector3(0.f, 2.f, 0.f), 1e-5f,
                      "crosshairLock lands a segment exactly on its resting place");
            checkNear(left.sample(at(30)).position, Vector3(-2.f, 0.f, 0.f), 1e-5f,
                      "crosshairLock lands every segment, not just the first");

            float closest = top.sample(at(0)).position.y;
            for (int frame = 0; frame <= 30; ++frame)
                closest = std::min(closest, top.sample(at(frame)).position.y);
            check(closest < 2.f, "crosshairLock snaps past the point before it settles");
        }

        // orbitalSlamZoom: the camera arrives over the target on a dive curve.
        {
            CameraRig rig(20, 60.f, 16.f / 9.f, 0.1f, 2000.f);
            rig.setPosition(Vector3(0.f, 0.f, 12.f)).setTarget(Vector3(0.f, 0.f, 0.f));
            presets::orbitalSlamZoom(rig, 0, 30, 45.f, 10.f, 5.f, 50.f, 35.f, kFps);

            checkNear(rig.sample(at(0)).position.distanceTo(Vector3(0.f, 0.f, 0.f)), 12.f, 1e-3f,
                      "orbitalSlamZoom starts at the authored distance");
            checkNear(rig.sample(at(30)).position.distanceTo(Vector3(0.f, 0.f, 0.f)), 7.f, 1e-3f,
                      "orbitalSlamZoom closes in by the dolly distance");
            checkNear(rig.sample(at(0)).fov, 50.f, 1e-3f, "orbitalSlamZoom starts at the wide lens");
            checkNear(rig.sample(at(30)).fov, 35.f, 1e-3f, "orbitalSlamZoom compresses to the telephoto lens");
            // The dive, not a slide: more than half the travel is done by the middle.
            check(rig.sample(at(15)).position.distanceTo(Vector3(0.f, 0.f, 0.f)) < 9.4f,
                  "orbitalSlamZoom accelerates through the middle of the move");
            // The swing is a real orbit: the arrival is off the start axis.
            check(std::fabs(rig.sample(at(30)).position.x) > 1.f,
                  "orbitalSlamZoom orbits as it descends");
        }

        // cameraDrift: a slow orbit keyed at a fixed step, so it never freezes.
        {
            CameraRig rig(20, 60.f, 16.f / 9.f, 0.1f, 2000.f);
            rig.setPosition(Vector3(0.f, 0.f, 12.f)).setTarget(Vector3(0.f, 0.f, 0.f));
            presets::cameraDrift(rig, 0, 60, 6.f, 0.f, kFps);

            check(rig.positionTrack().size() > 2,
                  "cameraDrift is keyed at more than two points, so it does not freeze");
            checkNear(rig.sample(at(60)).position.distanceTo(Vector3(0.f, 0.f, 0.f)), 12.f, 1e-2f,
                      "cameraDrift keeps the orbit radius");
            // 6 deg/s over 2 s is a 12 degree yaw: x = 12 sin(12 deg), z on the axis.
            checkNear(rig.sample(at(60)).position.x, 12.f * std::sin(math::degToRad(12.f)), 1e-2f,
                      "cameraDrift swings at the requested rate");
            check(rig.sample(at(30)).position.x > 0.4f,
                  "cameraDrift is moving at the midpoint, not holding");
        }
    }
}
