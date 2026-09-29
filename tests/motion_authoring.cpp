#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "motion_check.hpp"
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include <nlohmann/json.hpp>
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

#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
bool sameLoweredPlan(const chronon3d::render_plan::RenderPlan& left,
                     const chronon3d::render_plan::RenderPlan& right) {
    using namespace chronon3d::render_plan;
    if (left.schema != right.schema || left.job_id != right.job_id ||
        left.canvas.width != right.canvas.width || left.canvas.height != right.canvas.height ||
        left.canvas.fps != right.canvas.fps || left.canvas.duration != right.canvas.duration ||
        left.output.path != right.output.path || left.layers.size() != right.layers.size())
        return false;
    for (std::size_t layerIndex = 0; layerIndex < left.layers.size(); ++layerIndex) {
        const auto& a = left.layers[layerIndex];
        const auto& b = right.layers[layerIndex];
        if (a.id != b.id || a.type != b.type || a.asset != b.asset || a.source != b.source ||
            a.position != b.position || a.scale != b.scale || a.rotation != b.rotation ||
            a.enable_3d != b.enable_3d || a.opacity != b.opacity ||
            a.animation.has_value() != b.animation.has_value())
            return false;
        if (!a.animation) continue;
        if (a.animation->tracks.size() != b.animation->tracks.size()) return false;
        for (std::size_t trackIndex = 0; trackIndex < a.animation->tracks.size(); ++trackIndex) {
            const auto& trackA = a.animation->tracks[trackIndex];
            const auto& trackB = b.animation->tracks[trackIndex];
            if (trackA.property != trackB.property || trackA.easing != trackB.easing ||
                trackA.keyframes.size() != trackB.keyframes.size())
                return false;
            for (std::size_t keyIndex = 0; keyIndex < trackA.keyframes.size(); ++keyIndex)
                if (trackA.keyframes[keyIndex].frame != trackB.keyframes[keyIndex].frame ||
                    trackA.keyframes[keyIndex].value != trackB.keyframes[keyIndex].value)
                    return false;
        }
    }
    return true;
}

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
    check(plan.layers[1].enable_3d,
          "lowering explicitly opts three-component transform vectors into 3D projection");
    checkNear(plan.layers[1].position[0], 10.f, 1e-6f,
              "fixed lowering golden: initial world X position");
    checkNear(plan.layers[1].position[1], 20.f, 1e-6f,
              "fixed lowering golden: initial world Y position");
    check(plan.layers[1].animation->tracks[0].keyframes.front().value ==
              std::vector<float>{10.f, 20.f, 0.f},
          "fixed lowering golden: first baked position key");
    check(plan.layers[1].animation->tracks[0].keyframes.back().value ==
              std::vector<float>{40.f, 50.f, 0.f},
          "fixed lowering golden: final baked position key");
    const auto repeatedV2 = lowerToRenderPlan(scene, options);
    check(sameLoweredPlan(plan, repeatedV2),
          "typed V2 lowering repeats every emitted transform and keyframe bit-exactly");

    options.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    auto v3 = lowerToRenderPlan(scene, options);
    check(v3.schema == chronon3d::render_plan::kRenderPlanSchemaV3,
          "typed lowering supports Chronon3D V3");
    const auto repeatedV3 = lowerToRenderPlan(scene, options);
    check(sameLoweredPlan(v3, repeatedV3),
          "typed V3 lowering repeats every emitted transform and keyframe bit-exactly");

    MotionScene videoScene("time remap", 30.f);
    auto& video = videoScene.addContent(31, "video", ContentRef::video("source-video", {640.f, 360.f}));
    video.lifetime = FrameRange::between(0, 3);
    video.timeRemap.stretch = 2.f;
    RenderPlanLoweringOptions videoOptions;
    videoOptions.width = 640;
    videoOptions.height = 360;
    videoOptions.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    chronon3d::render_plan::LayerPlan videoPlan;
    videoPlan.type = chronon3d::render_plan::LayerType::Video;
    videoPlan.source = "source-video";
    videoOptions.contentLayers.emplace("31", videoPlan);
    const auto slowedVideo = lowerToRenderPlan(videoScene, videoOptions);
    check(slowedVideo.layers[0].time_remap && slowedVideo.layers[0].time_remap->speed == 0.5f,
          "static Motion stretch lowers to the equivalent positive RenderPlan playback speed");
    const std::string slowedJson = renderPlanToJson(slowedVideo);
    const auto parsedSlowedJson = nlohmann::json::parse(slowedJson);
    check(parsedSlowedJson.at("layers").at(0).at("time_remap").at("speed") == 0.5f,
          "V3 emitter writes static video time-remap speed");
    check(renderPlanToJson(slowedVideo) == slowedJson,
          "time-remap JSON emission is byte-for-byte deterministic");

    MotionScene remappedVideoScene("animated time remap", 30.f);
    auto& remappedVideo = remappedVideoScene.addContent(
        32, "animated video", ContentRef::video("animated-source-video", {640.f, 360.f}));
    remappedVideo.lifetime = FrameRange::between(0, 3);
    remappedVideo.timeRemapTrack.add(0.f, 0.f).add(1.f, 20.f);
    videoOptions.contentLayers.clear();
    videoPlan.source = "animated-source-video";
    videoOptions.contentLayers.emplace("32", videoPlan);
    const auto animatedRemap = lowerToRenderPlan(remappedVideoScene, videoOptions);
    check(animatedRemap.canvas.duration.integral() == 31,
          "composition duration includes the end of a keyed time-remap curve");
    check(animatedRemap.layers[0].time_remap && animatedRemap.layers[0].time_remap->animation &&
          animatedRemap.layers[0].time_remap->animation->keyframes.size() == 31,
          "keyframed video remap lowers to a bounded deterministic scalar track");
    checkNear(animatedRemap.layers[0].time_remap->animation->keyframes[15].value.front(), 10.f, 1e-5f,
              "keyframed time-remap samples the authored source frame at the midpoint");
    const auto parsedAnimatedRemap = nlohmann::json::parse(renderPlanToJson(animatedRemap));
    check(parsedAnimatedRemap.at("layers").at(0).at("time_remap").at("animation")
              .at("keyframes").at(15).at("value") == 10.f,
          "V3 emitter preserves scalar time-remap keyframes");

    auto legacyVideoOptions = videoOptions;
    legacyVideoOptions.contentLayers.clear();
    legacyVideoOptions.contentLayers.emplace("31", videoPlan);
    legacyVideoOptions.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(throwsInvalid([&] { (void)lowerToRenderPlan(videoScene, legacyVideoOptions); }),
          "Motion time remap fails closed when the selected RenderPlan schema is V2");
    auto illegalRemapPlan = slowedVideo;
    illegalRemapPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(illegalRemapPlan).empty(),
          "time remap diagnostics reject a V3-only feature on V2");
    check(throwsInvalid([&] { (void)renderPlanToJson(illegalRemapPlan); }),
          "the emitter rejects V3 time remapping attached to a V2 plan");
    auto loopedVideoScene = MotionScene("looped time remap", 30.f);
    auto& loopedVideo = loopedVideoScene.addContent(
        33, "looped video", ContentRef::video("looped-source-video", {640.f, 360.f}));
    loopedVideo.lifetime = FrameRange::between(0, 3);
    loopedVideo.timeRemap.loopEnd = 12;
    auto loopedVideoOptions = videoOptions;
    loopedVideoOptions.contentLayers.clear();
    videoPlan.source = "looped-source-video";
    loopedVideoOptions.contentLayers.emplace("33", videoPlan);
    check(throwsInvalid([&] { (void)lowerToRenderPlan(loopedVideoScene, loopedVideoOptions); }),
          "looping time remap is explicitly rejected instead of silently changing its V3 semantics");
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
    animatedRig.positionTrack().add(3.f / 30.f, Vector3(3.f, 2.f, -8.f));
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
    check(cameraPlan.canvas.duration.integral() == 4,
          "camera animation extends the composition duration when its keys outlive scene content");

    chronon3d::render_plan::LayerPlan bakedShape;
    bakedShape.type = chronon3d::render_plan::LayerType::Shape;
    chronon3d::render_plan::ShapeContentPlan bakedContent;
    bakedContent.type = chronon3d::render_plan::ShapeContentTypePlan::Path;
    bakedShape.shape = bakedContent;
    const auto copies = expandPathRepeater(parseSvgPath("M0 0 L5 0"), RepeaterSpec{2, {8.f, 0.f}});
    applyBakedPathGeometry(bakedShape, parseSvgPath("M0 0 L5 0"), copies);
    check(bakedShape.shape->operators.size() == 1 &&
          bakedShape.shape->operators.front().kind == chronon3d::render_plan::PathOperatorKindPlan::Merge &&
          bakedShape.shape->operators.front().paths.size() == 1,
          "baked repeater identity copy remains the source path and additional copies lower as a V3 Merge operator");
    const auto morphFrames = bakeSvgPathMorph("M0 0 L10 0 Z", "M0 0 L20 0 Z", 4, 6);
    chronon3d::render_plan::LayerPlan morphShape = bakedShape;
    morphShape.shape->operators.clear();
    applyBakedPathMorph(morphShape, morphFrames, 4);
    check(morphShape.shape->operators.size() == 1 &&
          morphShape.shape->operators.front().kind == chronon3d::render_plan::PathOperatorKindPlan::Morph &&
          morphShape.shape->operators.front().morph_target.size() == 3 &&
          morphShape.shape->operators.front().morph_animation &&
          morphShape.shape->operators.front().morph_animation->keyframes.size() == 3,
          "baked compatible SVG frames lower to native endpoint Morph geometry and deterministic progress keys");
    auto unsupportedMorph = morphFrames;
    unsupportedMorph[1][1].point.x += 0.5f;
    check(throwsInvalid([&] { applyBakedPathMorph(morphShape, unsupportedMorph, 4); }),
          "morph adapter rejects baked intermediate samples not representable by linear endpoint morphing");
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
    cameraPose.aperture = 2.8f;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering rejects unsupported authored depth-of-field lens data");
    check(throwsInvalid([&] {
        const std::array<BakedPath, 2> missingIdentity{
            parseSvgPath("M8 0 L13 0"), parseSvgPath("M16 0 L21 0")};
        applyBakedPathGeometry(bakedShape, parseSvgPath("M0 0 L5 0"), missingIdentity);
    }), "repeater adapter rejects copies without the unmodified source at index zero");
    check(throwsInvalid([&] {
        MotionScene emptyScene("camera only", 30.f);
        CameraRig tooMany(41);
        tooMany.positionTrack().add(0.f, Vector3{});
        tooMany.positionTrack().add(65536.f / 30.f, Vector3{});
        RenderPlanLoweringOptions tooManyOptions;
        tooManyOptions.cameraRig = &tooMany;
        (void)lowerToRenderPlan(emptyScene, tooManyOptions);
    }), "camera-only output obeys the RenderPlan camera keyframe budget");
}
#endif

} // namespace

int main() {
    testVerletAndRigidBodyBaking();
    testTwoSegmentIk();
    testPcmAudioAnalysis();
    testPresetTrackGolden();
    testDynamicFlowLayout();
    testVectorPathBaking();
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
    testTypedChronon3dLowering();
#endif
    return chrononmotion_test::report();
}
