// ChrononMotion — the resolved frame plan contract.
//
// What this test is for: the plan is the *only* thing a backend is allowed to
// see, so it has to be (a) what the scene actually resolved, (b) reproducible
// byte for byte, and (c) refused — not repaired — when any rule breaks. Each of
// those is checked here, including one negative case per declared rule.
//
// The frame-grid check is the load-bearing one: every layer sample is compared
// against an independent `scene.evaluate(frame)` of the same frame, bit for bit.
// A plan that is half a frame off passes every self-consistency check and fails
// this one.

#include "chrononmotion/plan/ResolvedFramePlan.hpp"

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef CHRONONMOTION_FRAME_PLAN_GOLDEN
#define CHRONONMOTION_FRAME_PLAN_GOLDEN ""
#endif

using namespace chrononmotion;
using namespace chrononmotion::plan;

namespace {

    int checks = 0;

    void check(bool condition, const char* message) {

        ++checks;
        if (!condition) throw std::runtime_error(message);
    }

    void checkNear(float actual, float expected, float tolerance, const char* message) {

        ++checks;
        if (!(std::abs(actual - expected) <= tolerance))
            throw std::runtime_error(std::string(message) + ": got " + std::to_string(actual) +
                                     ", expected " + std::to_string(expected));
    }

    /// The contract's matrix layout, written out independently of the producer:
    /// element (row, column) is at index row*4 + column. If the producer emitted
    /// Motion's column-major storage unchanged, the translation checks below
    /// would not find the position.
    std::array<float, 16> transposeToRowMajor(const Matrix4& matrix) {

        std::array<float, 16> out{};
        for (std::size_t column = 0; column < 4; ++column) {
            for (std::size_t row = 0; row < 4; ++row) out[row * 4 + column] = matrix.elements[column * 4 + row];
        }
        return out;
    }

    struct Fixture {
        motion::MotionScene scene{"frame-plan-fixture", 24.f};
        motion::CameraRig rig{100, 50.f};

        Fixture() {
            // 10 rig (null) -> 1 plate (image, -Z 8 units) -> 2 title (text, keyed) -> 3 box (mesh)
            motion::Layer& rigNode = scene.addNull(10, "rig");
            rigNode.transform.position = Vector3(0.f, 0.f, 0.f);

            motion::Layer& plate = scene.addContent(
                1, "plate", motion::ContentRef::image("assets/plate.png", {1920.f, 1080.f}), 10);
            plate.transform.position = Vector3(0.f, 0.f, -8.f);
            plate.tracks.opacity.add(0.f, 0.5f);

            motion::Layer& title = scene.addContent(
                2, "title", motion::ContentRef::text("assets/title", {800.f, 200.f}), 1);
            title.transform.position = Vector3(0.f, 300.f, 0.f);
            title.tracks.position.add(0.f, Vector3(0.f, 300.f, 0.f)).add(1.f, Vector3(100.f, 300.f, 0.f));
            title.tracks.opacity.add(0.f, 0.5f);

            motion::MeshAttachment attachment;
            attachment.assetId = "models/box.glb";
            scene.addMesh(3, "box", attachment, 2);

            scene.setShutter(motion::ShutterSettings{180.f, 12});

            // The camera hangs off the rig node, so the plan has to compose its
            // parent's world matrix rather than trusting the local pose.
            rig.setParent(10);
            rig.setPosition(Vector3(1.5f, 2.5f, 9.f));
            // No focal length authored: the fov is the cone the plan carries.
            rig.setSensorWidth(36.f);
            rig.setSensorHeight(24.f);
            rig.setFStop(2.8f);
            rig.setFocusDistance(6.5f);
        }
    };

    ResolveFramePlanOptions fiveFrames() {

        ResolveFramePlanOptions options;
        options.width = 1920;
        options.height = 1080;
        options.firstFrame = 0;
        options.frameCount = 5;
        return options;
    }

    void expectProblem(const ResolvedFramePlan& plan, FramePlanProblem expected, const std::string& what) {

        const auto problem = verifyFramePlan(plan);
        check(problem.has_value(), ("expected a rejection: " + what).c_str());
        if (problem->code != expected) {
            throw std::runtime_error("wrong rejection code for " + what + ": got " +
                                     std::string(framePlanProblemName(problem->code)) + ", expected " +
                                     std::string(framePlanProblemName(expected)));
        }
        check(!problem->detail.empty(), ("empty diagnostic detail for " + what).c_str());
    }

    /// A verified plan to mutate. Every negative case below starts from a plan
    /// that passes, so the rejection can only come from the mutation.
    ResolvedFramePlan validPlan(Fixture& fixture) {

        const ResolvedFramePlan plan = resolveFramePlan(fixture.scene, fixture.rig, fiveFrames());
        check(!verifyFramePlan(plan).has_value(), "the fixture plan does not verify");
        return plan;
    }

    void contractShape() {

        Fixture fixture;
        const ResolvedFramePlan plan = validPlan(fixture);

        check(plan.schema == kResolvedFramePlanSchema, "schema is not the contract schema");
        check(plan.space == kResolvedFramePlanSpace, "space is not the contract space");
        check(plan.planId == "frame-plan-fixture", "plan_id does not default to the scene name");
        check(plan.width == 1920 && plan.height == 1080, "canvas is not the requested one");
        check(plan.fpsNumerator == 24 && plan.fpsDenominator == 1, "fps is not 24/1");

        check(plan.timeline.size() == 5, "timeline is not one entry per frame");
        for (int index = 0; index < plan.frameCount; ++index) {
            const PlanTimestamp& time = plan.timeline[static_cast<std::size_t>(index)];
            check(time.frame == index, "timeline frame is not the first frame + position");
            check(time.subFrameUnits == 0, "a whole-frame plan carries sub-frame units");
            if (time.seconds != planSeconds(index, 0, 24, 1))
                throw std::runtime_error("timeline seconds is not frame/fps");
        }

        check(plan.camera.size() == 5, "camera is not one sample per frame");

        // The camera is parented; an identity rig node must leave the world pose
        // exactly on the authored one.
        const PlanCameraSample& first = plan.camera.front();
        checkNear(first.position[0], 1.5f, 0.f, "camera world x");
        checkNear(first.position[1], 2.5f, 0.f, "camera world y");
        checkNear(first.position[2], 9.f, 0.f, "camera world z");
        checkNear(first.orientation[3], 1.f, 0.f, "camera orientation is not identity");
        checkNear(first.fovDeg, 50.f, 0.f, "camera fov");
        checkNear(first.lens.fStop, 2.8f, 0.f, "camera f-stop");
        checkNear(first.lens.focusDistance, 6.5f, 0.f, "camera focus distance");
        checkNear(first.lens.sensorWidthMm, 36.f, 0.f, "camera sensor width");

        check(plan.layers.size() == 4, "the plan did not keep all four layers");
        for (std::size_t index = 0; index < plan.layers.size(); ++index) {
            check(plan.layers[index].order == static_cast<int>(index), "layer order is not the list position");
        }
        check(plan.layers[0].id == 10 && plan.layers[0].kind == PlanLayerKind::Null, "rig is not the first layer");
        check(plan.layers[0].parent == 0, "rig is not parented to the root");
        check(plan.layers[1].id == 1 && plan.layers[1].kind == PlanLayerKind::Image, "plate kind/parent order");
        check(plan.layers[1].asset == "assets/plate.png", "plate asset is not carried");
        check(plan.layers[2].id == 2 && plan.layers[2].kind == PlanLayerKind::Text, "title kind");
        check(plan.layers[2].asset == "assets/title", "title asset is not carried");
        check(plan.layers[3].id == 3 && plan.layers[3].kind == PlanLayerKind::Mesh, "box kind");
        check(plan.layers[3].asset == "models/box.glb", "mesh asset is not taken from the attachment");
        check(plan.layers[3].parent == 2, "mesh parent is not the title layer");

        // Every layer is alive with no lifetime authored, and the composed
        // opacity of the 10 -> 1 -> 2 chain is 0.5 * 0.5 = 0.25 on the title.
        check(plan.layers[1].samples[0].visible, "plate is not visible");
        checkNear(plan.layers[1].samples[0].opacity, 0.5f, 0.f, "plate opacity");
        checkNear(plan.layers[2].samples[0].opacity, 0.25f, 1e-6f, "title composed opacity");
    }

    void frameGridIsExact() {

        Fixture fixture;
        const ResolvedFramePlan plan = validPlan(fixture);

        for (int index = 0; index < plan.frameCount; ++index) {
            // Independent evaluation of the same frame, through the frame-based
            // entry point rather than the plan's own timestamp.
            fixture.scene.evaluate(plan.firstFrame + index);

            for (const PlanLayer& layer : plan.layers) {
                const std::array<float, 16> expected = transposeToRowMajor(fixture.scene.worldMatrix(layer.id));
                const PlanLayerSample& sample = layer.samples[static_cast<std::size_t>(index)];
                for (std::size_t element = 0; element < 16; ++element) {
                    if (sample.world[element] != expected[element]) {
                        throw std::runtime_error("layer " + std::to_string(layer.id) + " frame " +
                                                 std::to_string(index) + " element " +
                                                 std::to_string(element) + " is " +
                                                 std::to_string(sample.world[element]) + ", expected " +
                                                 std::to_string(expected[element]));
                    }
                }
            }
        }

        // Independent of the transposed comparison above: the translation column
        // of a row-major matrix lives at 3, 7 and 11, and the plate sits at -Z 8.
        const std::array<float, 16>& plate = plan.layers[1].samples[0].world;
        checkNear(plate[3], 0.f, 0.f, "plate translation x");
        checkNear(plate[7], 0.f, 0.f, "plate translation y");
        checkNear(plate[11], -8.f, 0.f, "plate translation z");

        // The keyed title interpolates linearly from x=0 at frame 0 to x=100 at
        // frame 24; frame 4 is therefore 100*4/24 in world space (parent x = 0).
        checkNear(plan.layers[2].samples[4].world[3], 100.f * 4.f / 24.f, 1e-5f, "title keyed x at frame 4");
        checkNear(plan.layers[2].samples[4].world[7], 300.f, 1e-5f, "title y at frame 4");
        checkNear(plan.layers[2].samples[4].world[11], -8.f, 1e-5f, "title z at frame 4");
    }

    void negativeFirstFrameStaysOnTheGrid() {

        Fixture fixture;
        ResolveFramePlanOptions options = fiveFrames();
        options.firstFrame = -2;
        options.frameCount = 3;
        const ResolvedFramePlan plan = resolveFramePlan(fixture.scene, fixture.rig, options);
        check(!verifyFramePlan(plan).has_value(), "a pre-roll plan does not verify");
        check(plan.timeline[0].frame == -2, "negative frame is not carried");
        check(plan.timeline[0].seconds < 0.0, "negative frame has a non-negative timestamp");

        for (int index = 0; index < plan.frameCount; ++index) {
            fixture.scene.evaluate(plan.firstFrame + index);
            const std::array<float, 16> expected = transposeToRowMajor(fixture.scene.worldMatrix(2));
            const PlanLayerSample& sample = plan.layers[2].samples[static_cast<std::size_t>(index)];
            for (std::size_t element = 0; element < 16; ++element) {
                if (sample.world[element] != expected[element]) {
                    throw std::runtime_error("pre-roll frame " + std::to_string(index) + " does not match");
                }
            }
        }
    }

    void shutterWindow() {

        Fixture fixture;
        const ResolvedFramePlan plan = validPlan(fixture);

        check(plan.shutter.has_value(), "the authored shutter is missing from the plan");
        const PlanShutter& shutter = *plan.shutter;
        checkNear(shutter.angleDegrees, 180.f, 0.f, "shutter angle");
        check(shutter.offsetUnits.size() == 12, "shutter sample count");

        const std::int32_t first = shutter.offsetUnits.front();
        const std::int32_t last = shutter.offsetUnits.back();
        check(first == -250000, "a 180 degree shutter does not open a quarter frame early");
        check(last == 250000, "a 180 degree shutter does not close a quarter frame late");
        for (std::size_t index = 1; index < shutter.offsetUnits.size(); ++index) {
            check(shutter.offsetUnits[index] > shutter.offsetUnits[index - 1], "shutter offsets are not ascending");
        }
        // The window is symmetric about the frame; integer division may leave one
        // unit of truncation on the mirrored pair, and nothing more.
        for (std::size_t index = 0; index < shutter.offsetUnits.size(); ++index) {
            const std::int32_t mirrored = shutter.offsetUnits[shutter.offsetUnits.size() - 1 - index];
            check(std::abs(shutter.offsetUnits[index] + mirrored) <= 1, "shutter window is not symmetric");
        }
    }

    void determinismAndDigest() {

        Fixture first;
        const ResolvedFramePlan planA = resolveFramePlan(first.scene, first.rig, fiveFrames());

        Fixture second;
        const ResolvedFramePlan planB = resolveFramePlan(second.scene, second.rig, fiveFrames());

        const std::string jsonA = resolvedFramePlanToJson(planA, 2);
        const std::string jsonB = resolvedFramePlanToJson(planB, 2);
        check(jsonA == jsonB, "two runs over the same scene produced different JSON");
        check(planA.digest == planB.digest, "two runs over the same scene produced different digests");
        check(planA.digest == computeFramePlanDigest(planA), "the stamped digest is not the recomputed one");

        // Re-resolving the same scene a second time must not depend on the state
        // the previous resolve left behind.
        const ResolvedFramePlan planC = resolveFramePlan(first.scene, first.rig, fiveFrames());
        check(resolvedFramePlanToJson(planC, 2) == jsonA, "re-resolving the same scene changed the plan");

        check(resolvedFramePlanToJson(planA, 0).find('\n') == std::string::npos, "indent 0 is not a single line");
        check(jsonA.find("chrononmotion.resolved-frame-plan.v1") != std::string::npos, "schema missing from the JSON");
        check(jsonA.find("\"kind\": \"mesh\"") != std::string::npos, "layer kind missing from the JSON");
        check(jsonA.find("\"shutter\"") != std::string::npos, "shutter missing from the JSON");

        // The plan is a value: changing any resolved number changes the digest.
        ResolvedFramePlan mutated = planA;
        mutated.layers[2].samples[1].world[3] += 1e-3f;
        check(computeFramePlanDigest(mutated) != planA.digest, "the digest does not cover the layer matrices");
        expectProblem(mutated, FramePlanProblem::Digest, "a stale digest");

        ResolvedFramePlan unstamped = planA;
        unstamped.digest = 0;
        expectProblem(unstamped, FramePlanProblem::Digest, "an unstamped digest");
    }

    void refusesEveryRule() {

        Fixture fixture;
        const ResolvedFramePlan valid = validPlan(fixture);

        struct Case {
            const char* what;
            FramePlanProblem expected;
            void (*mutate)(ResolvedFramePlan&);
        };

        const Case cases[] = {
            {"schema", FramePlanProblem::Schema,
             [](ResolvedFramePlan& plan) { plan.schema = "chrononmotion.resolved-frame-plan.v2"; }},
            {"space", FramePlanProblem::Space,
             [](ResolvedFramePlan& plan) { plan.space = "screen/left-handed"; }},
            {"canvas", FramePlanProblem::Canvas,
             [](ResolvedFramePlan& plan) { plan.height = 0; }},
            {"frame rate", FramePlanProblem::FrameRate,
             [](ResolvedFramePlan& plan) { plan.fpsNumerator = 0; }},
            {"frame count", FramePlanProblem::TimelineEmpty,
             [](ResolvedFramePlan& plan) { plan.frameCount = 0; }},
            {"timeline size", FramePlanProblem::TimelineSize,
             [](ResolvedFramePlan& plan) { plan.timeline.pop_back(); }},
            {"timeline order", FramePlanProblem::TimelineOrder,
             [](ResolvedFramePlan& plan) { plan.timeline[1].frame = 7; }},
            {"sub-frame units", FramePlanProblem::TimelineSubFrame,
             [](ResolvedFramePlan& plan) { plan.timeline[0].subFrameUnits = kSubFrameUnits; }},
            {"timeline seconds", FramePlanProblem::TimelineSeconds,
             [](ResolvedFramePlan& plan) { plan.timeline[1].seconds += 0.5; }},
            {"shutter angle", FramePlanProblem::ShutterShape,
             [](ResolvedFramePlan& plan) { plan.shutter->angleDegrees = 0.f; }},
            {"shutter sample count", FramePlanProblem::ShutterShape,
             [](ResolvedFramePlan& plan) { plan.shutter->offsetUnits.resize(1); }},
            {"shutter ordering", FramePlanProblem::ShutterOffsets,
             [](ResolvedFramePlan& plan) { plan.shutter->offsetUnits[3] = plan.shutter->offsetUnits[2]; }},
            {"shutter window", FramePlanProblem::ShutterOffsets,
             [](ResolvedFramePlan& plan) { plan.shutter->offsetUnits[0] = -300000; }},
            {"shutter coverage", FramePlanProblem::ShutterCoverage,
             [](ResolvedFramePlan& plan) { plan.shutter->angleDegrees = 0.001f; }},
            {"camera count", FramePlanProblem::CameraCount,
             [](ResolvedFramePlan& plan) { plan.camera.pop_back(); }},
            {"camera timestamp", FramePlanProblem::CameraTime,
             [](ResolvedFramePlan& plan) { plan.camera[1].time.frame = 99; }},
            {"camera pose", FramePlanProblem::CameraPose,
             [](ResolvedFramePlan& plan) { plan.camera[0].orientation = {0.f, 0.f, 0.f, 0.f}; }},
            {"camera fov", FramePlanProblem::CameraOptics,
             [](ResolvedFramePlan& plan) { plan.camera[0].fovDeg = 0.f; }},
            {"camera f-stop", FramePlanProblem::CameraOptics,
             [](ResolvedFramePlan& plan) { plan.camera[0].lens.fStop = 0.2f; }},
            {"camera sensor gate", FramePlanProblem::CameraOptics,
             [](ResolvedFramePlan& plan) { plan.camera[0].lens.sensorHeightMm = 500.f; }},
            {"layer order", FramePlanProblem::LayerOrder,
             [](ResolvedFramePlan& plan) { plan.layers[1].order = 5; }},
            {"duplicate layer id", FramePlanProblem::LayerOrder,
             [](ResolvedFramePlan& plan) { plan.layers[1].id = plan.layers[0].id; }},
            {"unknown parent", FramePlanProblem::LayerParent,
             [](ResolvedFramePlan& plan) { plan.layers[1].parent = 777; }},
            {"null layer with an asset", FramePlanProblem::LayerKind,
             [](ResolvedFramePlan& plan) { plan.layers[0].asset = "assets/not-a-null.png"; }},
            {"content layer without an asset", FramePlanProblem::LayerAsset,
             [](ResolvedFramePlan& plan) { plan.layers[1].asset.clear(); }},
            {"layer sample count", FramePlanProblem::LayerSamples,
             [](ResolvedFramePlan& plan) { plan.layers[1].samples.pop_back(); }},
            {"layer timestamp", FramePlanProblem::LayerTimestamp,
             [](ResolvedFramePlan& plan) { plan.layers[1].samples[0].time.subFrameUnits = 5; }},
            {"layer transform", FramePlanProblem::LayerTransform,
             [](ResolvedFramePlan& plan) { plan.layers[1].samples[0].world[0] = NAN; }},
            {"layer opacity", FramePlanProblem::LayerOpacity,
             [](ResolvedFramePlan& plan) { plan.layers[1].samples[0].opacity = 2.f; }},
        };

        for (const Case& testCase : cases) {
            ResolvedFramePlan candidate = valid;
            testCase.mutate(candidate);
            expectProblem(candidate, testCase.expected, testCase.what);
        }
    }

    void producerFailsClosed() {

        Fixture fixture;

        const auto rejects = [&fixture](const ResolveFramePlanOptions& options, const char* what) {
            ++checks;
            try {
                const ResolvedFramePlan plan = resolveFramePlan(fixture.scene, fixture.rig, options);
                (void)plan;
            } catch (const std::invalid_argument&) {
                return;
            }
            throw std::runtime_error(std::string("the producer accepted: ") + what);
        };

        ResolveFramePlanOptions zeroCanvas = fiveFrames();
        zeroCanvas.width = 0;
        rejects(zeroCanvas, "a zero-width canvas");

        ResolveFramePlanOptions noFrames = fiveFrames();
        noFrames.frameCount = 0;
        rejects(noFrames, "a plan with no frames");

        ResolveFramePlanOptions wrongRate = fiveFrames();
        wrongRate.fpsNumerator = 30;
        wrongRate.fpsDenominator = 1;
        rejects(wrongRate, "a rate that is not the scene rate");

        ResolveFramePlanOptions matchingRate = fiveFrames();
        matchingRate.fpsNumerator = 24;
        matchingRate.fpsDenominator = 1;
        const ResolvedFramePlan matched = resolveFramePlan(fixture.scene, fixture.rig, matchingRate);
        check(!verifyFramePlan(matched).has_value(), "an explicitly matching rate was rejected");

        // A shutter so short that 1e-6 frame resolution cannot hold its samples
        // must be rejected, not silently collapsed into duplicate offsets.
        Fixture narrow;
        narrow.scene.setShutter(motion::ShutterSettings{0.01f, 64});
        ++checks;
        try {
            const ResolvedFramePlan plan = resolveFramePlan(narrow.scene, narrow.rig, fiveFrames());
            (void)plan;
            throw std::runtime_error("the producer accepted a shutter window too narrow for its samples");
        } catch (const std::invalid_argument&) {
        }

        // An opacity outside [0, 1] is an authoring error, and the producer says
        // so instead of clamping it into a lie.
        Fixture opaque;
        opaque.scene.findLayer(1)->tracks.opacity.clear();
        opaque.scene.findLayer(1)->tracks.opacity.add(0.f, 1.5f);
        ++checks;
        try {
            const ResolvedFramePlan plan = resolveFramePlan(opaque.scene, opaque.rig, fiveFrames());
            (void)plan;
            throw std::runtime_error("the producer accepted an opacity outside [0, 1]");
        } catch (const std::invalid_argument& error) {
            check(std::string(error.what()).find("layer_opacity") != std::string::npos,
                  "the opacity rejection does not name the rule");
        }

        // A camera parented to a layer the scene does not have is not resolvable.
        Fixture orphan;
        orphan.rig.setParent(4242);
        ++checks;
        try {
            const ResolvedFramePlan plan = resolveFramePlan(orphan.scene, orphan.rig, fiveFrames());
            (void)plan;
            throw std::runtime_error("the producer accepted a camera parent outside the scene");
        } catch (const std::invalid_argument&) {
        }

        // Emitting a plan the contract refuses is itself a failure, and the
        // message has to carry the rule that broke.
        const ResolvedFramePlan valid = validPlan(fixture);
        ResolvedFramePlan broken = valid;
        broken.digest = 0;
        ++checks;
        try {
            (void)resolvedFramePlanToJson(broken, 2);
            throw std::runtime_error("the JSON writer accepted an invalid plan");
        } catch (const std::invalid_argument& error) {
            check(std::string(error.what()).find("resolved frame plan:") == 0,
                  "the JSON rejection does not carry the contract prefix");
        }
    }

    void lifecycleAndFiltering() {

        // A layer whose lifetime has ended is still in the plan, marked not
        // visible: the backend decides, the plan does not drop it silently.
        Fixture fixture;
        fixture.scene.findLayer(2)->lifetime = motion::FrameRange::between(0, 2);
        ResolveFramePlanOptions options = fiveFrames();
        const ResolvedFramePlan plan = resolveFramePlan(fixture.scene, fixture.rig, options);
        check(!verifyFramePlan(plan).has_value(), "a lifetime-limited plan does not verify");
        check(plan.layers[2].samples[2].visible, "the title is not visible inside its lifetime");
        check(!plan.layers[2].samples[3].visible, "the title is still visible after its lifetime");

        // Dropping controller layers must re-base their children on the nearest
        // retained ancestor, not orphan them.
        Fixture filtered;
        ResolveFramePlanOptions noNulls = fiveFrames();
        noNulls.includeNullLayers = false;
        const ResolvedFramePlan lean = resolveFramePlan(filtered.scene, filtered.rig, noNulls);
        check(!verifyFramePlan(lean).has_value(), "a plan without controllers does not verify");
        check(lean.layers.size() == 3, "controller filtering did not drop the rig layer");
        check(lean.layers[0].id == 1 && lean.layers[0].parent == 0, "the plate was not re-based on the root");
        check(lean.layers[1].id == 2 && lean.layers[1].parent == 1, "the title lost its retained parent");

        // Without the rig layer the plan still carries the camera's composed
        // world pose: the filter drops controllers, never the parenting result.
        const std::array<float, 3> filteredCamera = lean.camera.front().position;
        checkNear(filteredCamera[2], 9.f, 0.f, "filtered plan moved the camera");
        check(lean.camera.front().position == plan.camera.front().position,
              "filtering controllers changed the resolved camera pose");
    }

    /// The wire document of the fixture, committed as a golden. The structural
    /// checks above prove the plan is right; this one proves the *bytes* a
    /// consumer parses have not drifted, including the digest and every number's
    /// exact spelling.
    std::string fixtureJson() {

        Fixture fixture;
        return resolvedFramePlanToJson(resolveFramePlan(fixture.scene, fixture.rig, fiveFrames()), 2);
    }

    void goldenMatches() {

        const std::string json = fixtureJson();
        std::ifstream input(CHRONONMOTION_FRAME_PLAN_GOLDEN, std::ios::binary);
        check(input.good(), "the committed frame plan golden is not readable");
        std::string golden((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        // Git commonly checks this committed JSON out with CRLF on Windows;
        // the schema serializer intentionally emits canonical LF on every OS.
        for (std::size_t carriageReturn = 0;
             (carriageReturn = golden.find("\r\n", carriageReturn)) != std::string::npos;)
            golden.erase(carriageReturn, 1);
        if (golden == json) {
            ++checks;
            return;
        }

        // Name the first divergent line instead of dumping two documents.
        std::size_t line = 1;
        std::size_t offset = 0;
        while (offset < golden.size() && offset < json.size() && golden[offset] == json[offset]) {
            if (golden[offset] == '\n') ++line;
            ++offset;
        }
        throw std::runtime_error("the frame plan document drifted from the golden at line " +
                                 std::to_string(line) + " (byte " + std::to_string(offset) + ")" +
                                 "; regenerate with --dump-golden");
    }

    void rateSelection() {

        const auto [ntsc, ntscDen] = rationalFrameRate(23.976f);
        check(ntsc == 24000 && ntscDen == 1001, "23.976 is not 24000/1001");
        const auto [pal, palDen] = rationalFrameRate(25.f);
        check(pal == 25 && palDen == 1, "25 is not 25/1");

        ++checks;
        try {
            (void)rationalFrameRate(17.3f);
            throw std::runtime_error("an unsupported rate was accepted");
        } catch (const std::invalid_argument&) {
        }
    }

}// namespace

int main(int argc, char** argv) {

    if (argc > 1 && std::string_view(argv[1]) == "--dump-golden") {
        try {
            std::ofstream output(CHRONONMOTION_FRAME_PLAN_GOLDEN, std::ios::binary);
            if (!output.good()) throw std::runtime_error("cannot open the golden for writing");
            output << fixtureJson();
            std::printf("wrote %s\n", CHRONONMOTION_FRAME_PLAN_GOLDEN);
            return 0;
        } catch (const std::exception& error) {
            std::printf("chrononmotion_frame_plan: --dump-golden failed: %s\n", error.what());
            return 1;
        }
    }

    try {
        contractShape();
        goldenMatches();
        frameGridIsExact();
        negativeFirstFrameStaysOnTheGrid();
        shutterWindow();
        determinismAndDigest();
        refusesEveryRule();
        producerFailsClosed();
        lifecycleAndFiltering();
        rateSelection();
    } catch (const std::exception& error) {
        std::printf("chrononmotion_frame_plan: FAILED after %d checks: %s\n", checks, error.what());
        return 1;
    }

    std::printf("chrononmotion_frame_plan: %d checks, 0 failures\n", checks);
    return 0;
}
