// ChrononMotion primitives test.
//
// Pins the three value-level primitives the rest of the motion model is built
// from: the timing curve (Easing), the layer transform (Transform) and the keyed
// channel (Track). Everything above them — layers, scenes, cameras, presets — is
// only composition of these, so a regression here must fail here.

#include "threepp/threepp.hpp"

#include "motion_check.hpp"

#include <vector>

using namespace threepp;
using namespace threepp::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    Quaternion aboutY(float degrees) {

        Quaternion q;
        q.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(degrees));
        return q;
    }

    // ------------------------------------------------------------------ easing

    void easingCurves() {

        section("easing curves");

        checkNear(Easing::linear()(0.f), 0.f, 1e-6f, "linear starts at 0");
        checkNear(Easing::linear()(0.37f), 0.37f, 1e-6f, "linear is the identity");
        checkNear(Easing::linear()(1.f), 1.f, 1e-6f, "linear ends at 1");

        checkNear(Easing::easeInOut()(0.f), 0.f, 1e-5f, "easeInOut starts at 0");
        checkNear(Easing::easeInOut()(1.f), 1.f, 1e-5f, "easeInOut ends at 1");
        checkNear(Easing::easeInOut()(0.5f), 0.5f, 1e-4f, "easeInOut is symmetric about its midpoint");

        check(Easing::easeIn()(0.5f) < 0.5f, "easeIn lags behind linear at the midpoint");
        check(Easing::easeOut()(0.5f) > 0.5f, "easeOut leads linear at the midpoint");

        // Overshoot must leave [0,1] in the middle and still land exactly on 1.
        const Easing overshoot = Easing::overshoot();
        check(overshoot(0.5f) > 1.f, "overshoot passes its target mid-segment");
        checkNear(overshoot(1.f), 1.f, 1e-4f, "overshoot settles exactly on the target");

        checkNear(Easing::bounceOut()(0.f), 0.f, 1e-5f, "bounceOut starts at 0");
        checkNear(Easing::bounceOut()(1.f), 1.f, 1e-5f, "bounceOut ends at 1");
        checkNear(Easing::elasticOut()(0.f), 0.f, 1e-6f, "elasticOut starts at 0");
        checkNear(Easing::elasticOut()(1.f), 1.f, 1e-6f, "elasticOut ends exactly at 1");

        // A custom curve is honored verbatim, and x is clamped so it stays a
        // function of time even if the author writes a non-monotonic x.
        const Easing custom = Easing::cubicBezier(0.25f, 0.1f, 0.25f, 1.f);
        checkNear(custom(0.f), 0.f, 1e-5f, "custom bezier starts at 0");
        checkNear(custom(1.f), 1.f, 1e-5f, "custom bezier ends at 1");

        const Easing slowStart = Easing::cubicBezier(0.9f, 0.f, 1.f, 1.f);
        check(slowStart(0.5f) < 0.5f, "a slow-start custom bezier lags linear");
        check(Easing::cubicBezier(0.42f, 0.f, 1.f, 1.f) == Easing::easeIn(),
              "a custom bezier matching a preset compares equal to it");

        const Easing clamped = Easing::cubicBezier(-3.f, 0.f, 4.f, 1.f);
        checkNear(clamped.x1(), 0.f, 1e-6f, "cubicBezier clamps x1 into [0,1]");
        checkNear(clamped.x2(), 1.f, 1e-6f, "cubicBezier clamps x2 into [0,1]");

        check(Easing::linear() == Easing::linear(), "equal curves compare equal");
        check(Easing::easeIn() != Easing::easeOut(), "different curves compare different");
        check(Easing::linear().isLinear(), "linear reports itself");
        check(!Easing::easeIn().isLinear(), "easeIn does not report itself as linear");

        // Pure function: repeated evaluation is identical.
        for (int i = 0; i <= 10; ++i) {
            const float t = static_cast<float>(i) / 10.f;
            checkNear(Easing::easeInOut()(t), Easing::easeInOut()(t), 0.f,
                      "easing is a pure function of t");
        }
    }

    // --------------------------------------------------------------- transform

    void transformComposition() {

        section("transform composition");

        const Transform identity = Transform::identity();
        check(identity.isIdentity(), "the default transform reports itself as the identity");
        check(Transform::identity() == identity, "two identity transforms compare equal");

        Transform changed;
        changed.position.set(1.f, 0.f, 0.f);
        check(!changed.isIdentity(), "a translated transform is not the identity");
        check(changed != identity, "a translated transform compares different");

        const Matrix4 identityMatrix = identity.toMatrix();
        for (int i = 0; i < 16; ++i) {
            checkNear(identityMatrix[static_cast<unsigned int>(i)],
                      Matrix4()[static_cast<unsigned int>(i)], 1e-6f,
                      "the identity transform is the identity matrix");
        }

        Transform translated;
        translated.position.set(10.f, 20.f, 30.f);
        checkNear(translated.applyPoint(Vector3(1.f, 2.f, 3.f)), Vector3(11.f, 22.f, 33.f), 1e-4f,
                  "position translates a layer point");

        // Scale pivots on the anchor: with no anchor the origin is the pivot, so
        // (1,0,0) at scale 2 lands at (2,0,0).
        Transform scaled;
        scaled.scale.set(2.f, 2.f, 2.f);
        checkNear(scaled.applyPoint(Vector3(1.f, 0.f, 0.f)), Vector3(2.f, 0.f, 0.f), 1e-4f,
                  "scale multiplies a layer point");

        // With the anchor ON the point, scaling cannot move it.
        Transform anchored = scaled;
        anchored.anchor.set(1.f, 0.f, 0.f);
        checkNear(anchored.applyPoint(Vector3(1.f, 0.f, 0.f)), Vector3(0.f, 0.f, 0.f), 1e-4f,
                  "the anchor is the fixed point of scale");

        // ... and the same anchor is the pivot of rotation.
        Transform rotated;
        rotated.anchor.set(1.f, 0.f, 0.f);
        rotated.rotation.set(0.f, 0.f, math::degToRad(90.f));
        checkNear(rotated.applyPoint(Vector3(1.f, 0.f, 0.f)), Vector3(0.f, 0.f, 0.f), 1e-4f,
                  "the anchor is also the fixed point of rotation");
        checkNear(rotated.applyPoint(Vector3(2.f, 0.f, 0.f)), Vector3(0.f, 1.f, 0.f), 1e-4f,
                  "rotation turns about the anchor, not the origin");

        // Rotation X/Y/Z and orientation are independent channels that compose.
        Transform both;
        both.rotation.set(0.f, 0.f, math::degToRad(90.f));
        both.orientation = aboutY(90.f);

        Euler eulerAngles(0.f, 0.f, math::degToRad(90.f));
        Quaternion eulerPart;
        eulerPart.setFromEuler(eulerAngles);
        Quaternion expectedOrientation;
        expectedOrientation.multiplyQuaternions(aboutY(90.f), eulerPart);

        checkNear(both.localQuaternion().angleTo(expectedOrientation), 0.f, 1e-3f,
                  "orientation composes on top of Rotation X/Y/Z");

        checkNear(Transform::identity().localQuaternion().angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-5f,
                  "an identity transform has an identity orientation");

        // toMatrix agrees with applyPoint.
        Transform full;
        full.position.set(3.f, -2.f, 5.f);
        full.anchor.set(0.5f, 0.5f, 0.f);
        full.scale.set(1.5f, 0.5f, 1.f);
        full.rotation.set(0.2f, -0.4f, 0.7f);
        full.orientation = aboutY(25.f);

        const Vector3 point(0.3f, -0.7f, 0.1f);
        Vector3 viaMatrix = point;
        viaMatrix.applyMatrix4(full.toMatrix());
        checkNear(viaMatrix, full.applyPoint(point), 1e-4f,
                  "toMatrix and applyPoint agree");
    }

    void transformBlending() {

        section("transform blending");

        Transform a;
        a.position.set(0.f, 0.f, 0.f);
        a.opacity = 0.f;

        Transform b;
        b.position.set(10.f, 0.f, 0.f);
        b.opacity = 1.f;

        checkNear(Transform::blend(a, b, 0.f).position, Vector3(0.f, 0.f, 0.f), 1e-5f,
                  "blend at 0 is the first pose");
        checkNear(Transform::blend(a, b, 1.f).position, Vector3(10.f, 0.f, 0.f), 1e-5f,
                  "blend at 1 is the second pose");
        checkNear(Transform::blend(a, b, 0.5f).position, Vector3(5.f, 0.f, 0.f), 1e-5f,
                  "blend interpolates position");
        checkNear(Transform::blend(a, b, 0.5f).opacity, 0.5f, 1e-5f, "blend interpolates opacity");

        // Shortest-path orientation: 179° -> -179° must pass through 180°, not
        // sweep back through 0°.
        Transform nearPositive;
        nearPositive.orientation = aboutY(179.f);
        Transform nearNegative;
        nearNegative.orientation = aboutY(-179.f);

        const Quaternion mid = Transform::blend(nearPositive, nearNegative, 0.5f).orientation;
        checkNear(mid.angleTo(aboutY(180.f)), 0.f, 1e-3f,
                  "blending across ±180° takes the short way through 180°");

        // A full half turn is the degenerate case: both directions are equally
        // short, so the direction is ambiguous — but the blend must still travel
        // exactly half the angle (90°), never past it and never back to 0°.
        Transform identityPose;
        Transform halfTurn;
        halfTurn.orientation = aboutY(180.f);
        const Quaternion quarter = Transform::blend(identityPose, halfTurn, 0.5f).orientation;
        checkNear(quarter.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), math::PI * 0.5f, 1e-3f,
                  "blending identity to 180° travels exactly 90° at the midpoint");
    }

    // ------------------------------------------------------------------- track

    void trackSampling() {

        section("track sampling");

        Track<float> opacity;
        check(opacity.empty(), "a fresh track is empty");
        float untouched = 42.f;
        opacity.sample(0.5f, untouched);
        checkNear(untouched, 42.f, 0.f, "an empty track leaves the output untouched");
        checkNear(opacity.sample(0.5f), 0.f, 0.f, "sampling an empty track yields a default value");

        opacity.add(0.f, 0.f);
        opacity.add(1.f, 10.f);
        check(opacity.size() == 2, "adding keys grows the track");
        checkNear(opacity.startTime(), 0.f, 1e-6f, "startTime is the first key");
        checkNear(opacity.endTime(), 1.f, 1e-6f, "endTime is the last key");
        checkNear(opacity.duration(), 1.f, 1e-6f, "duration spans the keys");

        checkNear(opacity.sample(0.f), 0.f, 1e-6f, "sampling at the first key returns it");
        checkNear(opacity.sample(0.5f), 5.f, 1e-5f, "linear interpolation lands halfway");
        checkNear(opacity.sample(1.f), 10.f, 1e-6f, "sampling at the last key returns it");
        checkNear(opacity.sample(-99.f), 0.f, 1e-6f, "sampling before the first key clamps");
        checkNear(opacity.sample(99.f), 10.f, 1e-6f, "sampling after the last key clamps");

        // Out-of-order authoring sorts; a duplicate time replaces.
        Track<float> sorted;
        sorted.add(1.f, 10.f);
        sorted.add(0.f, 0.f);
        sorted.add(0.5f, 5.f);
        checkNear(sorted.sample(0.5f), 5.f, 1e-5f, "keys are kept sorted by time");
        sorted.add(1.f, 20.f);
        check(sorted.size() == 3, "a key at an existing time replaces rather than duplicates");
        checkNear(sorted.sample(1.f), 20.f, 1e-6f, "the replacing key wins");

        check(sorted.remove(0.5f), "remove finds a key at an exact time");
        check(!sorted.remove(0.5f), "removing twice reports nothing left");
        check(sorted.size() == 2, "remove shrinks the track");

        // Per-segment easing: the curve on a key shapes the segment that follows it.
        Track<float> eased;
        eased.add(0.f, 0.f, Easing::easeIn());
        eased.add(1.f, 1.f);
        check(eased.sample(0.5f) < 0.5f, "the outgoing key's easing shapes the segment");
        checkNear(eased.sample(1.f), 1.f, 1e-6f, "the segment still lands on the next key");

        // Discrete hold.
        Track<float> held;
        held.addDiscrete(0.f, 3.f);
        held.add(1.f, 9.f);
        checkNear(held.sample(0.5f), 3.f, 1e-6f, "a discrete key holds its value");
        checkNear(held.sample(0.999f), 3.f, 1e-6f, "the hold lasts until the next key");

        // Vector channels interpolate component-wise.
        Track<Vector3> position;
        position.add(0.f, Vector3(0.f, 0.f, 0.f));
        position.add(1.f, Vector3(4.f, 2.f, -6.f));
        checkNear(position.sample(0.5f), Vector3(2.f, 1.f, -3.f), 1e-5f,
                  "vector channels interpolate component-wise");

        // Quaternion channels slerp along the shortest arc.
        Track<Quaternion> orientation;
        orientation.add(0.f, aboutY(179.f));
        orientation.add(1.f, aboutY(-179.f));
        checkNear(orientation.sample(0.5f).angleTo(aboutY(180.f)), 0.f, 1e-3f,
                  "quaternion channels slerp the short way across ±180°");

        // Determinism, including call-order independence (no cached cursor).
        const float forward = opacity.sample(0.3f);
        const float unrelated = opacity.sample(0.9f);
        const float again = opacity.sample(0.3f);
        checkNear(unrelated, 9.f, 1e-5f, "sampling a later time still interpolates correctly");
        checkNear(forward, again, 0.f, "sampling has no hidden state: the same time is the same value");
    }

}// namespace

int main() {

    easingCurves();
    transformComposition();
    transformBlending();
    trackSampling();

    return chrononmotion_test::report();
}
