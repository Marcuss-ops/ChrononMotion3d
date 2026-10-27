#include "motion_presets_internal.hpp"
namespace motion_presets_test {


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
}
