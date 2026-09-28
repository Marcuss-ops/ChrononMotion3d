// ChrononMotion timeline test.
//
// Pins the layer model and the evaluated hierarchy: per-channel animation on top
// of a static pose, the in/out lifetime, Parent/Null composition, world opacity,
// deterministic frame/PTS evaluation, and the authoring checks `validate()`.

#include "chrononmotion/chrononmotion.hpp"

#include "motion_check.hpp"

#include <stdexcept>
#include <string>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    bool sameMatrix(const Matrix4& a, const Matrix4& b) {

        for (unsigned int i = 0; i < 16; ++i) {
            if (a[i] != b[i]) return false;
        }
        return true;
    }

    Vector3 worldPositionOf(const MotionScene& scene, Layer::Id id) {

        Vector3 out;
        out.setFromMatrixPosition(scene.worldMatrix(id));
        return out;
    }

    // -------------------------------------------------------------- the layer

    void layerSamplesStaticAndAnimatedPose() {

        section("layer sampling");

        Layer layer(7, "Title");
        layer.transform.position.set(1.f, 2.f, 3.f);
        layer.transform.scale.set(2.f, 2.f, 2.f);
        layer.transform.opacity = 0.8f;

        const Transform atRest = layer.sample(0.f);
        checkNear(atRest.position, Vector3(1.f, 2.f, 3.f), 1e-5f, "an un-keyed layer reports its static pose");
        checkNear(atRest.opacity, 0.8f, 1e-5f, "an un-keyed layer reports its static opacity");
        check(!layer.animated(), "a layer with no keys reports itself static");

        layer.tracks.position.add(0.f, Vector3(10.f, 0.f, 0.f));
        layer.tracks.position.add(1.f, Vector3(20.f, 0.f, 0.f));
        layer.tracks.opacity.add(0.f, 1.f);
        layer.tracks.opacity.add(1.f, 0.f);

        check(layer.animated(), "a layer with keys reports itself animated");

        const Transform half = layer.sample(0.5f);
        checkNear(half.position, Vector3(15.f, 0.f, 0.f), 1e-4f, "a keyed channel overrides the static pose");
        checkNear(half.scale, Vector3(2.f, 2.f, 2.f), 1e-5f, "an un-keyed channel keeps the static pose");
        checkNear(half.opacity, 0.5f, 1e-4f, "the keyed opacity channel drives opacity");

        checkNear(layer.endTime(), 1.f, 1e-6f, "the layer reports the end of its keys");
    }

    void timeRemapIsDeterministic() {

        section("time remap");

        TimeRemap remap;
        remap.offsetFrames = 10.f;
        remap.stretch = 2.f;
        checkNear(remap.mapFrame(4.f), 12.f, 1e-5f, "stretch maps source frames into local time");

        remap.reverse = true;
        checkNear(remap.mapFrame(4.f), 8.f, 1e-5f, "reverse remap runs deterministically");

        remap.reverse = false;
        remap.loopStart = 0;
        remap.loopEnd = 4;
        checkNear(remap.mapFrame(9.f), 2.5f, 1e-5f, "loop remap wraps at its declared end");

        remap.pingPong = true;
        checkNear(remap.mapFrame(3.f), 3.5f, 1e-5f, "ping-pong remap follows its first half");

        remap.freeze = true;
        checkNear(remap.mapFrame(999.f), 10.f, 1e-5f, "freeze remap holds the offset");

        remap.stretch = 0.f;
        check(!remap.valid(), "a non-positive stretch is invalid");
    }

    void constraintsAndDriversAreDeterministic() {

        section("constraints and procedural drivers");

        MotionScene scene("operators", 30.f);
        scene.addContent(1, "Target", ContentRef::image("target", {10.f, 10.f}));
        scene.addContent(2, "Follower", ContentRef::image("follower", {10.f, 10.f}));
        scene.findLayer(1)->transform.position.set(10.f, 0.f, 0.f);
        Layer* follower = scene.findLayer(2);
        follower->transform.position.set(0.f, 0.f, 0.f);
        follower->constraints.push_back(ConstraintSpec{ConstraintKind::KeepDistance, 1, {}, {1.f, 1.f, 1.f}, 5.f, 1.f});
        DriverSpec wiggle;
        wiggle.kind = DriverKind::Wiggle;
        wiggle.property = DriverProperty::Position;
        wiggle.seed = 7;
        wiggle.amplitude = 0.25f;
        wiggle.frequency = 2.f;
        follower->drivers.push_back(wiggle);

        scene.evaluate(15);
        Vector3 position;
        position.setFromMatrixPosition(scene.worldMatrix(2));
        checkNear(position.x, 5.f + evaluateDriverOffset(wiggle, 0.5f, Vector3(5.f, 0.f, 0.f)).x,
                  1e-4f, "keep-distance and wiggle compose deterministically");
        const Matrix4 first = scene.worldMatrix(2);
        scene.evaluate(15);
        check(sameMatrix(first, scene.worldMatrix(2)), "constraints and drivers are random-access deterministic");

        DriverSpec pulse;
        pulse.kind = DriverKind::Pulse;
        pulse.amplitude = 1.f;
        pulse.frequency = 1.f;
        const Vector3 pulseOffset = evaluateDriverOffset(pulse, 0.25f, Vector3{});
        check(pulseOffset.x >= 0.f, "pulse driver produces positive modulation");

        Layer* pathLayer = scene.findLayer(2);
        pathLayer->constraints.clear();
        pathLayer->drivers.clear();
        ConstraintSpec follow;
        follow.kind = ConstraintKind::FollowPath;
        follow.path = std::make_shared<MotionPath>(MotionPath::line(Vector3(0.f, 0.f, 0.f), Vector3(10.f, 0.f, 0.f)));
        follow.amount = 1.f;
        follow.progress = 0.25f;
        pathLayer->constraints.push_back(follow);
        scene.evaluate(0);
        checkNear(worldPositionOf(scene, 2), Vector3(2.5f, 0.f, 0.f), 1e-4f,
                  "follow-path constraint places a layer on its path");
    }

    void frameRangeLifetime() {

        section("frame range");

        const FrameRange open = FrameRange::all();
        check(open.openEnded(), "the default range is open-ended");
        check(open.contains(0), "an open range contains its in point");
        check(open.contains(99999), "an open range contains everything after its in point");
        check(!open.contains(-1), "an open range still excludes frames before its in point");
        check(open.duration() == -1, "an open range has no duration");

        const FrameRange closed = FrameRange::between(15, 45);
        check(closed.contains(15), "the in point is inside the range");
        check(closed.contains(45), "the out point is inside the range");
        check(closed.contains(30), "the middle is inside the range");
        check(!closed.contains(14), "the frame before the in point is outside");
        check(!closed.contains(46), "the frame after the out point is outside");
        check(closed.duration() == 30, "a closed range reports its length in frames");

        bool rejectedRange = false;
        try {
            (void)FrameRange::between(20, 10);
        } catch (const std::invalid_argument&) {
            rejectedRange = true;
        }
        check(rejectedRange, "an inverted frame range is rejected at construction");

        Layer layer(1, "L");
        layer.alive(10, 20);
        check(layer.visibleAt(10) && layer.visibleAt(20), "a layer is visible inside its lifetime");
        check(!layer.visibleAt(9) && !layer.visibleAt(21), "a layer is hidden outside its lifetime");
        layer.alive(FrameRange::all());
        check(layer.visibleAt(5000), "clearing the lifetime makes the layer permanent");
    }

    // ------------------------------------------------------------- the scene

    void hierarchyComposesWorldTransforms() {

        section("hierarchy composition");

        MotionScene scene("canary", 30.f);

        // Deliberately insert the child before its parent: the evaluation order
        // must come from the hierarchy, not from insertion order.
        scene.addContent(11, "Title", ContentRef::text("title", {800.f, 120.f}), 10);
        scene.addNull(10, "Null_Main");

        Layer* controller = scene.findLayer(10);
        Layer* title = scene.findLayer(11);
        check(controller != nullptr && title != nullptr, "layers are found by id");
        check(controller->isNull(), "a null layer holds no content");
        check(!title->isNull() && title->isContent(), "a content layer holds content");

        controller->transform.position.set(5.f, 0.f, 0.f);
        title->transform.position.set(1.f, 2.f, 3.f);

        const std::vector<Layer::Id> order = scene.evaluationOrder();
        check(order.size() == 2, "every layer appears in the evaluation order");
        check(order[0] == 10 && order[1] == 11, "a parent is evaluated before its child");

        scene.evaluate(0);
        checkNear(worldPositionOf(scene, 11), Vector3(6.f, 2.f, 3.f), 1e-4f,
                  "a child's world position includes the parent offset");

        // Parent rotation must rotate the child's offset, not just translate it:
        // (5,0,0) + rotY(90) * (1,2,3) = (5,0,0) + (3,2,-1).
        controller->transform.rotation.y = math::degToRad(90.f);
        scene.evaluate(0);
        checkNear(worldPositionOf(scene, 11), Vector3(8.f, 2.f, -1.f), 1e-4f,
                  "a parent's rotation rotates the child's local offset");

        // The Null drives a whole group: two children with different local
        // offsets both move with it.
        scene.addContent(12, "Product", ContentRef::image("product", {480.f, 480.f}), 10);
        Layer* product = scene.findLayer(12);
        product->transform.position.set(0.f, 0.f, -2.f);
        scene.evaluate(0);
        // (5,0,0) + rotY(90) * (0,0,-2) = (5,0,0) + (-2,0,0).
        checkNear(worldPositionOf(scene, 12), Vector3(3.f, 0.f, 0.f), 1e-4f,
                  "a second child under the same null inherits the same motion");

        // Removing the parent link roots the layer again.
        title->parentId = Layer::kRoot;
        scene.evaluate(0);
        checkNear(worldPositionOf(scene, 11), Vector3(1.f, 2.f, 3.f), 1e-4f,
                  "detaching from the parent drops the inherited transform");
        title->parentId = 10;
    }

    void lifetimeHidesSubtreesAndOpacityMultiplies() {

        section("lifetime and opacity propagation");

        MotionScene scene("lifetime", 30.f);
        scene.addNull(10, "Null_Main");
        scene.addContent(11, "Title", ContentRef::text("t", {10.f, 10.f}), 10);

        Layer* controller = scene.findLayer(10);
        Layer* title = scene.findLayer(11);

        title->lifetime = FrameRange::between(15, 45);
        scene.evaluate(15);
        check(scene.visible(11), "a layer is visible on its in point");
        scene.evaluate(45);
        check(scene.visible(11), "a layer is visible on its out point");
        scene.evaluate(14);
        check(!scene.visible(11), "a layer is hidden before its in point");
        scene.evaluate(46);
        check(!scene.visible(11), "a layer is hidden after its out point");

        // A hidden controller hides everything under it.
        controller->lifetime = FrameRange::between(0, 30);
        scene.evaluate(40);
        check(!scene.visible(10), "the controller is hidden after its own out point");
        check(!scene.visible(11), "a hidden controller hides its children");

        controller->lifetime = FrameRange::all();
        controller->transform.opacity = 0.5f;
        title->transform.opacity = 0.4f;
        scene.evaluate(20);
        checkNear(scene.worldOpacity(10), 0.5f, 1e-5f, "the controller's opacity is its own");
        checkNear(scene.worldOpacity(11), 0.2f, 1e-5f, "a child's world opacity is the parent chain product");

        // Animating the controller's opacity drives the children's too.
        controller->tracks.opacity.add(0.f, 1.f);
        controller->tracks.opacity.add(1.f, 0.f);
        scene.evaluate(15);  // 0.5s at 30fps
        checkNear(scene.worldOpacity(11), 0.2f, 1e-4f, "an animated controller opacity reaches its children");
    }

    void evaluationIsDeterministic() {

        section("deterministic evaluation");

        MotionScene scene("det", 30.f);
        scene.addNull(10, "Null_Main");
        scene.addContent(11, "Title", ContentRef::text("t", {10.f, 10.f}), 10);

        Layer* controller = scene.findLayer(10);
        Layer* title = scene.findLayer(11);

        controller->tracks.position.add(0.f, Vector3(0.f, 0.f, 0.f), Easing::easeInOut());
        controller->tracks.position.add(1.f, Vector3(10.f, 0.f, 0.f), Easing::linear());
        controller->tracks.rotation.add(0.f, Vector3(0.f, 0.f, 0.f));
        controller->tracks.rotation.add(1.f, Vector3(0.f, math::degToRad(15.f), 0.f));
        title->tracks.opacity.add(0.f, 0.f);
        title->tracks.opacity.add(1.f, 1.f);
        title->castsShadow = true;
        title->shadowOpacity = 0.75f;
        title->shadowBlur = 3.f;

        scene.evaluate(15);
        const Matrix4 first = scene.worldMatrix(11);
        const Matrix4 previous = scene.state(11).previousWorld;
        const float firstOpacity = scene.worldOpacity(11);
        const bool firstVisible = scene.visible(11);

        scene.evaluate(15);
        check(sameMatrix(first, scene.worldMatrix(11)), "evaluating the same frame twice is identical");
        check(!sameMatrix(first, previous), "motion-blur state carries a distinct previous transform");
        const CompiledScene compiled = scene.compile();
        check(compiled.transforms[1].castsShadow && compiled.transforms[1].shadowOpacity == 0.75f &&
                      compiled.transforms[1].shadowBlur == 3.f,
              "compiled transform carries layer shadow intent");
        checkNear(scene.worldOpacity(11), firstOpacity, 0.f, "opacity is identical across evaluations");
        check(scene.visible(11) == firstVisible, "visibility is identical across evaluations");

        // Calling a different frame in between must not change the answer: the
        // state is a function of the frame, not of the evaluation history.
        scene.evaluate(90);
        scene.evaluate(15);
        check(sameMatrix(first, scene.worldMatrix(11)),
              "a frame's state does not depend on which frame was evaluated before it");

        // Frame and PTS agree.
        const Matrix4 byFrame = scene.worldMatrix(11);
        scene.evaluatePts(15.f / 30.f);
        check(sameMatrix(byFrame, scene.worldMatrix(11)), "evaluating by PTS matches evaluating by frame");
        check(scene.lastEvaluatedFrame() == 15, "the scene reports the last evaluated frame");
        checkNear(scene.lastEvaluatedPts(), 15.f / 30.f, 1e-6f, "the scene reports the last evaluated PTS");

        check(MotionTime::atFrame(48, 24.f).frame == 48, "a frame time keeps its frame");
        checkNear(MotionTime::atFrame(48, 24.f).pts(), 2.f, 1e-6f, "a frame time derives its PTS");
        check(MotionTime::atPts(2.f, 24.f).frame == 48, "a PTS time derives its frame");
        check(MotionTime::atPts(1.99f, 24.f).frame == 48, "a PTS just under an exact frame rounds onto it");

        // ... and a mid-segment sample is strictly between the two keys.
        scene.evaluate(0);
        const Vector3 atZero = worldPositionOf(scene, 11);
        scene.evaluate(30);
        const Vector3 atOne = worldPositionOf(scene, 11);
        scene.evaluate(15);
        const Vector3 atHalf = worldPositionOf(scene, 11);
        check(atZero.x < atHalf.x && atHalf.x < atOne.x, "a mid-segment sample lands between its keys");
    }

    void sceneReportsAuthoringProblems() {

        section("validation");

        MotionScene healthy("ok", 24.f);
        healthy.addNull(10, "Null_Main");
        healthy.addContent(11, "Title", ContentRef::text("t", {10.f, 10.f}), 10);
        check(healthy.validate().empty(), "a well-formed hierarchy has no problems");

        MotionScene orphan("orphan", 24.f);
        orphan.addContent(11, "Title", ContentRef::text("t", {10.f, 10.f}), 999);
        const std::vector<std::string> orphanProblems = orphan.validate();
        check(orphanProblems.size() == 1, "an unknown parent is reported once");
        check(orphanProblems[0].find("unknown parent") != std::string::npos,
              "the unknown-parent problem names the cause");
        bool rejectedOrphan = false;
        try {
            orphan.evaluate(0);
        } catch (const std::invalid_argument&) {
            rejectedOrphan = true;
        }
        check(rejectedOrphan, "evaluation rejects a hierarchy with a missing parent");

        bool rejectedMissingState = false;
        try {
            (void)orphan.state(404);
        } catch (const std::out_of_range&) {
            rejectedMissingState = true;
        }
        check(rejectedMissingState, "reading an unknown layer state is observable");

        MotionScene selfParent("self", 24.f);
        selfParent.addNull(10, "Null_Main");
        selfParent.findLayer(10)->parentId = 10;
        check(!selfParent.validate().empty(), "a self-parented layer is reported");

        MotionScene cyclic("cycle", 24.f);
        cyclic.addNull(10, "A");
        cyclic.addNull(11, "B");
        cyclic.findLayer(10)->parentId = 11;
        cyclic.findLayer(11)->parentId = 10;
        const std::vector<std::string> cycleProblems = cyclic.validate();
        check(cycleProblems.size() == 1, "a parent cycle is reported exactly once");
        check(cycleProblems[0].find("cycle") != std::string::npos, "the cycle problem names the cause");
        bool rejectedCycle = false;
        try {
            cyclic.evaluate(5);
        } catch (const std::invalid_argument&) {
            rejectedCycle = true;
        }
        check(rejectedCycle, "evaluation rejects a cyclic hierarchy");
    }

    void sceneEndFrameAndLookup() {

        section("scene bounds and lookup");

        MotionScene scene("bounds", 24.f);
        scene.addContent(1, "Background", ContentRef::image("bg", {1920.f, 1080.f}));
        scene.addNull(10, "Null_Main");
        Layer& title = scene.addContent(11, "Title", ContentRef::text("t", {10.f, 10.f}), 10);

        title.lifetime = FrameRange::between(0, 90);
        title.tracks.opacity.add(0.f, 0.f);
        title.tracks.opacity.add(2.f, 1.f);  // 2s at 24fps = frame 48

        check(scene.layerCount() == 3, "the scene counts its layers");
        check(scene.findLayer(1) != nullptr, "a layer is found by id");
        check(scene.findLayer("Title") != nullptr, "a layer is found by name");
        check(scene.findLayer("nope") == nullptr, "an absent name finds nothing");
        check(scene.findLayer(404) == nullptr, "an absent id finds nothing");
        check(scene.hasLayer(11), "hasLayer agrees with findLayer");
        check(scene.contentEndFrame() == 90, "the scene end frame covers the closed lifetimes");
        checkNear(scene.fps(), 24.f, 0.f, "the scene reports its fps");
        scene.setFps(30.f);
        checkNear(scene.fps(), 30.f, 0.f, "the scene accepts a new fps");

        bool rejectedFps = false;
        try {
            scene.setFps(0.f);
        } catch (const std::invalid_argument&) {
            rejectedFps = true;
        }
        check(rejectedFps, "a non-positive fps is rejected");

        // Replacing a layer by id must not leave two entries behind.
        const std::size_t before = scene.layerCount();
        scene.addLayer(Layer(11, "Title"));
        check(scene.layerCount() == before, "re-adding a layer id replaces it");
        check(scene.findLayer(11)->isNull(), "the replacement layer's own content wins");
    }

}// namespace

int main() {

    layerSamplesStaticAndAnimatedPose();
    timeRemapIsDeterministic();
    constraintsAndDriversAreDeterministic();
    frameRangeLifetime();
    hierarchyComposesWorldTransforms();
    lifetimeHidesSubtreesAndOpacityMultiplies();
    evaluationIsDeterministic();
    sceneReportsAuthoringProblems();
    sceneEndFrameAndLookup();

    return chrononmotion_test::report();
}
