#include "motion_presets_internal.hpp"
namespace motion_presets_test {


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


    void environmentAndBackgroundPresets() {

        section("environment and background presets");

        // 1. frustumCoverage and setupBackgroundPlane
        {
            const Vector2 coverage = presets::frustumCoverage(60.f, 16.f / 9.f, 100.f, 1.25f);
            check(coverage.x > 0.f && coverage.y > 0.f, "coverage returns positive dimensions");
            checkNear(coverage.x / coverage.y, 16.f / 9.f, 1e-4f, "coverage preserves aspect ratio");

            Layer bg(1, "Background");
            presets::setupBackgroundPlane(bg, -100.f, 60.f, 16.f / 9.f, 1.25f);
            checkNear(bg.transform.position.z, -100.f, 1e-5f, "setupBackgroundPlane sets depth");
            checkNear(bg.transform.scale.x, coverage.x, 1e-4f, "setupBackgroundPlane sets scale X");
            checkNear(bg.transform.scale.y, coverage.y, 1e-4f, "setupBackgroundPlane sets scale Y");
        }

        // 2. horizonGrid and groundTravel
        {
            Layer floor(2, "FloorGrid");
            presets::horizonGrid(floor, -20.f, -50.f, 85.f);
            checkNear(floor.transform.position.y, -20.f, 1e-5f, "horizonGrid sets elevation");
            checkNear(floor.transform.rotation.x, math::degToRad(85.f), 1e-4f, "horizonGrid tilts about X");

            presets::groundTravel(floor, 0, 30, 40.f, kFps);
            checkNear(floor.sample(at(0)).position.z, -50.f, 1e-4f, "groundTravel starts at rest Z");
            checkNear(floor.sample(at(30)).position.z, -10.f, 1e-4f, "groundTravel moves along Z by distance");
        }

        // 3. ambientDriftField
        {
            Layer p1(3, "Particle1"), p2(4, "Particle2");
            p1.transform.position.set(0.f, 0.f, -10.f);
            p2.transform.position.set(5.f, -2.f, -20.f);
            std::vector<Layer*> particles{&p1, &p2};

            presets::ambientDriftField(particles, 0, 30, kFps, 12345, 10.f, 1.f);
            const Vector3 p1_t0 = p1.sample(at(0)).position;
            const Vector3 p1_t15 = p1.sample(at(15)).position;
            const Vector3 p2_t15 = p2.sample(at(15)).position;
            check(p1_t0.distanceTo(p1_t15) > 0.05f, "ambient drift moves particle over time");
            check(p1_t15.distanceTo(p2_t15) > 0.05f, "particles drift independently based on phase");
        }

        // 4. auraBlobMotion
        {
            Layer blob(5, "AuraBlob");
            blob.transform.position.set(0.f, 0.f, 0.f);
            presets::auraBlobMotion(blob, 0, 60, Vector3(0.f, 0.f, -5.f), Vector2(100.f, 50.f), 1.f, kFps, 99);
            const Vector3 b_t0 = blob.sample(at(0)).position;
            const Vector3 b_t30 = blob.sample(at(30)).position;
            check(b_t0.distanceTo(b_t30) > 1.f, "aura blob moves along organic path");
        }

        // 5. autoFocus
        {
            CameraRig rig(6, 60.f, 16.f / 9.f, 0.1f, 2000.f);
            rig.setPosition(Vector3(0.f, 0.f, 10.f));

            Layer subject(7, "Subject");
            subject.transform.position.set(0.f, 0.f, 0.f);
            subject.tracks.position.add(at(0), Vector3(0.f, 0.f, 0.f), Easing::linear());
            subject.tracks.position.add(at(30), Vector3(0.f, 0.f, 4.f), Easing::linear());

            presets::autoFocus(rig, subject, 0, 30, kFps);
            const CameraPose pose0 = rig.sample(at(0));
            const CameraPose pose30 = rig.sample(at(30));
            checkNear(pose0.focusDistance, 10.f, 1e-3f, "autoFocus tracks camera-to-subject distance at t0");
            checkNear(pose30.focusDistance, 6.f, 1e-3f, "autoFocus tracks camera-to-subject distance at t1");
        }
    }
}
