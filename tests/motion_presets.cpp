// ChrononMotion presets test.
//
// Every preset in the list is exercised here, because a preset is only a thin
// authoring layer over the primitives: if one of them stops writing the keys it
// promises, nothing else in the build would notice. The assertion for each preset
// is its *defining* property — where it starts, where it ends, and the one thing
// that makes it that preset and not a neighbour.

#include "chrononmotion/chrononmotion.hpp"

#include "motion_check.hpp"

#include <cmath>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr float kFps = 30.f;

    float at(int frame) {

        return static_cast<float>(frame) / kFps;
    }

    Quaternion aboutY(float degrees) {

        Quaternion q;
        q.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(degrees));
        return q;
    }

    Quaternion aboutX(float degrees) {

        Quaternion q;
        q.setFromAxisAngle(Vector3(1.f, 0.f, 0.f), math::degToRad(degrees));
        return q;
    }

    // ------------------------------------------------------------- in and out

    void slideAndFadePresets() {

        section("slide, fade and depth presets");

        {
            Layer layer(1, "L");
            layer.transform.position.set(1.f, 0.f, 0.f);
            presets::slideIn(layer, presets::Direction::Left, 0, 30, 5.f, kFps);
            checkNear(layer.sample(at(0)).position, Vector3(-4.f, 0.f, 0.f), 1e-3f,
                      "slideIn starts the layer one distance away, in the named direction");
            checkNear(layer.sample(at(30)).position, Vector3(1.f, 0.f, 0.f), 1e-3f,
                      "slideIn settles on the resting position");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(1.f, 2.f, 3.f);
            presets::slideOut(layer, presets::Direction::Down, 0, 30, 4.f, kFps);
            checkNear(layer.sample(at(0)).position, Vector3(1.f, 2.f, 3.f), 1e-3f, "slideOut starts at rest");
            checkNear(layer.sample(at(30)).position, Vector3(1.f, -2.f, 3.f), 1e-3f,
                      "slideOut leaves along the named direction");
        }
        {
            Layer layer(1, "L");
            presets::pushInZ(layer, 0, 30, -10.f, kFps);
            checkNear(layer.sample(at(0)).position.z, -10.f, 1e-3f, "pushInZ starts at the given depth");
            checkNear(layer.sample(at(30)).position.z, 0.f, 1e-3f, "pushInZ arrives at the resting depth");
        }
        {
            Layer layer(1, "L");
            presets::pullOutZ(layer, 30, 30, -20.f, kFps);
            checkNear(layer.sample(at(30)).position.z, 0.f, 1e-3f, "pullOutZ starts at the resting depth");
            checkNear(layer.sample(at(60)).position.z, -20.f, 1e-3f, "pullOutZ leaves toward the given depth");
        }
        {
            Layer layer(1, "L");
            layer.transform.opacity = 0.9f;
            presets::fadeIn(layer, 0, 30, kFps);
            checkNear(layer.sample(at(0)).opacity, 0.f, 1e-4f, "fadeIn starts transparent");
            checkNear(layer.sample(at(30)).opacity, 0.9f, 1e-4f, "fadeIn lands on the layer's own opacity");

            presets::fadeOut(layer, 60, 30, kFps);
            checkNear(layer.sample(at(60)).opacity, 0.9f, 1e-4f, "fadeOut starts from the layer's own opacity");
            checkNear(layer.sample(at(90)).opacity, 0.f, 1e-4f, "fadeOut ends transparent");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(0.f, 0.f, -2.f);
            presets::fadeSlide(layer, presets::Direction::Right, 0, 30, 6.f, kFps);
            checkNear(layer.sample(at(0)).opacity, 0.f, 1e-4f, "fadeSlide fades in");
            checkNear(layer.sample(at(0)).position, Vector3(6.f, 0.f, -2.f), 1e-3f,
                      "fadeSlide also slides in");
            checkNear(layer.sample(at(30)).position, Vector3(0.f, 0.f, -2.f), 1e-3f,
                      "fadeSlide settles on the resting position");
            checkNear(layer.sample(at(30)).position.z, -2.f, 1e-4f, "fadeSlide keeps the Z depth");
        }
        {
            Layer layer(1, "L");
            presets::fadeScale(layer, 0, 30, 0.8f, kFps);
            checkNear(layer.sample(at(0)).scale.x, 0.8f, 1e-3f, "fadeScale starts at the given scale factor");
            checkNear(layer.sample(at(0)).opacity, 0.f, 1e-4f, "fadeScale starts transparent");
            checkNear(layer.sample(at(30)).scale.x, 1.f, 1e-3f, "fadeScale lands at full scale");
        }
        {
            Layer layer(1, "L");
            presets::parallaxEntrance(layer, 0, 30, -12.f, kFps);
            checkNear(layer.sample(at(0)).position.z, -12.f, 1e-3f, "parallaxEntrance starts deep");
            checkNear(layer.sample(at(30)).position.z, 0.f, 1e-3f, "parallaxEntrance arrives at depth 0");
        }
    }

    void scalePresets() {

        section("scale presets");

        {
            Layer layer(1, "L");
            presets::scalePop(layer, 0, 30, 0.5f, 1.5f, kFps);
            checkNear(layer.sample(at(0)).scale.x, 0.5f, 1e-3f, "scalePop starts at the given scale");
            checkNear(layer.sample(at(30)).scale.x, 1.5f, 1e-3f, "scalePop lands exactly on its target scale");

            // "Pop" is the overshoot: the value passes the target mid-segment.
            float peak = 0.f;
            for (int frame = 0; frame <= 30; ++frame) {
                peak = std::max(peak, layer.sample(at(frame)).scale.x);
            }
            check(peak > 1.5f, "scalePop overshoots past its target before settling");
        }
        {
            Layer layer(1, "L");
            presets::scaleOvershoot(layer, 0, 30, 0.6f, 1.f, kFps);
            checkNear(layer.sample(at(0)).scale.x, 0.6f, 1e-3f, "scaleOvershoot starts at the given scale");
            checkNear(layer.sample(at(30)).scale.x, 1.f, 1e-3f, "scaleOvershoot lands at full scale");

            float peak = 0.f;
            for (int frame = 0; frame <= 30; ++frame) {
                peak = std::max(peak, layer.sample(at(frame)).scale.x);
            }
            check(peak > 1.f, "scaleOvershoot overshoots past full scale");
        }
        {
            // The overshoot is uniform on all three axes.
            Layer layer(1, "L");
            presets::scalePop(layer, 0, 30, 0.5f, 1.5f, kFps);
            const Vector3 mid = layer.sample(at(15)).scale;
            checkNear(mid, Vector3(mid.x, mid.x, mid.x), 1e-5f, "scale presets scale uniformly");
        }
    }

    void rotationPresets() {

        section("rotation presets");

        {
            Layer layer(1, "L");
            presets::flip(layer, presets::Axis::Y, 0, 30, kFps);
            checkNear(layer.sample(at(0)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "flip starts from the identity orientation");
            checkNear(layer.sample(at(30)).orientation.angleTo(aboutY(180.f)), 0.f, 1e-3f,
                      "flip about Y ends at 180°, the short way");
            checkNear(layer.sample(at(15)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)),
                      math::PI * 0.5f, 1e-3f, "flip travels half the angle at its midpoint");
        }
        {
            Layer layer(1, "L");
            presets::flip(layer, presets::Axis::X, 0, 30, kFps, 90.f);
            checkNear(layer.sample(at(30)).orientation.angleTo(aboutX(90.f)), 0.f, 1e-3f,
                      "flip about X honors the given angle");
        }
        {
            Layer layer(1, "L");
            presets::cardTurn(layer, 0, 30, kFps);
            checkNear(layer.sample(at(0)).orientation.angleTo(aboutY(-90.f)), 0.f, 1e-3f,
                      "cardTurn starts folded a quarter turn away");
            checkNear(layer.sample(at(30)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "cardTurn lands facing the viewer");
        }
        {
            Layer layer(1, "L");
            layer.transform.rotation.set(0.1f, 0.2f, 0.3f);
            presets::tilt3D(layer, 0, 30, 20.f, kFps);
            checkNear(layer.sample(at(0)).rotation.x, 0.1f, 1e-4f, "tilt3D starts from the resting rotation");
            checkNear(layer.sample(at(30)).rotation.x, 0.1f + math::degToRad(20.f), 1e-4f,
                      "tilt3D adds its angle about X");
            checkNear(layer.sample(at(30)).rotation.y, 0.2f, 1e-4f, "tilt3D leaves the other axes alone");
        }
        {
            Layer layer(1, "L");
            presets::spinZ(layer, 0, 30, 1.f, kFps);
            checkNear(layer.sample(at(0)).rotation.z, 0.f, 1e-4f, "spinZ starts unrotated");
            checkNear(layer.sample(at(30)).rotation.z, math::TWO_PI, 1e-3f, "spinZ completes one full turn");
        }
        {
            Layer layer(1, "L");
            presets::spinXYZ(layer, 0, 30, 1.f, kFps);
            checkNear(layer.sample(at(0)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "spinXYZ starts from the identity orientation");
            checkNear(layer.sample(at(30)).orientation.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), 0.f, 1e-3f,
                      "a full spinXYZ turn comes back to the identity orientation");
            // One sixth of the way is one sixth of the turn: 60° of the 360°.
            const Quaternion sixth = layer.sample(at(5)).orientation;
            checkNear(sixth.angleTo(Quaternion(0.f, 0.f, 0.f, 1.f)), math::PI / 3.f, 1e-2f,
                      "spinXYZ rotates continuously rather than snapping between keys");
        }
    }

    void motionPresets() {

        section("drift, hover and motion-path presets");

        {
            Layer layer(1, "L");
            layer.transform.position.set(1.f, 1.f, 1.f);
            presets::drift(layer, 0, 30, Vector3(3.f, -2.f, 1.f), kFps);
            checkNear(layer.sample(at(0)).position, Vector3(1.f, 1.f, 1.f), 1e-4f, "drift starts at rest");
            checkNear(layer.sample(at(15)).position, Vector3(2.5f, 0.f, 1.5f), 1e-3f,
                      "drift is constant-speed, so it is halfway at the midpoint");
            checkNear(layer.sample(at(30)).position, Vector3(4.f, -1.f, 2.f), 1e-3f,
                      "drift ends at the resting position plus the offset");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(0.f, 5.f, 0.f);
            presets::floatHover(layer, 0, 40, 2.f, kFps);
            checkNear(layer.sample(at(0)).position.y, 5.f, 1e-3f, "hover starts at rest");
            checkNear(layer.sample(at(10)).position.y, 7.f, 1e-3f, "hover rises by the amplitude");
            checkNear(layer.sample(at(20)).position.y, 5.f, 1e-3f, "hover returns to rest");
            checkNear(layer.sample(at(30)).position.y, 3.f, 1e-3f, "hover dips by the amplitude");
            checkNear(layer.sample(at(40)).position.y, 5.f, 1e-3f, "hover ends at rest");
        }
        {
            Layer layer(1, "L");
            layer.transform.position.set(0.f, 0.f, 0.f);
            presets::arcMotion(layer, Vector3(10.f, 0.f, 0.f), Vector3(5.f, 8.f, 0.f), 0, 30, kFps);
            checkNear(layer.sample(at(0)).position, Vector3(0.f, 0.f, 0.f), 1e-3f, "arcMotion starts at rest");
            checkNear(layer.sample(at(30)).position, Vector3(10.f, 0.f, 0.f), 1e-3f, "arcMotion ends at its target");
            check(layer.sample(at(15)).position.y > 3.f, "arcMotion travels a curve through its apex");
        }
        {
            Layer layer(1, "L");
            const MotionPath path = MotionPath::line(Vector3(0.f, 0.f, 0.f), Vector3(9.f, 0.f, 0.f));
            presets::pathMotion(layer, path, 0, 30, kFps, true, 9);
            checkNear(layer.sample(at(0)).position, Vector3(0.f, 0.f, 0.f), 1e-4f, "pathMotion starts on the path");
            checkNear(layer.sample(at(30)).position, Vector3(9.f, 0.f, 0.f), 1e-4f, "pathMotion ends on the path");
            checkNear(layer.sample(at(15)).position, Vector3(4.5f, 0.f, 0.f), 1e-3f,
                      "pathMotion samples uniformly along the path");

            Vector3 alongZ(0.f, 0.f, 1.f);
            alongZ.applyQuaternion(layer.sample(at(15)).orientation);
            checkNear(alongZ, Vector3(1.f, 0.f, 0.f), 1e-3f, "pathMotion can orient the layer along the path");
        }
        {
            Layer layer(1, "L");
            const MotionPath path = MotionPath::catmullRom(
                    {Vector3(0.f, 0.f, 0.f), Vector3(3.f, 2.f, 0.f), Vector3(6.f, 0.f, 0.f)});
            presets::pathMotion(layer, path, 0, 30, kFps, false, 12);
            check(layer.sample(at(15)).position.y > 0.5f, "pathMotion follows a curved catmull-rom path");
        }
    }

    void groupPresets() {

        section("hierarchy presets");

        {
            Layer first(1, "First");
            Layer second(2, "Second");
            first.transform.position.set(0.f, 0.f, 0.f);
            second.transform.position.set(0.f, 0.f, 0.f);

            presets::staggeredEntrance({&first, &second}, presets::Direction::Up, 0, 6, 30, 5.f, kFps);

            const float firstRemaining = std::fabs(first.sample(at(15)).position.y - 0.f);
            const float secondRemaining = std::fabs(second.sample(at(15)).position.y - 0.f);
            check(firstRemaining < secondRemaining,
                  "staggeredEntrance offsets each layer so the first is further along");
            checkNear(second.sample(at(0)).position.y, 5.f, 1e-3f,
                      "a staggered layer is still off-position when its own delay has not elapsed");
            checkNear(first.sample(at(30)).position.y, 0.f, 1e-3f, "the first staggered layer has landed"); 
            checkNear(second.sample(at(36)).position.y, 0.f, 1e-3f,
                      "the second staggered layer lands one stagger later");
            check(first.sample(at(15)).opacity < 1.f && second.sample(at(15)).opacity < 1.f,
                  "staggeredEntrance also fades each layer in");
        }
        {
            Layer parent(10, "Null_Main");
            Layer childA(11, "A");
            Layer childB(12, "B");

            presets::parentGroupReveal(parent, {&childA, &childB}, 0, 30, kFps);

            checkNear(parent.sample(at(0)).scale.x, 0.85f, 1e-3f, "parentGroupReveal starts the group folded");
            checkNear(parent.sample(at(30)).scale.x, 1.f, 1e-3f, "parentGroupReveal opens to full scale");
            checkNear(parent.sample(at(0)).opacity, 0.f, 1e-4f, "parentGroupReveal starts the group invisible");
            checkNear(parent.sample(at(30)).opacity, 1.f, 1e-4f, "parentGroupReveal ends the group visible");
            checkNear(childA.sample(at(30)).opacity, 1.f, 1e-3f, "the first child has faded in");
            checkNear(childB.sample(at(30)).opacity, 1.f, 1e-3f, "the second child has faded in");
            // The stagger is real: the first child is further into its fade.
            check(childA.sample(at(9)).opacity > childB.sample(at(9)).opacity,
                  "the children fade in staggered under the parent");

            // The reveal drives a real hierarchy: the parent's scale reaches the child.
            MotionScene scene("reveal", kFps);
            scene.addLayer(parent);
            childA.parentId = 10;
            scene.addLayer(childA);
            scene.evaluate(0);

            Vector3 childScale;
            childScale.setFromMatrixScale(scene.worldMatrix(11));
            checkNear(childScale.x, 0.85f, 1e-3f, "the group's scale composes into its children");
        }
    }

    void pivotAndLifetimePresets() {

        section("pivot and lifetime presets");

        // The anchor is the fixed point of rotation, so moving it over time moves
        // the point the layer turns about. That is the whole of "animated anchor".
        {
            Layer layer(1, "L");
            layer.transform.position.set(3.f, 0.f, 0.f);
            layer.transform.anchor.set(-2.f, -2.f, 0.f);

            presets::anchorTo(layer, Vector3(2.f, 2.f, 0.f), 0, 30, kFps);

            checkNear(layer.sample(at(0)).anchor, Vector3(-2.f, -2.f, 0.f), 1e-4f,
                      "anchorTo starts from the layer's resting anchor");
            checkNear(layer.sample(at(30)).anchor, Vector3(2.f, 2.f, 0.f), 1e-4f,
                      "anchorTo lands on the requested pivot");
            checkNear(layer.sample(at(15)).anchor, Vector3(0.f, 0.f, 0.f), 1e-4f,
                      "the pivot travels through the midpoint of its window");

            // Scale and rotation pivot on the *sampled* anchor, so a point that was
            // fixed at the first key is no longer fixed at the last one.
            const Transform end = layer.sample(at(30));
            check(end.applyPoint(Vector3(-2.f, -2.f, 0.f)).distanceTo(end.position) > 1.f,
                  "the point the layer pivots about has actually moved");

            // The sampled anchor is still the fixed point, at every frame.
            for (const int frame : {0, 7, 15, 22, 30}) {
                const Transform pose = layer.sample(at(frame));
                checkNear(pose.applyPoint(pose.anchor), pose.position, 1e-4f,
                          "the sampled anchor is the fixed point of the transform");
            }
        }
        {
            // The pivot composes with a rotation track: the layer spins about
            // whatever pivot it has at that instant.
            Layer layer(1, "L");
            layer.transform.anchor.set(-1.f, 0.f, 0.f);
            layer.tracks.rotation.add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
            layer.tracks.rotation.add(1.f, Vector3(0.f, 0.f, math::degToRad(90.f)), Easing::linear());
            presets::anchorTo(layer, Vector3(1.f, 0.f, 0.f), 0, 30, kFps);

            const Transform late = layer.sample(at(30));
            checkNear(late.applyPoint(Vector3(1.f, 0.f, 0.f)), late.position, 1e-4f,
                      "with the pivot moved, the turn is about the new pivot");
            // A point one unit to the right of the *new* pivot has been turned a
            // quarter turn about it, which is only true if the pivot really moved.
            checkNear(late.applyPoint(Vector3(2.f, 0.f, 0.f)), Vector3(0.f, 1.f, 0.f), 1e-4f,
                      "the turn about the moved pivot is a real quarter turn");
        }

        // The in/out windows are read from the layer's lifetime, not chosen by hand.
        {
            Layer layer(1, "L");
            layer.lifetime = FrameRange::between(10, 70);

            const presets::LifetimeWindows windows = presets::lifetimeWindows(layer, 10);
            check(windows.hasIn && windows.hasOut, "a 60-frame lifetime holds a 10-frame in and out");
            check(windows.in.inFrame == 10 && windows.in.outFrame == 20,
                  "the entrance starts on the lifetime's in frame");
            check(windows.out.inFrame == 60 && windows.out.outFrame == 70,
                  "the exit finishes on the lifetime's out frame");

            presets::lifetimeSlide(layer, presets::Direction::Left, presets::Direction::Right, 10, 5.f, kFps);

            checkNear(layer.sample(at(10)).position, Vector3(-5.f, 0.f, 0.f), 1e-3f,
                      "the entrance slides in from the lifetime's in frame");
            checkNear(layer.sample(at(20)).position, Vector3(0.f, 0.f, 0.f), 1e-3f,
                      "the entrance settles one window later");
            checkNear(layer.sample(at(60)).position, Vector3(0.f, 0.f, 0.f), 1e-3f,
                      "the layer holds its resting position until the exit window");
            checkNear(layer.sample(at(70)).position, Vector3(5.f, 0.f, 0.f), 1e-3f,
                      "the exit leaves as it finishes on the out frame");
            checkNear(layer.sample(at(10)).opacity, 0.f, 1e-4f, "the entrance fades in from transparent");
            checkNear(layer.sample(at(20)).opacity, 1.f, 1e-4f, "the entrance lands fully opaque");
            checkNear(layer.sample(at(70)).opacity, 0.f, 1e-4f, "the exit fades out to transparent");

            check(layer.tracks.position.startTime() >= at(10) - 1e-5f,
                  "no key is written before the layer's in frame");
            check(layer.tracks.position.endTime() <= at(70) + 1e-5f,
                  "no key is written past the layer's out frame");
        }
        {
            // Fail closed: a window the lifetime cannot hold is left un-keyed
            // rather than moved to a frame the layer is not alive for.
            Layer open(1, "Open");
            open.lifetime = FrameRange::all();
            const presets::LifetimeWindows openWindows = presets::lifetimeWindows(open, 10);
            check(openWindows.hasIn && !openWindows.hasOut,
                  "an open-ended layer has an entrance but no exit window");

            Layer tight(2, "Tight");
            tight.lifetime = FrameRange::between(0, 5);
            const presets::LifetimeWindows tightWindows = presets::lifetimeWindows(tight, 10);
            check(!tightWindows.hasIn && !tightWindows.hasOut,
                  "a lifetime shorter than the window holds nothing");
            presets::lifetimeSlide(tight, presets::Direction::Left, presets::Direction::Right, 10, 5.f, kFps);
            check(tight.tracks.empty(), "lifetimeSlide writes no key it cannot fit");

            Layer single(3, "Single");
            single.lifetime = FrameRange::between(0, 15);
            const presets::LifetimeWindows singleWindows = presets::lifetimeWindows(single, 10);
            check(singleWindows.hasIn && !singleWindows.hasOut,
                  "a lifetime that holds one window gets an entrance but no exit");
            check(!presets::lifetimeWindows(single, 0).hasIn,
                  "a zero-length window has no entrance");
            check(!presets::lifetimeWindows(single, -5).hasIn,
                  "a negative window has no entrance");

            // An open-ended layer still gets a real entrance, keyed from its own
            // in frame; only the exit is absent.
            presets::lifetimeSlide(open, presets::Direction::Up, presets::Direction::Down, 10, 4.f, kFps);
            checkNear(open.sample(at(0)).position, Vector3(0.f, 4.f, 0.f), 1e-3f,
                      "an open-ended layer's entrance still starts on its lifetime's in frame");
            checkNear(open.sample(at(10)).position, Vector3(0.f, 0.f, 0.f), 1e-3f,
                      "the open-ended entrance settles and stays there");
            check(open.tracks.position.keys().size() == 2,
                  "an open-ended layer gets exactly the entrance keys, and no exit");
        }
    }

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

}// namespace

int main() {

    slideAndFadePresets();
    scalePresets();
    rotationPresets();
    motionPresets();
    groupPresets();
    pivotAndLifetimePresets();
    cameraPresets();
    presetTimingUsesFrames();

    return chrononmotion_test::report();
}
