#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "motion_check.hpp"
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#endif

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

template<class Function>
bool throwsInvalid(Function&& function) {
    try { function(); }
    catch (const std::invalid_argument&) { return true; }
    return false;
}

void testVerletAndRigidBodyBaking() {
    section("offline secondary motion and 2D collisions");
    VerletChainSpec chain;
    chain.joints = {Vector2(0.f, 0.f), Vector2(1.f, 0.f), Vector2(2.f, 0.f)};
    chain.gravity.set(0.f, -9.81f);
    chain.fps = 60.f;
    const auto baked = bakeVerletChain(chain, 0, 12);
    check(baked.samples.size() == 13 && baked.jointPositions.size() == 3 && baked.segmentRotations.size() == 2,
          "Verlet chain bakes inclusive fixed-rate joint-position and segment-rotation tracks");
    check(baked.jointPositions[0].size() == 13 && baked.segmentRotations[1].size() == 13,
          "every Verlet joint and segment has a dense frame track");
    checkNear(baked.segmentRotations[0].sample(0.f).z, 0.f, 1e-6f,
              "Verlet segment orientation corresponds to the same-frame position sample");
    check(throwsInvalid([&] { auto tiny = chain; tiny.fps = std::numeric_limits<float>::denorm_min();
        (void)bakeVerletChain(tiny, 0, 1); }),
        "Verlet rejects a frame rate whose fixed-step square overflows");
    check(throwsInvalid([&] { auto tooMany = chain; tooMany.joints.resize(4);
        (void)bakeVerletChain(tooMany, 0, 1'000'000); }),
        "Verlet rejects requests beyond its bounded pose sample budget");
    checkNear(baked.samples.front().front(), Vector2(0.f, 0.f), 1e-6f,
              "Verlet chain root remains pinned");
    for (const auto& pose : baked.samples) {
        checkNear(pose[0].distanceTo(pose[1]), 1.f, 2e-2f,
                  "Verlet constraint keeps segment length within solver tolerance");
    }
    const auto again = bakeVerletChain(chain, 0, 12);
    check(baked.samples.back()[2] == again.samples.back()[2],
          "offline chain is repeatably deterministic");
    check(throwsInvalid([&] { (void)bakeVerletChain(chain, -1, 2); }),
          "Verlet chain rejects negative frame ranges");
    auto invalidChain = chain;
    invalidChain.joints[1] = invalidChain.joints[0];
    check(throwsInvalid([&] { (void)bakeVerletChain(invalidChain, 0, 1); }),
          "Verlet chain rejects zero-length segments");

    RigidBodyWorld2D world;
    world.gravity.set(0.f, -10.f);
    world.floorY = 0.f;
    world.left = -10.f;
    world.right = 10.f;
    world.fps = 30.f;
    std::vector<RigidBody2D> bodies{
        RigidBody2D{1, Vector2(-0.4f, 2.f), Vector2(0.5f, 0.f), 0.5f, 1.f, 0.8f, 0.1f},
        RigidBody2D{2, Vector2(0.4f, 3.f), Vector2(-0.5f, 0.f), 0.5f, 1.f, 0.8f, 0.1f}};
    const auto physics = bakeRigidBodies2D(bodies, world, 0, 60);
    check(physics.samples.size() == 61 && physics.positionTracks.size() == 2 && physics.ids[1] == 2,
          "2D impulse solver bakes stable identity and a position track per body");
    check(physics.positionTracks[0].size() == 61,
          "rigid body tracks include every requested frame");
    check(physics.samples.back()[0].y >= 0.49f && physics.samples.back()[1].y >= 0.49f,
          "bodies settle above the floor without penetrating it");
    check(physics.samples.back()[0].distanceTo(physics.samples.back()[1]) >= 0.99f,
          "circle collision solver resolves overlap");
    const auto bounced = bakeRigidBodies2D(
        {RigidBody2D{3, {0.f, 0.6f}, {0.f, -3.f}, 0.5f, 1.f, 1.f, 0.f}}, world, 0, 2);
    check(bounced.samples[1][0].y >= 0.5f && bounced.samples[1][0].y < 0.7f,
          "a fast circle resolves floor crossing during its impact frame");
    check(bounced.samples[2][0].y > bounced.samples[1][0].y,
          "floor restitution reverses vertical motion immediately on impact");
    const auto physicsAgain = bakeRigidBodies2D(bodies, world, 0, 60);
    check(physics.samples.back() == physicsAgain.samples.back(),
          "rigid-body bake is repeatably deterministic");
    check(throwsInvalid([&] { (void)bakeRigidBodies2D({bodies[0], bodies[0]}, world, 0, 1); }),
          "rigid-body solver rejects duplicate identities");
    check(throwsInvalid([&] { auto bad = world; bad.fps = 0.f; (void)bakeRigidBodies2D(bodies, bad, 0, 1); }),
          "rigid-body solver rejects invalid simulation rate");
    check(throwsInvalid([&] { auto bad = world; bad.fps = std::numeric_limits<float>::denorm_min();
        (void)bakeRigidBodies2D(bodies, bad, 0, 1); }),
        "rigid-body solver rejects a non-finite fixed step");
    const auto fixedBody = bakeRigidBodies2D({RigidBody2D{9, {0.f, 1.f}, {}, .5f, 0.f}}, world, 0, 2);
    check(fixedBody.samples[0][0] == fixedBody.samples[2][0],
          "non-positive body mass denotes a stationary immovable body");
    check(throwsInvalid([&] { auto bad = world; bad.left = -std::numeric_limits<float>::max();
        bad.right = std::numeric_limits<float>::max(); (void)bakeRigidBodies2D({}, bad, 0, 1); }),
        "rigid-body solver rejects overflowing world bounds");
}

void testTwoSegmentIk() {
    section("analytic two-segment IK");
    const auto solved = solveTwoSegmentIK2D(Vector2(0.f, 0.f), Vector2(1.f, 1.f), 1.f, 1.f);
    check(solved.targetReachable, "reachable IK target is reported as reachable");
    checkNear(solved.elbow.distanceTo(Vector2(0.f, 0.f)), 1.f, 1e-5f,
              "IK upper segment length matches the constraint");
    checkNear(solved.end.distanceTo(solved.elbow), 1.f, 1e-5f,
              "IK lower segment length matches the constraint");
    const auto unreachable = solveTwoSegmentIK2D(Vector2(0.f, 0.f), Vector2(10.f, 0.f), 2.f, 1.f);
    check(!unreachable.targetReachable, "unreachable IK target is reported");
    checkNear(unreachable.end.distanceTo(Vector2(0.f, 0.f)), 3.f, 1e-5f,
              "unreachable IK target clamps to maximum reach");
    const auto farReach = solveTwoSegmentIK2D(Vector2(0.f, 0.f), Vector2(1e30f, 0.f), 2.f, 1.f);
    check(!farReach.targetReachable && std::isfinite(farReach.end.x),
          "large finite unreachable targets clamp without overflowing intermediate distance arithmetic");
    const auto tooClose = solveTwoSegmentIK2D(Vector2(0.f, 0.f), Vector2(0.1f, 0.f), 2.f, 1.f);
    check(!tooClose.targetReachable, "inside-annulus IK target is reported unreachable");
    checkNear(tooClose.end.distanceTo(Vector2(0.f, 0.f)), 1.f, 1e-5f,
              "inside-annulus target clamps to minimum reach");
    const auto rootTarget = solveTwoSegmentIK2D(Vector2(3.f, 4.f), Vector2(3.f, 4.f), 2.f, 1.f,
                                                math::PI * 0.5f);
    checkNear(rootTarget.end.distanceTo(Vector2(3.f, 4.f)), 1.f, 1e-5f,
              "coincident IK targets deterministically select a minimum-reach direction");
    const auto unequalReach = solveTwoSegmentIK2D(Vector2(0.f, 0.f), Vector2(0.1f, 0.f), 3.f, 1.f);
    checkNear(unequalReach.end.distanceTo(Vector2{}), 2.f, 1e-5f,
              "inside-annulus IK reach clamps correctly when segment lengths differ");
    const auto folded = solveTwoSegmentIK2D(Vector2(2.f, -1.f), Vector2(2.f, -1.f), 2.f, 2.f);
    checkNear(folded.end, Vector2(2.f, -1.f), 1e-6f,
              "equal-length coincident IK target remains a valid zero-radius folded solution");
    checkNear(folded.elbow.distanceTo(Vector2(2.f, -1.f)), 2.f, 1e-5f,
              "folded IK elbow preserves the upper-segment length");
    checkNear(folded.end.distanceTo(folded.elbow), 2.f, 1e-5f,
              "folded IK elbow preserves the lower-segment length");
    check(throwsInvalid([&] { (void)solveTwoSegmentIK2D({3e38f, 0.f}, {3e38f, 0.f}, 3e38f, 1.f); }),
          "IK rejects results that cannot be represented as finite float coordinates");
    const auto reversedBend = solveTwoSegmentIK2D(Vector2(0.f, 0.f), Vector2(1.f, 1.f), 1.f, 1.f, 0.f, false);
    check(reversedBend.elbow.y < solved.elbow.y,
          "bend sign selects the opposite elbow branch deterministically");
    check(throwsInvalid([&] { (void)solveTwoSegmentIK2D({}, {}, 0.f, 1.f); }),
          "IK rejects zero segment lengths");
}

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

#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
void testTypedChronon3dLowering() {
    section("typed Chronon3D v2/v3 RenderPlan lowering");
    MotionScene scene("plan", 30.f);
    scene.addNull(10, "Controller");
    auto& layer = scene.addContent(11, "Promo", ContentRef::image("opaque-promo", Vector2(100.f, 50.f)), 10);
    layer.transform.position.set(10.f, 20.f, 0.f);
    layer.tracks.position.add(0.f, Vector3(10.f, 20.f, 0.f));
    layer.tracks.position.add(1.f, Vector3(40.f, 50.f, 0.f));
    scene.evaluate(0);
    RenderPlanLoweringOptions options;
    options.width = 640;
    options.height = 360;
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    chronon3d::render_plan::LayerPlan content;
    content.type = chronon3d::render_plan::LayerType::Image;
    content.asset = "opaque-promo";
    content.color = {0.f, 0.f, 0.f, 0.f};
    content.size = {100.f, 50.f};
    content.size_dimensions = 2;
    options.contentLayers.emplace("11", content);
    const auto priorPose = scene.state(11).local.position;
    auto plan = lowerToRenderPlan(scene, options);
    check(scene.lastEvaluatedFrame() == 0 && scene.state(11).local.position == priorPose,
          "lowering restores the original frame after dense offline sampling");
    check(plan.layers.size() == 2 && plan.canvas.duration.integral() == 31,
          "typed lowering emits controller/content and inclusive composition duration");
    check(plan.schema == chronon3d::render_plan::kRenderPlanSchemaV2,
          "typed lowering selects the canonical V2 schema");
    check(!plan.layers[1].parent.has_value(), "world composition is flattened exactly once");
    check(plan.layers[1].animation && plan.layers[1].animation->tracks.size() == 4,
          "world motion is baked into v2 transform and opacity tracks");
    check(plan.layers[1].animation->tracks[0].property == "position" &&
          plan.layers[1].animation->tracks[0].keyframes[30].value[0] == 40.f,
          "the final baked keyframe contains the evaluated world position");
    check(plan.layers[1].animation->tracks.front().keyframes.size() == 31,
          "dense lowering emits a frame-resolved key for every frame");
    checkNear(plan.layers[1].position[0], 10.f, 1e-5f,
              "image plan position preserves authored world coordinates");
    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    auto v3 = lowerToRenderPlan(scene, options);
    check(v3.schema == chronon3d::render_plan::kRenderPlanSchemaV3,
          "typed lowering supports Chronon3D V3");
    const auto shapePath = lowerBakedPath(parseSvgPath("M0 0 C1 0 2 1 3 1 Z"));
    check(shapePath.size() == 3 && shapePath[1].type == chronon3d::render_plan::ShapePathCommandTypePlan::CubicTo,
          "baked paths map to Chronon3D's native shape path command type");
    MotionScene cameraScene("camera lower", 30.f);
    auto& cameraContent = cameraScene.addContent(22, "camera content", ContentRef::image("camera-asset", {4.f, 4.f}));
    cameraContent.lifetime = FrameRange::between(0, 2);
    cameraScene.evaluate(0);
    CameraRig animatedRig(23, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    animatedRig.setPosition({1.f, 2.f, -8.f});
    animatedRig.positionTrack().add(0.f, Vector3(1.f, 2.f, -8.f));
    animatedRig.positionTrack().add(2.f / 30.f, Vector3(3.f, 2.f, -8.f));
    RenderPlanLoweringOptions cameraOptions;
    cameraOptions.width = 640;
    cameraOptions.height = 360;
    cameraOptions.cameraRig = &animatedRig;
    chronon3d::render_plan::LayerPlan cameraImage;
    cameraImage.type = chronon3d::render_plan::LayerType::Image;
    cameraImage.asset = "camera-asset";
    cameraOptions.contentLayers.emplace("22", cameraImage);
    const auto cameraPlan = lowerToRenderPlan(cameraScene, cameraOptions);
    check(cameraPlan.camera && cameraPlan.camera_animation && cameraPlan.camera_animation->size() == 7,
          "camera rig lowers to a typed base projection and dense camera tracks");
    check(cameraPlan.camera_animation->front().keyframes.back().value.front() == 3.f,
          "camera position animation samples the final authored rig pose");

    chronon3d::render_plan::LayerPlan bakedShape;
    bakedShape.type = chronon3d::render_plan::LayerType::Shape;
    chronon3d::render_plan::ShapeContentPlan bakedContent;
    bakedContent.type = chronon3d::render_plan::ShapeContentTypePlan::Path;
    bakedShape.shape = bakedContent;
    const auto copies = expandPathRepeater(parseSvgPath("M0 0 L5 0"), RepeaterSpec{2, {8.f, 0.f}});
    applyBakedPathGeometry(bakedShape, parseSvgPath("M0 0 L5 0"), copies);
    check(bakedShape.shape->operators.size() == 1 &&
          bakedShape.shape->operators.front().kind == chronon3d::render_plan::PathOperatorKindPlan::Merge &&
          bakedShape.shape->operators.front().paths.size() == 2,
          "baked repeater copies lower as a static V3 compound-path operator");
    bool missingContentRejected = false;
    try { RenderPlanLoweringOptions empty; (void)lowerToRenderPlan(scene, empty); }
    catch (const std::invalid_argument&) { missingContentRejected = true; }
    check(missingContentRejected, "lowering rejects content whose rendering data is not supplied");
    check(throwsInvalid([&] { auto bad = options; bad.width = 0; (void)lowerToRenderPlan(scene, bad); }),
          "lowering rejects invalid canvas dimensions");
    check(throwsInvalid([&] { auto bad = options; bad.contentLayers.emplace("999", content);
        (void)lowerToRenderPlan(scene, bad); }),
        "lowering rejects stale content mappings that name no scene layer");
    check(throwsInvalid([&] { auto bad = options; bad.camera = chronon3d::render_plan::CameraPlan{};
        bad.camera->far = std::numeric_limits<float>::infinity(); (void)lowerToRenderPlan(scene, bad); }),
        "lowering rejects non-finite camera projection data");
    CameraPose cameraPose;
    cameraPose.fov = 50.f;
    const auto camera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    checkNear(camera.fov_deg, 50.f, 1e-6f, "camera pose lowering maps typed projection parameters");
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 2.f, 1.f); }),
          "camera lowering rejects an invalid clipping interval");
    const CameraRig* cameraRig = nullptr;
    (void)cameraRig;
}
#endif

} // namespace

int main() {
    testVerletAndRigidBodyBaking();
    testTwoSegmentIk();
    testPcmAudioAnalysis();
    testDynamicFlowLayout();
    testVectorPathBaking();
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
    testTypedChronon3dLowering();
#endif
    return chrononmotion_test::report();
}
