// ChrononMotion — the compositing order.
//
// The acceptance this file owns: the draw order of a scene is a resolved fact,
// not a side effect of insertion order. The load-bearing case is the After
// Effects composition the contract exists for — a 3D object with a text plane
// behind it — plus the frame where the object crosses the text, the layer that
// is behind the camera, and the plans that must refuse an incomplete stack.

#include "chrononmotion/motion/Compositing.hpp"
#include "chrononmotion/plan/ResolvedFramePlan.hpp"

#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/SceneImport.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
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

    void checkIds(const CompositeStack& stack, const std::vector<Layer::Id>& expected,
                  const char* what) {

        ++checks;
        if (stack.size() != expected.size())
            throw std::runtime_error(std::string(what) + ": got " + std::to_string(stack.size()) +
                                     " layers, expected " + std::to_string(expected.size()));
        for (std::size_t index = 0; index < expected.size(); ++index) {
            ++checks;
            if (stack.backToFront[index].layerId != expected[index])
                throw std::runtime_error(std::string(what) + ": position " + std::to_string(index) +
                                         " is layer " +
                                         std::to_string(stack.backToFront[index].layerId) +
                                         ", expected " + std::to_string(expected[index]));
        }
    }

    void checkOrder(const std::vector<std::uint32_t>& actual,
                    const std::vector<std::uint32_t>& expected, const char* what) {

        ++checks;
        if (actual != expected)
            throw std::runtime_error(std::string(what) + ": order does not match the expectation");
    }

    /// The exact AE case: object 1 at the origin, text 2 at `textDepth`, camera
    /// at +Z 10 looking down -Z. The object is authored first, so the authored
    /// stack would put the text in front; depth mode must not.
    struct AeScene {
        MotionScene scene{"ae-object-text", 24.f};
        CameraRig rig{1, 50.f, 16.f / 9.f};

        explicit AeScene(CompositeMode mode, float textDepth = -4.f) {

            MeshAttachment attachment;
            attachment.assetId = "hero";
            Layer& object = scene.addMesh(1, "hero", std::move(attachment));
            object.transform.position = Vector3(0.f, 0.f, 0.f);

            Layer& title = scene.addContent(
                2, "title", ContentRef::text("title", Vector2(800.f, 200.f)));
            title.transform.position = Vector3(0.f, 0.f, textDepth);

            scene.setCompositeMode(mode);
            rig.setPosition(Vector3(0.f, 0.f, 10.f));
            rig.setTarget(Vector3(0.f, 0.f, 0.f));
        }

        [[nodiscard]] CompositeStack stackAt(int frame) {

            scene.evaluate(frame);
            return resolveCompositeStack(scene, rig.sample(static_cast<float>(frame) / scene.fps()));
        }
    };

    void stackModeKeepsTheAuthoredOrder() {

        AeScene fixture(CompositeMode::Stack, 0.f);
        const CompositeStack stack = fixture.stackAt(0);
        check(stack.mode == CompositeMode::Stack, "stack mode was not carried");
        checkIds(stack, {1, 2}, "stack mode authored order");
        check(stack.backToFront[0].stackIndex == 0 && stack.backToFront[1].stackIndex == 1,
              "stack indices do not follow the authored list");
    }

    void depthModePutsTheTextBehindTheObject() {

        AeScene fixture(CompositeMode::Depth);
        const CompositeStack stack = fixture.stackAt(0);
        check(stack.mode == CompositeMode::Depth, "depth mode was not carried");
        // Object authored first, text last: stack mode would say 1 then 2. The
        // text sits at -Z 4, so the camera resolves it 14 units away and the
        // object 10; the object must paint last.
        checkIds(stack, {2, 1}, "depth mode object/text");
        checkNear(stack.backToFront[0].viewDepth, 14.f, 1e-4f, "text depth");
        checkNear(stack.backToFront[1].viewDepth, 10.f, 1e-4f, "object depth");
    }

    void crossingFlipsTheOrder() {

        AeScene fixture(CompositeMode::Depth);
        Layer* object = fixture.scene.findLayer(1);
        check(object != nullptr, "object layer missing");
        object->tracks.position.add(0.f, Vector3(0.f, 0.f, -10.f))
            .add(1.f, Vector3(0.f, 0.f, 4.f));

        checkIds(fixture.stackAt(0), {1, 2}, "frame 0: object behind the text");
        checkIds(fixture.stackAt(24), {2, 1}, "frame 24: object in front of the text");
    }

    void behindCameraPaintsFirst() {

        AeScene fixture(CompositeMode::Depth, 20.f);
        const CompositeStack stack = fixture.stackAt(0);
        checkIds(stack, {2, 1}, "a layer behind the camera sorts behind everything");
        check(stack.backToFront[0].viewDepth < 0.f,
              "the behind-camera layer did not report a negative depth");
    }

    void lifetimeExcludesHiddenLayers() {

        AeScene fixture(CompositeMode::Depth);
        fixture.scene.findLayer(2)->lifetime = FrameRange::between(0, 4);
        checkIds(fixture.stackAt(4), {2, 1}, "inside the lifetime");
        checkIds(fixture.stackAt(5), {1}, "outside the lifetime");
    }

    void equalDepthKeepsTheAuthoredStack() {

        AeScene fixture(CompositeMode::Depth, 0.f);
        checkIds(fixture.stackAt(0), {1, 2}, "equal depth uses the authored order");
    }

    void resolveRefusesAnUnevaluatedScene() {

        AeScene fixture(CompositeMode::Depth);
        ++checks;
        try {
            const CompositeStack stack = resolveCompositeStack(
                fixture.scene, fixture.rig.sample(0.f));
            (void)stack;
            throw std::runtime_error("the resolver accepted a scene that was never evaluated");
        } catch (const std::invalid_argument& error) {
            check(std::string(error.what()).find("evaluate") != std::string::npos,
                  "the unevaluated-scene rejection does not name the precondition");
        }
    }

    void resolveRefusesANonFiniteAnchor() {

        AeScene fixture(CompositeMode::Depth);
        fixture.scene.findLayer(2)->transform.position.x =
            std::numeric_limits<float>::quiet_NaN();
        fixture.scene.evaluate(0);

        ++checks;
        try {
            const CompositeStack stack = resolveCompositeStack(
                fixture.scene, fixture.rig.sample(0.f));
            (void)stack;
            throw std::runtime_error("the resolver accepted a non-finite anchor");
        } catch (const std::invalid_argument& error) {
            check(std::string(error.what()).find("non-finite") != std::string::npos,
                  "the non-finite rejection does not name the value");
        }
    }

    void sceneFileRoundTripsTheMode() {

        AeScene fixture(CompositeMode::Depth);
        const std::string json = fixture.scene.toJson(2);
        check(json.find("\"compositeMode\": \"depth\"") != std::string::npos,
              "depth mode is missing from the scene document");

        MotionScene restored = sceneFromJson(json);
        check(restored.compositeMode() == CompositeMode::Depth,
              "the scene reader lost the composite mode");
        check(restored.toJson(2) == json,
              "the scene round trip changed the document");

        MotionScene plain("plain", 24.f);
        check(plain.compositeMode() == CompositeMode::Stack,
              "the default mode is not stack");
        check(plain.toJson(2).find("compositeMode") == std::string::npos,
              "the default mode was written even though it is the default");

        ++checks;
        try {
            MotionScene bad = sceneFromJson(
                "{\"format\":\"chrononmotion.scene\",\"version\":1,\"name\":\"bad\","
                "\"fps\":24,\"compositeMode\":\"painter\",\"layers\":[],\"lights\":[]}");
            (void)bad;
            throw std::runtime_error("the scene reader accepted an unknown composite mode");
        } catch (const std::invalid_argument&) {
        }
    }

    void planCarriesTheResolvedOrder() {

        AeScene fixture(CompositeMode::Depth);
        ResolveFramePlanOptions options;
        options.frameCount = 3;
        const ResolvedFramePlan plan = resolveFramePlan(fixture.scene, fixture.rig, options);

        check(!verifyFramePlan(plan).has_value(), "the depth plan does not verify");
        check(plan.compositeOrder.size() == 3, "the plan did not resolve one order per frame");
        for (const std::vector<std::uint32_t>& order : plan.compositeOrder)
            checkOrder(order, {2, 1}, "depth plan order");

        const std::string json = resolvedFramePlanToJson(plan, 2);
        check(json.find("\"composite_order\"") != std::string::npos,
              "composite_order is missing from the depth plan document");

        // Stack mode must stay byte-identical to the document that existed
        // before this contract: no key, no extra field.
        AeScene stacked(CompositeMode::Stack, 0.f);
        const ResolvedFramePlan plain = resolveFramePlan(stacked.scene, stacked.rig, options);
        check(plain.compositeOrder.empty(), "a stack-mode plan resolved a composite order");
        check(resolvedFramePlanToJson(plain, 2).find("composite_order") == std::string::npos,
              "a stack-mode plan emitted composite_order");
        check(!verifyFramePlan(plain).has_value(), "the stack plan does not verify");
    }

    void planOrderFollowsTheCrossing() {

        AeScene fixture(CompositeMode::Depth);
        fixture.scene.findLayer(1)->tracks.position.add(0.f, Vector3(0.f, 0.f, -10.f))
            .add(1.f, Vector3(0.f, 0.f, 4.f));
        ResolveFramePlanOptions options;
        options.frameCount = 25;
        const ResolvedFramePlan plan = resolveFramePlan(fixture.scene, fixture.rig, options);
        check(!verifyFramePlan(plan).has_value(), "the crossing plan does not verify");
        checkOrder(plan.compositeOrder[0], {1, 2}, "plan frame 0");
        checkOrder(plan.compositeOrder[24], {2, 1}, "plan frame 24");
    }

    void planRefusesAnIncompleteStack() {

        AeScene fixture(CompositeMode::Depth);
        ResolveFramePlanOptions options;
        options.frameCount = 2;
        const ResolvedFramePlan valid = resolveFramePlan(fixture.scene, fixture.rig, options);
        check(!verifyFramePlan(valid).has_value(), "the fixture plan does not verify");

        const auto expect = [](const ResolvedFramePlan& candidate, FramePlanProblem expected,
                               const char* what) {
            const auto problem = verifyFramePlan(candidate);
            ++checks;
            if (!problem.has_value())
                throw std::runtime_error(std::string("expected a rejection: ") + what);
            if (problem->code != expected)
                throw std::runtime_error(std::string("wrong rejection for ") + what + ": got " +
                                         std::string(framePlanProblemName(problem->code)) +
                                         ", expected " +
                                         std::string(framePlanProblemName(expected)));
            if (problem->detail.empty())
                throw std::runtime_error(std::string("empty diagnostic detail for ") + what);
        };

        ResolvedFramePlan missing = valid;
        missing.compositeOrder[0].pop_back();
        expect(missing, FramePlanProblem::CompositeOrder, "a missing layer");

        ResolvedFramePlan duplicate = valid;
        duplicate.compositeOrder[0].push_back(duplicate.compositeOrder[0][0]);
        expect(duplicate, FramePlanProblem::CompositeOrder, "a duplicated layer");

        ResolvedFramePlan unknown = valid;
        unknown.compositeOrder[0][0] = 999;
        expect(unknown, FramePlanProblem::CompositeOrder, "an unknown layer id");

        ResolvedFramePlan wrongCount = valid;
        wrongCount.compositeOrder.pop_back();
        expect(wrongCount, FramePlanProblem::CompositeOrder, "a missing frame entry");

        ResolvedFramePlan hidden = valid;
        for (PlanLayer& layer : hidden.layers)
            if (layer.id == 2) layer.samples[0].visible = false;
        expect(hidden, FramePlanProblem::CompositeOrder, "a hidden layer still in the order");
    }

    void digestCoversTheOrder() {

        AeScene fixture(CompositeMode::Depth);
        ResolveFramePlanOptions options;
        options.frameCount = 2;
        const ResolvedFramePlan valid = resolveFramePlan(fixture.scene, fixture.rig, options);

        ResolvedFramePlan swapped = valid;
        std::swap(swapped.compositeOrder[0][0], swapped.compositeOrder[0][1]);
        check(computeFramePlanDigest(swapped) != valid.digest,
              "the digest does not cover the composite order");

        const auto problem = verifyFramePlan(swapped);
        check(problem.has_value() && problem->code == FramePlanProblem::Digest,
              "a reordered plan with a stale digest was not rejected");

        swapped.digest = computeFramePlanDigest(swapped);
        check(!verifyFramePlan(swapped).has_value(),
              "a reordered plan with a fresh digest must still verify");
    }

}// namespace

int main() {

    try {
        stackModeKeepsTheAuthoredOrder();
        depthModePutsTheTextBehindTheObject();
        crossingFlipsTheOrder();
        behindCameraPaintsFirst();
        lifetimeExcludesHiddenLayers();
        equalDepthKeepsTheAuthoredStack();
        resolveRefusesAnUnevaluatedScene();
        resolveRefusesANonFiniteAnchor();
        sceneFileRoundTripsTheMode();
        planCarriesTheResolvedOrder();
        planOrderFollowsTheCrossing();
        planRefusesAnIncompleteStack();
        digestCoversTheOrder();
    } catch (const std::exception& error) {
        std::printf("chrononmotion_compositing: FAILED after %d checks: %s\n", checks, error.what());
        return 1;
    }

    std::printf("chrononmotion_compositing: %d checks, 0 failures\n", checks);
    return 0;
}
