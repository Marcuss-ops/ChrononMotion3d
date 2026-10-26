#include "motion_showcase_shared.hpp"

namespace chrononmotion_test::showcase {
void addRuntimeCases(std::vector<ShowcaseCase>& cases) {
    const auto add = [&](unsigned number, const char* id, const char* description, std::function<void(Checks&, Snapshot&)> run) {
        cases.push_back({number, id, description, {}, std::move(run)});
    };
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
}
} // namespace chrononmotion_test::showcase
