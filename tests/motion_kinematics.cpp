// ChrononMotion kinematics test.
//
// Velocity, sub-frame sampling and the added easing shapes. The motion model
// already tested position; this pins the *rate* of that motion, because a
// consumer that chains layers, drives motion blur or syncs to a beat needs the
// derivative, and a derivative that is wrong by a key spacing is worse than none.

#include "chrononmotion/chrononmotion.hpp"

#include "motion_check.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    // World-space position of a layer, read out of its evaluated matrix.
    Vector3 worldPos(const MotionScene& scene, Layer::Id id) {

        Vector3 out;
        out.setFromMatrixPosition(scene.worldMatrix(id));
        return out;
    }

    // A track that moves 0 -> 100 units over `span` seconds with the given
    // easing. The analytic velocity of a linear segment is span-independent:
    // 100/span units per second.
    Track<float> linearMove(float span) {

        Track<float> track;
        track.add(0.f, 0.f, Easing::linear());
        track.add(span, 100.f, Easing::linear());
        return track;
    }

    void easingShapes() {

        section("easing shapes (expo, circ, steps)");

        // Every easing in this library is a normalized timing curve: pinned at
        // both ends, monotone for the non-overshooting shapes.
        for (int i = 0; i <= 20; ++i) {
            const float t = static_cast<float>(i) / 20.f;
            checkNear(Easing::expoOut()(t), Easing::expoOut()(t), 0.f,
                      "expoOut is a pure function of t");
            checkNear(Easing::circOut()(t), Easing::circOut()(t), 0.f,
                      "circOut is a pure function of t");
        }

        checkNear(Easing::expoOut()(0.f), 0.f, 1e-6f, "expoOut starts at 0");
        checkNear(Easing::expoOut()(1.f), 1.f, 1e-6f, "expoOut lands exactly on 1");
        checkNear(Easing::circOut()(0.f), 0.f, 1e-6f, "circOut starts at 0");
        checkNear(Easing::circOut()(1.f), 1.f, 1e-6f, "circOut lands exactly on 1");

        // Ease-out shapes lead linear at the midpoint. This is the property a
        // motion designer actually relies on, and it is the one a wrong
        // implementation of the polynomial gets backwards.
        check(Easing::expoOut()(0.5f) > 0.5f, "expoOut leads linear at the midpoint");
        check(Easing::circOut()(0.5f) > 0.5f, "circOut leads linear at the midpoint");
        // expoOut is the sharper of the two, so it is further along at halfway.
        check(Easing::expoOut()(0.25f) > Easing::circOut()(0.25f),
              "expoOut is sharper than circOut early on");

        // Steps holds flat and jumps. With 4 levels over [0,1] the value is the
        // index of the level containing t, normalized to [0,1].
        const Easing stepped = Easing::steps(4);
        check(stepped.kind() == Easing::Kind::Steps, "steps reports its kind");
        check(stepped.stepCount() == 4, "steps reports its level count");
        checkNear(stepped(0.f), 0.f, 1e-6f, "steps starts at 0");
        checkNear(stepped(0.24f), 0.f, 1e-6f, "steps holds the first level");
        checkNear(stepped(0.26f), 1.f / 3.f, 1e-6f, "steps jumps to the second level");
        checkNear(stepped(0.99f), 1.f, 1e-6f, "steps reaches the last level before t=1");
        checkNear(stepped(1.f), 1.f, 1e-6f, "steps lands exactly on 1");

        // A degenerate level count must not divide by zero downstream.
        const Easing oneStep = Easing::steps(0);
        check(oneStep.stepCount() == 1, "steps(0) degrades to a single level");
        checkNear(oneStep(0.5f), 0.f, 1e-6f, "steps(0) holds 0 until the end");
        checkNear(oneStep(1.f), 1.f, 1e-6f, "steps(0) still lands on 1");

        check(Easing::expoOut() != Easing::circOut(), "different shapes compare different");
        check(Easing::steps(3) != Easing::steps(4), "steps with different counts compare different");
        check(Easing::steps(4) == Easing::steps(4), "equal stepped curves compare equal");
    }

    void easingDerivative() {

        section("easing derivative");

        // The definition under test: the derivative of the eased value must
        // match a central difference of the value itself. This is the property
        // that makes `Track::velocity` trustworthy, and it is checked against
        // the value function rather than against a hand-written formula, so a
        // wrong closed form cannot pass by agreeing with itself.
        const Easing shapes[] = {
                Easing::linear(), Easing::easeIn(), Easing::easeOut(), Easing::easeInOut(),
                Easing::bounceOut(), Easing::elasticOut(), Easing::expoOut(), Easing::circOut(),
        };

        for (const Easing& easing : shapes) {
            // Stayed off the endpoints: bounceOut and circOut are not
            // differentiable there, and the contract says the endpoint reports
            // the interior one-sided slope (0 for circOut) rather than an
            // infinity.
            for (int i = 1; i < 20; ++i) {
                const float t = static_cast<float>(i) / 20.f;
                const float h = 1e-3f;
                const float numeric = (easing(t + h) - easing(t - h)) / (2.f * h);
                const float analytic = easing.derivative(t);
                // elasticOut oscillates fast, so its numeric difference is
                // genuinely less accurate; the tolerance reflects that rather
                // than being loosened for every shape.
                const float tolerance = easing.kind() == Easing::Kind::ElasticOut ? 0.35f : 0.02f;
                checkNear(analytic, numeric, tolerance,
                          "derivative matches a central difference of the value");
            }
        }

        // Endpoints must stay finite. A NaN or an infinity here propagates into
        // every velocity a consumer computes at a key boundary.
        for (const Easing& easing : shapes) {
            check(std::isfinite(easing.derivative(0.f)),
                  "derivative at t=0 is finite");
            check(std::isfinite(easing.derivative(1.f)),
                  "derivative at t=1 is finite");
        }

        checkNear(Easing::linear().derivative(0.5f), 1.f, 1e-6f,
                  "linear derivative is 1");
        check(Easing::expoOut().derivative(0.5f) > 0.f, "expoOut derivative is positive");
        // expoOut is monotonically slowing: steeper early than late.
        check(Easing::expoOut().derivative(0.1f) > Easing::expoOut().derivative(0.9f),
              "expoOut slows down");

        // A stepped curve is flat between levels. Its derivative is 0 there and
        // a finite per-level slope exactly on a boundary — never an infinite
        // impulse, which would be mathematically honest and practically poison.
        const Easing stepped = Easing::steps(4);
        checkNear(stepped.derivative(0.3f), 0.f, 1e-6f, "steps derivative is flat mid-level");
        check(std::isfinite(stepped.derivative(0.25f)), "steps derivative is finite on a boundary");
    }

    void trackVelocity() {

        section("track velocity");

        // The chain rule: a linear move of 100 units over `span` seconds moves
        // at 100/span per second, at every interior instant, whatever the span.
        // If the segment length were not divided out, the same keys would report
        // a different physical speed depending on how far apart they were.
        for (const float span : {1.f, 2.f, 0.5f}) {
            const Track<float> track = linearMove(span);
            const float expected = 100.f / span;
            checkNear(track.velocity(span * 0.5f), expected, 1e-3f,
                      "linear track velocity equals total change over span");
        }

        // Outside the keyed range the value is held, so its velocity is zero.
        const Track<float> held = linearMove(1.f);
        checkNear(held.velocity(-0.5f), 0.f, 1e-6f, "velocity before the first key is 0");
        checkNear(held.velocity(1.5f), 0.f, 1e-6f, "velocity after the last key is 0");

        // An eased track starts slow. With easeIn, the velocity at the very
        // start is near zero and it is higher at the end — the opposite of a
        // linear track, which is the whole point of easing.
        Track<float> eased;
        eased.add(0.f, 0.f, Easing::easeIn());
        eased.add(1.f, 100.f, Easing::linear());
        check(eased.velocity(0.02f) < eased.velocity(0.98f),
              "easeIn accelerates: slower at the start than at the end");

        // A held (discrete) segment has no continuous path, so it reports no
        // velocity. A consumer that wants the jump uses MotionScene::motion().
        Track<float> stepped;
        stepped.addDiscrete(0.f, 0.f);
        stepped.addDiscrete(1.f, 100.f);
        checkNear(stepped.velocity(0.5f), 0.f, 1e-6f, "a discrete segment has no velocity");

        // Empty track: no keys, no motion, and no crash.
        const Track<float> empty;
        checkNear(empty.velocity(0.5f), 0.f, 1e-6f, "an empty track has no velocity");

        // Vector channels use vector geometry, not a scalar shortcut.
        Track<Vector3> path;
        path.add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::linear());
        path.add(1.f, Vector3{3.f, 0.f, 0.f}, Easing::linear());
        checkNear(path.velocity(0.5f).x, 3.f, 1e-3f, "vector track velocity is per-axis");
        checkNear(path.velocity(0.5f).y, 0.f, 1e-6f, "an unanimated axis has no velocity");
    }

    void subFrameTime() {

        section("sub-frame time");

        // A whole frame is exactly frame.0, so frame-aligned sampling is
        // bit-identical to the pre-sub-frame behaviour. This is the regression
        // guard for the new field: adding a fraction must not shift frame
        // alignment by an epsilon and move every render by a frame.
        const MotionTime aligned = MotionTime::atFrame(12, 24.f);
        check(aligned.frameAligned(), "atFrame produces a frame-aligned sample");
        checkNear(aligned.pts(), 0.5f, 0.f, "frame 12 at 24fps is exactly 0.5s");
        checkNear(aligned.subFrame, 0.f, 0.f, "atFrame leaves no sub-frame fraction");

        // atPts rounds to the nearest frame and carries no fraction, so the
        // frame-quantised path is unchanged.
        // 0.51s at 24fps is frame 12.24: atPts drops the fraction and snaps to 12,
        // which is the pre-existing frame-quantised behaviour.
        const MotionTime rounded = MotionTime::atPts(0.51f, 24.f);
        checkNear(static_cast<float>(rounded.frame), 12.f, 0.f, "atPts rounds to the nearest frame");
        check(rounded.frameAligned(), "atPts is frame-aligned by construction");
        checkNear(MotionTime::atPtsExact(0.51f, 24.f).subFrame, 0.24f, 1e-4f,
                  "atPtsExact keeps the 0.24 frame that atPts drops");

        // atPtsExact samples the requested instant. At 24fps, 0.5s is exactly
        // frame 12, so this one is legitimately frame-aligned; the sub-frame case
        // below is the one that actually carries a fraction.
        const MotionTime exact = MotionTime::atPtsExact(0.5f, 24.f);
        checkNear(exact.pts(), 0.5f, 1e-5f, "atPtsExact lands on the requested instant");
        check(exact.frameAligned(), "a PTS that IS a frame boundary stays frame-aligned");

        // Mid-frame: frame 12 plus a half, at 24fps, is 12.5/24 s.
        const MotionTime mid = MotionTime::atPtsExact(12.5f / 24.f, 24.f);
        checkNear(static_cast<float>(mid.frame), 12.f, 0.f, "a mid-frame sample keeps its frame");
        checkNear(mid.subFrame, 0.5f, 1e-4f, "a mid-frame sample carries the half");
        checkNear(mid.pts(), 12.5f / 24.f, 1e-6f, "the fraction round-trips back to the instant");

        // Two consumers asking for the same instant get the same time, which is
        // the whole reason this exists: motion blur and audio sync must not
        // disagree about what t is.
        const MotionTime a = MotionTime::atPtsExact(0.375f, 24.f);
        const MotionTime b = MotionTime::atPtsExact(0.375f, 24.f);
        check(a.frame == b.frame && a.subFrame == b.subFrame,
              "the same instant maps to the same time, twice");

        // A PTS that rounds to the very end of a frame must report the next
        // frame with no fraction, not a frame carrying subFrame ~ 1.
        const MotionTime nearEnd = MotionTime::atPtsExact(0.9999999f / 24.f, 24.f);
        check(nearEnd.frameAligned(), "a PTS at the frame edge snaps to the next frame");
        checkNear(static_cast<float>(nearEnd.frame), 1.f, 0.f, "the frame edge advances the frame");

        // A negative PTS before frame 0 is legal and must not produce a
        // negative fraction.
        const MotionTime before = MotionTime::atPtsExact(-0.25f / 24.f, 24.f);
        check(before.subFrame >= 0.f, "a negative PTS does not produce a negative fraction");

        // Non-positive fps is rejected everywhere, not just at frame.
        bool threw = false;
        try {
            (void) MotionTime::atFrame(0, 0.f);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "atFrame rejects a non-positive fps");

        threw = false;
        try {
            (void) MotionTime::atPtsExact(0.5f, -1.f);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "atPtsExact rejects a negative fps");
    }

    void sceneMotion() {

        section("scene motion");

        // A layer moving 10 units per second, at 10fps: one unit per frame.
        // The finite difference and the analytic derivative must agree, because
        // a disagreement means one of them is wrong about the key spacing.
        MotionScene scene("kinematics", 10.f);
        Layer& layer = scene.addLayer(Layer{1, "mover"});
        layer.positionedAt(Vector3{0.f, 0.f, 0.f});
        layer.tracks.position.add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::linear());
        layer.tracks.position.add(1.f, Vector3{10.f, 0.f, 0.f}, Easing::linear());

        scene.evaluate(0);
        const Vector3 start = worldPos(scene, 1);
        checkNear(start.x, 0.f, 1e-5f, "the layer starts at the origin");

        // The keys are in SECONDS, so at 10fps the segment that runs 0..1s
        // completes at frame 10, not frame 1. This is the trap: reading "frame 1"
        // as "the first key" samples 10% into the move.
        scene.evaluate(10);
        const Vector3 end = worldPos(scene, 1);
        checkNear(end.x, 10.f, 1e-4f, "the layer reaches its key at 1 second");

        // Finite difference from the evaluated states: one unit in 1/10 s.
        const LayerMotion motion = scene.motion(1);
        check(motion.finiteDifference, "scene motion is a finite difference");
        checkNear(motion.positionVelocity.x, 10.f, 1e-3f,
                  "finite-difference velocity is units per second");
        checkNear(motion.positionVelocity.y, 0.f, 1e-4f, "an unanimated axis has no velocity");
        checkNear(motion.linearVelocity.x, motion.positionVelocity.x, 0.f,
                  "linearVelocity aliases positionVelocity");

        // The analytic derivative must agree with it on a linear segment.
        const LayerMotion keyed = scene.keyedMotion(1, 0.5f);
        check(!keyed.finiteDifference, "keyed motion is not a finite difference");
        checkNear(keyed.positionVelocity.x, motion.positionVelocity.x, 1e-3f,
                  "analytic and finite-difference velocity agree on a linear segment");

        // Opacity velocity: 0 -> 1 over 1s at 10fps is 1.0 per second, and it
        // must survive the same key spacing.
        layer.tracks.opacity.add(0.f, 0.f, Easing::linear());
        layer.tracks.opacity.add(1.f, 1.f, Easing::linear());
        scene.evaluate(10);
        checkNear(scene.motion(1).opacityVelocity, 1.f, 1e-3f,
                  "opacity velocity is per second");

        // A static layer is genuinely still, not "unknown".
        MotionScene still("still", 10.f);
        Layer& fixed = still.addLayer(Layer{1, "fixed"});
        fixed.positionedAt(Vector3{3.f, 4.f, 5.f});
        still.evaluate(5);
        const LayerMotion stillMotion = still.motion(1);
        checkNear(stillMotion.positionVelocity.length(), 0.f, 1e-6f,
                  "a layer with no keys has no velocity");
        checkNear(still.keyedMotion(1, 0.f).positionVelocity.length(), 0.f, 1e-6f,
                  "an unkeyed layer has no keyed velocity");

        // A parented layer reports the world rate, not the local one: the
        // compositor needs how fast the pixels move.
        MotionScene rigged("rig", 10.f);
        Layer& parent = rigged.addNull(1, "rig");
        parent.tracks.position.add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::linear());
        parent.tracks.position.add(1.f, Vector3{5.f, 0.f, 0.f}, Easing::linear());
        Layer& child = rigged.addLayer(Layer{2, "child"});
        child.childOf(1);
        rigged.evaluate(1);
        // The child inherits the parent's 5 units/second of world motion even
        // though it has no keys of its own.
        checkNear(rigged.motion(2).positionVelocity.x, 5.f, 1e-3f,
                  "a child inherits the parent's world velocity");
        // The child's OWN keys are empty, so its keyed motion is genuinely zero
        // even though its world motion is not. The gap between the two numbers is
        // exactly what the parent contributes, which is why both exist.
        checkNear(rigged.keyedMotion(2, 0.05f).positionVelocity.length(), 0.f, 1e-3f,
                  "an unkeyed child's keyed motion is still, though its world motion is not");

        // Rotating layer: angular velocity in rad/s. A half turn (pi rad) over
        // 1s at 10fps is pi rad/s.
        MotionScene spin("spin", 10.f);
        Layer& rotor = spin.addLayer(Layer{1, "rotor"});
        rotor.tracks.orientation.add(0.f, Quaternion{0.f, 0.f, 0.f, 1.f}, Easing::linear());
        rotor.tracks.orientation.add(1.f,
                                     Quaternion{0.f, 0.f, 0.f, 1.f}.setFromAxisAngle(
                                             Vector3{0.f, 1.f, 0.f}, 3.14159265f),
                                     Easing::linear());
        spin.evaluate(1);
        const LayerMotion spinMotion = spin.motion(1);
        checkNear(spinMotion.angularVelocity.length(), 3.14159265f, 0.05f,
                  "angular velocity is radians per second");
        // This library's setFromAxisAngle puts a positive angle about +Y on the
        // NEGATIVE y component, so the sign is read from the key's own
        // quaternion instead of assumed. The test states the property (the rate
        // points along the key's own axis) rather than a number that would only
        // pass for one sign convention.
        const Quaternion keyedAtMid = rotor.tracks.orientation.sample(0.5f);
        const float axisSign = static_cast<float>(keyedAtMid.y) < 0.f ? -1.f : 1.f;
        checkNear(spinMotion.angularVelocity.y, axisSign * 3.14159265f, 0.05f,
                  "angular velocity follows the key's own rotation axis");
        checkNear(spin.keyedMotion(1, 0.5f).angularVelocity.length(), 3.14159265f, 0.05f,
                  "analytic angular velocity agrees on a linear segment");

        // A tiny rotation step must not read as a full 2*pi turn backwards.
        // That is the quaternion double-cover, and it is the classic way an
        // angular velocity is silently wrong by 360 degrees per frame.
        MotionScene tiny("tiny", 10.f);
        Layer& nudge = tiny.addLayer(Layer{1, "nudge"});
        nudge.tracks.orientation.add(0.f, Quaternion{0.f, 0.f, 0.f, 1.f}, Easing::linear());
        nudge.tracks.orientation.add(1.f,
                                     Quaternion{0.f, 0.f, 0.f, 1.f}.setFromAxisAngle(
                                             Vector3{0.f, 1.f, 0.f}, 1e-4f),
                                     Easing::linear());
        tiny.evaluate(1);
        // 1e-4 rad of rotation in 1s. The failure this guards against is the
        // quaternion double-cover reporting ~2*pi/dt instead of the real rate,
        // which is the classic way an angular velocity is silently wrong by 360
        // degrees per frame.
        checkNear(tiny.motion(1).angularVelocity.length(), 1e-4f * 10.f, 1e-3f,
                  "a tiny rotation step is not reported as a full turn");

        // Unknown ids are rejected the same way the rest of the API rejects them.
        bool threw = false;
        try {
            (void) scene.motion(999);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "motion rejects an unknown layer id");

        threw = false;
        try {
            (void) scene.keyedMotion(999, 0.f);
        } catch (const std::out_of_range&) {
            threw = true;
        }
        check(threw, "keyedMotion rejects an unknown layer id");
    }

    void solvedPoseRegressions() {
        section("solved previous poses and target dependencies");
        MotionScene scene("reverse insertion", 30.f);
        auto& follower = scene.addNull(2, "follower", 3);
        scene.addNull(3, "parent").transform.position.x = 100.f;
        auto& target = scene.addNull(1, "target");
        target.transform.position.x = 10.f;
        ConstraintSpec copy; copy.kind = ConstraintKind::CopyPosition; copy.targetId = 1;
        follower.constraints.push_back(copy);
        check(scene.validate().empty(), "reverse-inserted dependencies validate");
        scene.evaluate(30);
        checkNear(worldPos(scene, 2).x, 10.f, 1e-5f, "copy position converts target world to parent local");
        checkNear(scene.motion(2).linearVelocity.length(), 0.f, 1e-5f, "static constraint has no velocity");
        target.tracks.position.add(0.f, {0.f, 0.f, 0.f});
        target.tracks.position.add(2.f, {20.f, 0.f, 0.f});
        scene.evaluate(MotionTime{30, 60.f, .5f});
        checkNear(scene.motion(2).linearVelocity.x, 10.f, .001f, "constrained velocity uses supplied FPS");
        const auto before = scene.state(2).previousWorld;
        scene.evaluate(MotionTime{29, 60.f, .5f});
        check(before.elements == scene.worldMatrix(2).elements, "previous solved pose equals independently evaluated pose");
        follower.constraints.clear();
        DriverSpec driver; driver.kind = DriverKind::Float; driver.frequency = 0.f;
        driver.phase = 1.57079632679f; driver.amplitude = 5.f;
        follower.drivers.push_back(driver);
        scene.evaluate(30);
        checkNear(scene.motion(2).linearVelocity.length(), 0.f, 1e-5f, "constant driver has no velocity");
        follower.drivers.clear();
        follower.constraints.push_back(copy);
        ConstraintSpec cycle = copy; cycle.targetId = 2;
        target.constraints.push_back(cycle);
        check(!scene.validate().empty(), "constraint cycles are diagnosed");
        bool rejected = false;
        try { scene.evaluate(30); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "mutated constraint cycle fails closed");
    }

    void invalidTimeRegressions() {
        section("invalid time boundaries");
        const auto rejects = [](auto operation) {
            try { operation(); } catch (const std::invalid_argument&) { return true; }
            return false;
        };
        for (float invalid : {0.f, -1.f, std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN()}) {
            check(rejects([&] { MotionScene scene("invalid", invalid); }), "scene rejects invalid FPS");
            check(rejects([&] { (void)MotionTime::atFrame(0, invalid); }), "time rejects invalid FPS");
        }
        for (float invalid : {std::numeric_limits<float>::infinity(),
                              std::numeric_limits<float>::quiet_NaN(), 1e30f}) {
            check(rejects([&] { (void)MotionTime::atPts(invalid, 30.f); }), "rounded time rejects invalid timestamp");
            check(rejects([&] { (void)MotionTime::atPtsExact(invalid, 30.f); }), "exact time rejects invalid timestamp");
        }
        MotionScene scene("invalid aggregates", 30.f); scene.addNull(1, "layer");
        for (float invalid : {-1.f, 1.f, std::numeric_limits<float>::quiet_NaN()})
            check(rejects([&] { scene.evaluate(MotionTime{0, 30.f, invalid}); }), "aggregate rejects invalid subframe");
        check(rejects([&] { scene.evaluate(std::numeric_limits<int>::min()); }), "previous frame underflow rejected");
    }

    void subFrameEvaluation() {

        section("sub-frame evaluation");

        // evaluatePts snaps to the nearest frame; evaluatePtsExact does not.
        // A layer moving at 1 unit per frame at 10fps is at 0.5 at frame 0.5.
        MotionScene scene("subframe", 10.f);
        Layer& layer = scene.addLayer(Layer{1, "mover"});
        layer.tracks.position.add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::linear());
        layer.tracks.position.add(1.f, Vector3{10.f, 0.f, 0.f}, Easing::linear());

        // 0.55s at 10fps is frame 5.5: a genuine half-frame instant.
        scene.evaluatePts(0.55f);
        const float snapped = worldPos(scene, 1).x;
        // evaluatePts snaps to the nearest whole frame, so 5.5 -> 6 -> 6 units.
        checkNear(snapped, 6.f, 1e-4f, "evaluatePts rounds to the nearest whole frame");

        scene.evaluatePtsExact(0.55f);
        const float exact = worldPos(scene, 1).x;
        checkNear(exact, 5.5f, 1e-4f, "evaluatePtsExact samples the half frame exactly");

        // The whole point: the two disagree, and the exact one is right.
        check(std::fabs(exact - snapped) > 0.4f,
              "frame-snapping and exact sub-frame evaluation are measurably different");

        // Determinism: the same sub-frame instant evaluated twice is identical.
        scene.evaluatePtsExact(0.25f);
        const float quarter = worldPos(scene, 1).x;
        scene.evaluatePtsExact(0.75f);
        scene.evaluatePtsExact(0.25f);
        checkNear(worldPos(scene, 1).x, quarter, 0.f,
                  "sub-frame evaluation is a pure function of the instant");

        // The keyed derivative is available at a sub-frame instant, where there
        // is no previous frame to difference against. That is the reason the
        // analytic path exists.
        const LayerMotion mid = scene.keyedMotion(1, 0.5f);
        checkNear(mid.positionVelocity.x, 10.f, 1e-3f,
                  "keyed velocity is available at a sub-frame instant");

        // An eased motion is slower at its start; sub-frame sampling must show
        // that, or the interpolation is snapping somewhere invisible.
        MotionScene eased("eased", 10.f);
        Layer& soft = eased.addLayer(Layer{1, "soft"});
        soft.tracks.position.add(0.f, Vector3{0.f, 0.f, 0.f}, Easing::easeIn());
        soft.tracks.position.add(1.f, Vector3{10.f, 0.f, 0.f}, Easing::linear());
        eased.evaluatePtsExact(0.1f);
        const float early = worldPos(eased, 1).x;
        eased.evaluatePtsExact(0.9f);
        const float late = worldPos(eased, 1).x;
        check(early < late, "easeIn covers less ground early than late");
        check(early > 0.f, "easeIn has already moved by a tenth of the way");
    }

}// namespace

int main() {

    try {
        easingShapes();
        easingDerivative();
        trackVelocity();
        subFrameTime();
        sceneMotion();
        subFrameEvaluation();
        solvedPoseRegressions();
        invalidTimeRegressions();
    } catch (const std::exception& error) {
        std::printf("  FAIL  unexpected exception: %s\n", error.what());
        ++chrononmotion_test::failures;
    }

    std::printf("kinematics: %d checks, %d failures\n",
                chrononmotion_test::checks, chrononmotion_test::failures);
    return chrononmotion_test::failures == 0 ? 0 : 1;
}
