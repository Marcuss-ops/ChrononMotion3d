#include "motion_presets_shared.hpp"

namespace chrononmotion_test::preset_tests {

void cameraPresets() {

        section("camera presets");

        const auto makeRig = [](const Vector3& position) {
            CameraRig rig(20, 60.f, 16.f / 9.f, 0.1f, 2000.f);
            rig.setPosition(position);
            rig.setTarget(Vector3(0.f, 0.f, 0.f));
            return rig;
        };

        {
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraPush(rig, 0, 30, 5.f, kFps);
            checkNear(rig.sample(at(0)).position.distanceTo(Vector3(0.f, 0.f, 0.f)), 12.f, 1e-3f,
                      "cameraPush starts at the authored distance");
            checkNear(rig.sample(at(30)).position.distanceTo(Vector3(0.f, 0.f, 0.f)), 7.f, 1e-3f,
                      "cameraPush closes in by the given distance");
        }
        {
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraPull(rig, 0, 30, 5.f, kFps);
            checkNear(rig.sample(at(30)).position.distanceTo(Vector3(0.f, 0.f, 0.f)), 17.f, 1e-3f,
                      "cameraPull opens out by the given distance");
        }
        {
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraPan(rig, 0, 30, Vector2(3.f, -2.f), kFps);
            checkNear(rig.sample(at(0)).position, Vector3(0.f, 0.f, 12.f), 1e-3f, "cameraPan starts at rest");
            checkNear(rig.sample(at(30)).position, Vector3(3.f, -2.f, 12.f), 1e-3f,
                      "cameraPan translates in the camera's own plane");
        }
        {
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraOrbit(rig, 0, 30, math::PI * 0.5f, 0.f, kFps);
            const CameraPose end = rig.sample(at(30));
            checkNear(end.position, Vector3(12.f, 0.f, 0.f), 1e-2f, "cameraOrbit swings around the target");
            checkNear(end.position.distanceTo(end.target), 12.f, 1e-2f,
                      "cameraOrbit keeps the distance to the target");
        }
        {
            // The 2.5D orbit: yaw swings the camera around the target while pitch
            // lifts it, which is what gives a flat plate a sense of volume. The
            // target stays welded to the frame throughout: only the eye moves.
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            const float yaw = 20.f * math::PI / 180.f;
            const float pitch = 35.f * math::PI / 180.f;
            presets::cameraOrbit(rig, 0, 30, yaw, pitch, kFps);
            const CameraPose mid = rig.sample(at(15));
            const CameraPose end = rig.sample(at(30));
            check(rig.sample(at(0)).position.distanceTo(Vector3(0.f, 0.f, 12.f)) < 1e-3f,
                  "the 2.5D orbit starts on the authored pose");
            check(end.position.y > 0.1f, "the 2.5D orbit pitches the camera up off the plane");
            check(mid.position.distanceTo(end.position) > 0.5f,
                  "the 2.5D orbit is still turning through the middle of the move");
            checkNear(end.position.distanceTo(end.target), 12.f, 1e-2f,
                      "the 2.5D orbit holds its distance while it pitches and yaws");
            checkNear(end.target.distanceTo(Vector3(0.f, 0.f, 0.f)), 0.f, 1e-3f,
                      "the 2.5D orbit never lets the target drift off its anchor");
        }
        {
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraShake(rig, 0, 30, 0.5f, kFps);
            checkNear(rig.sample(at(0)).position, Vector3(0.f, 0.f, 12.f), 1e-4f,
                      "cameraShake starts on the base pose, with no pop");
            checkNear(rig.sample(at(30)).position, Vector3(0.f, 0.f, 12.f), 1e-4f,
                      "cameraShake resolves exactly back to the base pose");
            check(rig.sample(at(4)).position.x != 0.f, "cameraShake actually displaces the camera");

            for (int frame = 0; frame <= 30; ++frame) {
                const Vector3 p = rig.sample(at(frame)).position;
                check(std::fabs(p.x) <= 0.5f + 1e-4f && std::fabs(p.y) <= 0.5f + 1e-4f,
                      "cameraShake stays within its amplitude");
            }

            // Deterministic: a second rig shaken identically shakes identically.
            CameraRig twin = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraShake(twin, 0, 30, 0.5f, kFps);
            bool identical = true;
            for (int frame = 0; frame <= 30; ++frame) {
                if (rig.sample(at(frame)).position != twin.sample(at(frame)).position) identical = false;
            }
            check(identical, "cameraShake is deterministic, not random");
        }
        {
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraFov(rig, 0, 30, 50.f, 20.f, kFps);
            checkNear(rig.sample(at(0)).fov, 50.f, 1e-3f, "cameraFov starts at the given fov");
            checkNear(rig.sample(at(30)).fov, 20.f, 1e-3f, "cameraFov ends at the given fov");
            check(rig.sample(at(15)).fov < 50.f && rig.sample(at(15)).fov > 20.f,
                  "cameraFov interpolates between them");
        }
        {
            CameraRig rig = makeRig(Vector3(0.f, 0.f, 12.f));
            presets::cameraPush(rig, 0, 30, 5.f, kFps);
            presets::cameraShake(rig, 40, 20, 0.3f, kFps);
            checkNear(rig.endTime(), at(60), 1e-3f, "the rig reports the end of every authored channel");
        }
    }

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

} // namespace chrononmotion_test::preset_tests

