#include "motion_check.hpp"
#include "chrononmotion/motion/Motion.hpp"

#include <stdexcept>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using namespace chrononmotion_test;

namespace {
    template<class F> void rejects(F&& action, const char* message) {
        bool rejected = false;
        try { action(); } catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, message);
    }

    RigProgram makeProgram() {
        RigProgram program;
        RigNode node;
        node.id = 1;
        node.parent = 0;
        node.rest.position = {0.f, 0.f, 0.f};
        node.bind = node.rest;
        program.graph.nodes.push_back(node);

        RigGeometryBinding geometry;
        geometry.id = 10;
        geometry.source = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
        geometry.indices = {0, 1, 2};
        geometry.morphs.resize(5);
        for (auto& morph : geometry.morphs) morph.positions = geometry.source;
        geometry.morphs[0].positions[1] = {2.f, 0.f, 0.f}; // AA
        geometry.morphs[1].positions[1] = {1.f, 2.f, 0.f}; // M
        geometry.morphs[2].positions[1] = {1.f, 0.f, 2.f}; // blink
        geometry.morphs[3].positions[0] = {-1.f, 0.f, 0.f}; // gaze left
        geometry.morphs[4].positions[2] = {0.f, -1.f, 0.f}; // gaze right
        program.geometry.push_back(geometry);
        return program;
    }

    FaceRigMap makeMap() {
        FaceRigMap map;
        map.geometryId = 10;
        map.visemes[static_cast<std::size_t>(Viseme::AA)] = {{0, 1.f}};
        map.visemes[static_cast<std::size_t>(Viseme::M)] = {{1, 1.f}};
        map.blinkMorph = 2;
        map.gazeLeftMorph = 3;
        map.gazeRightMorph = 4;
        return map;
    }

    FacePerformance makePerformance() {
        FacePerformance performance;
        performance.firstFrame = 0;
        performance.lastFrame = 20;
        performance.visemes = {{5, Viseme::AA, 1.f}, {15, Viseme::M, 1.f}};
        performance.blinks = {{5, 3, 1.f}};
        performance.gaze = {{10, 4, 1.f, 1.f}};
        return performance;
    }

    Vector3 position(const RigProgramEvaluator& evaluator, int frame,
                     std::size_t vertex) {
        return evaluator.evaluate(frame, 30.f).geometry.front().positions[vertex];
    }
}// namespace

int main() {
    section("TestFaceRigMorphOnlyVisemes / BlinkGazeAndDeterminism");
    auto program = makeProgram();
    const auto map = makeMap();
    const auto performance = makePerformance();
    FaceRigOptions options;
    options.coarticulationSeconds = 0.1f; // 3 frames at 30 fps
    const auto result = applyFacePerformance(program, map, performance, 30.f, options);
    check(result.morphTracks == 5, "all mapped morphs receive tracks");
    check(result.visemeCues == 2 && result.blinkCues == 1 && result.gazeCues == 1,
          "face result counts every cue kind");
    check(program.geometry.front().morphTracks.size() == 5,
          "morph tracks are sized to the mapped target count");

    const RigProgramEvaluator evaluator(program);
    checkNear(position(evaluator, 0, 1), {1.f, 0.f, 0.f}, 1e-5f,
              "frame zero stays neutral");
    checkNear(position(evaluator, 3, 1), {1.333333f, 0.f, 0.f}, 1e-5f,
              "first viseme is anticipated, not popped");
    checkNear(position(evaluator, 5, 1), {2.f, 0.f, 0.f}, 1e-5f,
              "AA cue reaches its morph");
    checkNear(position(evaluator, 6, 1), {2.f, 0.f, 2.f}, 1e-5f,
              "blink pulse composes with the viseme");
    checkNear(position(evaluator, 7, 1), {2.f, 0.f, 0.f}, 1e-5f,
              "blink closes and reopens without a residual");
    checkNear(position(evaluator, 11, 1), {2.f, 0.f, 0.f}, 1e-5f,
              "gaze does not move the mouth");
    checkNear(position(evaluator, 11, 0), {-0.866025f, 0.f, 0.f}, 1e-5f,
              "left gaze reaches the left-eye morph");
    checkNear(position(evaluator, 13, 1), {1.666667f, 0.666667f, 0.f}, 1e-5f,
              "co-articulation blends AA into M");
    checkNear(position(evaluator, 15, 1), {1.f, 2.f, 0.f}, 1e-5f,
              "M cue is exact on its frame");
    checkNear(position(evaluator, 20, 1), {1.f, 2.f, 0.f}, 1e-5f,
              "last viseme is held to the performance end");

    auto second = makeProgram();
    const auto secondResult = applyFacePerformance(second, map, performance, 30.f, options);
    check(result.signature == secondResult.signature, "face application signature is deterministic");
    const RigProgramEvaluator secondEvaluator(second);
    check(evaluator.bake(0, 20, 30.f).signature == secondEvaluator.bake(0, 20, 30.f).signature,
          "two identical face programs bake to the same geometry signature");

    section("TestFaceRigRejectsMalformedInputs");
    auto invalid = makeProgram();
    invalid.geometry.front().morphs.clear();
    rejects([&] { (void) applyFacePerformance(invalid, map, performance, 30.f); },
            "rig without morph targets rejected");

    auto badMap = makeMap();
    badMap.geometryId = 999;
    rejects([&] { (void) applyFacePerformance(program, badMap, performance, 30.f); },
            "unknown geometry rejected");

    badMap = makeMap();
    badMap.visemes[static_cast<std::size_t>(Viseme::AA)] = {{99, 1.f}};
    rejects([&] { (void) applyFacePerformance(program, badMap, performance, 30.f); },
            "out-of-range morph rejected");

    badMap = makeMap();
    badMap.visemes[static_cast<std::size_t>(Viseme::AA)] = {{0, 1.f}, {0, .5f}};
    rejects([&] { (void) applyFacePerformance(program, badMap, performance, 30.f); },
            "duplicate morph in one viseme rejected");

    badMap = makeMap();
    badMap.visemes[static_cast<std::size_t>(Viseme::Neutral)] = {{0, 1.f}};
    rejects([&] { (void) applyFacePerformance(program, badMap, performance, 30.f); },
            "explicit Neutral mapping rejected");

    auto badPerformance = makePerformance();
    badPerformance.visemes = {{15, Viseme::AA, 1.f}, {5, Viseme::M, 1.f}};
    rejects([&] { (void) applyFacePerformance(program, map, badPerformance, 30.f); },
            "non-monotonic viseme cues rejected");

    badPerformance = makePerformance();
    badPerformance.visemes[0].weight = 2.f;
    rejects([&] { (void) applyFacePerformance(program, map, badPerformance, 30.f); },
            "out-of-range viseme weight rejected");

    badPerformance = makePerformance();
    badPerformance.blinks[0].durationFrames = 0;
    rejects([&] { (void) applyFacePerformance(program, map, badPerformance, 30.f); },
            "zero-length blink rejected");

    badPerformance = makePerformance();
    badPerformance.gaze[0].horizontal = 2.f;
    rejects([&] { (void) applyFacePerformance(program, map, badPerformance, 30.f); },
            "out-of-range gaze rejected");

    badPerformance = makePerformance();
    badPerformance.visemes.clear();
    badPerformance.blinks.clear();
    badPerformance.gaze.clear();
    rejects([&] { (void) applyFacePerformance(program, map, badPerformance, 30.f); },
            "empty performance rejected");

    rejects([&] { (void) applyFacePerformance(program, map, performance, 0.f); },
            "non-positive fps rejected");

    FaceRigOptions badOptions;
    badOptions.coarticulationSeconds = -1.f;
    rejects([&] { (void) applyFacePerformance(program, map, performance, 30.f, badOptions); },
            "negative co-articulation rejected");

    section("TestFaceRigNoOpWithoutCues");
    auto noCues = makeProgram();
    auto noCueMap = makeMap();
    noCueMap.visemes[static_cast<std::size_t>(Viseme::AA)] = {{0, 1.f}};
    FacePerformance noPerformance;
    noPerformance.lastFrame = 20;
    rejects([&] { (void) applyFacePerformance(noCues, noCueMap, noPerformance, 30.f); },
            "no-cue performance rejected instead of silently muting");
    return report();
}
