#include "motion_showcase_shared.hpp"

namespace chrononmotion_test::showcase {
void addShapeCases(std::vector<ShowcaseCase>& cases) {
    const auto add = [&](unsigned number, const char* id, const char* description, std::function<void(Checks&, Snapshot&)> run) {
        cases.push_back({number, id, description, {}, std::move(run)});
    };
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
}
} // namespace chrononmotion_test::showcase
