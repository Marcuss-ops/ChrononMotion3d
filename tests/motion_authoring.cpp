#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "motion_check.hpp"
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include "chrononmotion/motion/RigBaking.hpp"
#include <chronon3d/render_plan/effect_presets.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

using chronon3d::render_plan::TrackMattePlan;
using chronon3d::render_plan::TrackMatteTypePlan;

#include <nlohmann/json.hpp>
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
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

void testThreeDimensionalIkChain() {
    section("deterministic 3D FABRIK IK and pole vector");
    const std::array<Vector3, 3> rest{{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {2.f, 0.f, 0.f}}};
    const auto solved = solveIKChain3D(rest, {1.f, 1.f, 0.f}, {0.f, 0.f, 4.f});
    check(solved.targetReachable && solved.joints.size() == 3,
          "reachable 3D IK reports its endpoint and stable joint count");
    checkNear(solved.joints.front(), rest.front(), 1e-6f, "3D IK keeps its root pinned");
    checkNear(solved.joints.back(), Vector3(1.f, 1.f, 0.f), 2e-4f,
              "3D IK reaches a reachable target");
    checkNear(solved.joints[0].distanceTo(solved.joints[1]), 1.f, 2e-4f,
              "3D IK preserves the first segment length");
    checkNear(solved.joints[1].distanceTo(solved.joints[2]), 1.f, 2e-4f,
              "3D IK preserves the second segment length");
    check(solved.joints[1].z > 0.5f, "pole vector bends the chain toward its positive-Z side");

    const auto oppositePole = solveIKChain3D(rest, {1.f, 1.f, 0.f}, {0.f, 0.f, -4.f});
    check(oppositePole.joints[1].z < -0.5f,
          "reversing the pole vector selects the opposite bend plane side");

    const auto tooFar = solveIKChain3D(rest, {5.f, 0.f, 0.f}, {0.f, 0.f, 1.f});
    check(!tooFar.targetReachable, "3D IK reports an unreachable distant target");
    checkNear(tooFar.joints.back(), Vector3(2.f, 0.f, 0.f), 1e-5f,
              "distant target clamps to the maximum chain reach");

    const std::array<Vector3, 3> unequalRest{{{0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, {3.f, 0.f, 0.f}}};
    const auto insideAnnulus = solveIKChain3D(unequalRest, {0.1f, 0.f, 0.f}, {0.f, 0.f, 2.f});
    check(!insideAnnulus.targetReachable,
          "3D IK reports targets inside the chain's minimum reachable radius");
    checkNear(insideAnnulus.joints.back().distanceTo(unequalRest.front()), 1.f, 1e-5f,
              "inside-annulus target clamps to the minimum reachable radius");

    IK3BakeSpec spec;
    spec.initialJoints.assign(rest.begin(), rest.end());
    spec.pole.set(0.f, 0.f, 4.f);
    spec.fps = 10.f;
    spec.targetTrack.add(0.f, Vector3(2.f, 0.f, 0.f));
    spec.targetTrack.add(1.f, Vector3(1.f, 1.f, 0.f));
    const auto baked = bakeIKChain3D(spec, 0, 10);
    const auto bakedAgain = bakeIKChain3D(spec, 0, 10);
    check(baked.samples.size() == 11 && baked.jointPositions.size() == 3 &&
          baked.targetReachable.size() == 11,
          "3D IK baker emits inclusive frame samples, reach status and one track per joint");
    check(baked.samples == bakedAgain.samples,
          "3D IK baking is bit-repeatable and independent of evaluation history");
    checkNear(baked.jointPositions[2].sample(1.f), Vector3(1.f, 1.f, 0.f), 2e-4f,
              "baked endpoint track matches the target track at the final frame");
    check(throwsInvalid([&] { auto bad = spec; bad.initialJoints[1].x = bad.initialJoints[0].x;
        (void)bakeIKChain3D(bad, 0, 1); }),
        "3D IK baker rejects zero-length bind segments");
    check(throwsInvalid([&] { (void)solveIKChain3D(rest, {0.f, 0.f, 0.f}, {}, 0); }),
          "3D IK rejects a zero-iteration solver configuration");
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
    using namespace chronon3d::render_plan;
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

    auto inlineMeshPlan = slowedVideo;
    auto& meshLayer = inlineMeshPlan.layers[0];
    meshLayer.id = "procedural-triangle";
    meshLayer.type = chronon3d::render_plan::LayerType::Mesh;
    meshLayer.source.clear();
    meshLayer.time_remap.reset();
    meshLayer.animation.reset();
    chronon3d::render_plan::InlineMeshPlan triangle;
    triangle.vertices = {
        {{{-1.f, -1.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.f, 0.f}}},
        {{{ 1.f, -1.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{1.f, 0.f}}},
        {{{ 0.f,  1.f, 0.f}}, {{0.f, 0.f, 1.f}}, {{0.5f, 1.f}}},
    };
    triangle.indices = {0, 1, 2};
    triangle.material = chronon3d::render_plan::InlineMeshMaterialPlan{};
    triangle.material->base_color = std::array<float, 4>{0.1f, 0.6f, 0.3f, 1.f};
    meshLayer.inline_mesh = triangle;
    check(renderPlanEmissionProblem(inlineMeshPlan).empty(),
          "the producer accepts bounded indexed inline mesh geometry with a typed material");
    const std::string inlineMeshJson = renderPlanToJson(inlineMeshPlan);
    const auto parsedInlineMesh = nlohmann::json::parse(inlineMeshJson);
    check(parsedInlineMesh.at("layers").at(0).at("inline_mesh").at("vertices").size() == 3 &&
          parsedInlineMesh.at("layers").at(0).at("inline_mesh").at("indices") == nlohmann::json{0, 1, 2},
          "the emitter preserves procedural mesh vertices and triangle indices");
    const auto& emittedBaseColor = parsedInlineMesh.at("layers").at(0).at("inline_mesh").at("material").at("base_color");
    check(emittedBaseColor.size() == 4,
          "the emitter preserves all four inline mesh base color components");
    if (emittedBaseColor.size() == 4) {
        checkNear(emittedBaseColor.at(0).get<float>(), 0.1f, 1e-6f,
                  "the emitter preserves inline mesh base color red");
        checkNear(emittedBaseColor.at(1).get<float>(), 0.6f, 1e-6f,
                  "the emitter preserves inline mesh base color green");
        checkNear(emittedBaseColor.at(2).get<float>(), 0.3f, 1e-6f,
                  "the emitter preserves inline mesh base color blue");
        checkNear(emittedBaseColor.at(3).get<float>(), 1.f, 1e-6f,
                  "the emitter preserves inline mesh base color alpha");
    }
    check(renderPlanToJson(inlineMeshPlan) == inlineMeshJson,
          "inline mesh emission is byte deterministic");
    auto invalidMeshPlan = inlineMeshPlan;
    invalidMeshPlan.layers[0].inline_mesh->indices[2] = 3;
    check(!renderPlanEmissionProblem(invalidMeshPlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidMeshPlan); }),
          "out-of-range inline mesh indices fail closed before emission");

    MotionScene proceduralMeshScene("procedural mesh canary", 30.f);
    auto proceduralTriangle = BufferGeometry::create();
    proceduralTriangle->setAttribute("position", FloatBufferAttribute::create(
        {-1.f, -1.f, 0.f, 1.f, -1.f, 0.f, 0.f, 1.f, 0.f}, 3));
    proceduralTriangle->setAttribute("normal", FloatBufferAttribute::create(
        {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f}, 3));
    proceduralTriangle->setAttribute("uv", FloatBufferAttribute::create(
        {0.f, 0.f, 1.f, 0.f, 0.5f, 1.f}, 2));
    proceduralTriangle->setIndex(std::vector<unsigned int>{0, 1, 2});
    const MaterialDescriptor proceduralMaterial = MaterialDescriptor::lambert(9, Color(0x336699));
    proceduralMeshScene.addMesh(71, "triangle", MeshAttachment{
        .geometry = proceduralTriangle, .material = proceduralMaterial})
        .alive(0, 1);
    RenderPlanLoweringOptions proceduralMeshOptions;
    proceduralMeshOptions.width = 64;
    proceduralMeshOptions.height = 64;
    proceduralMeshOptions.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    chronon3d::render_plan::LayerPlan proceduralMeshContent;
    proceduralMeshContent.type = chronon3d::render_plan::LayerType::Mesh;
    proceduralMeshOptions.contentLayers.emplace("71", proceduralMeshContent);
    const auto loweredProceduralMesh = lowerToRenderPlan(proceduralMeshScene, proceduralMeshOptions);
    check(loweredProceduralMesh.layers.size() == 1 && loweredProceduralMesh.layers[0].inline_mesh &&
          loweredProceduralMesh.layers[0].inline_mesh->vertices.size() == 3 &&
          loweredProceduralMesh.layers[0].inline_mesh->material.has_value(),
          "MotionScene geometry and Lambert material lower into a native RenderPlan inline mesh");
    Color expectedPlanMaterialColor = proceduralMaterial.baseColor;
    expectedPlanMaterialColor.convertLinearToSRGB();
    checkNear(loweredProceduralMesh.layers[0].inline_mesh->material->base_color->at(0),
              expectedPlanMaterialColor.r, 1e-6f,
              "mesh lowering converts the native material color to the RenderPlan sRGB contract");
    checkNear(loweredProceduralMesh.layers[0].inline_mesh->material->base_color->at(3),
              proceduralMaterial.opacity, 1e-6f,
              "mesh lowering preserves the native material opacity");
    const auto emittedProceduralMesh = nlohmann::json::parse(renderPlanToJson(loweredProceduralMesh));
    check(emittedProceduralMesh.at("layers").at(0).at("inline_mesh").at("indices") ==
              nlohmann::json{0, 1, 2},
          "procedural MotionScene geometry reaches the emitted RenderPlan without a second adapter");
    MotionScene pbrMeshScene("procedural pbr mesh contract", 30.f);
    const MaterialDescriptor pbrDescriptor = MaterialDescriptor::pbr(10, Color(0x8090a0), 0.25f, 0.8f);
    pbrMeshScene.addMesh(73, "pbr triangle", MeshAttachment{
        .geometry = proceduralTriangle, .material = pbrDescriptor}).alive(0, 1);
    proceduralMeshOptions.contentLayers.clear();
    proceduralMeshOptions.contentLayers.emplace("73", proceduralMeshContent);
    const auto loweredPbrMesh = lowerToRenderPlan(pbrMeshScene, proceduralMeshOptions);
    check(loweredPbrMesh.layers.size() == 1 && loweredPbrMesh.layers[0].inline_mesh &&
          loweredPbrMesh.layers[0].inline_mesh->material.has_value(),
          "PBR MotionScene meshes remain expressible in the compact RenderPlan material contract");
    if (loweredPbrMesh.layers.size() == 1 && loweredPbrMesh.layers[0].inline_mesh &&
        loweredPbrMesh.layers[0].inline_mesh->material) {
        checkNear(loweredPbrMesh.layers[0].inline_mesh->material->specular,
                  pbrDescriptor.metalness, 1e-6f,
                  "PBR lowering maps metalness into RenderPlan specular strength");
        checkNear(loweredPbrMesh.layers[0].inline_mesh->material->shininess,
                  1.f + (1.f - pbrDescriptor.roughness) * 1023.f, 1e-3f,
                  "PBR lowering maps roughness into RenderPlan shininess approximation");
    }
    MotionScene animatedMeshScene("animated mesh contract", 30.f);
    Layer& animatedMesh = animatedMeshScene.addMesh(
        72, "animated triangle", MeshAttachment{.geometry = proceduralTriangle});
    animatedMesh.alive(0, 1);
    animatedMesh.tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f));
    animatedMesh.tracks.position.add(1.f / 30.f, Vector3(1.f, 0.f, 0.f));
    proceduralMeshOptions.jobId = "animated_mesh_contract";
    proceduralMeshOptions.contentLayers.clear();
    proceduralMeshOptions.contentLayers.emplace("72", proceduralMeshContent);
    check(throwsInvalid([&] { (void)lowerToRenderPlan(animatedMeshScene, proceduralMeshOptions); }),
          "animated inline mesh transforms fail closed instead of emitting decoder-invalid tracks");

    auto chalkPathPlan = slowedVideo;
    auto& chalkLayer = chalkPathPlan.layers[0];
    chalkLayer.id = "chalk-stroke";
    chalkLayer.type = chronon3d::render_plan::LayerType::Shape;
    chalkLayer.source.clear();
    chalkLayer.time_remap.reset();
    chalkLayer.animation.reset();
    chalkLayer.size = {640.f, 360.f};
    chalkLayer.size_dimensions = 2;
    chronon3d::render_plan::ShapeContentPlan chalkPath;
    chalkPath.type = chronon3d::render_plan::ShapeContentTypePlan::Path;
    chalkPath.path = {
        {chronon3d::render_plan::ShapePathCommandTypePlan::MoveTo, {20.f, 30.f}},
        {chronon3d::render_plan::ShapePathCommandTypePlan::CubicTo, {120.f, 80.f},
            {40.f, 5.f}, {90.f, 110.f}},
    };
    chronon3d::render_plan::StrokeStyle chalk;
    chalk.color = "#F6F1D5";
    chalk.width = 8.f;
    chalk.cap = "round";
    chalk.join = "round";
    chalkPath.stroke = chalk;
    chronon3d::render_plan::PathOperatorPlan trim;
    trim.kind = chronon3d::render_plan::PathOperatorKindPlan::Trim;
    trim.trim_animation = chronon3d::render_plan::AnimationTrackPlan{};
    trim.trim_animation->property = "trim";
    trim.trim_animation->keyframes = {
        {chronon3d::Frame{0}, {0.f}}, {chronon3d::Frame{12}, {1.f}}};
    chalkPath.operators.push_back(trim);
    chalkLayer.shape = chalkPath;
    check(renderPlanEmissionProblem(chalkPathPlan).empty(),
          "a cubic chalk path with animated trim is accepted by the shared RenderPlan boundary");
    const std::string chalkJson = renderPlanToJson(chalkPathPlan);
    const auto emittedChalk = nlohmann::json::parse(chalkJson);
    check(emittedChalk.at("layers").at(0).at("shape").at("path").at(1).at("type") == "cubic_to" &&
          emittedChalk.at("layers").at(0).at("shape").at("path").at(1).at("control2") ==
              nlohmann::json{90.f, 110.f},
          "the shape writer preserves cubic endpoints and both Bézier controls");
    check(emittedChalk.at("layers").at(0).at("shape").at("stroke").at("width") == 8.f &&
          emittedChalk.at("layers").at(0).at("shape").at("operators").at(0).at("params")
              .at("animation").at("keyframes").size() == 2,
          "the shape writer preserves chalk stroke style and the trim reveal track");
    auto unsupportedShapePlan = chalkPathPlan;
    unsupportedShapePlan.layers[0].shape->operators[0].kind =
        chronon3d::render_plan::PathOperatorKindPlan::Repeater;
    check(!renderPlanEmissionProblem(unsupportedShapePlan).empty(),
          "unsupported path operators are refused instead of silently discarded");
    const auto checkProceduralShapeEmission = [&](chronon3d::render_plan::ShapeContentPlan shape,
                                                   const char* expectedType) {
        auto plan = chalkPathPlan;
        plan.layers[0].shape = std::move(shape);
        const std::string problem = renderPlanEmissionProblem(plan);
        check(problem.empty(), problem.empty() ? "procedural shape is accepted" : problem.c_str());
        if (problem.empty()) {
            const auto json = nlohmann::json::parse(renderPlanToJson(plan));
            check(json.at("layers").at(0).at("shape").at("type") == expectedType,
                  "procedural shape kind survives typed-plan JSON emission");
        }
    };
    chronon3d::render_plan::ShapeContentPlan proceduralShape;
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Line;
    proceduralShape.path = chalkPath.path;
    proceduralShape.stroke = chalk;
    checkProceduralShapeEmission(proceduralShape, "line");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Rect;
    proceduralShape.fill_color = std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.f};
    checkProceduralShapeEmission(proceduralShape, "rect");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::RoundedRect;
    proceduralShape.corner_radius = std::array<float, 4>{8.f, 12.f, 8.f, 12.f};
    proceduralShape.fill_color = std::array<float, 4>{0.1f, 0.2f, 0.3f, 1.f};
    checkProceduralShapeEmission(proceduralShape, "rounded_rect");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Ellipse;
    checkProceduralShapeEmission(proceduralShape, "ellipse");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Star;
    proceduralShape.points = 7;
    checkProceduralShapeEmission(proceduralShape, "star");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Polygon;
    proceduralShape.points = 6;
    checkProceduralShapeEmission(proceduralShape, "polygon");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Arrow;
    checkProceduralShapeEmission(proceduralShape, "arrow");
    proceduralShape.arrow_direction = "left";
    auto unrepresentableArrow = chalkPathPlan;
    unrepresentableArrow.layers[0].shape = proceduralShape;
    check(!renderPlanEmissionProblem(unrepresentableArrow).empty(),
          "arrow direction is refused because the consumer schema has no matching field");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Arc;
    proceduralShape.stroke = chalk;
    checkProceduralShapeEmission(proceduralShape, "arc");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::Grid;
    proceduralShape.stroke = chalk;
    checkProceduralShapeEmission(proceduralShape, "grid");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::DotGrid;
    proceduralShape.fill_color = std::array<float, 4>{1.f, 1.f, 1.f, 1.f};
    checkProceduralShapeEmission(proceduralShape, "dot_grid");
    proceduralShape = {};
    proceduralShape.type = chronon3d::render_plan::ShapeContentTypePlan::DeviceFrame;
    proceduralShape.fill_color = std::array<float, 4>{0.08f, 0.09f, 0.1f, 1.f};
    proceduralShape.device_frame_kind = chronon3d::render_plan::DeviceFrameKindPlan::Browser;
    checkProceduralShapeEmission(proceduralShape, "device_frame");

    using FieldPlan = chronon3d::render_plan::ShapeContentPlan;
    using FieldType = chronon3d::render_plan::ShapeContentTypePlan;
    chronon3d::render_plan::ShapeContentPlan fieldShape;
    fieldShape.type = FieldType::Rect;
    fieldShape.field = chronon3d::graphics::Field2D{
        .generator = chronon3d::graphics::Field2DGenerator::Fractal,
        .seed = 73, .frequency = 4.f, .octaves = 5};
    fieldShape.field_ramp = chronon3d::graphics::GradientDefinition::linear(
        {0.f, 0.5f}, {1.f, 0.5f}, {{0.f, {0.1f, 0.05f, 0.2f, 0.2f}},
                                  {0.5f, {0.6f, 0.2f, 0.8f, 0.8f}},
                                  {1.f, {0.9f, 0.8f, 1.f, 1.f}}});
    fieldShape.field_render_scale = 2;
    fieldShape.field_drift = glm::vec2{0.02f, -0.01f};
    auto fieldPlan = chalkPathPlan;
    fieldPlan.layers[0].shape = fieldShape;
    const std::string fieldProblem = renderPlanEmissionProblem(fieldPlan);
    check(fieldProblem.empty(), fieldProblem.empty() ? "native Field2D fill is accepted" : fieldProblem.c_str());
    if (fieldProblem.empty()) {
        const auto json = nlohmann::json::parse(renderPlanToJson(fieldPlan));
        const auto& fill = json.at("layers").at(0).at("shape").at("fill");
        check(fill.at("type") == "field", "Field2D type stays inside the closed fill payload");
        check(fill.at("field").at("seed") == 73, "Field2D source seed stays inside the closed fill payload");
        check(fill.at("field_ramp").at("color_stops").size() == 3, "Field2D ramp stays inside the closed fill payload");
        check(fill.at("field_render_scale") == 2, "Field2D render scale stays inside the closed fill payload");
        const auto& drift = fill.at("field_drift");
        check(drift.is_array() && drift.size() == 2 &&
              std::abs(drift.at(0).get<float>() - 0.02f) < 1e-6f &&
              std::abs(drift.at(1).get<float>() + 0.01f) < 1e-6f,
              "Field2D drift stays inside the closed fill payload");
    }
    chronon3d::render_plan::ShapeContentPlan meshShape;
    meshShape.type = FieldType::Rect;
    meshShape.gradient_mesh = chronon3d::graphics::GradientMesh::four_corner(
        {0.1f, 0.1f, 0.2f, 1.f}, {0.8f, 0.1f, 0.3f, 1.f},
        {0.1f, 0.7f, 0.8f, 1.f}, {0.9f, 0.8f, 1.f, 1.f});
    auto meshFillPlan = chalkPathPlan;
    meshFillPlan.layers[0].shape = meshShape;
    const std::string meshProblem = renderPlanEmissionProblem(meshFillPlan);
    check(meshProblem.empty(), meshProblem.empty() ? "native GradientMesh fill is accepted" : meshProblem.c_str());
    if (meshProblem.empty()) {
        const auto json = nlohmann::json::parse(renderPlanToJson(meshFillPlan));
        check(json.at("layers").at(0).at("shape").at("fill").at("mesh").at("nodes").size() == 4,
              "GradientMesh nodes are preserved in the renderer's nested fill envelope");
    }

    auto chalkDustPlan = chalkPathPlan;
    chalkDustPlan.canvas.duration = chronon3d::Frame{13};
    chalkDustPlan.layers[0].duration_frames = chronon3d::Frame{13};
    auto dustLayer = slowedVideo.layers[0];
    dustLayer.id = "chalk-dust";
    dustLayer.type = chronon3d::render_plan::LayerType::Particles;
    dustLayer.source.clear();
    dustLayer.asset.clear();
    dustLayer.time_remap.reset();
    dustLayer.animation.reset();
    dustLayer.duration_frames = chronon3d::Frame{13};
    chronon3d::ParticleEmitterParams dust;
    dust.count = 48;
    dust.seed = 0xC4A1U;
    dust.emitter_size = {12.f, 4.f};
    dust.velocity_min = {-2.f, -15.f};
    dust.velocity_max = {2.f, -4.f};
    dust.lifetime_min = 0.2f;
    dust.lifetime_max = 0.8f;
    dust.size_min = 0.5f;
    dust.size_max = 2.5f;
    dust.gravity = 8.f;
    dust.turbulence = 0.15f;
    dust.shape = chronon3d::EmitterShape::Path;
    dust.path_id = "chalk-stroke";
    dust.path_points = {{0.f, 0.f}, {24.f, 6.f}, {48.f, 0.f}};
    dust.sprite_asset = "assets/chalk_dust.png";
    dust.sprite_scale_min = 0.8f;
    dust.sprite_scale_max = 1.2f;
    dust.rotation_min = -0.5f;
    dust.rotation_max = 0.5f;
    dust.angular_velocity_min = -1.f;
    dust.angular_velocity_max = 1.f;
    dust.blend_mode = chronon3d::BlendMode::Add;
    dust.color_ramp = chronon3d::graphics::GradientDefinition::linear(
        {0.f, 0.f}, {1.f, 0.f}, {{0.f, {1.f, 0.95f, 0.75f, 0.85f}},
                                {1.f, {0.8f, 0.75f, 0.6f, 0.f}}});
    dustLayer.particle_emitter = dust;
    chalkDustPlan.layers.push_back(dustLayer);
    check(renderPlanEmissionProblem(chalkDustPlan).empty(),
          "the deterministic chalk-particle payload is accepted by RenderPlan v3");
    const std::string dustJson = renderPlanToJson(chalkDustPlan);
    if (const char* canaryPath = std::getenv("CHRONONMOTION_RENDER_PLAN_CANARY")) {
        auto canaryPlan = chalkDustPlan;
        canaryPlan.layers[1].particle_emitter->sprite_asset.clear();
        std::ofstream canaryFile(canaryPath, std::ios::binary);
        if (!canaryFile || !(canaryFile << renderPlanToJson(canaryPlan)))
            throw std::runtime_error("cannot write requested RenderPlan consumer canary");
    }
    const auto emittedDust = nlohmann::json::parse(dustJson).at("layers").at(1).at("particle_emitter");
    check(emittedDust.at("count") == 48 && emittedDust.at("seed") == 0xC4A1U &&
          emittedDust.at("shape") == "path" && emittedDust.at("path_id") == "chalk-stroke" &&
          emittedDust.at("path_points").size() == 3,
          "particle count, seed, path and resolved points reach the renderer payload");
    check(emittedDust.at("color_ramp").size() == 2 &&
          emittedDust.at("sprite_asset") == "assets/chalk_dust.png" &&
          emittedDust.at("angular_velocity") == nlohmann::json{-1.f, 1.f},
          "particle color, sprite and rotation ranges survive emission");
    check(renderPlanToJson(chalkDustPlan) == dustJson,
          "seeded particle emission is byte deterministic");
    auto legacyDustPlan = slowedVideo;
    legacyDustPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    legacyDustPlan.layers[0].type = chronon3d::render_plan::LayerType::Particles;
    legacyDustPlan.layers[0].source.clear();
    legacyDustPlan.layers[0].time_remap.reset();
    legacyDustPlan.layers[0].animation.reset();
    legacyDustPlan.layers[0].particle_emitter = dust;
    check(!renderPlanEmissionProblem(legacyDustPlan).empty(),
          "V3-only particle path and sprite fields are rejected under the V2 schema");

    auto lightPlan = slowedVideo;
    lightPlan.layers[0].id = "chalk-light";
    lightPlan.layers[0].type = chronon3d::render_plan::LayerType::Light;
    lightPlan.layers[0].source.clear();
    lightPlan.layers[0].time_remap.reset();
    lightPlan.layers[0].animation.reset();
    lightPlan.layers[0].light = chronon3d::render_plan::LightLayerPlan{};
    check(renderPlanEmissionProblem(lightPlan).empty(),
          "a typed scene light is accepted by the common RenderPlan emitter");
    const auto emittedLight = nlohmann::json::parse(renderPlanToJson(lightPlan))
                                  .at("layers").at(0).at("light");
    check(emittedLight.at("radius") == 256.f && emittedLight.at("intensity") == 1.f &&
          emittedLight.at("color") == "#FFFFFF",
          "light radius, intensity and RGB color are preserved in the consumer's hex contract");
    auto invalidLightPlan = lightPlan;
    invalidLightPlan.layers[0].light->color[0] = 0.123f;
    check(!renderPlanEmissionProblem(invalidLightPlan).empty(),
          "light colors that the hexadecimal consumer contract would quantize are refused");

    auto safeAreaPlan = slowedVideo;
    safeAreaPlan.layers[0].safe_insets = std::array<float, 4>{80.f, 60.f, 220.f, 60.f};
    check(renderPlanEmissionProblem(safeAreaPlan).empty(),
          "V3 safe-area insets are a supported typed adapter field");
    const auto safeAreaJson = nlohmann::json::parse(renderPlanToJson(safeAreaPlan));
    check(safeAreaJson.at("layers").at(0).at("safe_insets") ==
              nlohmann::json{{"top", 80.f}, {"right", 60.f}, {"bottom", 220.f}, {"left", 60.f}},
          "V3 emitter preserves all four safe-area insets by name");
    auto invalidSafeAreaPlan = safeAreaPlan;
    invalidSafeAreaPlan.layers[0].safe_insets->at(0) = -1.f;
    check(!renderPlanEmissionProblem(invalidSafeAreaPlan).empty(),
          "negative safe-area insets fail closed at the emission boundary");
    auto legacySafeAreaPlan = safeAreaPlan;
    legacySafeAreaPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(legacySafeAreaPlan).empty(),
          "safe-area insets fail closed when attached to the V2 schema");

    chronon3d::render_plan::LayerPlan semanticText;
    semanticText.id = "semantic";
    semanticText.type = chronon3d::render_plan::LayerType::Text;
    semanticText.text = "Hi future";
    semanticText.style = chronon3d::render_plan::LayerStylePlan{};
    semanticText.style->font = "fonts/roboto"; // content identity only; no extension to load
    semanticText.style->font_size = 32.f;
    semanticText.style->fill = "#FFFFFF";
    chronon3d::render_plan::TextSelectorPlan selector;
    selector.unit = "word";
    selector.shape = "square";
    selector.amount.keyframes = {{chronon3d::Frame{0}, {0.f}},
                                 {chronon3d::Frame{8}, {100.f}}};
    chronon3d::render_plan::TextAnimatorPlan animator;
    animator.id = "word-highlight";
    animator.selectors.push_back(selector);
    chronon3d::render_plan::AnimationTrackPlan textOpacity;
    textOpacity.property = "opacity";
    textOpacity.keyframes = {{chronon3d::Frame{0}, {0.f}},
                             {chronon3d::Frame{8}, {1.f}}};
    chronon3d::render_plan::AnimationTrackPlan textColor;
    textColor.property = "fill_color";
    textColor.keyframes = {{chronon3d::Frame{0}, {1.f, .7f, 0.f, 1.f}}};
    animator.properties = {textOpacity, textColor};
    semanticText.text_animators.push_back(animator);
    auto semanticPlan = slowedVideo;
    semanticPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    semanticPlan.layers = {semanticText};
    check(renderPlanEmissionProblem(semanticPlan).empty(),
          "V3 semantic text selectors and property tracks are emit-able");
    const auto semanticJson = nlohmann::json::parse(renderPlanToJson(semanticPlan));
    check(semanticJson.at("layers").at(0).at("text_animators").at(0)
              .at("selectors").at(0).at("unit") == "word" &&
          semanticJson.at("layers").at(0).at("text_animators").at(0)
              .at("properties").at(1).at("property") == "fill_color",
          "V3 emitter preserves text unit selection and authored property order");
    auto v2TextAnimatorPlan = semanticPlan;
    v2TextAnimatorPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(v2TextAnimatorPlan).empty(),
          "text animators fail closed under the frozen V2 contract");
    check(renderPlanToJson(slowedVideo) == slowedJson,
          "time-remap JSON emission is byte-for-byte deterministic");

    auto bridgePlan = slowedVideo;
    auto& bridgeLayer = bridgePlan.layers.front();
    bridgeLayer.time_remap.reset();
    bridgeLayer.effects.clear();
    bridgeLayer.effects.push_back(LayerPlan::EffectPlan{});
    bridgeLayer.effects.back().kind = LayerPlan::EffectKindPlan::Catalog;
    bridgeLayer.effects.back().canonical_effect_id = "color.saturation";
    bridgeLayer.effects.back().canonical_params.push_back({"value", 1.f});
    auto exposureEffect = LayerPlan::EffectPlan{};
    exposureEffect.kind = LayerPlan::EffectKindPlan::Catalog;
    exposureEffect.canonical_effect_id = "color.contrast";
    exposureEffect.canonical_params.push_back({"value", 1.f});
    bridgeLayer.effects.push_back(exposureEffect);
    LayerPlan::PathMaskPlan mask;
    mask.type = "rounded_rect";
    mask.mode = "intersect";
    mask.position = {12.f, 18.f};
    mask.size = {320.f, 180.f};
    mask.radius = 14.f;
    mask.feather = 2.f;
    mask.opacity_track = chronon3d::render_plan::AnimationTrackPlan{};
    mask.opacity_track->property = "mask_opacity";
    mask.opacity_track->easing = "linear";
    mask.opacity_track->keyframes = {{chronon3d::Frame{0}, {0.f}},
                                     {chronon3d::Frame{8}, {1.f}}};
    bridgeLayer.masks.push_back(mask);
    bridgeLayer.track_matte = TrackMattePlan{"matte-layer", TrackMatteTypePlan::AlphaInverted};
    LayerPlan::EffectParamTrackPlan saturationTrack;
    saturationTrack.effect_id = "color.saturation";
    saturationTrack.param = "value";
    saturationTrack.track.property = "opacity";
    saturationTrack.track.easing = "linear";
    saturationTrack.track.keyframes = {{chronon3d::Frame{0}, {0.7f}},
                                        {chronon3d::Frame{8}, {1.2f}}};
    bridgeLayer.effect_param_tracks.push_back(saturationTrack);
    LayerPlan::EffectParamTrackPlan exposureTrack;
    exposureTrack.effect_id = "color.contrast";
    exposureTrack.param = "value";
    exposureTrack.track.property = "opacity";
    exposureTrack.track.easing = "linear";
    exposureTrack.track.keyframes = {{chronon3d::Frame{0}, {0.5f}},
                                     {chronon3d::Frame{8}, {2.f}}};
    bridgeLayer.effect_param_tracks.push_back(exposureTrack);
    check(renderPlanEmissionProblem(bridgePlan).empty(),
          "V3 bridge accepts canonical animated mask, track matte, and effect parameter data");
    const auto bridgeJsonText = renderPlanToJson(bridgePlan);
    const auto bridgeJson = nlohmann::json::parse(bridgeJsonText);
    const auto& bridgeJsonLayer = bridgeJson.at("layers").at(0);
    check(bridgeJsonLayer.at("masks").at(0).at("type") == "rounded_rect",
          "V3 JSON preserves mask geometry");
    check(bridgeJsonLayer.at("masks").at(0).at("opacity_track").at("keyframes").size() == 2,
          "V3 JSON preserves animated mask parameters");
    check(bridgeJsonLayer.at("track_matte").at("type") == "alpha_inverted",
          "V3 JSON preserves track matte type");
    check(bridgeJsonLayer.at("effect_param_tracks").at(0).at("effect_id") == "color.saturation" &&
          std::abs(bridgeJsonLayer.at("effect_param_tracks").at(0).at("keyframes").at(1).at("value").get<float>() - 1.2f) < 1e-6f,
          "V3 JSON preserves effect parameter tracks");
    check(renderPlanToJson(bridgePlan) == bridgeJsonText &&
          bridgeJsonText.find("\"effect_param_tracks\"") ==
              bridgeJsonText.rfind("\"effect_param_tracks\""),
          "mask, matte and effect-track emission is byte deterministic with one effect-track field");
    auto invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[0].param = "not_animatable";
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "non-animatable effect parameter tracks fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].masks[0].size[0] = -1.f;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "malformed mask geometry fails closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].track_matte->source.clear();
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "track mattes without a source layer id fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[0].effect_id = "color.brightness";
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "effect parameter tracks without a matching layer effect fail closed");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks.push_back(
        invalidBridgePlan.layers[0].effect_param_tracks.front());
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "duplicate effect parameter tracks fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].masks[0].opacity_track->keyframes[1].value[0] = 1.5f;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "out-of-range animated mask opacity fails closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].masks[0].type = "unknown_geometry";
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "unsupported V3 mask geometry fails closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[1].track.keyframes[1].value[0] = 25.0f;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "out-of-range animated effect parameters fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].effect_param_tracks[0].track.keyframes[1].frame =
        invalidBridgePlan.layers[0].effect_param_tracks[0].track.keyframes[0].frame;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "duplicate effect keyframe frames fail closed before JSON emission");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty(),
          "V3 mask payloads fail closed when V2 is selected");
    invalidBridgePlan = bridgePlan;
    invalidBridgePlan.layers[0].transition_in = chronon3d::render_plan::TransitionPlan{};
    check(!renderPlanEmissionProblem(invalidBridgePlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidBridgePlan); }),
          "transition fields stay fail-closed while the Chronon3D schema consumer is misaligned");
    check(renderPlanEmissionProblem(slowedVideo).empty(),
          "V3 adapter continues to accept canonical video content plans");

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

    auto cameraDofPlan = slowedVideo;
    cameraDofPlan.camera = chronon3d::render_plan::CameraPlan{};
    cameraDofPlan.camera->dof_enabled = true;
    cameraDofPlan.camera->dof_focus_distance = 425.f;
    cameraDofPlan.camera->dof_aperture = 0.035f;
    cameraDofPlan.camera->dof_max_blur = 18.f;
    check(renderPlanEmissionProblem(cameraDofPlan).empty(),
          "camera depth-of-field fields are part of the typed RenderPlan wire contract");
    const auto cameraDofJson = nlohmann::json::parse(renderPlanToJson(cameraDofPlan));
    check(cameraDofJson.at("camera").at("dof").at("enabled") == true,
          "V2/V3 camera emission preserves the DoF enabled flag");
    checkNear(cameraDofJson.at("camera").at("dof").at("focus_distance").get<float>(),
              425.f, 1e-6f, "V2/V3 camera emission preserves the DoF focus distance");
    checkNear(cameraDofJson.at("camera").at("dof").at("aperture").get<float>(),
              0.035f, 1e-6f, "V2/V3 camera emission preserves the DoF aperture");
    checkNear(cameraDofJson.at("camera").at("dof").at("max_blur").get<float>(),
              18.f, 1e-6f, "V2/V3 camera emission preserves the DoF maximum blur");
    const std::string cameraDofText = renderPlanToJson(cameraDofPlan);
    check(renderPlanToJson(cameraDofPlan) == cameraDofText,
          "camera depth-of-field JSON emission is byte-for-byte deterministic");
    auto cameraBlurPlan = slowedVideo;
    cameraBlurPlan.temporal_motion_blur = chronon3d::render_plan::MotionBlurPlan{180.f, 4};
    check(renderPlanEmissionProblem(cameraBlurPlan).empty(),
          "global temporal motion blur is supported by the V3 plan emitter");
    const auto cameraBlurJson = nlohmann::json::parse(renderPlanToJson(cameraBlurPlan));
    check(cameraBlurJson.at("temporal_motion_blur").at("shutter_angle") == 180.f &&
              cameraBlurJson.at("temporal_motion_blur").at("samples") == 4,
          "V3 emission preserves temporal shutter angle and sample count");
    check(renderPlanToJson(cameraBlurPlan) == renderPlanToJson(cameraBlurPlan),
          "temporal motion blur JSON emission is deterministic");
    auto legacyCameraBlurPlan = cameraBlurPlan;
    legacyCameraBlurPlan.schema = chronon3d::render_plan::kRenderPlanSchemaV2;
    check(!renderPlanEmissionProblem(legacyCameraBlurPlan).empty(),
          "global temporal motion blur is rejected for the V2 schema");
    auto invalidCameraDofPlan = cameraDofPlan;
    invalidCameraDofPlan.camera->dof_aperture = 2.8f;
    check(!renderPlanEmissionProblem(invalidCameraDofPlan).empty() &&
          throwsInvalid([&] { (void)renderPlanToJson(invalidCameraDofPlan); }),
          "camera depth-of-field values outside Chronon3D's contract fail closed");
    auto defaultCameraPlan = slowedVideo;
    defaultCameraPlan.camera = chronon3d::render_plan::CameraPlan{};
    check(!nlohmann::json::parse(renderPlanToJson(defaultCameraPlan)).at("camera").contains("dof"),
          "default camera optics remain omitted to preserve canonical compact JSON");

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
    cameraContent.transform.position.set(4.f, 5.f, -12.f);
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
    const auto findPositionZTrack = [](const auto& tracks) -> const chronon3d::render_plan::CameraTrackPlan* {
        const auto found = std::find_if(tracks.begin(), tracks.end(), [](const auto& track) {
            return track.property == chronon3d::render_plan::CameraPropertyPlan::PositionZ;
        });
        return found == tracks.end() ? nullptr : &*found;
    };
    const auto* cameraPositionZ = findPositionZTrack(*cameraPlan.camera_animation);
    const auto& loweredContent = cameraPlan.layers.front();
    const auto& loweredPositionTrack = loweredContent.animation->tracks.front();
    check(cameraPositionZ && cameraPlan.camera->position[2] == 8.f &&
          cameraPositionZ->keyframes.back().value.front() == 8.f,
          "camera position and its dense animation track mirror Motion depth into RenderPlan space");
    check(loweredContent.position_dimensions == 3 && loweredContent.position[2] == 12.f &&
          loweredPositionTrack.property == "position" &&
          loweredPositionTrack.keyframes.back().value[2] == 12.f,
          "3D layer anchor and its baked transform use the same mirrored RenderPlan depth");
    CameraRig dofRig(24, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    dofRig.setPosition({1.f, 2.f, -8.f}).setAperture(0.035f).setFocusDistance(425.f);
    auto dofCameraOptions = cameraOptions;
    dofCameraOptions.cameraRig = &dofRig;
    const auto loweredDofCamera = lowerToRenderPlan(cameraScene, dofCameraOptions);
    check(loweredDofCamera.camera && loweredDofCamera.camera->dof_enabled &&
          loweredDofCamera.camera->dof_aperture == 0.035f &&
          loweredDofCamera.camera->dof_focus_distance == 433.f,
          "CameraRig focus distance lowers to the RenderPlan world-Z focal plane");
    auto styledImagePlan = cameraPlan;
    auto& styledImage = styledImagePlan.layers.front();
    LayerStylePlan imageStyle;
    BackgroundStyle imageFrame;
    imageFrame.color = "#E8E2D5";
    imageFrame.radius = 18.f;
    imageFrame.padding = {6.f, 6.f};
    imageFrame.padding_dimensions = 2;
    imageStyle.background = imageFrame;
    styledImage.style = imageStyle;
    LayerPlan::EffectPlan saturation;
    saturation.kind = LayerPlan::EffectKindPlan::Catalog;
    saturation.canonical_effect_id = "color.saturation";
    saturation.canonical_params.push_back({"value", 0.78f});
    LayerPlan::EffectPlan contrast;
    contrast.kind = LayerPlan::EffectKindPlan::Catalog;
    contrast.canonical_effect_id = "color.contrast";
    contrast.canonical_params.push_back({"value", 1.08f});
    LayerPlan::EffectPlan grain;
    grain.kind = LayerPlan::EffectKindPlan::Noise;
    grain.amount = 0.02f;
    grain.seed = 1986;
    LayerPlan::EffectPlan vignette;
    vignette.kind = LayerPlan::EffectKindPlan::Vignette;
    vignette.amount = 0.12f;
    vignette.radius = 0.72f;
    vignette.softness = 0.55f;
    styledImage.effects = {saturation, contrast, grain, vignette};
    styledImage.crop = chronon3d::render_plan::ImageCropPlan{
        .enabled = true, .origin = {0.1f, 0.05f}, .size = {0.8f, 0.9f}, .flip_x = true};
    styledImagePlan.schema = chronon3d::render_plan::kRenderPlanSchemaV3;
    const auto styledImageJson = nlohmann::json::parse(renderPlanToJson(styledImagePlan));
    check(styledImageJson["layers"][0]["style"]["background"]["padding"] ==
              nlohmann::json::array({6.f, 6.f}) &&
          styledImageJson["layers"][0]["effects"].size() == 4 &&
          styledImageJson["layers"][0]["effects"][2]["seed"] == 1986 &&
          styledImageJson["layers"][0]["crop"]["flip_x"] == true &&
          static_cast<float>(styledImageJson["layers"][0]["crop"]["origin"][0].get<double>()) == 0.1f &&
          static_cast<float>(styledImageJson["layers"][0]["crop"]["origin"][1].get<double>()) == 0.05f,
          "RenderPlan emission preserves image frame, grade, crop mirror, seeded grain, and vignette");
    auto leakProfile = chronon3d::render_plan::effect_presets::light_leak::LightLeakProfile{};
    leakProfile.shape = chronon3d::render_plan::effect_presets::light_leak::LightLeakShape::Edge;
    leakProfile.edge = chronon3d::render_plan::effect_presets::light_leak::LightLeakEdge::Top;
    leakProfile.softness = 0.f;
    const auto leakLayer = chronon3d::render_plan::effect_presets::light_leak::LightLeakResolver::resolve(
        leakProfile, {1920.f, 1080.f}, "motion-authoring-light-leak");
    auto leakPlan = styledImagePlan;
    leakPlan.layers = {leakLayer};
    leakPlan.canvas.duration = chronon3d::Frame{2};
    leakPlan.layers.front().duration_frames = chronon3d::Frame{2};
    leakPlan.layers.front().animation = chronon3d::render_plan::AnimationPlan{};
    chronon3d::render_plan::AnimationTrackPlan leakOpacity;
    leakOpacity.property = "opacity";
    leakOpacity.keyframes = {
        {chronon3d::Frame{0}, {0.f}}, {chronon3d::Frame{1}, {1.f}},
        {chronon3d::Frame{2}, {0.f}}};
    leakPlan.layers.front().animation->tracks.push_back(leakOpacity);
    const auto leakJson = nlohmann::json::parse(renderPlanToJson(leakPlan));
    check(leakJson["layers"][0]["shape"]["fill"]["type"] == "radial" &&
          leakJson["layers"][0]["shape"]["fill"]["color_stops"].size() == 2 &&
          leakJson["layers"][0]["animation"]["tracks"][0]["keyframes"].size() == 3,
          "RenderPlan emission preserves native gradient fills and animated light-leak opacity");
    auto unsupportedEffectPlan = styledImagePlan;
    unsupportedEffectPlan.layers.front().effects.back().kind = LayerPlan::EffectKindPlan::FractalNoise;
    check(!renderPlanEmissionProblem(unsupportedEffectPlan).empty(),
          "RenderPlan emission rejects effect types outside its explicit native subset");
    styledImagePlan.camera_animation.reset();
    CameraPlan documentaryCamera;
    documentaryCamera.position = {0.f, 0.f, 850.f};
    documentaryCamera.rotation_deg = {0.f, 0.f, 0.f};
    documentaryCamera.fov_deg = 50.f;
    documentaryCamera.near = 0.1f;
    documentaryCamera.far = 2000.f;
    styledImagePlan.camera = documentaryCamera;
    styledImagePlan.canvas.duration = chronon3d::Frame{1};
    styledImagePlan.layers.front().asset = "assets/images/card_620x720_left.png";
    styledImagePlan.layers.front().position = {0.f, 0.f, 0.f};
    styledImagePlan.layers.front().position_dimensions = 3;
    styledImagePlan.layers.front().size = {400.f, 560.f};
    styledImagePlan.layers.front().size_dimensions = 2;
    styledImagePlan.layers.front().animation.reset();
    styledImagePlan.layers.front().duration_frames = chronon3d::Frame{1};
    styledImagePlan.output.path = "/tmp/chronon_documentary_snapshot_adapter_smoke.png";
    std::ofstream documentaryAdapterPlan("/tmp/chronon_documentary_snapshot_adapter_smoke.plan.json",
                                         std::ios::binary | std::ios::trunc);
    check(documentaryAdapterPlan && (documentaryAdapterPlan << renderPlanToJson(styledImagePlan)),
          "documentary image look plan can be written for Chronon3D's native decoder");
    CameraRig invalidDofRig(25, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    invalidDofRig.setAperture(2.8f).setFocusDistance(425.f);
    auto invalidDofCameraOptions = cameraOptions;
    invalidDofCameraOptions.cameraRig = &invalidDofRig;
    check(throwsInvalid([&] { (void)lowerToRenderPlan(cameraScene, invalidDofCameraOptions); }),
          "CameraRig camera lowering rejects aperture values outside Chronon3D RenderPlan bounds");
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

    chronon3d::render_plan::LayerPlan riggedMesh;
    riggedMesh.id = "rigged-triangle";
    riggedMesh.type = chronon3d::render_plan::LayerType::Mesh;
    riggedMesh.inline_mesh = triangle;
    RigFrameGeometry rigFrame{1, {{-1.f, -1.f, 0.f}, {1.f, -1.f, 0.f}, {0.f, 1.f, 1.f}}};
    applyRigGeometryFrame(riggedMesh, rigFrame, std::array<std::uint32_t, 3>{0, 1, 2});
    check(riggedMesh.inline_mesh->vertices[2].position == std::array<float, 3>{0.f, 1.f, 1.f},
          "rig geometry bake replaces the target frame's mesh positions");
    check(riggedMesh.inline_mesh->vertices[2].uv == std::array<float, 2>{0.5f, 1.f} &&
          riggedMesh.inline_mesh->material->base_color == triangle.material->base_color,
          "rig geometry bake retains authored texture coordinates and material");
    check(riggedMesh.inline_mesh->vertices[0].normal[2] > 0.f,
          "rig geometry bake regenerates normals from deformed triangles");
    auto riggedFramePlan = inlineMeshPlan;
    riggedFramePlan.layers[0] = riggedMesh;
    check(renderPlanEmissionProblem(riggedFramePlan).empty(),
          "one deformed rig frame is eligible for canonical V3 RenderPlan emission");
    const auto riggedFrameJson = renderPlanToJson(riggedFramePlan);
    check(renderPlanToJson(riggedFramePlan) == riggedFrameJson,
          "deformed rig mesh frame serialization is deterministic");
    auto secondRigFrame = rigFrame;
    secondRigFrame.positions[2].z = 2.f;
    applyRigGeometryFrame(riggedMesh, secondRigFrame, std::array<std::uint32_t, 3>{0, 1, 2});
    auto secondRigFramePlan = inlineMeshPlan;
    secondRigFramePlan.layers[0] = riggedMesh;
    check(renderPlanToJson(secondRigFramePlan) != riggedFrameJson,
          "different baked rig geometry produces a distinct frame payload");
    BakedRigProgram geometryBake;
    geometryBake.frames = {
        RigProgramFrame{{}, {rigFrame}},
        RigProgramFrame{{}, {secondRigFrame}}
    };
    auto animatedRigPlan = inlineMeshPlan;
    animatedRigPlan.layers[0] = riggedMesh;
    applyRigGeometryBake(animatedRigPlan.layers[0], geometryBake, 1,
                         std::array<std::uint32_t, 3>{0, 1, 2}, 4);
    check(animatedRigPlan.layers[0].inline_mesh->animation.size() == 2 &&
          animatedRigPlan.layers[0].inline_mesh->animation[1].frame == 5 &&
          animatedRigPlan.layers[0].inline_mesh->animation[1].positions[2][2] == 2.f,
          "RigProgram bake lowers all frame geometry to a V3 baked vertex sample sequence");
    check(animatedRigPlan.layers[0].inline_mesh->vertices[2].uv == triangle.vertices[2].uv &&
          animatedRigPlan.layers[0].inline_mesh->material->base_color == triangle.material->base_color,
          "animated RigProgram lowering keeps authored UV and alpha-bearing material unchanged");
    animatedRigPlan.canvas.duration = chronon3d::Frame{6};
    check(renderPlanEmissionProblem(animatedRigPlan).empty(),
          "animated rig geometry passes producer validation for V3 emission");
    const auto animatedRigJson = nlohmann::json::parse(renderPlanToJson(animatedRigPlan));
    check(animatedRigJson.at("layers").at(0).at("inline_mesh").at("animation").size() == 2 &&
          animatedRigJson.at("layers").at(0).at("inline_mesh").at("animation")[1].at("frame") == 5,
          "V3 emission writes baked rig sample times and deformed positions");
    auto rejectedBake = geometryBake;
    rejectedBake.frames[1].geometry.clear();
    const auto previousSamples = animatedRigPlan.layers[0].inline_mesh->animation;
    check(throwsInvalid([&] { applyRigGeometryBake(animatedRigPlan.layers[0], rejectedBake, 1,
              std::array<std::uint32_t, 3>{0, 1, 2}, 4); }) &&
          animatedRigPlan.layers[0].inline_mesh->animation.size() == previousSamples.size(),
          "failed RigProgram lowering leaves prior RenderPlan samples unchanged");
    applyRigGeometryFrame(riggedMesh, rigFrame, std::array<std::uint32_t, 3>{0, 1, 2});
    auto badRigFrame = rigFrame;
    badRigFrame.positions.pop_back();
    check(throwsInvalid([&] { applyRigGeometryFrame(riggedMesh, badRigFrame,
              std::array<std::uint32_t, 3>{0, 1, 2}); }),
          "rig geometry lowering rejects mismatched baked topology");
    badRigFrame = rigFrame;
    badRigFrame.positions[0].x = std::numeric_limits<float>::infinity();
    check(throwsInvalid([&] { applyRigGeometryFrame(riggedMesh, badRigFrame,
              std::array<std::uint32_t, 3>{0, 1, 2}); }),
          "rig geometry lowering rejects non-finite baked vertices");
    auto animatedRiggedMesh = riggedMesh;
    animatedRiggedMesh.animation = chronon3d::render_plan::AnimationPlan{};
    chronon3d::render_plan::AnimationTrackPlan illegalMeshTrack;
    illegalMeshTrack.property = "opacity";
    illegalMeshTrack.keyframes.push_back({chronon3d::Frame{0}, {1.f}});
    animatedRiggedMesh.animation->tracks.push_back(illegalMeshTrack);
    auto animatedRiggedFramePlan = riggedFramePlan;
    animatedRiggedFramePlan.layers[0] = animatedRiggedMesh;
    check(!renderPlanEmissionProblem(animatedRiggedFramePlan).empty(),
          "mesh transform animation remains rejected instead of being silently ignored");
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
    cameraPose.position = {1.f, 2.f, -3.f};
    const auto camera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    checkNear(camera.fov_deg, 50.f, 1e-6f, "camera pose lowering maps typed projection parameters");
    check(camera.position == std::array<float, 3>{1.f, 2.f, 3.f},
          "camera pose lowering maps Motion -Z depth to RenderPlan +Z-forward coordinates");
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 2.f, 1.f); }),
          "camera lowering rejects an invalid clipping interval");
    cameraPose.aperture = 0.04f;
    cameraPose.focusDistance = 480.f;
    const auto dofCamera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(dofCamera.dof_enabled && dofCamera.dof_aperture == 0.04f &&
          dofCamera.dof_focus_distance == 483.f,
          "camera pose lowering converts optical-axis distance to RenderPlan world-Z focus");
    cameraPose.position = {0.f, 0.f, 480.00006f};
    cameraPose.focusDistance = 480.f;
    const auto nearZeroFocusPlane = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(nearZeroFocusPlane.dof_focus_distance == 0.001f,
          "camera lowering clamps near-zero focus-plane roundoff to the schema epsilon");
    cameraPose.position.z = 481.f;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering still rejects materially negative RenderPlan focal planes");
    cameraPose.position = {1.f, 2.f, -3.f};
    cameraPose.focusDistance = 480.f;
    cameraPose.focalLength = 50.f;
    const auto focalLengthCamera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(focalLengthCamera.focal_length_mm == 50.f,
          "camera lowering preserves authored focal length in the RenderPlan lens contract");
    cameraPose.focalLength = 0.f;
    cameraPose.fStop = 2.8f;
    const auto physicalDofCamera = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    check(physicalDofCamera.f_stop == 2.8f && physicalDofCamera.dof_f_stop == 2.8f &&
          physicalDofCamera.dof_enabled,
          "camera lowering preserves physical f-stop and enables the RenderPlan physical DoF contract");
    cameraPose.fStop = 0.f;
    cameraPose.aperture = 2.8f;
    check(throwsInvalid([&] { (void)lowerCameraPose(cameraPose, 0.1f, 2000.f); }),
          "camera lowering rejects depth-of-field values outside the RenderPlan schema bounds");
    cameraPose.aperture = 0.04f;
    cameraPose.focusDistance = 480.f;
    cameraPose.focalLength = 0.f;
    cameraPose.fStop = 0.f;
    const auto loweredCameraPoseDof = lowerCameraPose(cameraPose, 0.1f, 2000.f);
    auto cameraOnlyScene = MotionScene("camera DoF parity", 30.f);
    cameraOnlyScene.evaluate(0);
    CameraRig animatedDofRig(26, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    animatedDofRig.setPosition({0.f, 0.f, -8.f}).setAperture(0.04f).setFocusDistance(480.f);
    animatedDofRig.apertureTrack().add(0.f, 0.04f).add(1.f, 0.06f);
    animatedDofRig.focusDistanceTrack().add(0.f, 480.f).add(1.f, 720.f);
    animatedDofRig.setMaxBlur(12.f).maxBlurTrack().add(0.f, 12.f).add(1.f, 20.f);
    RenderPlanLoweringOptions animatedDofOptions;
    animatedDofOptions.cameraRig = &animatedDofRig;
    const auto animatedDofPlan = lowerToRenderPlan(cameraOnlyScene, animatedDofOptions);
    const auto findCameraTrack = [&](chronon3d::render_plan::CameraPropertyPlan property)
        -> const chronon3d::render_plan::CameraTrackPlan* {
        if (!animatedDofPlan.camera_animation) return nullptr;
        const auto found = std::find_if(animatedDofPlan.camera_animation->begin(), animatedDofPlan.camera_animation->end(),
            [property](const auto& track) { return track.property == property; });
        return found == animatedDofPlan.camera_animation->end() ? nullptr : &*found;
    };
    const auto* focusTrack = findCameraTrack(chronon3d::render_plan::CameraPropertyPlan::FocusDistance);
    const auto* apertureTrack = findCameraTrack(chronon3d::render_plan::CameraPropertyPlan::Aperture);
    const auto* maxBlurTrack = findCameraTrack(chronon3d::render_plan::CameraPropertyPlan::MaxBlur);
    check(animatedDofPlan.camera && animatedDofPlan.camera->dof_enabled && focusTrack && apertureTrack && maxBlurTrack,
          "camera lowering enables and emits typed animated depth-of-field channels");
    check(focusTrack && focusTrack->keyframes.size() > 1 &&
          focusTrack->keyframes.front().value.front() == 488.f &&
          focusTrack->keyframes.back().value.front() == 728.f &&
          apertureTrack && apertureTrack->keyframes.front().value.front() == 0.04f &&
          apertureTrack->keyframes.back().value.front() == 0.06f &&
          maxBlurTrack && maxBlurTrack->keyframes.front().value.front() == 12.f &&
          maxBlurTrack->keyframes.back().value.front() == 20.f,
          "camera lowering samples focus, aperture, and maximum blur through their final authored values");
    const auto animatedDofJson = nlohmann::json::parse(renderPlanToJson(animatedDofPlan));
    bool emitsFocus = false, emitsAperture = false, emitsMaxBlur = false;
    for (const auto& track : animatedDofJson["camera_animation"]["tracks"]) {
        const auto property = track.at("property").get<std::string>();
        emitsFocus = emitsFocus || property == "camera_focus_distance";
        emitsAperture = emitsAperture || property == "camera_aperture";
        emitsMaxBlur = emitsMaxBlur || property == "camera_max_blur";
    }
    check(emitsFocus && emitsAperture && emitsMaxBlur,
          "RenderPlan JSON preserves focus, aperture, and maximum-blur camera track names");
    CameraRig dofHandoffRig(27, 50.f, 16.f / 9.f, 0.1f, 2000.f);
    dofHandoffRig.setPosition({0.f, 0.f, -8.f}).setFocusDistance(480.f);
    dofHandoffRig.apertureTrack().add(0.f, 0.f).add(1.f, 0.06f);
    auto dofHandoffOptions = animatedDofOptions;
    dofHandoffOptions.cameraRig = &dofHandoffRig;
    const auto dofHandoffPlan = lowerToRenderPlan(cameraOnlyScene, dofHandoffOptions);
    check(dofHandoffPlan.camera && dofHandoffPlan.camera->dof_enabled,
          "camera lowering enables DOF in the base plan when aperture becomes active after frame zero");
    (void)loweredCameraPoseDof;
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
    testThreeDimensionalIkChain();
    testPcmAudioAnalysis();
    testPresetTrackGolden();
    testDynamicFlowLayout();
    testVectorPathBaking();
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
    testTypedChronon3dLowering();
#endif
    return chrononmotion_test::report();
}
