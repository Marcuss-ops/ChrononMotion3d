// ChrononMotion — tool-builder interaction tests: anchor picking, exact
// gizmo deltas, and the deterministic scrub session.

#include "motion_check.hpp"

#include "chrononmotion/motion/Motion.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

    using namespace chrononmotion;
    using namespace chrononmotion::motion;
    using chrononmotion_test::check;
    using chrononmotion_test::checkNear;

    ScreenCamera testCamera() {
        CameraRig rig(20, 60.f, 16.f / 9.f);
        rig.setPosition(Vector3(0.f, 0.f, 14.f));
        rig.setTarget(Vector3(0.f, 0.f, 0.f));
        ScreenCamera camera;
        camera.pose = rig.sample(0.f);
        camera.intrinsics.viewport = Vector2(1920.f, 1080.f);
        camera.intrinsics.fovDegrees = camera.pose.fov;
        return camera;
    }

    bool throwsInvalidPick(const MotionScene& scene, const ScreenCamera& camera, const Vector2& pixel) {
        try {
            (void)pickLayer(scene, camera, pixel);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    }

    void picking() {
        chrononmotion_test::section("anchor picking");
        MotionScene scene("pick", 30.f);
        // The root sits on the same view ray behind the pick target: both
        // anchors project to the cursor, and the nearer depth must win.
        Layer& root = scene.addNull(1, "Root");
        root.transform.position = Vector3(0.f, 0.f, -2.f);
        Layer& near = scene.addNull(2, "Near", 1);
        near.transform.position = Vector3(0.f, 0.f, 2.f);
        Layer& far = scene.addNull(3, "Far", 1);
        far.transform.position = Vector3(0.f, 0.f, -6.f);
        (void)far;
        Layer& brief = scene.addNull(4, "Brief", 1);
        brief.alive(0, 10);
        brief.transform.position = Vector3(30.f, 0.f, 0.f);
        Layer& behind = scene.addNull(5, "Behind", 1);
        behind.transform.position = Vector3(0.f, 0.f, 30.f);
        (void)behind;

        const ScreenCamera camera = testCamera();
        scene.evaluate(0);

        // Pixels derive from evaluated world state, never from authoring
        // guesses: the query and the engine read the same matrices.
        const auto anchorOf = [&](Layer::Id id) {
            const Matrix4& world = scene.worldMatrix(id);
            return Vector3(world.elements[12], world.elements[13], world.elements[14]);
        };
        const Vector2 center = projectToScreen(anchorOf(2), camera.pose, camera.intrinsics);
        const PickHit hit = pickLayer(scene, camera, center);
        check(hit.hit && hit.id == 2, "cursor on the anchor picks the layer");
        check(hit.screenDistancePx < 1e-3f, "anchor distance is ~zero on its pixel");

        const PickHit miss = pickLayer(scene, camera, Vector2(4.f, 4.f), 2.f);
        check(!miss.hit, "empty corner misses with a tight radius");

        // The brief layer is alive at frame 0: its pixel picks it up.
        const Vector2 briefPx = projectToScreen(anchorOf(4), camera.pose, camera.intrinsics);
        check(pickLayer(scene, camera, briefPx).id == 4, "live layer picks at its pixel");
        scene.evaluate(100);
        check(pickLayer(scene, camera, briefPx).id != 4, "expired layer no longer picks");

        // Behind the camera never picks, however close the mirrored
        // projection lands.
        const Vector2 behindPx = projectToScreen(Vector3(0.f, 0.f, 30.f), camera.pose, camera.intrinsics);
        const PickHit noBehind = pickLayer(scene, camera, behindPx, 4000.f);
        check(!noBehind.hit || noBehind.id != 5, "behind-camera layer never picks");

        MotionScene fresh("fresh", 30.f);
        fresh.addNull(1, "A");
        check(throwsInvalidPick(fresh, camera, center), "pick on an unevaluated scene fails closed");
    }

    void gizmo() {
        chrononmotion_test::section("gizmo deltas");
        const ScreenCamera camera = testCamera();
        const Vector3 anchor(2.f, -1.f, 0.f);
        const float depth = viewDepth(anchor, camera.pose);
        check(depth > 0.f, "test anchor is in front of the camera");
        const Vector2 pixel = projectToScreen(anchor, camera.pose, camera.intrinsics);

        // A drag delta lands the projected point exactly on the cursor.
        const Vector2 target(pixel.x + 100.f, pixel.y - 50.f);
        const Vector2 delta(target.x - pixel.x, target.y - pixel.y);
        const Vector3 moved = dragWorldDelta(camera, depth, delta);
        const Vector2 landed = projectToScreen(
            Vector3(anchor.x + moved.x, anchor.y + moved.y, anchor.z + moved.z), camera.pose,
            camera.intrinsics);
        checkNear(landed, target, 1e-2f, "translate drag lands on the cursor");

        // A quarter arc reads as a quarter turn.
        const Vector2 pivot(500.f, 500.f);
        const float quarter = dragViewRotation(camera, pivot, Vector2(700.f, 500.f), Vector2(500.f, 300.f));
        checkNear(quarter, 3.14159265358979323846f / 2.f, 1e-5f, "quarter arc reads as a quarter turn");

        // Radial drags scale by the radius ratio.
        checkNear(dragScaleFactor(pivot, Vector2(510.f, 500.f), Vector2(530.f, 500.f)), 3.f, 1e-5f,
                  "radial drag scales by the radius ratio");

        bool depthRejected = false;
        try {
            (void)dragWorldDelta(camera, -2.f, delta);
        } catch (const std::invalid_argument&) {
            depthRejected = true;
        }
        check(depthRejected, "drag behind the camera fails closed");

        bool pivotRejected = false;
        try {
            (void)dragViewRotation(camera, pivot, pivot, Vector2(600.f, 500.f));
        } catch (const std::invalid_argument&) {
            pivotRejected = true;
        }
        check(pivotRejected, "rotation at the pivot fails closed");

        bool zeroRejected = false;
        try {
            (void)dragScaleFactor(pivot, pivot, Vector2(600.f, 500.f));
        } catch (const std::invalid_argument&) {
            zeroRejected = true;
        }
        check(zeroRejected, "scale from the pivot fails closed");
    }

    void scrub() {
        chrononmotion_test::section("scrub session");
        MotionScene scene("scrub", 30.f);
        Layer& mover = scene.addNull(1, "Mover");
        mover.alive(0, 60);
        mover.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f));
        mover.tracks.position.add(2.f, Vector3(4.f, 0.f, 0.f));

        ScrubSession session(scene);
        check(session.frame() == 0, "cursor starts at frame zero");
        session.seek(30);
        check(session.frame() == 30, "seek moves the cursor");
        check(scene.lastEvaluatedFrame() == 30, "seek evaluates the scene");
        checkNear(scene.worldMatrix(1).elements[12], 2.f, 1e-4f, "seek lands on the keyed value");

        session.setLoop(5, 8);
        session.seek(8);
        session.step(1);
        check(session.frame() == 5, "step wraps inside the loop");
        session.step(-1);
        check(session.frame() == 8, "negative step wraps backwards");
        session.clearLoop();
        session.step(1000);
        check(session.frame() == session.endFrame(), "step clamps without a loop");

        session.seek(0);
        session.play();
        session.advance();
        check(session.frame() == 1 && session.playing(), "advance steps while playing");
        session.pause();
        session.advance();
        check(session.frame() == 1, "advance is a no-op while paused");

        bool loopRejected = false;
        try {
            session.setLoop(8, 5);
        } catch (const std::invalid_argument&) {
            loopRejected = true;
        }
        check(loopRejected, "inverted loop fails closed");

        bool seekRejected = false;
        try {
            session.seek(-1);
        } catch (const std::invalid_argument&) {
            seekRejected = true;
        }
        check(seekRejected, "negative seek fails closed");
    }

}// namespace

int main() {
    picking();
    gizmo();
    scrub();
    return chrononmotion_test::report();
}
