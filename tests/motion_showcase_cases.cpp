#include "motion_showcase_internal.hpp"
#include "gltf_motion_fixture.hpp"
#include "hdri_fixture.hpp"
#if defined(CHRONONMOTION_SHOWCASE_HAS_ADAPTER)
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include <nlohmann/json.hpp>
#endif
namespace motion_showcase_detail {
std::vector<ShowcaseCase> makeCases() {
    std::vector<ShowcaseCase> cases;
    cases.reserve(63);
    const auto add = [&](unsigned number, const char* id, const char* description,
                         std::function<void(Checks&, Snapshot&)> run) {
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

    add(16, "two_bone_arm", "two-segment IK follows a circular reachable target", [](Checks& c, Snapshot& s) {
        for (int frame = 0; frame <= 60; ++frame) {
            const float angle = static_cast<float>(frame) / 60.f * 2.f * kPi;
            const Vector2 target(1.4f * std::cos(angle), 1.4f * std::sin(angle));
            const auto solved = solveTwoSegmentIK2D({}, target, 1.f, 1.f);
            c.that(solved.targetReachable, "circular arm target remains reachable");
            c.near(solved.end, target, 1e-5f, "arm endpoint meets its target");
            c.near(solved.elbow.distanceTo({}), 1.f, 1e-5f, "upper bone retains its length");
            c.near(solved.end.distanceTo(solved.elbow), 1.f, 1e-5f, "lower bone retains its length");
            s.vector(Vector3(solved.elbow.x, solved.elbow.y, 0.f));
        }
    });

    add(17, "ik_reach_limits", "2D IK clamps inner and outer unreachable targets", [](Checks& c, Snapshot& s) {
        const auto far = solveTwoSegmentIK2D({}, {8.f, 0.f}, 2.f, 1.f);
        const auto near = solveTwoSegmentIK2D({}, {.1f, 0.f}, 2.f, 1.f);
        const auto maxEdge = solveTwoSegmentIK2D({}, {3.f, 0.f}, 2.f, 1.f);
        const auto minEdge = solveTwoSegmentIK2D({}, {1.f, 0.f}, 2.f, 1.f);
        c.that(!far.targetReachable && !near.targetReachable, "targets outside either side of the reachable annulus are reported");
        c.near(far.end.distanceTo({}), 3.f, 1e-5f, "outer target clamps to total limb length");
        c.near(near.end.distanceTo({}), 1.f, 1e-5f, "inner target clamps to the minimum limb radius");
        for (const auto* solved : {&far, &near, &maxEdge, &minEdge}) {
            c.near(solved->elbow.distanceTo({}), 2.f, 1e-5f, "upper segment length survives reach clamping");
            c.near(solved->end.distanceTo(solved->elbow), 1.f, 1e-5f,
                   "lower segment length survives reach clamping");
        }
        c.that(maxEdge.targetReachable && minEdge.targetReachable, "both annulus boundaries are reachable");
        s.vector(Vector3(far.end.x, far.end.y, 0.f)); s.vector(Vector3(near.end.x, near.end.y, 0.f));
    });

    add(18, "general_ik_snake", "eight-segment FABRIK chain tracks a moving reachable target", [](Checks& c, Snapshot& s) {
        IK3BakeSpec spec;
        for (int i = 0; i <= 8; ++i) spec.initialJoints.emplace_back(static_cast<float>(i), 0.f, 0.f);
        spec.pole = {4.f, 0.f, 8.f}; spec.fps = 30.f; spec.iterations = 64; spec.tolerance = 1e-5f;
        spec.targetTrack.add(0.f, {8.f, 0.f, 0.f}).add(1.f, {5.f, 4.f, 0.f});
        const auto baked = bakeIKChain3D(spec, 0, 30);
        c.that(baked.samples.size() == 31 && baked.jointPositions.size() == 9,
               "general IK bakes every joint in the eight-segment chain");
        for (std::size_t frame = 0; frame < baked.samples.size(); ++frame) {
            const auto& pose = baked.samples[frame];
            c.near(pose.front(), spec.initialJoints.front(), 1e-6f, "snake root stays pinned");
            for (std::size_t joint = 1; joint < pose.size(); ++joint)
                c.near(pose[joint].distanceTo(pose[joint - 1]), 1.f, 3e-3f, "snake segment length remains one unit");
            c.that(baked.targetReachable[frame], "snake target remains within total chain reach");
            c.near(pose.back(), spec.targetTrack.sample(secondsAt(static_cast<int>(frame))), 3e-3f,
                   "snake end effector converges to the sampled target");
        }
        s.vector(baked.samples.back().back());
    });

    add(19, "pole_vector_knee", "pole position selects consistent opposite knee planes", [](Checks& c, Snapshot& s) {
        const std::array<Vector3, 3> rest{{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {2.f, 0.f, 0.f}}};
        float positive = 0.f, negative = 0.f;
        for (int i = 0; i < 11; ++i) {
            const Vector3 target(1.f + .2f * static_cast<float>(i) / 10.f, .7f, 0.f);
            const auto a = solveIKChain3D(rest, target, {0.f, 0.f, 3.f});
            const auto b = solveIKChain3D(rest, target, {0.f, 0.f, -3.f});
            positive += a.joints[1].z;
            negative += b.joints[1].z;
            c.that(a.joints[1].z > 0.f && b.joints[1].z < 0.f,
                   "each solved knee stays on its requested pole side");
            s.vector(a.joints[1]); s.vector(b.joints[1]);
        }
        c.that(positive > 0.f && negative < 0.f, "pole vector holds the knee on the requested side");
    });

    add(20, "space_switch", "hand switches world/torso/object space without a pop", [](Checks& c, Snapshot& s) {
        MotionScene scene("20_space_switch", kFps);
        Layer& torso = scene.addNull(1, "torso");
        torso.tracks.position.add(0.f, {}).add(1.f, {10.f, 0.f, 0.f});
        Layer& object = scene.addNull(2, "object");
        object.tracks.position.add(0.f, {}).add(1.f, {0.f, 10.f, 0.f});
        Layer& hand = addCard(scene, 3, "hand", {2.f, 0.f, 0.f});
        hand.spaceSwitches = {{.5f, 1}, {.75f, 2}};
        scene.evaluatePtsExact(.49f);
        const Matrix4 beforeTorsoHandoff = scene.worldMatrix(3);
        scene.evaluatePtsExact(.5f);
        const Vector3 worldAtTorsoHandoff = worldPosition(scene, 3);
        c.near(worldAtTorsoHandoff, Vector3(2.f, 0.f, 0.f), 1e-4f, "first handoff preserves world position");
        for (std::size_t element = 0; element < beforeTorsoHandoff.elements.size(); ++element)
            c.near(scene.worldMatrix(3).elements[element], beforeTorsoHandoff.elements[element], .011f,
                   "first handoff preserves every world-matrix component");
        scene.evaluatePtsExact(.749f);
        const Matrix4 immediatelyBeforeObject = scene.worldMatrix(3);
        scene.evaluatePtsExact(.75f);
        for (std::size_t element = 0; element < immediatelyBeforeObject.elements.size(); ++element)
            c.near(scene.worldMatrix(3).elements[element], immediatelyBeforeObject.elements[element], .011f,
                   "second handoff preserves every world-matrix component");
        scene.evaluatePtsExact(1.f);
        c.near(worldPosition(scene, 3), Vector3(4.5f, 2.5f, 0.f), 1e-4f,
               "hand follows its final object space with maintained offset");
        capture(scene, s, {0.f, .5f, .75f, 1.f});
    });

    add(21, "aim_turret", "Aim constraint tracks a moving target", [](Checks& c, Snapshot& s) {
        MotionScene scene("21_aim_turret", kFps);
        addCard(scene, 1, "target", {3.f, 4.f, 1.f}).tracks.position.add(0.f, {3.f, 4.f, 1.f}).add(1.f, {-2.f, 5.f, 1.f});
        Layer& turret = addCard(scene, 2, "turret");
        ConstraintSpec aim; aim.kind = ConstraintKind::Aim; aim.targetId = 1;
        turret.constraints.push_back(aim);
        for (const float time : {0.f, .25f, .5f, .75f, 1.f}) {
            scene.evaluatePtsExact(time);
            Vector3 desired = worldPosition(scene, 1) - worldPosition(scene, 2); desired.normalize();
            Vector3 actual = Vector3::Z(); actual.applyQuaternion(scene.state(2).local.orientation);
            c.near(actual, desired, 2e-4f, "turret local +Z aims at the target");
            s.vector(actual);
        }
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(22, "billboard_cards", "billboard orientation faces the camera position", [](Checks& c, Snapshot& s) {
        MotionScene scene("22_billboard_cards", kFps);
        addCard(scene, 1, "camera", {0.f, 0.f, 10.f});
        Layer& card = addCard(scene, 2, "billboard", {2.f, 1.f, 0.f});
        ConstraintSpec billboard; billboard.kind = ConstraintKind::Billboard; billboard.targetId = 1;
        card.constraints.push_back(billboard);
        scene.evaluate(0);
        Vector3 expected = worldPosition(scene, 1) - worldPosition(scene, 2); expected.normalize();
        Vector3 facing = Vector3::Z(); facing.applyQuaternion(scene.state(2).local.orientation);
        c.near(facing, expected, 2e-4f, "billboard normal points toward the camera");
        capture(scene, s, {0.f});
    });

    add(23, "keep_distance_orbit", "follower preserves target radius while target moves", [](Checks& c, Snapshot& s) {
        MotionScene scene("23_keep_distance_orbit", kFps);
        Layer& target = addCard(scene, 1, "orbit_target");
        target.tracks.position.add(0.f, {}).add(1.f, {5.f, 4.f, 0.f});
        Layer& satellite = addCard(scene, 2, "satellite", {5.f, 0.f, 0.f});
        ConstraintSpec keep; keep.kind = ConstraintKind::KeepDistance; keep.targetId = 1; keep.distance = 5.f;
        satellite.constraints.push_back(keep);
        for (const float time : {0.f, .2f, .4f, .6f, .8f, 1.f}) {
            scene.evaluatePtsExact(time);
            c.near(worldPosition(scene, 2).distanceTo(worldPosition(scene, 1)), 5.f, 2e-4f,
                   "satellite remains at the authored distance");
            s.vector(worldPosition(scene, 2));
        }
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(24, "constraint_blend", "Aim constraint blends continuously from free motion to target", [](Checks& c, Snapshot& s) {
        MotionScene scene("24_constraint_blend", kFps);
        addCard(scene, 1, "aim_target", {0.f, 10.f, 0.f});
        Layer& follower = addCard(scene, 2, "aim_follower");
        ConstraintSpec aim; aim.kind = ConstraintKind::Aim; aim.targetId = 1;
        aim.amountTrack = std::make_shared<Track<float>>();
        aim.amountTrack->add(0.f, 0.f).add(1.f, 1.f);
        follower.constraints.push_back(aim);
        scene.evaluatePtsExact(0.f);
        const Quaternion free = scene.state(2).local.orientation;
        scene.evaluatePtsExact(.5f);
        const Quaternion halfway = scene.state(2).local.orientation;
        scene.evaluatePtsExact(1.f);
        const Quaternion aimed = scene.state(2).local.orientation;
        c.near(aimed.angleTo(free), kPi * .5f, 2e-4f, "amount 1 reaches the fully aimed orientation");
        c.near(halfway.angleTo(free), kPi * .25f, 2e-4f, "amount .5 slerps halfway from free motion");
        scene.evaluatePtsExact(.499f);
        const Quaternion before = scene.state(2).local.orientation;
        scene.evaluatePtsExact(.501f);
        c.that(before.angleTo(scene.state(2).local.orientation) < .01f, "animated influence crosses its midpoint without a pop");
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f});
    });

    add(25, "svg_morph_basic", "topology-compatible cubic paths morph deterministically", [](Checks& c, Snapshot& s) {
        const auto frames = bakeSvgPathMorph("M0 0 C0 1 1 1 1 0 Z", "M0 0 C0 3 3 3 3 0 Z", 0, 30);
        const auto repeated = bakeSvgPathMorph("M0 0 C0 1 1 1 1 0 Z", "M0 0 C0 3 3 3 3 0 Z", 0, 30);
        c.that(frames.size() == 31, "SVG morph emits the inclusive frame count");
        c.samePaths(frames, repeated, "SVG morph frames repeat bit-exactly");
        c.near(frames.front()[1].point, Vector2(1.f, 0.f), 1e-6f, "morph starts at source path");
        c.near(frames[15][1].point, Vector2(2.f, 0.f), 1e-6f, "morph midpoint interpolates path endpoint");
        c.near(frames[15][1].control1, Vector2(0.f, 2.f), 1e-6f,
               "morph midpoint interpolates the first cubic control point");
        c.near(frames[15][1].control2, Vector2(2.f, 2.f), 1e-6f,
               "morph midpoint interpolates the second cubic control point");
        c.that(frames.front().size() == 3 && frames[15].size() == 3 && frames.back().size() == 3 &&
               frames.front().back().type == BakedPathCommandType::Close &&
               frames[15].back().type == BakedPathCommandType::Close &&
               frames.back().back().type == BakedPathCommandType::Close,
               "morph preserves closed path topology across the full bake");
        c.near(frames.back()[1].point, Vector2(3.f, 0.f), 1e-6f, "morph ends at destination path");
        for (const auto& frame : frames) for (const auto& command : frame) {
            s.integer(static_cast<int>(command.type)); s.vector(command.point);
            s.vector(command.control1); s.vector(command.control2);
        }
    });

    add(26, "svg_morph_adversarial", "SVG parser normalizes relative and smooth path commands", [](Checks& c, Snapshot& s) {
        const BakedPath path = parseSvgPath("M10 10 l5 0 h5 v5 c1 0 1 1 2 1 s2 1 2 0 q1 -1 2 0 t2 0 a2 2 0 0 1 4 0 z");
        c.that(path.size() == 11, "all adversarial path segments normalize to eleven commands");
        c.that(path.front().type == BakedPathCommandType::MoveTo && path.back().type == BakedPathCommandType::Close,
               "relative path preserves its subpath boundary");
        c.near(path[1].point, Vector2(15.f, 10.f), 1e-5f, "relative line resolves from current point");
        c.near(path[4].point, Vector2(22.f, 16.f), 1e-5f, "cubic relative endpoint resolves correctly");
        c.near(path[9].point, Vector2(32.f, 16.f), 1e-4f, "relative arc preserves requested endpoint");
        for (const auto& command : path) { s.integer(static_cast<int>(command.type)); s.vector(command.point); }
    });

    add(27, "svg_repeater_spiral", "path repeater produces fifty deterministic transformed copies", [](Checks& c, Snapshot& s) {
        const BakedPath path = parseSvgPath("M10 0 L12 0");
        const RepeaterSpec spec{50, {1.f, 0.f}, {.99f, .99f}, .12f, {0.f, 0.f}};
        const auto first = expandPathRepeater(path, spec);
        const auto second = expandPathRepeater(path, spec);
        c.that(first.size() == 50 && first.size() == second.size(), "repeater creates fifty copies");
        c.near(first.front()[0].point, Vector2(10.f, 0.f), 1e-6f,
               "first repeated copy preserves the source path exactly");
        const Vector2 expectedSecond(1.f + 9.9f * std::cos(.12f), 9.9f * std::sin(.12f));
        c.near(first[1][0].point, expectedSecond, 2e-5f,
               "second copy applies its authored scale, rotation and translation");
        c.that(first.back()[0].point != first.front()[0].point, "repeated path accumulates translation and rotation");
        c.samePaths(first, second, "repeater geometry is deterministic");
        s.vector(first.front()[0].point); s.vector(first[24][0].point); s.vector(first.back()[0].point);
    });

    add(28, "svg_bone_arm", "SVG arm path follows a deterministic two-bone skin rig", [](Checks& c, Snapshot& s) {
        const BakedPath rest = parseSvgPath("M0 0 C1 0 2 0 3 0");
        const std::array<SvgBone, 2> bones{{{{0.f, 0.f}, {1.f, 0.f}, -1}, {{1.f, 0.f}, {3.f, 0.f}, 0}}};
        std::vector<std::vector<SvgSkinBinding>> bindings(rest.size() * 3);
        for (auto& binding : bindings) binding.push_back({0, 1.f});
        bindings[3].assign({{1, 1.f}}); // First cubic control follows the forearm.
        bindings[4].assign({{1, 1.f}});
        bindings[5].assign({{1, 1.f}});
        std::vector<std::vector<Matrix4>> boneWorld(2, std::vector<Matrix4>(2));
        boneWorld[1][1].makeTranslation(0.f, 1.f, 0.f);
        const auto frames = bakeSvgSkinDeform(rest, bones, bindings, boneWorld, 0, 1);
        c.that(frames.size() == 2 && frames[0][0].size() == rest.size(), "skinning bakes both frames and preserves path topology");
        c.near(frames[0][0][0].point, rest[0].point, 1e-6f, "shoulder/base control remains fixed");
        c.near(frames[1][0][1].point, Vector2(3.f, 1.f), 1e-6f, "forearm-bound endpoint follows its bone transform");
        c.near(frames[1][0][1].point.y - frames[0][0][1].point.y, 1.f, 1e-6f, "distal arm deforms by the expected bone translation");
        for (const auto& frame : frames) for (const auto& command : frame[0]) {
            s.integer(static_cast<int>(command.type)); s.vector(command.point);
            s.vector(command.control1); s.vector(command.control2);
        }
    });
    add(29, "svg_puppet_flag", "SVG flag transfers a pin oscillation through stiffness falloff", [](Checks& c, Snapshot& s) {
        const BakedPath rest = parseSvgPath("M0 0 L1 0 L2 0 L3 0 L4 0");
        const std::array<PathPin, 1> pins{{{0.f, {0.f, 0.f}, 1.f, {0.f, 1.f}}}};
        const auto frames = bakePathPinDeform(rest, pins, .2f, 0, 8);
        const auto repeated = bakePathPinDeform(rest, pins, .2f, 0, 8);
        c.that(frames.size() == 9 && frames[4][0].size() == rest.size(), "pinned flag bakes inclusive frames and preserves topology");
        c.near(frames[4][0][0].point, Vector2(0.f, 0.f), 1e-6f, "flag mast pin remains anchored");
        const float nearDisplacement = frames[2][0][2].point.y;
        const float farDisplacement = frames[2][0][4].point.y;
        c.that(farDisplacement > nearDisplacement && nearDisplacement > 0.f,
               "oscillation grows with distance from the stiff pin");
        c.near(frames[2][0][4].point, repeated[2][0][4].point, 0.f, "pin deformation repeats bit-exactly");
        for (const auto& frame : frames) for (const auto& command : frame[0]) s.vector(command.point);
    });

    add(30, "squash_stretch_icon", "transform-level squash and stretch returns to rest", [](Checks& c, Snapshot& s) {
        MotionScene scene("30_squash_stretch_icon", kFps);
        Layer& icon = addCard(scene, 1, "impact_icon");
        icon.tracks.scale.add(0.f, {1.f, 1.f, 1.f}, Easing::overshoot())
            .add(.4f, {1.35f, .65f, 1.f}, Easing::easeOut())
            .add(.7f, {.85f, 1.2f, 1.f}).add(1.f, {1.f, 1.f, 1.f});
        c.near(icon.tracks.scale.sample(.4f), Vector3(1.35f, .65f, 1.f), 1e-5f, "impact pose squashes vertically");
        c.that(icon.tracks.scale.sample(.2f).x > 1.35f && icon.tracks.scale.sample(.2f).y < .65f,
               "overshoot easing briefly exaggerates the squash before settling");
        c.near(icon.tracks.scale.sample(1.f), Vector3(1.f, 1.f, 1.f), 1e-5f, "icon restores its exact rest scale");
        capture(scene, s, {0.f, .2f, .4f, .7f, 1.f});
    });

    add(31, "wave_deformer", "deterministic wave deforms SVG ribbon geometry", [](Checks& c, Snapshot& s) {
        const BakedPath rest = parseSvgPath("M0 0 C1 1 2 1 3 0");
        WaveDeformerSpec wave; wave.axis = WaveAxis::Y; wave.amplitude = .4f;
        wave.wavelength = 3.f; wave.speed = kPi;
        wave.falloff = FalloffField{{1.5f, 0.f}, 10.f, 0.f, FalloffField::Kind::Linear};
        const auto frames = bakeWaveDeform(rest, wave, 0, 15, kFps);
        const auto repeated = bakeWaveDeform(rest, wave, 0, 15, kFps);
        c.that(frames.size() == 16, "wave baker emits every requested frame");
        c.samePath(frames.front(), rest, "wave frame zero equals its rest geometry");
        c.samePath(frames[8], repeated[8], "wave samples are bit-repeatable");
        c.that(std::fabs(frames[8][1].point.y - rest[1].point.y) > .1f,
               "wave reaches measurable displacement within full falloff");
        for (const auto& frame : frames) for (const auto& command : frame) {
            s.integer(static_cast<int>(command.type)); s.vector(command.point);
            s.vector(command.control1); s.vector(command.control2);
        }
    });
    add(32, "grid_falloff_wave", "linear falloff sweeps a 20 by 20 layer grid", [](Checks& c, Snapshot& s) {
        MotionScene scene("32_grid_falloff_wave", kFps);
        constexpr int side = 20;
        for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) {
            const std::uint32_t id = static_cast<std::uint32_t>(y * side + x + 1);
            Layer& card = addCard(scene, id, "falloff_" + std::to_string(id),
                                  {static_cast<float>(x), static_cast<float>(y), 0.f});
            Track<float> influence;
            for (int frame = 0; frame <= 30; ++frame) {
                const float time = secondsAt(frame);
                FalloffField field{{time * 19.f, static_cast<float>(y)}, 4.f, 0.f, FalloffField::Kind::Linear};
                influence.add(time, field.evaluate({static_cast<float>(x), static_cast<float>(y)}));
            }
            card.tracks.opacity = influence;
        }
        c.that(scene.layerCount() == 400, "falloff grid contains exactly four hundred cards");
        for (int x = 1; x < side - 1; ++x)
            c.that(scene.findLayer(static_cast<std::uint32_t>((side / 2) * side + x + 1))->tracks.opacity.sample(
                       secondsAt(static_cast<int>(std::lround((x - 1) * 30.f / 19.f)))) > 0.f,
                   "falloff sweep yields a nonzero influence through the grid");
        for (const float time : {0.f, .25f, .5f, .75f, 1.f}) {
            scene.evaluatePtsExact(time);
            s.number(time);
            for (int x = 0; x < side; ++x) s.number(scene.state(static_cast<std::uint32_t>((side / 2) * side + x + 1)).worldOpacity);
        }
        c.that(FalloffField{{0.f, 0.f}, 4.f, 0.f, FalloffField::Kind::Linear}.evaluate({0.f, 0.f}) >
               FalloffField{{0.f, 0.f}, 4.f, 0.f, FalloffField::Kind::Linear}.evaluate({2.f, 0.f}),
               "spatial falloff decreases monotonically away from its moving origin");
    });
    add(33, "radial_falloff_flip", "radial falloff rotates cards most strongly at its center", [](Checks& c, Snapshot& s) {
        MotionScene scene("33_radial_falloff_flip", kFps);
        FalloffField field{{2.f, 2.f}, 2.5f, 0.f, FalloffField::Kind::Radial};
        for (int y = 0; y < 5; ++y) for (int x = 0; x < 5; ++x) {
            const std::uint32_t id = static_cast<std::uint32_t>(y * 5 + x + 1);
            Layer& card = addCard(scene, id, "radial_" + std::to_string(id), {static_cast<float>(x), static_cast<float>(y), 0.f});
            const float rotation = field.evaluate({static_cast<float>(x), static_cast<float>(y)}) * kPi;
            card.tracks.rotation.add(0.f, {}).add(1.f, {0.f, 0.f, rotation});
        }
        const Vector3 center = scene.findLayer(13)->tracks.rotation.sample(1.f);
        const Vector3 nearCenter = scene.findLayer(12)->tracks.rotation.sample(1.f);
        const Vector3 outsideRadius = scene.findLayer(25)->tracks.rotation.sample(1.f);
        c.near(center.z, kPi, 1e-6f, "center card receives maximum radial rotation");
        c.near(outsideRadius.z, 0.f, 1e-6f, "card beyond the radius receives exactly zero rotation");
        c.that(std::fabs(nearCenter.z) > 0.f && std::fabs(nearCenter.z) < std::fabs(center.z),
               "radial falloff reduces rotation with distance from the center");
        c.that(std::fabs(center.x) < 1e-6f && std::fabs(center.y) < 1e-6f &&
               std::fabs(outsideRadius.x) < 1e-6f && std::fabs(outsideRadius.y) < 1e-6f,
               "radial flip only rotates around the intended card normal");
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f});
    });
    add(34, "cloner_spiral", "scene cloner distributes two hundred deterministic spiral layers", [](Checks& c, Snapshot& s) {
        MotionScene scene("34_cloner_spiral", kFps);
        Layer& prototype = addCard(scene, 1000, "spiral_prototype");
        prototype.tracks.opacity.add(0.f, 0.f).add(.5f, 1.f);
        ClonerSpec spec; spec.count = 200; spec.pattern = ClonerSpec::Pattern::Spiral;
        spec.spiralRadiusStep = .1f; spec.spiralAngleStepRad = .17f; spec.timeOffsetSeconds = .01f;
        const auto clones = instantiateCloner(scene, prototype, spec, "spiral_");
        c.that(clones.size() == 200 && scene.layerCount() == 201, "cloner adds exactly two hundred scene layers");
        c.that(clones.front()->id == 1 && clones.back()->id == 200, "clone IDs are allocated deterministically around occupied IDs");
        const Vector3 second = clones[1]->transform.position;
        c.near(second, Vector3(.1f * std::cos(.17f), .1f * std::sin(.17f), 0.f), 1e-6f,
               "second clone lies at its indexed spiral pose");
        c.near(clones[1]->tracks.opacity.startTime(), .01f, 1e-6f, "per-copy reveal is staggered by its time offset");
        c.that(clones[199]->transform.position.length() > clones[1]->transform.position.length(),
               "spiral radius increases monotonically with clone index");
        s.text(scene.toJson(0));
        for (const Layer* clone : {clones.front(), clones[1], clones[99], clones.back()}) {
            s.integer(clone->id); s.vector(clone->transform.position);
            s.number(clone->tracks.opacity.startTime());
        }
    });
    add(35, "magnetic_swarm", "attractor and repulsor move two hundred deterministic elements", [](Checks& c, Snapshot& s) {
        std::vector<Vector2> points;
        points.reserve(200);
        for (int i = 0; i < 200; ++i) {
            const float x = static_cast<float>(i % 20) - 10.f;
            const float y = static_cast<float>(i / 20) - 5.f;
            points.emplace_back(x, y);
        }
        const std::array<FieldEffectorSpec, 2> fields{{
            {FieldEffectorSpec::Kind::Attractor, {-8.f, 0.f}, 2.f, 3.f, .5f},
            {FieldEffectorSpec::Kind::Repulsor, {8.f, 0.f}, 2.f, 3.f, .5f}}};
        const auto baked = bakeFieldSimulation(points, fields, 0, 30, kFps);
        c.that(baked.size() == 31 && baked.back().size() == 200, "swarm baker covers every point and inclusive frame");
        c.that(baked.back()[101].distanceTo(Vector2(-8.f, 0.f)) < points[101].distanceTo(Vector2(-8.f, 0.f)),
               "attractor converges its nearby swarm element");
        c.that(baked.back()[119].distanceTo(Vector2(8.f, 0.f)) > points[119].distanceTo(Vector2(8.f, 0.f)),
               "repulsor disperses its nearby swarm element");
        for (const auto& pose : baked) for (const Vector2& point : pose) {
            c.that(std::isfinite(point.x) && std::isfinite(point.y), "all swarm positions remain finite");
        }
        for (const std::size_t index : {std::size_t{101}, std::size_t{119}, std::size_t{199}}) s.vector(baked.back()[index]);
    });
    add(36, "vortex", "vortex maintains outer orbits and expels its inner core", [](Checks& c, Snapshot& s) {
        std::vector<Vector2> points;
        points.reserve(200);
        for (int i = 0; i < 200; ++i) {
            const float angle = static_cast<float>(i) * .13f;
            const float radius = i % 2 == 0 ? 2.f : .25f;
            points.emplace_back(radius * std::cos(angle), radius * std::sin(angle));
        }
        const FieldEffectorSpec vortex{FieldEffectorSpec::Kind::Vortex, {}, 2.f, 5.f, .5f};
        const auto baked = bakeFieldSimulation(points, vortex, 0, 30, kFps);
        const float orbitBefore = points[0].length();
        const float orbitAfter = baked.back()[0].length();
        c.near(orbitAfter, orbitBefore, 1e-5f, "outer vortex point keeps a stable orbit radius");
        c.that(baked.back()[1].length() > points[1].length(), "inner vortex core point is expelled radially");
        c.that(baked.size() == 31 && baked.back().size() == 200, "vortex bakes all deterministic point samples");
        for (const auto& point : baked.back()) c.that(std::isfinite(point.x) && std::isfinite(point.y), "vortex output contains no NaNs");
        s.vector(baked.back()[0]); s.vector(baked.back()[1]); s.vector(baked.back()[199]);
    });

    add(37, "bento_assemble", "twelve panels assemble into a non-overlapping flow grid", [](Checks& c, Snapshot& s) {
        MotionScene scene("37_bento_assemble", kFps);
        std::vector<FlowItemResult> layouts;
        for (int row = 0; row < 3; ++row) {
            std::array<FlowItem, 4> items;
            for (int col = 0; col < 4; ++col) {
                const std::uint32_t id = static_cast<std::uint32_t>(row * 4 + col + 1);
                items[static_cast<std::size_t>(col)] = {id, {35.f + static_cast<float>((id % 3) * 5), 22.f}, {.5f, .5f}};
            }
            FlowSpec flow; flow.axis = FlowAxis::Row; flow.gap = 4.f; flow.containerSize = {190.f, 30.f};
            const auto result = solveFlowLayout(flow, items, {0.f, static_cast<float>(row) * 36.f});
            layouts.insert(layouts.end(), result.items.begin(), result.items.end());
        }
        for (const auto& layout : layouts) {
            Layer& panel = addCard(scene, layout.id, "panel_" + std::to_string(layout.id),
                                   {layout.position.x, layout.position.y, 0.f});
            const Vector3 rest = panel.transform.position;
            panel.tracks.position.add(0.f, rest + Vector3(0.f, -40.f, 20.f), Easing::overshoot()).add(1.f, rest);
            panel.tracks.opacity.add(0.f, 0.f).add(.5f, 1.f);
        }
        c.that(layouts.size() == 12, "bento contains twelve flow-positioned panels");
        for (std::size_t i = 0; i < layouts.size(); ++i) for (std::size_t j = i + 1; j < layouts.size(); ++j)
            c.that(!layouts[i].bounds.intersectsBox(layouts[j].bounds), "bento layout has no overlapping panel bounds");
        for (const auto& layout : layouts) {
            const Layer* panel = scene.findLayer(layout.id);
            c.near(panel->tracks.position.sample(1.f), Vector3(layout.position.x, layout.position.y, 0.f),
                   1e-5f, "assembled panel lands at its solved flow position");
            c.near(panel->tracks.opacity.sample(0.f), 0.f, 1e-6f, "panel begins its reveal transparent");
            c.near(panel->tracks.opacity.sample(.5f), 1.f, 1e-6f, "panel reveal reaches full opacity");
        }
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(38, "auto_layout_localization", "longer measured content expands layout without overlap", [](Checks& c, Snapshot& s) {
        const std::array<FlowItem, 3> shortItems{{{1, {40.f, 20.f}}, {2, {50.f, 20.f}}, {3, {30.f, 20.f}}}};
        const std::array<FlowItem, 3> longItems{{{1, {80.f, 20.f}}, {2, {110.f, 20.f}}, {3, {70.f, 20.f}}}};
        FlowSpec flow; flow.axis = FlowAxis::Row; flow.gap = 8.f;
        const auto shortLayout = solveFlowLayout(flow, shortItems);
        const auto longLayout = solveFlowLayout(flow, longItems);
        MotionScene shortScene("38_short_localization", kFps), longScene("38_long_localization", kFps);
        for (const auto& item : shortItems)
            (void)addCard(shortScene, item.id, "short_" + std::to_string(item.id));
        for (const auto& item : longItems)
            (void)addCard(longScene, item.id, "long_" + std::to_string(item.id));
        const auto refreshedShort = refreshFlowLayout(flow, shortItems, shortScene);
        const auto refreshedLong = refreshFlowLayout(flow, longItems, longScene);
        c.that(longLayout.bounds.getSize().x > shortLayout.bounds.getSize().x &&
               refreshedLong.bounds.getSize().x > refreshedShort.bounds.getSize().x,
               "translated content measurement grows the intrinsic layout width");
        c.that(refreshedShort.items.size() == shortItems.size() && refreshedLong.items.size() == longItems.size(),
               "measurement refresh resolves each item back into its scene");
        for (std::size_t i = 1; i < longLayout.items.size(); ++i)
            c.that(longLayout.items[i].bounds.getMin().x >= longLayout.items[i - 1].bounds.getMax().x,
                   "localized items remain ordered without overlap");
        for (std::size_t i = 0; i < longItems.size(); ++i) {
            const Layer* layer = longScene.findLayer(longItems[i].id);
            c.near(layer->transform.position, Vector3(refreshedLong.items[i].position.x,
                   refreshedLong.items[i].position.y, 0.f), 1e-5f,
                   "refreshed localized measurement is written to its layer transform");
            c.that(layer->content.metrics.fingerprint == longItems[i].measurementFingerprint &&
                   layer->content.metrics.naturalSize == longItems[i].measuredSize,
                   "refreshed layer retains the current measurement and fingerprint");
        }
        s.vector(shortLayout.bounds.getSize()); s.vector(longLayout.bounds.getSize());
        for (const auto& item : refreshedLong.items) { s.vector(item.position); s.vector(item.size); }
    });

    add(39, "audio_bass_grid", "low-frequency PCM energy drives deterministic scale keys", [](Checks& c, Snapshot& s) {
        constexpr std::uint32_t sampleRate = 8000;
        std::vector<float> pcm(2048);
        for (std::size_t i = 0; i < pcm.size(); ++i)
            pcm[i] = .5f * std::sin(2.f * kPi * 125.f * static_cast<float>(i) / sampleRate);
        AudioAnalysisSpec analysis; analysis.sampleRate = sampleRate; analysis.windowSize = 256;
        analysis.hopSize = 128; analysis.bassBelowHz = 300.f; analysis.midBelowHz = 1800.f;
        const auto features = analyzePcmAudio(pcm, analysis);
        c.that(features.size() > 8 && features[3].bass > features[3].treble,
               "PCM analyzer identifies the generated bass tone");
        const AudioDriverSpec driver{AudioFeature::Bass, 2.f, 0.f, .05f};
        const auto track = bakeAudioDriverTrack(features, driver, 30.f, 0, 6);
        Layer driven(1, "bass_scaled_grid");
        bakeAudioDriverToLayer(driven, features, driver, AudioDriverProperty::Scale,
                               {1.f, 1.f, 0.f}, 30.f, 0, 6);
        c.that(track.size() == 7 && track.sample(.1f) > 0.f, "bass envelope bakes into frame-rate keys");
        c.that(driven.tracks.scale.size() == 7 && driven.tracks.scale.sample(.1f).x > 1.f &&
               driven.tracks.scale.sample(.1f).y > 1.f,
               "analyzed bass energy drives actual X/Y scale animation channels");
        for (const auto& key : track.keys()) s.number(key.value);
    });

    add(40, "audio_onset_punch", "transient onset produces a scale-punch driver sample", [](Checks& c, Snapshot& s) {
        constexpr std::uint32_t sampleRate = 8000;
        std::vector<float> pcm(1024, 0.f);
        for (std::size_t i = 256; i < pcm.size(); ++i)
            pcm[i] = .7f * std::sin(2.f * kPi * 440.f * static_cast<float>(i - 256) / sampleRate);
        AudioAnalysisSpec analysis; analysis.sampleRate = sampleRate; analysis.windowSize = 128;
        analysis.hopSize = 64; analysis.onsetThreshold = 1.1f; analysis.rmsThreshold = .01f;
        const auto features = analyzePcmAudio(pcm, analysis);
        const auto onset = std::find_if(features.begin(), features.end(), [](const auto& f) { return f.onset > 0.f; });
        c.that(onset != features.end(), "silence-to-tone transition is detected as an onset");
        if (onset != features.end()) s.number(onset->seconds), s.number(onset->onset);
        const AudioDriverSpec driver{AudioFeature::Onset, 3.f, 0.f, 0.f};
        const auto track = bakeAudioDriverTrack(features, driver, 240.f, 0, 30);
        Layer punched(1, "onset_punch");
        bakeAudioDriverToLayer(punched, features, driver, AudioDriverProperty::Scale,
                               {1.f, 1.f, 0.f}, 240.f, 0, 30);
        c.that(track.size() == 31, "onset feature maps to a bounded thirty-one-frame driver track");
        c.that(std::all_of(track.keys().begin(), track.keys().end(),
                           [](const auto& key) { return std::isfinite(key.value); }) &&
               std::any_of(track.keys().begin(), track.keys().end(),
                           [](const auto& key) { return key.value > 0.f; }),
               "detected transient produces finite positive driver output");
        c.that(punched.tracks.scale.size() == 31 &&
               std::any_of(punched.tracks.scale.keys().begin(), punched.tracks.scale.keys().end(),
                           [](const auto& key) { return key.value.x > 1.f; }),
               "onset transient creates an actual scale punch on the driven layer");
        for (const auto& key : track.keys()) s.number(key.value);
    });

    add(41, "camera_dolly_parallax", "camera move has depth-dependent projected parallax", [](Checks& c, Snapshot& s) {
        CameraRig rig(1, 55.f, 16.f / 9.f);
        rig.setPosition({0.f, 0.f, 12.f}).setTarget({0.f, 0.f, 0.f});
        presets::cameraPush(rig, 0, 60, 4.f, kFps);
        auto camera = rig.makeCamera();
        rig.apply(0.f, *camera);
        PerspectiveView view(*camera, {1920.f, 1080.f});
        presets::cameraPan(rig, 0, 60, {4.f, 0.f}, kFps);
        const Vector3 startPosition = rig.sample(0.f).position;
        Vector2 nearShift, farShift;
        for (const float time : {0.f, 1.f, 2.f}) {
            const auto pose = rig.sample(time);
            const Vector3 cameraDelta = pose.position - startPosition;
            nearShift = view.parallaxShift(cameraDelta, 4.f);
            farShift = view.parallaxShift(cameraDelta, 20.f);
            if (time > 0.f)
                c.that(std::fabs(nearShift.x) > std::fabs(farShift.x) && nearShift.x < 0.f,
                       "near plane receives a larger opposite-direction shift from the authored camera pan");
            s.vector(pose.position); s.number(pose.fov);
        }
        s.vector(nearShift); s.vector(farShift);
    });

    add(42, "camera_orbit", "camera orbits the look-at target at constant radius", [](Checks& c, Snapshot& s) {
        CameraRig rig(1);
        rig.setPosition({0.f, 0.f, 12.f}).setTarget({0.f, 0.f, 0.f});
        presets::cameraOrbit(rig, 0, 60, kPi, .3f, kFps);
        for (int frame = 0; frame <= 60; frame += 5) {
            const CameraPose pose = rig.sample(secondsAt(frame));
            c.near(pose.position.distanceTo(pose.target), 12.f, 2e-3f, "orbit holds the camera-target radius");
            s.vector(pose.position);
        }
    });

    add(43, "camera_whip", "overshooting pan settles exactly at its destination", [](Checks& c, Snapshot& s) {
        CameraRig rig(1);
        rig.positionTrack().add(0.f, {}, Easing::easeOut()).add(.8f, {12.f, 0.f, 0.f})
            .add(1.f, {10.f, 0.f, 0.f});
        float peak = -std::numeric_limits<float>::max();
        for (int sample = 0; sample <= 100; ++sample) {
            const float time = static_cast<float>(sample) / 100.f;
            peak = std::max(peak, rig.sample(time).position.x);
            if (sample % 10 == 0) s.vector(rig.sample(time).position);
        }
        c.that(peak > 10.f, "whip overshoots its destination before settling");
        c.near(rig.sample(1.f).position, Vector3(10.f, 0.f, 0.f), 1e-5f, "whip lands exactly on its destination");
    });

    add(44, "dolly_zoom", "camera distance and FOV preserve apparent subject size", [](Checks& c, Snapshot& s) {
        CameraRig rig(1, 60.f, 16.f / 9.f);
        rig.setPosition({0.f, 0.f, 10.f}).setTarget({0.f, 0.f, 0.f});
        const float startTangent = std::tan(math::degToRad(60.f) * .5f);
        for (int frame = 0; frame <= 100; ++frame) {
            const float time = static_cast<float>(frame) / 100.f;
            const float distance = 10.f + 10.f * time;
            const float fov = 2.f * std::atan(10.f * startTangent / distance);
            rig.positionTrack().add(time, {0.f, 0.f, distance});
            rig.fovTrack().add(time, math::radToDeg(fov));
        }
        const auto apparent = [&](float time) {
            const auto pose = rig.sample(time);
            const float distance = pose.position.distanceTo(pose.target);
            return 1.f / (distance * std::tan(math::degToRad(pose.fov) * .5f));
        };
        for (int frame = 0; frame <= 100; frame += 10) {
            const float time = static_cast<float>(frame) / 100.f;
            c.near(apparent(time), apparent(0.f), 3e-4f,
                   "dolly zoom holds normalized subject size through the move");
            s.number(apparent(time));
        }
    });

    add(45, "focus_pull", "camera focus-distance channel pulls between two subject depths", [](Checks& c, Snapshot& s) {
        CameraRig rig(1);
        rig.setFStop(2.8f).setFocusDistance(12.f);
        rig.focusDistanceTrack().add(0.f, 12.f, Easing::easeInOut()).add(2.f, 4.f);
        c.near(rig.sample(0.f).focusDistance, 12.f, 1e-5f, "focus starts at the far subject");
        c.near(rig.sample(2.f).focusDistance, 4.f, 1e-5f, "focus ends at the near subject");
        c.that(rig.sample(1.f).focusDistance < 12.f && rig.sample(1.f).focusDistance > 4.f,
               "focus plane moves continuously between subjects");
        s.number(rig.sample(0.f).focusDistance); s.number(rig.sample(1.f).focusDistance);
        s.number(rig.sample(2.f).focusDistance); s.number(rig.sample(1.f).fStop);
    });

    add(46, "depth_cascade", "thirty cards arrive from depth with staggered offsets", [](Checks& c, Snapshot& s) {
        MotionScene scene("46_depth_cascade", kFps);
        for (std::uint32_t i = 0; i < 30; ++i) {
            Layer& card = addCard(scene, i + 1, "depth_card_" + std::to_string(i), {static_cast<float>(i), 0.f, 0.f});
            presets::pushInZ(card, static_cast<int>(i) * 2, 30, -40.f, kFps);
        }
        c.that(scene.findLayer(1)->tracks.position.startTime() < scene.findLayer(30)->tracks.position.startTime(),
               "depth cards enter in deterministic sequence");
        for (std::uint32_t i = 0; i < 30; ++i) {
            const Layer* card = scene.findLayer(i + 1);
            const float start = secondsAt(static_cast<int>(i) * 2);
            c.near(card->tracks.position.startTime(), start, 1e-6f,
                   "each depth card receives its exact cascade offset");
            c.near(card->tracks.position.sample(start).z, -40.f, 1e-5f,
                   "each depth card starts at the authored depth offset");
            c.near(card->tracks.position.sample(start + 1.f).z, 0.f, 1e-5f,
                   "each depth card arrives at its own resting depth");
        }
        c.that(scene.findLayer(1)->tracks.position.sample(0.f).z != scene.findLayer(1)->transform.position.z,
               "first card begins offset in depth");
        capture(scene, s, {0.f, .5f, 1.f, 2.f, 3.f});
    });

    add(47, "perspective_deck", "nested card deck opens around animated pivots", [](Checks& c, Snapshot& s) {
        MotionScene scene("47_perspective_deck", kFps);
        Layer& root = scene.addNull(1, "deck_root");
        root.tracks.rotation.add(0.f, {}).add(1.f, {0.f, .3f, 0.f});
        for (std::uint32_t i = 0; i < 8; ++i) {
            Layer& card = addCard(scene, i + 2, "deck_card_" + std::to_string(i),
                                  {static_cast<float>(i) * 2.f, 0.f, -static_cast<float>(i)});
            card.parentId = 1;
            card.transform.anchor.set(-16.f, 0.f, 0.f);
            card.tracks.rotation.add(0.f, {}).add(1.f, {0.f, 0.f, (static_cast<float>(i) - 3.5f) * .08f});
        }
        c.that(scene.validate().empty(), "deck parent graph is valid");
        scene.evaluatePtsExact(0.f);
        const Matrix4 closedPose = scene.worldMatrix(5);
        scene.evaluatePtsExact(1.f);
        c.that(scene.state(5).evaluated && !scene.worldMatrix(5).equals(closedPose),
               "animated root and card pivot change the child world pose");
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(48, "time_reverse", "time remap reads a source animation in reverse", [](Checks& c, Snapshot& s) {
        MotionScene scene("48_time_reverse", 30.f);
        Layer& layer = addCard(scene, 1, "reversed_clip");
        layer.tracks.position.add(0.f, {}).add(1.f, {30.f, 0.f, 0.f});
        layer.timeRemap.offsetFrames = 30.f;
        layer.timeRemap.reverse = true;
        scene.evaluate(0); c.near(worldPosition(scene, 1).x, 30.f, 1e-5f, "reverse starts at the source end");
        scene.evaluate(15); c.near(worldPosition(scene, 1).x, 15.f, 1e-5f, "reverse samples the source midpoint");
        scene.evaluate(30); c.near(worldPosition(scene, 1).x, 0.f, 1e-5f, "reverse ends at the source start");
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f});
    });

    add(49, "freeze_and_ramp", "remap accelerates, freezes, then ramps onward", [](Checks& c, Snapshot& s) {
        MotionScene scene("49_freeze_and_ramp", 10.f);
        Layer& layer = addCard(scene, 1, "remapped_clip");
        layer.tracks.position.add(0.f, {}).add(3.f, {30.f, 0.f, 0.f});
        layer.timeRemapTrack.add(0.f, 0.f).add(1.f, 10.f).add(2.f, 10.f).add(3.f, 30.f);
        const std::array<float, 7> expected{{0.f, 5.f, 10.f, 10.f, 10.f, 20.f, 30.f}};
        for (std::size_t i = 0; i < expected.size(); ++i) {
            const float time = static_cast<float>(i) * .5f;
            scene.evaluatePtsExact(time);
            c.near(worldPosition(scene, 1).x, expected[i], 1e-5f, "freeze/ramp remap resolves its authored value");
            s.number(worldPosition(scene, 1).x);
        }
        capture(scene, s, {0.f, .5f, 1.f, 1.5f, 2.f, 2.5f, 3.f});
    });

    add(50, "posterize_motion", "continuous output samples a four-Hz held motion channel", [](Checks& c, Snapshot& s) {
        Layer layer(1, "posterized");
        layer.tracks.position.add(0.f, {}).add(1.f, {24.f, 0.f, 0.f}).posterize(4.f);
        c.near(layer.sample(.26f).position.x, 6.f, 1e-5f, "posterize holds the prior 0.25-second value");
        c.near(layer.sample(.49f).position.x, 6.f, 1e-5f, "posterized channel remains held within the interval");
        c.near(layer.sample(.51f).position.x, 12.f, 1e-5f, "next posterized sample advances at its boundary");
        for (int i = 0; i <= 12; ++i) s.number(layer.sample(static_cast<float>(i) / 24.f).position.x);
    });

    add(51, "infinite_loop", "position/rotation/scale loop closes its pose seam", [](Checks& c, Snapshot& s) {
        Layer layer(1, "looping");
        layer.tracks.position.add(0.f, {}).add(1.f, {10.f, 0.f, 0.f}).loop(0.f, 1.f);
        layer.tracks.rotation.add(0.f, {}).add(1.f, {0.f, 0.f, kPi}).loop(0.f, 1.f);
        layer.tracks.scale.add(0.f, {1.f, 1.f, 1.f}).add(1.f, {2.f, 2.f, 2.f}).loop(0.f, 1.f);
        c.near(layer.tracks.position.sample(0.f), layer.tracks.position.sample(1.f), 1e-6f,
               "position at loop seam is identical");
        c.near(layer.tracks.position.sample(.25f), layer.tracks.position.sample(1.25f), 1e-6f,
               "position repeats exactly after one cycle");
        c.near(layer.tracks.rotation.sample(.25f), layer.tracks.rotation.sample(1.25f), 1e-6f,
               "rotation repeats exactly after one cycle");
        c.near(layer.tracks.scale.sample(.25f), layer.tracks.scale.sample(1.25f), 1e-6f,
               "scale repeats exactly after one cycle");
        s.vector(layer.tracks.position.sample(0.f)); s.vector(layer.tracks.position.sample(1.f));
        s.vector(layer.tracks.scale.sample(.25f)); s.vector(layer.tracks.scale.sample(1.25f));
    });

    add(52, "subframe_stress", "random-access quarter-frame evaluations remain deterministic", [](Checks& c, Snapshot& s) {
        MotionScene scene("52_subframe_stress", 24.f);
        Layer& layer = addCard(scene, 1, "subframe_layer");
        layer.tracks.position.add(0.f, {}).add(1.f, {24.f, 12.f, 0.f});
        const std::array<float, 4> fractions{{.25f, .5f, .75f, .25f}};
        Vector3 firstQuarter{};
        for (std::size_t i = 0; i < fractions.size(); ++i) {
            const float time = .5f + fractions[i] / 24.f;
            scene.evaluatePtsExact(time);
            const Vector3 current = worldPosition(scene, 1);
            if (i == 0) firstQuarter = current;
            if (i == 3) c.near(current, firstQuarter, 1e-6f, "seeking back to the same subframe reproduces its pose");
            c.near(current.x, time * 24.f, 2e-5f, "subframe X agrees with exact time");
            c.near(current.y, time * 12.f, 2e-5f, "subframe Y agrees with exact time");
            s.vector(current);
        }
        capture(scene, s, {.5f, .5f + .25f / 24.f, .5f + .5f / 24.f, .5f + .75f / 24.f});
    });

    add(53, "layer_stress_2000", "two thousand layers evaluate and validate", [](Checks& c, Snapshot& s) {
        MotionScene scene("53_layer_stress", kFps);
        for (std::uint32_t i = 0; i < 2000; ++i) {
            Layer& layer = scene.addNull(i + 1, "stress_" + std::to_string(i));
            layer.transform.position.set(static_cast<float>(i % 100), static_cast<float>(i / 100), 0.f);
            layer.tracks.position.add(0.f, layer.transform.position).add(1.f, layer.transform.position + Vector3(1.f, 0.f, 0.f));
        }
        scene.evaluate(15);
        c.that(scene.layerCount() == 2000 && scene.evaluationOrder().size() == 2000,
               "all stress layers participate in evaluation order");
        c.that(scene.validate().empty(), "large flat hierarchy validates");
        c.near(worldPosition(scene, 1), Vector3(.5f, 0.f, 0.f), 1e-5f,
               "first stress transform evaluates at the requested half-second");
        c.near(worldPosition(scene, 2000), Vector3(99.5f, 19.f, 0.f), 1e-5f,
               "last stress transform evaluates its distinct authored position");
        s.integer(scene.layerCount()); s.vector(worldPosition(scene, 1)); s.vector(worldPosition(scene, 2000));
    });

    add(54, "track_stress_1000", "one thousand dense transform tracks sample correctly", [](Checks& c, Snapshot& s) {
        MotionScene scene("54_track_stress", kFps);
        for (std::uint32_t i = 0; i < 1000; ++i) {
            Layer& layer = scene.addNull(i + 1, "track_" + std::to_string(i));
            layer.tracks.position.add(0.f, {static_cast<float>(i), 0.f, 0.f});
            for (int key = 0; key <= 20; ++key) {
                const float time = static_cast<float>(key) / 20.f;
                layer.tracks.position.add(time, {static_cast<float>(i), time * 10.f, 0.f});
            }
        }
        scene.evaluatePtsExact(.375f);
        c.that(scene.layerCount() == 1000, "one thousand layer position tracks evaluate");
        c.near(worldPosition(scene, 501), Vector3(500.f, 3.75f, 0.f), 1e-4f,
               "dense track samples the correct interpolated value");
        s.vector(worldPosition(scene, 1)); s.vector(worldPosition(scene, 501)); s.vector(worldPosition(scene, 1000));
    });

    add(55, "hierarchy_stress_100", "one hundred nested parents compose without recursion overflow", [](Checks& c, Snapshot& s) {
        MotionScene scene("55_hierarchy_stress", kFps);
        for (std::uint32_t i = 0; i < 100; ++i) {
            Layer& layer = scene.addNull(i + 1, "depth_" + std::to_string(i), i == 0 ? 0 : i);
            layer.transform.position.set(1.f, 0.f, 0.f);
        }
        scene.evaluate(0);
        c.that(scene.validate().empty() && scene.evaluationOrder().size() == 100,
               "deep valid hierarchy has a complete evaluation order");
        scene.evaluate(0);
        c.near(worldPosition(scene, 100), Vector3(100.f, 0.f, 0.f), 2e-4f,
               "deep chain world position composes every parent offset");
        s.vector(worldPosition(scene, 100));
    });

    add(56, "same_seed_golden", "procedural driver with the same seed repeats exactly", [](Checks& c, Snapshot& s) {
        const auto run = [](std::uint32_t seed) {
            MotionScene scene("seeded", kFps);
            Layer& layer = scene.addNull(1, "noise");
            DriverSpec driver; driver.kind = DriverKind::Wiggle; driver.property = DriverProperty::Position;
            driver.seed = seed; driver.amplitude = 2.f; driver.frequency = 1.7f;
            layer.drivers.push_back(driver);
            std::vector<Vector3> values;
            for (int i = 0; i <= 30; ++i) {
                scene.evaluate(i);
                values.push_back(worldPosition(scene, 1));
            }
            return values;
        };
        const auto first = run(77), again = run(77), other = run(78);
        c.that(first.size() == again.size() && std::equal(first.begin(), first.end(), again.begin(),
            [](const auto& x, const auto& y) { return x == y; }), "same seed produces bit-identical driver samples");
        c.that(first.size() == other.size() && !std::equal(first.begin(), first.end(), other.begin(),
            [](const auto& x, const auto& y) { return x == y; }), "different seed changes at least one procedural sample");
        for (const auto& value : first) s.vector(value);
    });

    add(57, "fps_equivalence", "30 and 60 fps scenes agree at shared timestamps", [](Checks& c, Snapshot& s) {
        MotionScene at30("fps30", 30.f), at60("fps60", 60.f);
        Layer& a = at30.addNull(1, "sample"); Layer& b = at60.addNull(1, "sample");
        for (Layer* layer : {&a, &b})
            layer->tracks.position.add(0.f, {}, Easing::easeInOut()).add(2.f, {20.f, 8.f, -2.f});
        for (int sample = 0; sample <= 8; ++sample) {
            const float time = static_cast<float>(sample) * .25f;
            at30.evaluatePtsExact(time);
            at60.evaluatePtsExact(time);
            const auto p30 = worldPosition(at30, 1), p60 = worldPosition(at60, 1);
            c.near(p30, p60, 1e-6f, "equal PTS yields equal pose at either sampling rate");
            s.vector(p30);
        }
    });

    add(58, "bake_runtime_parity", "baked path keys agree with the source path at every bake key", [](Checks& c, Snapshot& s) {
        const MotionPath path = MotionPath::cubicBezier({0.f, 0.f, 0.f}, {2.f, 5.f, 0.f},
                                                          {8.f, -3.f, 0.f}, {10.f, 0.f, 0.f});
        Layer baked(1, "baked_path");
        constexpr int steps = 80;
        presets::pathMotion(baked, path, 0, steps, kFps, false, steps);
        for (int frame = 0; frame <= steps; ++frame) {
            const float progress = static_cast<float>(frame) / static_cast<float>(steps);
            const float time = secondsAt(frame);
            c.near(baked.tracks.position.sample(time), path.sampleUniform(progress), 1e-5f,
                   "baked key matches direct runtime path sample");
            s.vector(baked.tracks.position.sample(time));
        }
    });

#if defined(CHRONONMOTION_SHOWCASE_HAS_ADAPTER)
    add(59, "renderplan_roundtrip", "Motion lowering emits and parses deterministic RenderPlan JSON",
        [](Checks& c, Snapshot& s) {
            MotionScene scene("59_renderplan_roundtrip", 30.f);
            Layer& imageLayer = addCard(scene, 1, "roundtrip_image");
            imageLayer.lifetime = FrameRange::between(0, 30);
            imageLayer.tracks.position.add(0.f, {}).add(1.f, {12.f, 5.f, 0.f});
            RenderPlanLoweringOptions options;
            options.width = 64; options.height = 64; options.jobId = "acceptance_roundtrip";
            options.outputPath = "acceptance.png";
            chronon3d::render_plan::LayerPlan image;
            image.type = chronon3d::render_plan::LayerType::Image;
            image.asset = "roundtrip_image";
            image.size = {32.f, 24.f}; image.size_dimensions = 2;
            options.contentLayers.emplace("1", image);
            auto plan = lowerToRenderPlan(scene, options);
            const std::string jsonText = renderPlanToJson(plan);
            const auto json = nlohmann::json::parse(jsonText);
            c.that(json.at("schema") == chronon3d::render_plan::kRenderPlanSchemaV2,
                   "emitted JSON retains the selected RenderPlan schema identifier");
            c.that(json.at("layers").size() == 1, "parsed RenderPlan JSON retains one content layer");
            c.that(json.at("layers").at(0).at("id") == "1",
                   "parsed RenderPlan JSON retains the source layer identity");
            const auto& tracks = json.at("layers").at(0).at("animation").at("tracks");
            c.that(tracks.size() == 4, "parsed RenderPlan JSON retains all lowered transform channels");
            const auto positionTrack = std::find_if(tracks.begin(), tracks.end(), [](const auto& track) {
                return track.at("property") == "position";
            });
            c.that(positionTrack != tracks.end(), "emitted transform tracks include position");
            if (positionTrack != tracks.end()) {
                c.that(positionTrack->at("keyframes").size() == 31,
                       "position bake covers all 31 inclusive frames");
                c.near(positionTrack->at("keyframes").at(0).at("value").at(0).get<float>(), 0.f, 1e-5f,
                       "position animation begins at the authored origin");
                c.near(positionTrack->at("keyframes").at(30).at("value").at(0).get<float>(), 12.f, 1e-4f,
                       "position animation reaches the authored endpoint");
            }
            c.that(json.at("canvas").at("width") == 64 && json.at("canvas").at("height") == 64 &&
                   json.at("canvas").at("duration_frames") == 31 &&
                   json.at("output").at("path") == "acceptance.png",
                   "emitted canvas duration and output contract survive JSON serialization");
            c.that(renderPlanToJson(plan) == jsonText,
                   "typed RenderPlan emission repeats byte-for-byte");
            s.text(jsonText);
            capture(scene, s, {0.f, .5f, 1.f});
        });
#else
    cases.push_back({59, "renderplan_roundtrip", "Motion lowers to JSON and round-trips through Chronon3D",
                     "Configure ChrononMotion with CHRONON3D_INCLUDE_DIR to enable the optional typed adapter.", {}});
#endif

    add(60, "mega_scene", "hierarchy, path, constraints, Verlet and 3D IK coexist deterministically",
        [](Checks& c, Snapshot& s) {
            MotionScene scene("60_mega_scene", 30.f);
            Layer& controller = scene.addNull(1, "controller");
            controller.tracks.rotation.add(0.f, {}).add(2.f, {0.f, 0.f, .2f});
            Layer& logo = addCard(scene, 2, "logo", {0.f, 0.f, 0.f}); logo.parentId = 1;
            const MotionPath path = MotionPath::cubicBezier({-4.f, 0.f, 0.f}, {-2.f, 3.f, 0.f},
                                                              {2.f, 3.f, 0.f}, {4.f, 0.f, 0.f});
            presets::pathMotion(logo, path, 0, 60, kFps, true, 60);
            (void)addCard(scene, 3, "aim_target", {0.f, 4.f, 0.f});
            Layer& pointer = addCard(scene, 4, "aim_pointer", {-2.f, 0.f, 0.f});
            ConstraintSpec aim; aim.kind = ConstraintKind::Aim; aim.targetId = 3; pointer.constraints.push_back(aim);
            VerletChainSpec tail; tail.joints = {{0.f, 0.f}, {1.f, 0.f}, {2.f, 0.f}};
            tail.gravity = {0.f, -1.f}; tail.fps = 30.f;
            const auto tailBake = bakeVerletChain(tail, 0, 60);
            for (std::size_t joint = 0; joint < tailBake.jointPositions.size(); ++joint) {
                Layer& bone = scene.addNull(static_cast<std::uint32_t>(10 + joint), "tail_joint_" + std::to_string(joint));
                for (const auto& key : tailBake.jointPositions[joint].keys())
                    bone.tracks.position.add(key.time, {key.value.x, key.value.y, -1.f});
            }
            IK3BakeSpec ik; ik.initialJoints = {{0.f, -2.f, 0.f}, {1.f, -2.f, 0.f}, {2.f, -2.f, 0.f}};
            ik.pole = {0.f, -2.f, 3.f}; ik.fps = 30.f;
            ik.targetTrack.add(0.f, {2.f, -2.f, 0.f}).add(2.f, {1.f, -1.f, 0.f});
            const auto ikBake = bakeIKChain3D(ik, 0, 60);
            for (std::size_t joint = 0; joint < ikBake.jointPositions.size(); ++joint) {
                Layer& bone = scene.addNull(static_cast<std::uint32_t>(20 + joint), "ik_joint_" + std::to_string(joint));
                bone.tracks.position = ikBake.jointPositions[joint];
            }
            c.that(scene.validate().empty(), "mega composition has valid parent and link graphs");
            for (const float time : {0.f, .5f, 1.f, 1.5f, 2.f}) {
                scene.evaluatePtsExact(time);
                c.that(scene.state(2).evaluated && scene.state(4).evaluated && scene.state(12).evaluated,
                       "motion paths, constraints, baked dynamics and IK evaluate in the integrated scene");
                Vector3 aimDirection = worldPosition(scene, 3) - worldPosition(scene, 4);
                aimDirection.normalize();
                Vector3 pointerFacing = Vector3::Z();
                pointerFacing.applyQuaternion(scene.state(4).local.orientation);
                c.near(pointerFacing, aimDirection, 2e-4f,
                       "integrated Aim constraint points the pointer at its target");
                Vector3 expectedTail = tailBake.jointPositions[2].sample(time);
                expectedTail.z = -1.f;
                c.near(worldPosition(scene, 12), expectedTail, 1e-5f,
                       "integrated Verlet tail joint matches its offline bake");
                c.near(worldPosition(scene, 22), ik.targetTrack.sample(time), 3e-3f,
                       "integrated IK end effector follows its time-varying target");
                c.near(worldPosition(scene, 21).distanceTo(worldPosition(scene, 20)), 1.f, 1e-5f,
                       "integrated IK upper segment preserves its length");
                c.near(worldPosition(scene, 22).distanceTo(worldPosition(scene, 21)), 1.f, 1e-5f,
                       "integrated IK lower segment preserves its length");
                s.vector(worldPosition(scene, 2)); s.vector(worldPosition(scene, 4));
                s.vector(worldPosition(scene, 20));
            }
            capture(scene, s, {0.f, .5f, 1.f, 1.5f, 2.f});
        });

    add(61, "gltf_skinned_morph", "glTF skin and animated morph bake deterministically",
        [](Checks& c, Snapshot& s) {
            chrononmotion_test::GltfMotionFixture fixture;
            const auto asset = fixture.load(fixture.withAnimatedMorphChannel());
            c.that(asset.skins.size() == 1 && asset.geometry.size() == 1 &&
                   asset.clips.size() == 1 && asset.clips[0].morphs.size() == 1,
                   "glTF fixture imports skin, geometry, clip and morph channel");
            RigProgram program;
            program.graph = asset.graph;
            program.animation = asset.clips[0].animation;
            program.geometry = asset.geometry;
            for (const auto& morph : asset.clips[0].morphs)
                program.geometry.at(morph.geometryId - 1).morphTracks = morph.weights;
            const RigProgramEvaluator evaluator(program);
            const auto first = evaluator.bake(0, 30, kFps);
            const auto repeated = evaluator.bake(0, 30, kFps);
            c.that(first.signature == repeated.signature,
                   "skin and morph samples have a repeatable bake signature");
            c.near(first.frames[0].geometry[0].positions[0], Vector3{0.f, 0.f, 0.f},
                   1e-6f, "skinned geometry starts at its bind pose");
            c.near(first.frames[15].geometry[0].positions[1], Vector3{2.f, .5f, 0.f},
                   1e-5f, "midpoint combines joint translation and half morph weight");
            c.near(first.frames[30].geometry[0].positions[1], Vector3{3.f, 1.f, 0.f},
                   1e-5f, "final sample combines full joint translation and morph weight");
            s.text("glTF skin + animated morph");
            s.text(std::to_string(first.signature));
            for (const int frame : {0, 15, 30}) {
                s.integer(frame);
                s.vector(first.frames[frame].geometry[0].positions[0]);
                s.vector(first.frames[frame].geometry[0].positions[1]);
                s.vector(first.frames[frame].geometry[0].positions[2]);
            }
        });

    add(62, "editorial_light_rig", "three editorial multi-light rigs rotate and lower deterministically",
        [](Checks& c, Snapshot& s) {
            const auto ids = lightRigPresets::catalog();
            c.that(ids.size() == 3, "catalog exposes three stable editorial rigs");
            for (const auto id : ids) {
                auto rig = lightRigPresets::make(id);
                lightRigPresets::animateRotation(rig, 0.f, 1.f, {}, {0.f, .4f, .2f});
                const auto start = rig.sample(0.f);
                const auto end = rig.sample(1.f);
                c.that(start.size() == rig.sources.size() && end.size() == start.size(),
                       "rig samples preserve deterministic source order");
                s.text(std::string(id));
                s.text(rig.serialize());
                for (std::size_t index = 0; index < start.size(); ++index) {
                    c.that(start[index].id == end[index].id,
                           "rotation keeps source identity stable");
                    s.integer(start[index].id);
                    s.vector(start[index].local.position);
                    s.vector(end[index].local.position);
                }
            }
        });

    add(63, "hdri_constant_probe", "Radiance HDRI projects to stable cubemap, mip and SH data",
        [](Checks& c, Snapshot& s) {
            const auto encoded = chrononmotion_test::constantRadianceHdr();
            const auto environment = decodeHdriRadiance(encoded);
            const auto payload = environment.serialize();
            c.that(environment.mips.size() == 2 && environment.mips[0].size == 2 &&
                   environment.mips[1].size == 1, "HDRI produces a complete bounded mip chain");
            c.that(environment.irradianceSH[0] > 10.f && environment.irradianceSH[0] < 12.f,
                   "HDRI probe carries integrated Lambert irradiance");
            c.that(payload == decodeHdriRadiance(encoded).serialize(),
                   "HDRI cubemap and SH payload are deterministic");
            s.integer(environment.sourceWidth);
            s.integer(environment.sourceHeight);
            s.integer(environment.faceSize);
            s.text(std::string_view(reinterpret_cast<const char*>(payload.data()), payload.size()));
        });

    if (cases.size() != 63) throw std::logic_error("acceptance catalog must contain exactly sixty-three cases");
    return cases;
}
} // namespace motion_showcase_detail
