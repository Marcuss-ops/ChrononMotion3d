#include "motion_showcase_shared.hpp"

namespace chrononmotion_test::showcase {
void addTransformCases(std::vector<ShowcaseCase>& cases) {
    const auto add = [&](unsigned number, const char* id, const char* description, std::function<void(Checks&, Snapshot&)> run) {
        cases.push_back({number, id, description, {}, std::move(run)});
    };
    add(1, "transform_grid", "nine independent transform channels", [](Checks& c, Snapshot& s) {
        MotionScene scene("01_transform_grid", kFps);
        for (std::uint32_t i = 0; i < 9; ++i) {
            Layer& layer = addCard(scene, i + 1, "tile_" + std::to_string(i),
                                   Vector3(static_cast<float>(i % 3) * 40.f,
                                           static_cast<float>(i / 3) * 30.f, 0.f));
            const float x = static_cast<float>(i + 1);
            const float t0 = 0.f, t1 = 1.f;
            switch (i) {
                case 0: layer.tracks.position.add(t0, layer.transform.position).add(t1, {x, 0.f, 0.f}); break;
                case 1: layer.tracks.rotation.add(t0, {}).add(t1, {x * .1f, 0.f, 0.f}); break;
                case 2: layer.tracks.rotation.add(t0, {}).add(t1, {0.f, x * .1f, 0.f}); break;
                case 3: layer.tracks.rotation.add(t0, {}).add(t1, {0.f, 0.f, x * .1f}); break;
                case 4: layer.tracks.scale.add(t0, {1.f, 1.f, 1.f}).add(t1, {x * .1f, 1.f, 1.f}); break;
                case 5: layer.tracks.scale.add(t0, {1.f, 1.f, 1.f}).add(t1, {1.f, x * .1f, 1.f}); break;
                case 6: layer.tracks.scale.add(t0, {1.f, 1.f, 1.f}).add(t1, {1.f, 1.f, x * .1f}); break;
                case 7: layer.tracks.anchor.add(t0, {}).add(t1, {x, -x, 0.f}); break;
                case 8: layer.tracks.position.add(t0, layer.transform.position).add(t1, {0.f, x, 0.f}); break;
            }
        }
        c.that(scene.layerCount() == 9, "grid has nine independently animated layers");
        for (std::uint32_t i = 0; i < 9; ++i) {
            const Transform pose = scene.findLayer(i + 1)->sample(1.f);
            switch (i) {
                case 0: c.near(pose.position, Vector3(1.f, 0.f, 0.f), 1e-5f, "tile 0 animates position X"); break;
                case 1: c.near(pose.rotation.x, .2f, 1e-5f, "tile 1 animates rotation X"); break;
                case 2: c.near(pose.rotation.y, .3f, 1e-5f, "tile 2 animates rotation Y"); break;
                case 3: c.near(pose.rotation.z, .4f, 1e-5f, "tile 3 animates rotation Z"); break;
                case 4: c.near(pose.scale.x, .5f, 1e-5f, "tile 4 animates scale X"); break;
                case 5: c.near(pose.scale.y, .6f, 1e-5f, "tile 5 animates scale Y"); break;
                case 6: c.near(pose.scale.z, .7f, 1e-5f, "tile 6 animates scale Z"); break;
                case 7: c.near(pose.anchor, Vector3(8.f, -8.f, 0.f), 1e-5f, "tile 7 animates its anchor"); break;
                case 8: c.near(pose.position.y, 9.f, 1e-5f, "tile 8 animates position Y"); break;
            }
        }
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(2, "easing_gallery", "same move through ten temporal response curves", [](Checks& c, Snapshot& s) {
        MotionScene scene("02_easing_gallery", kFps);
        const std::array<Easing, 9> curves{{Easing::linear(), Easing::easeIn(), Easing::easeOut(),
            Easing::easeInOut(), Easing::circOut(), Easing::expoOut(), Easing::overshoot(),
            Easing::bounceOut(), Easing::elasticOut()}};
        for (std::size_t i = 0; i < curves.size(); ++i) {
            Layer& layer = addCard(scene, static_cast<std::uint32_t>(i + 1), "ease_" + std::to_string(i));
            layer.tracks.position.add(0.f, {}, curves[i]).add(1.f, {100.f, 0.f, 0.f});
            c.near(layer.tracks.position.sample(0.f).x, 0.f, 1e-6f, "easing begins at the authored start");
            c.near(layer.tracks.position.sample(1.f).x, 100.f, 1e-4f, "easing reaches the authored endpoint");
        }
        c.near(curves[0](.5f), .5f, 1e-5f, "linear curve travels halfway at half time");
        c.that(curves[1](.5f) < curves[0](.5f) && curves[2](.5f) > curves[0](.5f),
               "ease-in and ease-out have opposite midpoint responses");
        c.near(curves[3](.5f), .5f, 1e-4f, "symmetric ease-in-out crosses its midpoint halfway");
        c.that(curves[4](.5f) > curves[3](.5f) && curves[5](.5f) > curves[4](.5f),
               "circular and exponential ease-outs accelerate the response tail");
        c.that(curves[6](.4f) > 1.f, "overshoot curve passes beyond its destination before settling");
        c.that(curves[7](.5f) > .5f && curves[8](.5f) > .5f,
               "bounce and elastic curves respond ahead of linear progress");
        Layer& spring = addCard(scene, 10, "spring_settle");
        presets::springSettle(spring, presets::Axis::Y, 0, 30, 12.f, kFps);
        c.near(spring.tracks.position.sample(1.f).y, 0.f, 1e-5f, "spring settle lands on rest");
        c.that(scene.layerCount() == 10, "gallery has ten animation samples");
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f});
    });

    add(3, "moving_pivot", "animated anchor changes the rotation pivot", [](Checks& c, Snapshot& s) {
        MotionScene scene("03_moving_pivot", kFps);
        Layer& card = addCard(scene, 1, "pivot_card");
        card.transform.position.set(10.f, 20.f, 0.f);
        card.transform.anchor.set(0.f, 0.f, 0.f);
        card.tracks.anchor.add(0.f, {0.f, 0.f, 0.f}).add(.5f, {16.f, 12.f, 0.f})
            .add(1.f, {32.f, 0.f, 0.f});
        card.tracks.rotation.add(0.f, {}).add(1.f, {0.f, 0.f, kPi});
        c.near(card.sample(.5f).anchor, Vector3(16.f, 12.f, 0.f), 1e-5f, "pivot reaches its midpoint");
        c.near(card.sample(1.f).anchor, Vector3(32.f, 0.f, 0.f), 1e-5f, "pivot reaches the edge");
        Matrix4 middleMatrix;
        card.sample(.5f).compose(middleMatrix);
        const Vector3 middleOrigin = Vector3{}.applyMatrix4(middleMatrix);
        c.near(middleOrigin, Vector3(22.f, 4.f, 0.f), 2e-4f,
               "animated midpoint pivot changes the rotated card origin");
        Matrix4 edgeMatrix;
        card.sample(1.f).compose(edgeMatrix);
        c.near(Vector3{}.applyMatrix4(edgeMatrix), Vector3(42.f, 20.f, 0.f), 2e-4f,
               "edge pivot changes the final rotated card origin");
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(4, "parent_solar_system", "parent rotation carries child and grandchild", [](Checks& c, Snapshot& s) {
        MotionScene scene("04_parent_solar_system", kFps);
        Layer& sun = scene.addNull(1, "sun");
        sun.tracks.rotation.add(0.f, {}).add(1.f, {0.f, 0.f, kPi});
        Layer& planet = addCard(scene, 2, "planet", {4.f, 0.f, 0.f});
        planet.parentId = 1;
        planet.transform.position.set(4.f, 0.f, 0.f);
        planet.tracks.position.add(0.f, {4.f, 0.f, 0.f}).add(1.f, {4.f, 0.f, 0.f});
        Layer& moon = addCard(scene, 3, "moon", {1.f, 0.f, 0.f});
        moon.parentId = 2;
        scene.evaluatePtsExact(0.f);
        c.near(worldPosition(scene, 3), Vector3(5.f, 0.f, 0.f), 1e-5f, "grandchild starts on the hierarchy");
        scene.evaluatePtsExact(.5f);
        c.near(worldPosition(scene, 2), Vector3(0.f, 4.f, 0.f), 2e-4f, "planet orbits with its parent");
        c.near(worldPosition(scene, 3), Vector3(0.f, 5.f, 0.f), 3e-4f, "moon inherits both rotations");
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f});
    });

    add(5, "parent_scale_torture", "nonuniform parent scale and rotated nested child matrix", [](Checks& c, Snapshot& s) {
        MotionScene scene("05_parent_scale_torture", kFps);
        Layer& parent = scene.addNull(1, "scale_parent");
        parent.transform.scale.set(2.f, 3.f, 1.f);
        Layer& child = addCard(scene, 2, "rotated_child", {1.f, 1.f, 0.f});
        child.parentId = 1;
        child.transform.rotation.z = kPi * .25f;
        Layer& nested = addCard(scene, 3, "nested", {2.f, 0.f, 0.f});
        nested.parentId = 2;
        scene.evaluate(0);
        Matrix4 parentLocal, childLocal, expected;
        parent.transform.compose(parentLocal);
        child.transform.compose(childLocal);
        expected.multiplyMatrices(parentLocal, childLocal);
        c.that(scene.worldMatrix(2).equals(expected), "parented world matrix equals ordered matrix product");
        c.that(std::fabs(scene.worldMatrix(2)[1]) > .5f && std::fabs(scene.worldMatrix(2)[4]) > .5f,
               "nonuniform scale and rotation retain their expected off-diagonal shear terms");
        capture(scene, s, {0.f});
    });

    add(6, "quaternion_spin", "three tilted-axis turns remain continuous", [](Checks& c, Snapshot& s) {
        MotionScene scene("06_quaternion_spin", kFps);
        Layer& layer = addCard(scene, 1, "spinner");
        presets::spinXYZ(layer, 0, 120, 3.f, kFps);
        float accumulated = 0.f;
        float largestStep = 0.f;
        Quaternion previous = layer.tracks.orientation.sample(0.f);
        for (int frame = 1; frame <= 120; ++frame) {
            const Quaternion current = layer.tracks.orientation.sample(secondsAt(frame));
            const float step = previous.angleTo(current);
            largestStep = std::max(largestStep, step);
            accumulated += step;
            previous = current;
        }
        c.near(accumulated, 6.f * kPi, .03f, "quaternion keys preserve all three turns");
        c.that(largestStep < .2f, "no adjacent frame takes a quaternion flip");
        s.number(accumulated); s.number(largestStep);
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f, 4.f});
    });

    add(7, "bezier_path_ride", "layer travels and orients to a cubic path", [](Checks& c, Snapshot& s) {
        MotionScene scene("07_bezier_path_ride", kFps);
        Layer& icon = addCard(scene, 1, "path_icon");
        const MotionPath path = MotionPath::cubicBezier({0.f, 0.f, 0.f}, {0.f, 8.f, 0.f},
                                                          {10.f, 8.f, 0.f}, {10.f, 0.f, 0.f});
        presets::pathMotion(icon, path, 0, 60, kFps, true, 60);
        c.near(icon.tracks.position.sample(0.f), path.sampleUniform(0.f), 1e-5f, "path ride starts at the path origin");
        c.near(icon.tracks.position.sample(2.f), path.sampleUniform(1.f), 1e-5f, "path ride ends at the path endpoint");
        c.that(icon.tracks.orientation.size() == 61, "path ride keys orientation along the tangent");
        Vector3 facing = Vector3::Z();
        facing.applyQuaternion(icon.tracks.orientation.sample(1.f));
        c.near(facing, path.tangent(.5f), 2e-4f, "midpoint orientation aligns local +Z with path tangent");
        capture(scene, s, {0.f, .5f, 1.f, 1.5f, 2.f});
    });

    add(8, "path_constant_speed", "line, bezier and Catmull-Rom use arc-length samples", [](Checks& c, Snapshot& s) {
        const std::array<MotionPath, 3> paths{{
            MotionPath::line({0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}),
            MotionPath::cubicBezier({0.f, 0.f, 0.f}, {0.f, 8.f, 0.f}, {10.f, 8.f, 0.f}, {10.f, 0.f, 0.f}),
            MotionPath::catmullRom({{0.f, 0.f, 0.f}, {5.f, 10.f, 0.f}, {10.f, 0.f, 0.f}})}};
        for (const auto& path : paths) {
            float minStep = std::numeric_limits<float>::max();
            float maxStep = 0.f;
            Vector3 previous = path.sampleUniform(0.f);
            for (int i = 1; i <= 20; ++i) {
                const Vector3 current = path.sampleUniform(static_cast<float>(i) / 20.f);
                const float step = current.distanceTo(previous);
                minStep = std::min(minStep, step);
                maxStep = std::max(maxStep, step);
                previous = current;
                s.vector(current);
            }
            c.that(maxStep - minStep < .04f, "equal path progress produces nearly equal distances");
        }
    });

    add(9, "path_loop_seam", "closed path has no endpoint position seam", [](Checks& c, Snapshot& s) {
        const MotionPath path = MotionPath::catmullRom(
            {{0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}, {4.f, 4.f, 0.f}, {0.f, 4.f, 0.f}}, true);
        const Vector3 start = path.sampleUniform(0.f);
        const Vector3 end = path.sampleUniform(1.f);
        c.near(start, end, 1e-5f, "closed curve endpoints coincide exactly");
        for (int loop = 0; loop < 5; ++loop) {
            s.vector(path.sampleUniform(0.f));
            s.vector(path.sampleUniform(1.f));
        }
        c.that(path.closed(), "path explicitly carries closed topology");
    });

    add(10, "stagger_wall", "fifty cards enter at distinct stagger times", [](Checks& c, Snapshot& s) {
        MotionScene scene("10_stagger_wall", kFps);
        std::vector<Layer*> layers;
        for (std::uint32_t i = 0; i < 50; ++i)
            layers.push_back(&addCard(scene, i + 1, "card_" + std::to_string(i), {0.f, static_cast<float>(i), 0.f}));
        presets::staggeredEntrance(layers, presets::Direction::Left, 0, 2, 24, 100.f, kFps);
        c.that(layers.front()->tracks.position.startTime() < layers.back()->tracks.position.startTime(),
               "later cards start after earlier cards");
        for (std::size_t i = 0; i < layers.size(); ++i) {
            const float start = secondsAt(static_cast<int>(i) * 2);
            c.near(layers[i]->tracks.position.startTime(), start, 1e-6f,
                   "each card receives its exact authored stagger offset");
            c.near(layers[i]->tracks.position.sample(start).x, -100.f, 1e-4f,
                   "each card starts one entrance distance offscreen");
            c.near(layers[i]->tracks.position.sample(start + secondsAt(24)).x, 0.f, 1e-4f,
                   "each card reaches its resting X at its own endpoint");
        }
        c.near(layers.front()->tracks.position.sample(secondsAt(24)).x, 0.f, 1e-5f,
               "first card completes its entrance at its own duration");
        c.that(layers.back()->tracks.position.sample(secondsAt(24)).x < 0.f,
               "last card is still entering at the first card's endpoint");
        capture(scene, s, {0.f, .4f, .8f, 1.6f, 2.4f});
    });

    add(11, "center_out_reveal", "twenty-one elements are ordered from center outward", [](Checks& c, Snapshot& s) {
        MotionScene scene("11_center_out_reveal", kFps);
        std::vector<std::pair<float, Layer*>> radial;
        for (int i = 0; i < 21; ++i) {
            const float x = static_cast<float>(i - 10);
            Layer& layer = addCard(scene, static_cast<std::uint32_t>(i + 1), "radial_" + std::to_string(i), {x, 0.f, 0.f});
            radial.push_back({std::fabs(x), &layer});
        }
        std::stable_sort(radial.begin(), radial.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        std::vector<Layer*> ordered;
        for (const auto& entry : radial) ordered.push_back(entry.second);
        presets::staggeredEntrance(ordered, presets::Direction::Up, 0, 1, 15, 30.f, kFps);
        c.that(ordered.front()->tracks.position.startTime() == 0.f, "center element starts first");
        for (std::size_t i = 0; i < ordered.size(); ++i) {
            c.near(ordered[i]->tracks.position.startTime(), secondsAt(static_cast<int>(i)), 1e-6f,
                   "every radial rank maps to its expected stagger time");
            c.near(std::fabs(ordered[i]->transform.position.x), static_cast<float>((i + 1) / 2), 1e-6f,
                   "stagger rank increases by radial distance with deterministic tie order");
        }
        c.that(ordered.back()->tracks.position.startTime() > ordered[1]->tracks.position.startTime(),
               "edge element starts after near-center elements");
        for (const auto& entry : radial) s.number(entry.first);
        capture(scene, s, {0.f, .25f, .5f, 1.f});
    });

    add(12, "random_deterministic_reveal", "seeded reveal order repeats and changes with seed", [](Checks& c, Snapshot& s) {
        const auto orderFor = [](std::uint32_t seed) {
            std::vector<std::pair<std::uint32_t, std::uint32_t>> ranks;
            for (std::uint32_t i = 0; i < 100; ++i) ranks.emplace_back(xorshift(seed), i);
            std::stable_sort(ranks.begin(), ranks.end());
            std::vector<std::uint32_t> ids;
            for (const auto& rank : ranks) ids.push_back(rank.second);
            return ids;
        };
        const auto first = orderFor(12345U);
        const auto repeat = orderFor(12345U);
        const auto changed = orderFor(54321U);
        c.that(first == repeat, "same seed yields exactly the same 100-element permutation");
        c.that(first != changed, "different seed changes the reveal order");
        c.that(first.size() == 100 && std::set<std::uint32_t>(first.begin(), first.end()).size() == 100,
               "seeded reveal order is a complete permutation without repeated items");
        MotionScene scene("12_random_deterministic_reveal", kFps);
        std::vector<Layer*> cards;
        cards.reserve(100);
        for (std::uint32_t i = 0; i < 100; ++i)
            cards.push_back(&addCard(scene, i + 1, "random_card_" + std::to_string(i)));
        for (std::size_t rank = 0; rank < first.size(); ++rank) {
            Layer& card = *cards[first[rank]];
            presets::fadeSlide(card, presets::Direction::Up, static_cast<int>(rank) * 2, 12, 20.f, kFps);
            c.near(card.tracks.position.startTime(), secondsAt(static_cast<int>(rank) * 2), 1e-6f,
                   "seeded rank authors the matching reveal start time");
        }
        c.that(scene.layerCount() == 100, "seeded permutation drives a 100-layer reveal scene");
        capture(scene, s, {0.f, 1.f, 2.f, 3.f, 5.f, 7.f});
    });

    add(13, "follow_through_chain", "ten linked layers follow with increasing time lag", [](Checks& c, Snapshot& s) {
        MotionScene scene("13_follow_through_chain", kFps);
        std::vector<Layer*> chain;
        for (std::uint32_t i = 0; i < 10; ++i)
            chain.push_back(&addCard(scene, i + 1, "follower_" + std::to_string(i)));
        chain.front()->tracks.position.add(0.f, {}).add(1.f, {100.f, 0.f, 0.f});
        for (std::size_t i = 1; i < chain.size(); ++i)
            presets::followThrough(*chain[i], *chain[i - 1], .04f);
        const auto problems = scene.validate();
        c.that(problems.empty(), "follow-through chain has no link or hierarchy cycle");
        scene.evaluatePtsExact(.5f);
        float prior = worldPosition(scene, chain.front()->id).x;
        for (std::size_t i = 1; i < chain.size(); ++i) {
            const float current = worldPosition(scene, chain[i]->id).x;
            c.that(current < prior + 1e-4f, "each delayed follower trails its source");
            c.near(current, 50.f - static_cast<float>(i) * 4.f, 2e-4f,
                   "each follower accumulates exactly one 0.04-second source lag");
            prior = current;
        }
        capture(scene, s, {0.f, .2f, .5f, .8f, 1.f});
    });

    add(14, "spring_tail", "Verlet tail keeps segment lengths and pinned root", [](Checks& c, Snapshot& s) {
        VerletChainSpec spec;
        for (int i = 0; i < 9; ++i) spec.joints.emplace_back(static_cast<float>(i), 0.f);
        spec.gravity = {0.f, -4.f}; spec.fps = 60.f; spec.iterations = 12;
        const auto baked = bakeVerletChain(spec, 0, 60);
        c.that(baked.samples.size() == 61 && baked.samples.back().size() == 9,
               "tail has sixty-one inclusive frames and nine joints");
        float maximumLengthError = 0.f;
        for (const auto& pose : baked.samples) {
            c.near(pose.front(), spec.joints.front(), 1e-6f, "tail root remains pinned");
            for (std::size_t j = 0; j < pose.size(); ++j)
                c.that(std::isfinite(pose[j].x) && std::isfinite(pose[j].y),
                       "tail joint remains finite at every baked frame");
            for (std::size_t j = 1; j < pose.size(); ++j)
                maximumLengthError = std::max(maximumLengthError, std::fabs(pose[j].distanceTo(pose[j - 1]) - 1.f));
        }
        c.that(maximumLengthError < .03f, "tail preserves its segment lengths within solver tolerance");
        s.number(maximumLengthError); s.vector(baked.samples.back().back());
    });

    add(15, "rigid_drop", "twenty 2D bodies remain above the floor", [](Checks& c, Snapshot& s) {
        RigidBodyWorld2D world;
        world.gravity = {0.f, -9.81f}; world.floorY = 0.f; world.left = -12.f; world.right = 12.f;
        world.fps = 30.f; world.solverIterations = 8;
        std::vector<RigidBody2D> bodies;
        for (std::uint32_t i = 0; i < 20; ++i)
            bodies.push_back({i + 1, {-9.f + static_cast<float>(i) * .9f, 5.f + static_cast<float>(i % 3)},
                              {0.f, 0.f}, .35f, 1.f, .35f, .1f});
        const auto baked = bakeRigidBodies2D(bodies, world, 0, 180);
        c.that(baked.samples.size() == 181 && baked.positionTracks.size() == 20,
               "rigid drop bakes all bodies across the inclusive frame range");
        for (std::size_t frameIndex = 0; frameIndex < baked.samples.size(); ++frameIndex) {
            const auto& frame = baked.samples[frameIndex];
            c.that(frame.size() == bodies.size(), "every rigid-body frame preserves the full body count");
            for (std::size_t i = 0; i < frame.size(); ++i) {
                const auto& pose = frame[i];
                c.that(std::isfinite(pose.x) && std::isfinite(pose.y) &&
                       pose.y >= world.floorY + .349f && pose.x >= world.left + .349f &&
                       pose.x <= world.right - .349f,
                       "every circle remains finite and inside the floor/wall bounds");
                c.near(baked.positionTracks[i].sample(secondsAt(static_cast<int>(frameIndex))),
                       Vector3(pose.x, pose.y, 0.f), 1e-5f,
                       "baked rigid-body track reproduces its stored frame sample");
            }
        }
        for (std::size_t frameIndex = 0; frameIndex < baked.samples.size(); ++frameIndex) {
            const auto& frame = baked.samples[frameIndex];
            for (std::size_t i = 0; i < frame.size(); ++i) {
                const auto& body = bodies[i];
                c.that(frame[i].y >= world.floorY + body.radius - 1e-3f,
                       "every saved frame keeps circles above the floor");
                for (std::size_t j = i + 1; j < frame.size(); ++j)
                    c.that(frame[i].distanceTo(frame[j]) >= body.radius + bodies[j].radius - 2e-2f,
                           "collision solver separates every body pair at each saved frame");
            }
        }
        for (const auto& pose : baked.samples.back()) s.vector(pose);
    });
}
} // namespace chrononmotion_test::showcase
