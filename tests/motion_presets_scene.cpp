#include "motion_presets_shared.hpp"

namespace chrononmotion_test::preset_tests {

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

} // namespace chrononmotion_test::preset_tests

