#include "motion_offline_golden.hpp"

#include <array>
#include <cmath>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using namespace chrononmotion_test::offline_golden;

int main() {
    VerletChainSpec chain;
    chain.joints = {{0.f, 0.f}, {1.f, 0.f}, {2.f, 0.f}};
    chain.gravity = {0.f, -9.81f};
    chain.fps = 60.f;
    chain.damping = 0.02f;
    chain.iterations = 8;
    const auto verlet = bakeVerletChain(chain, 0, 3);
    const auto verletAgain = bakeVerletChain(chain, 0, 3);
    check(verlet.samples == verletAgain.samples, "Verlet frame array repeats bit-exactly");
    check(verlet.samples.size() == 4 && verlet.samples.back().size() == 3,
          "Verlet golden dimensions are stable");
    near(verlet.samples[1][1].x, 0.99999636f, 1e-7f, "Verlet frame 1 joint 1 x");
    near(verlet.samples[1][1].y, -0.002725f, 2e-5f, "Verlet frame 1 joint 1 y");
    near(verlet.samples[3][2].x, 2.f, 2e-3f, "Verlet frame 3 tip x");
    near(verlet.samples[3][2].y, -0.01635f, 3e-3f, "Verlet frame 3 tip y");
    near(verlet.jointPositions[2].sample(3.f / 60.f).x, verlet.samples[3][2].x, 1e-6f,
         "Verlet emitted tip track equals sampled golden pose");

    RigidBodyWorld2D world;
    world.gravity = {0.f, -10.f};
    world.floorY = 0.f;
    world.left = -5.f;
    world.right = 5.f;
    world.fps = 30.f;
    world.solverIterations = 4;
    const std::vector<RigidBody2D> bodies{{7, {0.f, 2.f}, {1.f, 0.f}, .5f, 1.f, .8f, .1f}};
    const auto rigid = bakeRigidBodies2D(bodies, world, 0, 3);
    const auto rigidAgain = bakeRigidBodies2D(bodies, world, 0, 3);
    check(rigid.samples == rigidAgain.samples, "rigid-body frame array repeats bit-exactly");
    check(rigid.ids == std::vector<std::uint32_t>{7}, "rigid-body identity ordering golden");
    near(rigid.samples[1][0].x, 1.f / 30.f, 1e-6f, "rigid body frame 1 x");
    near(rigid.samples[1][0].y, 2.f - 10.f / 900.f, 1e-6f, "rigid body frame 1 y");
    near(rigid.samples[3][0].x, .1f, 1e-6f, "rigid body frame 3 x");
    near(rigid.samples[3][0].y, 2.f - 60.f / 900.f, 1e-7f, "rigid body frame 3 y");

    const auto ik = solveTwoSegmentIK2D({0.f, 0.f}, {1.f, 1.f}, 1.f, 1.f);
    near(ik.elbow.x, 0.f, 1e-6f, "IK elbow golden x");
    near(ik.elbow.y, 1.f, 1e-6f, "IK elbow golden y");
    near(ik.end.x, 1.f, 1e-6f, "IK endpoint golden x");
    near(ik.end.y, 1.f, 1e-6f, "IK endpoint golden y");
    check(ik.targetReachable, "IK reachable status golden");

    constexpr std::uint32_t sampleRate = 8000;
    std::vector<float> pcm(512);
    constexpr float pi = 3.14159265358979323846f;
    for (std::size_t index = 0; index < pcm.size(); ++index)
        pcm[index] = .5f * std::sin(2.f * pi * 125.f * static_cast<float>(index) / sampleRate);
    AudioAnalysisSpec audio;
    audio.sampleRate = sampleRate;
    audio.windowSize = 128;
    audio.hopSize = 64;
    audio.bassBelowHz = 300.f;
    audio.midBelowHz = 1800.f;
    const auto features = analyzePcmAudio(pcm, audio);
    const auto featuresAgain = analyzePcmAudio(pcm, audio);
    check(features.size() == 8 && features.size() == featuresAgain.size(), "PCM window count golden");
    for (std::size_t index = 0; index < features.size(); ++index) {
        check(features[index].rms == featuresAgain[index].rms &&
              features[index].bass == featuresAgain[index].bass &&
              features[index].mid == featuresAgain[index].mid &&
              features[index].treble == featuresAgain[index].treble &&
              features[index].onset == featuresAgain[index].onset,
              "PCM feature sample repeats bit-exactly");
    }
    near(features[1].rms, .35355339f, .015f, "PCM RMS golden");
    near(features[1].bass, .21f, .08f, "PCM bass golden");
    check(features[1].bass > features[1].treble, "PCM spectral band ordering golden");
    const auto audioTrack = bakeAudioDriverTrack(features,
        AudioDriverSpec{AudioFeature::Bass, 1.5f, .1f, 0.f}, 30.f, 0, 2);
    const auto audioTrackAgain = bakeAudioDriverTrack(features,
        AudioDriverSpec{AudioFeature::Bass, 1.5f, .1f, 0.f}, 30.f, 0, 2);
    for (int frame = 0; frame <= 2; ++frame)
        check(audioTrack.sample(static_cast<float>(frame) / 30.f) ==
              audioTrackAgain.sample(static_cast<float>(frame) / 30.f),
              "audio driver key repeats bit-exactly");
    near(audioTrack.sample(0.f), .32876146f, .005f, "audio driver first key golden");

    const std::array<FlowItem, 2> items{{{1, {20.f, 10.f}, {.5f, .5f}},
                                         {2, {30.f, 20.f}, {0.f, 1.f}}}};
    FlowSpec flow;
    flow.axis = FlowAxis::Row;
    flow.gap = 5.f;
    flow.crossAlignment = FlowCrossAlignment::Center;
    const auto flowResult = solveFlowLayout(flow, items, {10.f, 20.f});
    const auto flowAgain = solveFlowLayout(flow, items, {10.f, 20.f});
    check(flowResult.items.size() == 2 && flowResult.items.size() == flowAgain.items.size(),
          "flow item count golden");
    near(flowResult.items[0].position.x, 20.f, 1e-6f, "flow first anchor x");
    near(flowResult.items[0].position.y, 30.f, 1e-6f, "flow first anchor y");
    near(flowResult.items[1].position.x, 35.f, 1e-6f, "flow second anchor x");
    near(flowResult.items[1].position.y, 40.f, 1e-6f, "flow second anchor y");
    check(flowResult.items[0].position == flowAgain.items[0].position &&
          flowResult.items[1].bounds.getMin() == flowAgain.items[1].bounds.getMin(),
          "flow geometry repeats exactly");

    const auto pathFrames = bakeSvgPathMorph("M0 0 L10 0 Z", "M0 0 L20 0 Z", 0, 2);
    const auto pathFramesAgain = bakeSvgPathMorph("M0 0 L10 0 Z", "M0 0 L20 0 Z", 0, 2);
    const auto samePath = [](const BakedPath& left, const BakedPath& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            const auto& a = left[index];
            const auto& b = right[index];
            if (a.type != b.type || a.point != b.point ||
                a.control1 != b.control1 || a.control2 != b.control2) return false;
        }
        return true;
    };
    bool morphRepeatable = pathFrames.size() == pathFramesAgain.size();
    for (std::size_t index = 0; morphRepeatable && index < pathFrames.size(); ++index)
        morphRepeatable = samePath(pathFrames[index], pathFramesAgain[index]);
    check(morphRepeatable, "SVG morph command frames repeat bit-exactly");
    check(pathFrames.size() == 3 && pathFrames.front()[1].point.x == 10.f &&
          pathFrames[1][1].point.x == 15.f && pathFrames.back()[1].point.x == 20.f,
          "SVG morph endpoint and midpoint command goldens");
    const auto copies = expandPathRepeater(parseSvgPath("M0 0 L5 0"), RepeaterSpec{3, {8.f, 0.f}});
    const auto copiesAgain = expandPathRepeater(parseSvgPath("M0 0 L5 0"), RepeaterSpec{3, {8.f, 0.f}});
    bool repeaterRepeatable = copies.size() == copiesAgain.size();
    for (std::size_t index = 0; repeaterRepeatable && index < copies.size(); ++index)
        repeaterRepeatable = samePath(copies[index], copiesAgain[index]);
    check(repeaterRepeatable, "SVG repeater copies repeat bit-exactly");
    check(copies.size() == 3 && copies[0][1].point.x == 5.f &&
          copies[1][1].point.x == 13.f && copies[2][1].point.x == 21.f,
          "SVG repeater copy-coordinate goldens");

    return report();
}
