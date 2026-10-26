#include "motion_authoring_shared.hpp"

namespace chrononmotion_test::authoring {
void testPcmAudioAnalysis() {
    section("PCM audio feature analysis and keyframes");
    constexpr std::uint32_t sampleRate = 8000;
    std::vector<float> pcm(512);
    for (std::size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = 0.5f * std::sin(2.f * math::PI * 125.f * static_cast<float>(i) / sampleRate);
    AudioAnalysisSpec config;
    config.sampleRate = sampleRate;
    config.windowSize = 128;
    config.hopSize = 64;
    config.bassBelowHz = 300.f;
    config.midBelowHz = 1800.f;
    const auto features = analyzePcmAudio(pcm, config);
    check(features.size() == 8, "PCM analyzer returns deterministic overlapping windows");
    checkNear(features[1].rms, 0.35f, 0.04f, "RMS envelope measures sine amplitude");
    check(features[1].bass > features[1].treble, "DFT bands identify low-frequency energy");
    const auto featuresAgain = analyzePcmAudio(pcm, config);
    check(features.front().bass == featuresAgain.front().bass && features.back().onset == featuresAgain.back().onset,
          "PCM analysis is repeatably deterministic");
    std::array<AudioFeatureSample, 3> impulse{{{0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
        {0.1f, 1.f, 0.f, 0.f, 0.f, 1.f}, {0.2f, 0.f, 0.f, 0.f, 0.f, 0.f}}};
    const auto onsetTrack = bakeAudioDriverTrack(impulse,
        AudioDriverSpec{AudioFeature::Onset, 1.f, 0.f, 0.f}, 10.f, 0, 2);
    checkNear(onsetTrack.sample(0.1f), 1.f, 1e-6f, "transient feature maps to animation keyframes");
    AudioAnalysisSpec onsetConfig = config;
    onsetConfig.windowSize = 8;
    onsetConfig.hopSize = 8;
    onsetConfig.onsetThreshold = 1.5f;
    onsetConfig.rmsThreshold = 0.01f;
    const std::array<float, 16> onsetPcm{
        0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f,
        0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f, 0.8f};
    const auto onsetFeatures = analyzePcmAudio(onsetPcm, onsetConfig);
    check(onsetFeatures.back().onset > 0.f,
          "PCM onset feature triggers on a quiet-to-loud transient without dividing by zero");
    const auto track = bakeAudioDriverTrack(features, AudioDriverSpec{AudioFeature::Bass, 2.f, 1.f, 0.f},
                                            30.f, 0, 2);
    check(track.size() == 3, "audio driver bakes per-frame animation keys");
    check(track.sample(0.f) > 1.f, "audio driver multiplier and bias affect the sampled result");
    const auto smoothed = bakeAudioDriverTrack(impulse,
        AudioDriverSpec{AudioFeature::Rms, 1.f, 0.f, 0.1f}, 10.f, 0, 2);
    check(smoothed.sample(0.1f) > 0.f && smoothed.sample(0.1f) < 1.f,
          "audio smoothing applies a deterministic exponential response");
    auto invalidFeature = impulse;
    invalidFeature[1].treble = std::numeric_limits<float>::infinity();
    check(throwsInvalid([&] { (void)bakeAudioDriverTrack(invalidFeature,
        AudioDriverSpec{AudioFeature::Rms}, 10.f, 0, 2); }),
        "audio driver rejects invalid values even in an unselected feature channel");
    check(throwsInvalid([&] { (void)analyzePcmAudio(std::array<float, 1>{NAN}, config); }),
          "non-finite PCM is rejected");
    check(throwsInvalid([&] { auto bad = config; bad.channels = 0; (void)analyzePcmAudio(pcm, bad); }),
          "PCM analyzer rejects invalid channel counts");
    check(throwsInvalid([&] { auto bad = pcm; bad[0] = 1.01f; (void)analyzePcmAudio(bad, config); }),
          "PCM analyzer rejects out-of-range non-normalized samples");
    check(throwsInvalid([&] { auto expensive = config; expensive.windowSize = 8192; expensive.hopSize = 1;
        (void)analyzePcmAudio(pcm, expensive); }),
        "PCM analyzer enforces its deterministic direct-DFT operation budget");
    check(throwsInvalid([&] { auto bad = config; bad.midBelowHz = std::numeric_limits<float>::max();
        (void)analyzePcmAudio(pcm, bad); }),
        "PCM analyzer rejects frequency boundaries above the Nyquist limit");

    Layer driven(7, "audio driven");
    driven.transform.scale.set(2.f, 3.f, 1.f);
    const std::array<AudioFeatureSample, 1> constant{{{0.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.f}}};
    bakeAudioDriverToLayer(driven, constant, AudioDriverSpec{AudioFeature::Rms, 1.f, 0.f, 0.f},
                           AudioDriverProperty::Scale, Vector3(1.f, 0.f, 0.f), 30.f, 0, 1);
    checkNear(driven.tracks.scale.sample(0.f).x, 3.f, 1e-6f,
              "audio scale multiplier is applied multiplicatively to the authored base scale");
    checkNear(driven.tracks.scale.sample(0.f).y, 3.f, 1e-6f,
              "unselected scale axes remain unchanged");
    bakeAudioDriverToLayer(driven, constant, AudioDriverSpec{AudioFeature::Rms, 1.f, 0.f, 0.f},
                           AudioDriverProperty::Opacity, Vector3(1.f, 0.f, 0.f), 30.f, 0, 1);
    checkNear(driven.tracks.opacity.sample(0.f), 1.f, 1e-6f,
              "audio envelope opacity is clamped to the renderable normalized range");
    Layer opacityOverflow(8, "opacity overflow");
    opacityOverflow.transform.opacity = std::numeric_limits<float>::max();
    const std::array<AudioFeatureSample, 1> hugeAudio{{{0.f, std::numeric_limits<float>::max(), 0.f, 0.f, 0.f, 0.f}}};
    check(throwsInvalid([&] { bakeAudioDriverToLayer(opacityOverflow, hugeAudio,
        AudioDriverSpec{AudioFeature::Rms}, AudioDriverProperty::Opacity, {}, 30.f, 0, 0); }),
        "audio opacity rejects non-finite additive output");
}

void testPresetTrackGolden() {
    section("deterministic motion preset track goldens");

    Layer breathing(50, "breathing");
    breathing.transform.scale.set(2.f, 3.f, 4.f);
    presets::breathe(breathing, 0, 240, 30.f);
    check(breathing.tracks.scale.size() == 9,
          "four-second breathing cycle bakes quarter-period scale keys over two cycles");
    checkNear(breathing.tracks.scale.keys()[1].time, 1.f, 1e-6f,
              "breathing quarter-period key lands at one second");
    checkNear(breathing.tracks.scale.keys()[1].value, Vector3(2.03f, 3.045f, 4.06f), 1e-5f,
              "breathing scale key golden preserves the authored proportions");
    checkNear(breathing.tracks.scale.keys()[2].value, Vector3(2.f, 3.f, 4.f), 1e-5f,
              "breathing returns to its resting scale at the half-period key");

    Layer bobbing(51, "bobbing");
    bobbing.transform.position.set(2.f, 5.f, -1.f);
    presets::bob(bobbing, 0, 150, 30.f);
    check(bobbing.tracks.position.size() == 9,
          "2.5-second bob bakes quarter-period position keys for two periods");
    checkNear(bobbing.tracks.position.sample(0.625f), Vector3(2.f, 5.025f, -1.f), 1e-5f,
              "bob quarter-period position golden");
    checkNear(bobbing.tracks.position.sample(1.25f), Vector3(2.f, 5.f, -1.f), 1e-5f,
              "bob returns to rest at the half-period key");

    Layer pulsing(52, "pulsing");
    pulsing.transform.scale.set(2.f, 1.f, 0.5f);
    presets::pulse(pulsing, 0, 120, 30.f);
    check(pulsing.tracks.scale.size() == 5,
          "pulse bakes four fixed quarter-window intervals");
    checkNear(pulsing.tracks.scale.keys()[2].value, Vector3(2.06f, 1.03f, 0.515f), 1e-5f,
              "pulse reaches the 1.03x scale golden at its midpoint");
    checkNear(pulsing.tracks.scale.keys().back().value, Vector3(2.f, 1.f, 0.5f), 1e-6f,
              "pulse settles exactly to its authored scale");

    Layer drifting(53, "drifting");
    drifting.transform.position.set(1.f, 2.f, 3.f);
    drifting.transform.rotation.set(0.f, 0.f, 0.1f);
    presets::floatDrift(drifting, 0, 180, 30.f);
    check(drifting.tracks.position.size() == 5 && drifting.tracks.rotation.size() == 5,
          "float drift bakes synchronized position and roll keys at fixed quarter periods");
    checkNear(drifting.tracks.position.keys()[1].value, Vector3(1.03f, 2.f, 3.f), 1e-5f,
              "float drift quarter-period XY position golden");
    checkNear(drifting.tracks.rotation.keys()[1].value.z, 0.1f + math::PI / 360.f, 1e-6f,
              "float drift quarter-period roll golden is half a degree");
    checkNear(drifting.tracks.position.keys().back().value, Vector3(1.f, 2.02f, 3.f), 1e-5f,
              "float drift returns to its initial XY phase after six seconds");

    CameraRig handheld(54);
    handheld.setPosition(Vector3(1.f, 2.f, 12.f));
    presets::handheldShake(handheld, 0.4f, 3.f, 123u, 2.f, 2.1f);
    check(handheld.positionTrack().size() == 7 && handheld.rollTrack().size() == 7,
          "handheld shake bakes deterministic fixed-step position and roll tracks");
    checkNear(handheld.positionTrack().keys().front().time, 2.f, 1e-6f,
              "handheld shake keys the requested start time");
    checkNear(handheld.positionTrack().keys().back().time, 2.1f, 1e-6f,
              "handheld shake includes the requested end time");
    checkNear(handheld.positionTrack().keys().front().value, Vector3(1.f, 2.f, 12.f), 1e-6f,
              "handheld shake starts at its base pose");
    checkNear(handheld.positionTrack().keys().back().value, Vector3(1.f, 2.f, 12.f), 1e-6f,
              "handheld shake resolves to its base pose");
    check(handheld.positionTrack().keys()[3].value != Vector3(1.f, 2.f, 12.f) &&
          handheld.rollTrack().keys()[3].value != 0.f,
          "handheld shake golden includes seeded translation and micro-roll");
    CameraRig handheldTwin(56);
    handheldTwin.setPosition(Vector3(1.f, 2.f, 12.f));
    presets::handheldShake(handheldTwin, 0.4f, 3.f, 123u, 2.f, 2.1f);
    bool shakeKeysMatch = handheld.positionTrack().size() == handheldTwin.positionTrack().size() &&
                          handheld.rollTrack().size() == handheldTwin.rollTrack().size();
    for (std::size_t i = 0; shakeKeysMatch && i < handheld.positionTrack().size(); ++i) {
        shakeKeysMatch = handheld.positionTrack().keys()[i].time == handheldTwin.positionTrack().keys()[i].time &&
                         handheld.positionTrack().keys()[i].value == handheldTwin.positionTrack().keys()[i].value &&
                         handheld.rollTrack().keys()[i].value == handheldTwin.rollTrack().keys()[i].value;
    }
    check(shakeKeysMatch, "handheld shake repeats each seeded track key exactly");
    check(throwsInvalid([&] { presets::handheldShake(handheld, 1.f, 0.f, 1u, 0.f, 1.f); }),
          "handheld shake rejects zero frequency");
    check(throwsInvalid([&] { Layer invalid(55, "invalid"); presets::bob(invalid, 0, 0, 30.f); }),
          "idle presets reject empty duration windows");
}

void testDynamicFlowLayout() {
    section("row and column flow layout with measured content");
    std::array<FlowItem, 3> row{
        FlowItem{1, Vector2(32.f, 32.f), Vector2(0.5f, 0.5f)},
        FlowItem{2, Vector2(100.f, 24.f), Vector2(0.f, 0.5f)},
        FlowItem{3, Vector2(40.f, 20.f), Vector2(1.f, 0.5f)}};
    FlowSpec spec;
    spec.axis = FlowAxis::Row;
    spec.padding = FlowInsets{4.f, 6.f, 8.f, 10.f};
    spec.gap = 12.f;
    spec.crossAlignment = FlowCrossAlignment::Center;
    const auto first = solveFlowLayout(spec, row, Vector2(5.f, 7.f));
    check(first.items.size() == 3, "flow emits a resolved box for every child");
    checkNear(first.items[1].bounds.getMin(), Vector2(59.f, 15.f), 1e-5f,
              "row positions honor origin, leading inset, gap, and cross alignment");
    checkNear(first.items[2].bounds.getMin().x - first.items[1].bounds.getMax().x, 12.f, 1e-5f,
              "row preserves the configured gap");
    row[1].measuredSize.x = 180.f;
    const auto resized = solveFlowLayout(spec, row, Vector2(5.f, 7.f));
    check(resized.items[2].position.x > first.items[2].position.x,
          "content remeasurement shifts subsequent row items");
    FlowSpec column;
    column.axis = FlowAxis::Column;
    column.gap = 5.f;
    const auto vertical = solveFlowLayout(column, row);
    check(vertical.items[1].bounds.getMin().y >= vertical.items[0].bounds.getMax().y + 5.f,
          "column layout applies vertical gaps");
    FlowSpec aligned;
    aligned.axis = FlowAxis::Row;
    aligned.containerSize.set(200.f, 80.f);
    aligned.padding = FlowInsets{5.f, 10.f, 5.f, 10.f};
    aligned.gap = 4.f;
    aligned.mainAlignment = FlowMainAlignment::SpaceBetween;
    aligned.crossAlignment = FlowCrossAlignment::End;
    const std::array<FlowItem, 2> pair{{{1, {20.f, 20.f}}, {2, {30.f, 30.f}}}};
    const auto spaced = solveFlowLayout(aligned, pair);
    checkNear(spaced.items[1].bounds.getMin().x - spaced.items[0].bounds.getMax().x, 130.f, 1e-5f,
              "space-between treats the declared gap as its minimum and distributes remaining space");
    checkNear(spaced.items[0].bounds.getMax().y, 75.f, 1e-5f,
              "cross-axis end alignment resolves item bounds within padding");
    checkNear(spaced.bounds.getMax(), Vector2(200.f, 80.f), 1e-5f,
              "resolved container bounds include the explicit size");
    const std::array<FlowItem, 1> stretched{{{3, {20.f, 10.f}}}};
    aligned.crossAlignment = FlowCrossAlignment::Stretch;
    const auto stretch = solveFlowLayout(aligned, stretched);
    checkNear(stretch.items[0].size.y, 70.f, 1e-5f,
              "cross-axis stretch expands content to the padded inner extent");
    aligned.axis = FlowAxis::Column;
    const auto columnStretch = solveFlowLayout(aligned, stretched);
    checkNear(columnStretch.items[0].size.x, 180.f, 1e-5f,
              "column cross-axis stretch expands width rather than the main-axis height");

    MotionScene scene("flow");
    auto& content = scene.addContent(2, "Title", ContentRef::text("title", {100.f, 24.f}));
    const std::array<FlowItem, 1> only{FlowItem{2, Vector2(180.f, 24.f), Vector2(0.f, 0.5f), "measure-v2"}};
    const auto applied = refreshFlowLayout(spec, only, scene);
    checkNear(content.content.metrics.naturalSize.x, 180.f, 1e-5f,
              "measurement refresh updates the layer content metrics");
    check(content.content.metrics.fingerprint == "measure-v2",
          "measurement refresh replaces the measurement fingerprint");
    checkNear(content.transform.position.x, applied.items.front().position.x, 1e-5f,
              "flow writes its resolved anchor position to the layer");
    check(throwsInvalid([&] { const std::array<FlowItem, 1> missing{{{99, {1.f, 1.f}}}};
        (void)refreshFlowLayout(spec, missing, scene); }),
        "flow refresh rejects unknown layer ids before mutating any content");
    MotionScene atomicScene("atomic flow");
    auto& firstItem = atomicScene.addContent(1, "first", ContentRef::text("first", {10.f, 10.f}));
    auto& animatedItem = atomicScene.addContent(2, "animated", ContentRef::text("animated", {10.f, 10.f}));
    animatedItem.tracks.position.add(0.f, Vector3{});
    const std::array<FlowItem, 2> atomicItems{{{1, {40.f, 20.f}, {0.5f, 0.5f}, "new-a"},
                                               {2, {60.f, 30.f}, {0.5f, 0.5f}, "new-b"}}};
    check(throwsInvalid([&] { (void)refreshFlowLayout(spec, atomicItems, atomicScene); }),
          "flow refresh rejects a conflicting animation in any target");
    check(firstItem.content.metrics.naturalSize.x == 10.f && firstItem.content.metrics.fingerprint.empty(),
          "flow conflict validation is atomic and leaves earlier targets untouched");
    check(throwsInvalid([&] { auto bad = spec; bad.gap = -1.f; (void)solveFlowLayout(bad, row); }),
          "flow solver rejects negative gaps");
    FlowSpec overflow;
    overflow.axis = FlowAxis::Row;
    overflow.mainAlignment = FlowMainAlignment::Center;
    overflow.containerSize = {1.f, 1.f};
    const std::array<FlowItem, 1> oversized{{{4, {2.f, 1.f}}}};
    check(solveFlowLayout(overflow, oversized).items.front().bounds.getMin().x < 0.f,
          "center alignment keeps overflow symmetric around the inner container");
}

void testVectorPathBaking() {
    section("SVG morphing and affine repeater expansion");
    const auto start = parseSvgPath("M0,0 L10,0 Q15,5 10,10 A5,5 0 0 1 0,10 Z");
    const auto finish = parseSvgPath("M0,0 L20,0 Q25,5 20,10 A10,5 0 0 1 0,10 Z");
    check(start.size() == finish.size(), "SVG lines, quadratics and arcs normalize to compatible cubics");
    const auto smooth = parseSvgPath("M0 0 C1 0 2 1 3 1 S5 2 6 2 T8 3");
    check(smooth.size() == 4 && smooth[2].type == BakedPathCommandType::CubicTo,
          "smooth cubic and quadratic commands normalize into cubic segments");
    const auto baked = bakeSvgPathMorph("M0 0 L10 0 Z", "M0 0 L20 0 Z", 0, 2);
    check(baked.size() == 3, "path morph emits inclusive frame samples");
    checkNear(baked.front()[1].point.x, 10.f, 1e-5f, "path morph starts at the source path");
    checkNear(baked.back()[1].point.x, 20.f, 1e-5f, "path morph ends at the destination path");
    const auto copies = expandPathRepeater(start, RepeaterSpec{3, Vector2(20.f, 0.f), Vector2(1.f, 1.f),
                                                               math::PI * 0.5f, Vector2{}});
    check(copies.size() == 3, "repeater emits the requested number of paths");
    checkNear(copies[1].front().point, Vector2(20.f, 0.f), 1e-5f,
              "repeater translation is applied to each copy");
    const auto repeatedAgain = expandPathRepeater(start, RepeaterSpec{3, Vector2(20.f, 0.f), Vector2(1.f, 1.f),
                                                                       math::PI * 0.5f, Vector2{}});
    check(copies[2][1].point == repeatedAgain[2][1].point,
          "repeater geometry is repeatably deterministic");
    check(throwsInvalid([&] { (void)interpolateBakedPaths(parseSvgPath("M0 0 L1 0"),
        parseSvgPath("M0 0 L1 0 L2 0"), .5f); }),
        "incompatible morph topology fails closed");
    check(throwsInvalid([&] { (void)parseSvgPath("M0 0 A1 1 0 2 0 2 2"); }),
          "invalid SVG arc flags are rejected");
    check(throwsInvalid([&] { (void)parseSvgPath("M0 0 L1,"); }),
          "malformed SVG coordinate sequences are rejected");
    check(throwsInvalid([&] { (void)parseSvgPath("M,0 0"); }),
          "SVG separators cannot precede the first numeric argument");
    const auto compactArc = parseSvgPath("M0 0 A10 10 0 0110 10");
    check(compactArc.size() >= 2 && compactArc.back().point == Vector2(10.f, 10.f),
          "SVG arc flags accept the compact adjacent-flag grammar");
    check(throwsInvalid([&] { (void)parseSvgPath("M0 0 A1 1 1e39 0 1 10 0"); }),
          "SVG arc conversion rejects rotation conversion overflow");
    check(throwsInvalid([&] { (void)interpolateBakedPaths(
        {BakedPathCommand{static_cast<BakedPathCommandType>(255)}},
        {BakedPathCommand{static_cast<BakedPathCommandType>(255)}}, 0.5f); }),
          "path morph rejects malformed command topology at its API boundary");
    check(throwsInvalid([&] { (void)expandPathRepeater({BakedPathCommand{BakedPathCommandType::MoveTo},
        BakedPathCommand{static_cast<BakedPathCommandType>(255)}}, RepeaterSpec{}); }),
        "repeater rejects out-of-range command tags");
    check(throwsInvalid([&] { (void)parseSvgPath("M0 0 L1 1e999"); }),
          "SVG coordinates outside finite float range are rejected");
    check(throwsInvalid([&] { (void)parseSvgPath("M0 0 C1 2 3 4"); }),
          "incomplete SVG cubic commands are rejected");
    check(throwsInvalid([&] { (void)expandPathRepeater(start, RepeaterSpec{0}); }),
          "repeater rejects zero copies");
}
}
