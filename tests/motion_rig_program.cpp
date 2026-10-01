#include "motion_check.hpp"
#include "chrononmotion/motion/Motion.hpp"

#include <limits>
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
        RigProgram p;
        for (unsigned i = 0; i < 3; ++i) {
            RigNode n; n.id = i + 1; n.parent = i;
            if (i) n.rest.position = {1.f, 0.f, 0.f};
            n.bind = n.rest; n.layerId = n.id; p.graph.nodes.push_back(n);
        }
        RigNode target; target.id = 4; target.rest.position = {1.f, 1.f, 0.f}; target.bind = target.rest;
        RigNode pole; pole.id = 5; pole.rest.position = {0.f, 0.f, 3.f}; pole.bind = pole.rest;
        RigNode weight; weight.id = 6; weight.kind = RigNodeKind::Controller;
        RigNode decoration; decoration.id = 7; decoration.rest.position = {0.f, 0.f, 5.f}; decoration.bind = decoration.rest;
        ConstraintSpec point; point.kind = ConstraintKind::Point; point.targetId = 4;
        decoration.constraints.push_back(point);
        p.graph.nodes.insert(p.graph.nodes.end(), {target, pole, weight, decoration});
        RigController control; control.id = 6; control.minimum = {}; control.maximum = {1.f, 0.f, 0.f};
        p.drivers.controllers.push_back(control);
        RigDriverNode drive; drive.id = 1; drive.kind = RigDriverKind::Track;
        drive.track.add(0.f, 0.f).add(1.f, 1.f).add(9.f, 1.f).add(10.f, 0.f);
        p.drivers.nodes.push_back(drive);
        p.drivers.outputs.push_back({1, 6, RigDrivenProperty::Position, 0});
        RigScheduledIK ik; ik.id = 10; ik.chain.joints = {1, 2, 3};
        ik.chain.target = target.rest.position; ik.chain.pole = pole.rest.position;
        ik.targetNode = 4; ik.poleNode = 5; p.ik.push_back(ik);
        RigAnimatedNode animated; animated.id = 4;
        animated.tracks.position.add(0.f, {1.f, 1.f, 0.f}).add(5.f, {.5f, 1.5f, 0.f}).add(10.f, {1.f, 1.f, 0.f});
        p.animation.push_back(animated);
        RigPoseLayer pose; Transform posed; posed.position.y = 1.f;
        pose.pose = {{5, posed}}; pose.weight = 0.f; p.poses.push_back(pose);
        RigGeometryBinding rigid; rigid.id = 20; rigid.rigidNode = 3;
        rigid.source = {{0.f, 0.f, 0.f}, {.25f, .5f, 0.f}, {1.f, 0.f, 0.f}};
        RigMorphTarget morph; morph.positions = rigid.source; morph.positions[1].y = 1.f;
        rigid.morphs.push_back(morph);
        RigDeformer wave; wave.wavelength = 3.f; rigid.deformers.operators.push_back(wave);
        p.geometry.push_back(rigid);
        RigGeometryBinding skin = rigid; skin.id = 21; skin.rigidNode = 0; skin.morphs.clear(); skin.deformers.operators.clear();
        skin.skin.resize(skin.source.size());
        for (auto& vertex : skin.skin) { vertex.joints[0] = 1; vertex.weights[0] = 1.f; }
        p.geometry.push_back(skin);
        p.weights = {{6, RigWeightKind::IK, 10, 0}, {6, RigWeightKind::Constraint, 7, 0},
                     {6, RigWeightKind::Deformer, 20, 0}, {6, RigWeightKind::Pose, 0, 0},
                     {6, RigWeightKind::Morph, 20, 0}};
        return p;
    }
}

int main() {
    section("TestRigProgramUnifiedStages / AllWeightRoutes");
    auto program = makeProgram();
    const RigProgramEvaluator evaluator(program);
    const auto neutral = evaluator.evaluate(0, 30.f);
    checkNear(neutral.pose.localToWorld(3, {}), {2.f, 0.f, 0.f}, 1e-5f, "IK weight zero retains FK");
    checkNear(neutral.pose.localToWorld(7, {}), {0.f, 0.f, 5.f}, 0.f, "constraint weight zero retains authored transform");
    checkNear(neutral.geometry[0].positions[1], {2.25f, .5f, 0.f}, 1e-5f, "zero deformer/morph retains rigid supplied SVG points");
    const auto active = evaluator.evaluate(30, 30.f);
    checkNear(active.pose.localToWorld(3, {}), active.pose.localToWorld(4, {}), 1e-4f, "scheduled canonical IK follows animated control");
    checkNear(active.pose.localToWorld(7, {}), active.pose.localToWorld(4, {}), 1e-5f, "constraint driver weight applied after solve");
    checkNear(active.pose.local[active.pose.index(5)].position.y, 1.f, 0.f, "pose weight applied before solve");
    check(active.geometry[0].positions != neutral.geometry[0].positions, "morph/deformer and rig transform reach geometry");
    checkNear(active.pose.localToWorld(1, {}).distanceTo(active.pose.localToWorld(2, {})), 1.f, 1e-4f, "upper segment preserved");
    checkNear(active.pose.localToWorld(2, {}).distanceTo(active.pose.localToWorld(3, {})), 1.f, 1e-4f, "lower segment preserved");
    program.graph.nodes.clear();
    check(rigFrameSignature(evaluator.evaluate(30, 30.f)) == rigFrameSignature(active), "program snapshot independent of author edits");

    section("TestRigProgramRepeatBakeAllChannelsAndGeometry / Direct197");
    const auto first = evaluator.bake(0, 300, 30.f);
    const auto second = evaluator.bake(0, 300, 30.f);
    check(first.frames.size() == 301 && first.transforms.lastFrame == 300, "10-second inclusive program bake");
    check(first.signature == second.signature && first.signature != 0, "full pose/tracks/geometry bake signature repeats");
    const auto direct = rigFrameSignature(evaluator.evaluate(197, 30.f));
    for (int frame = 0; frame <= 197; ++frame) {
        const auto sampled = evaluator.evaluate(frame, 30.f);
        check(rigFrameSignature(sampled) == rigFrameSignature(first.frames[frame]), "sequential evaluation agrees with baked frame");
    }
    check(direct == rigFrameSignature(evaluator.evaluate(197, 30.f)), "direct197 equals sequential197 bit-stable");
    auto changed = first.frames[197]; changed.geometry[0].positions[0].x += .01f;
    check(rigFrameSignature(changed) != direct, "geometry values participate in signature");
    changed = first.frames[197]; changed.pose.local[0].opacity = .5f;
    check(rigFrameSignature(changed) != direct, "nonposition transform channels participate in signature");
    checkNear(first.frames.back().pose.localToWorld(3, {}), {2.f, 0.f, 0.f}, 1e-5f, "returns neutral at 10 seconds");

    section("TestRigProgramLayerLowering");
    const auto raw = makeProgram().graph.compile().withoutConstraints();
    MotionScene scene("rig-program", 30.f);
    for (const auto& node : raw.nodes()) if (node.layerId) scene.addNull(node.layerId, "bound", node.parent);
    raw.applyBake(first.transforms, scene);
    scene.evaluate(197);
    for (const auto& node : raw.nodes()) if (node.layerId) {
        const auto& actual = scene.worldMatrix(node.layerId);
        const auto& expected = first.frames[197].pose.world[first.frames[197].pose.index(node.id)];
        for (unsigned c = 0; c < 16; ++c) checkNear(actual.elements[c], expected.elements[c], 1e-5f, "ordinary track lowering preserves program world pose");
    }

    section("TestRigProgramSpaces / SecondarySamples");
    auto spaced = makeProgram();
    RigSpaceSwitch space; space.node = 7; space.spaces = {0, 4}; space.switches = {{1.f, 1}, {2.f, 0}};
    spaced.spaces.push_back(space);
    const RigProgramEvaluator spaceEvaluator(spaced);
    check(rigFrameSignature(spaceEvaluator.evaluate(197, 30.f)) == rigFrameSignature(spaceEvaluator.evaluate(197, 30.f)), "spaces combine with other scheduled operators deterministically");
    auto secondary = makeProgram();
    secondary.secondaryFps = 30.f; secondary.secondaryFirstFrame = 20;
    Transform overridePose; overridePose.position = {3.f, 2.f, 1.f};
    secondary.secondary = {RigPose{{7, overridePose}}, RigPose{{7, overridePose}}};
    const RigProgramEvaluator secondaryEvaluator(secondary);
    checkNear(secondaryEvaluator.evaluate(20, 30.f).pose.localToWorld(7, {}), overridePose.position, 0.f, "offline secondary has explicit postconstraint stage");
    rejects([&] { (void) secondaryEvaluator.evaluate(19, 30.f); }, "secondary outside range fails closed");
    rejects([&] { (void) secondaryEvaluator.evaluate(20, 60.f); }, "secondary mismatched sampling rate fails closed");

    section("TestRigProgramRejectsMalformedAndFeedback");
    auto invalid = makeProgram(); invalid.ik.push_back(invalid.ik.front());
    rejects([&] { RigProgramEvaluator bad(invalid); }, "duplicate operator IDs rejected");
    invalid = makeProgram(); invalid.ik[0].targetNode = 3;
    rejects([&] { RigProgramEvaluator bad(invalid); }, "IK control feedback rejected");
    invalid = makeProgram(); invalid.weights.push_back(invalid.weights.front());
    rejects([&] { RigProgramEvaluator bad(invalid); }, "ambiguous weight routes rejected");
    invalid = makeProgram(); invalid.weights[0].target = 999;
    rejects([&] { RigProgramEvaluator bad(invalid); }, "missing weight target rejected");
    invalid = makeProgram(); invalid.geometry[0].skin = invalid.geometry[1].skin;
    rejects([&] { RigProgramEvaluator bad(invalid); }, "double geometry transform binding rejected");
    invalid = makeProgram(); invalid.animation[0].tracks.scale.add(0.f, {1.f, 1.f, 1.f}).add(5.f, {0.f, 1.f, 1.f});
    rejects([&] { RigProgramEvaluator bad(invalid); }, "invalid future animated pose rejected before bake");
    invalid = makeProgram(); invalid.ik[0].amount.add(0.f, 1.f).add(5.f, 2.f);
    rejects([&] { RigProgramEvaluator bad(invalid); }, "invalid future IK weight rejected");
    rejects([&] { (void) evaluator.bake(0, 1000000, 30.f); }, "bake work budget enforced");
    rejects([&] { (void) evaluator.evaluate(-1, 30.f); }, "negative frame rejected");
    rejects([&] { (void) evaluator.evaluate(0, std::numeric_limits<float>::infinity()); }, "nonfinite fps rejected");
    section("TestRigOperatorRegistryCanonicalCoverage");
    const auto registry = rigOperatorRegistry();
    check(registry.size() == 48, "one canonical catalog covers constraint/driver/deformer/solver operators");
    for (std::size_t i = 0; i < registry.size(); ++i) {
        check(&rigOperator(registry[i].kind) == &registry[i], "typed registry returns canonical descriptor");
        for (std::size_t j = 0; j < i; ++j) check(registry[i].kind != registry[j].kind, "operator kind registered exactly once");
    }
    check(rigOperator(RigDriverKind::Multiply).inputArity == 2, "driver evaluator arity comes from canonical catalog");
    check(rigOperator(RigSolverKind::SecondaryMotion).offlineOnly &&
          !rigOperator(RigSolverKind::SecondaryMotion).supports3D, "registry states real planar offline capabilities");
    rejects([&] { (void) rigOperator(static_cast<RigDriverKind>(999)); }, "unknown operator fail closed");
    invalid = makeProgram(); invalid.ik[0].amount.add(0.f, 1.f);
    rejects([&] { RigProgramEvaluator bad(invalid); }, "competing driver and track weight authorities rejected");
    invalid = makeProgram(); invalid.ik[0].target.add(0.f, {1.f, 1.f, 0.f});
    rejects([&] { RigProgramEvaluator bad(invalid); }, "competing node and track target authorities rejected");
    section("TestSecondaryParameterContractCanonicalVerlet");
    VerletChainSpec tail; tail.joints = {{0.f, 0.f}, {1.f, 0.f}}; tail.fps = 60.f;
    tail.force = {0.f, -5.f}; tail.mass = 1.f;
    const auto light = bakeVerletChain(tail, 0, 60);
    tail.mass = 5.f;
    const auto heavy = bakeVerletChain(tail, 0, 60);
    check(std::abs(light.samples[10][1].y) > std::abs(heavy.samples[10][1].y), "force response scales by inverse mass");
    tail.mass = 1.f; tail.stiffness = 30.f;
    const auto stiff = bakeVerletChain(tail, 0, 60);
    check(std::abs(stiff.samples.back()[1].y) < std::abs(light.samples.back()[1].y), "rest stiffness resists external force");
    tail.stiffness = 0.f; tail.drag = 20.f;
    const auto dragged = bakeVerletChain(tail, 0, 60);
    check(std::abs(dragged.samples[10][1].y) < std::abs(light.samples[10][1].y), "drag attenuates velocity independently of mass");
    tail.drag = 0.f; tail.maxAngle = .1f;
    const auto limited = bakeVerletChain(tail, 0, 120);
    for (const auto& sample : limited.samples) {
        check(std::abs(std::atan2(sample[1].y, sample[1].x)) <= .10001f, "secondary angle bound honored");
        checkNear(sample[0].distanceTo(sample[1]), 1.f, 1e-5f, "secondary angular projection retains segment length");
    }
    tail.mass = 0.f;
    rejects([&] { (void) bakeVerletChain(tail, 0, 10); }, "invalid secondary mass rejected");
    tail.mass = 1.f; tail.stiffness = 100000.f;
    rejects([&] { (void) bakeVerletChain(tail, 0, 10); }, "unstable restoring fixed step rejected");
    section("TestAllControllerKinds / DriverArithmeticAndSpatialInputs");
    RigGraph controllerGraph;
    for (unsigned i = 1; i <= 7; ++i) { RigNode n; n.id = i; n.kind = i <= 5 ? RigNodeKind::Controller : RigNodeKind::Bone; controllerGraph.nodes.push_back(n); }
    controllerGraph.nodes[6].rest.position = {3.f, 4.f, 0.f};
    controllerGraph.nodes[6].rest.rotation.z = .5f;
    const auto controllerRig = controllerGraph.compile();
    RigDriverGraph controllerDrivers;
    const RigControllerKind kinds[]{RigControllerKind::Scalar, RigControllerKind::Joystick2D, RigControllerKind::Spatial3D,
                                     RigControllerKind::Boolean, RigControllerKind::Enum};
    for (unsigned i = 0; i < 5; ++i) {
        RigController c; c.id = i + 1; c.kind = kinds[i];
        if (i == 3) { c.minimum = {}; c.maximum = {1.f, 0.f, 0.f}; }
        if (i == 4) { c.minimum = {}; c.maximum = {4.f, 0.f, 0.f}; }
        controllerDrivers.controllers.push_back(c);
        RigDriverNode d; d.id = i + 1; d.kind = RigDriverKind::Controller; d.controller = c.id;
        d.component = i == 1 ? 1U : i == 2 ? 2U : 0U; controllerDrivers.nodes.push_back(d);
    }
    controllerDrivers.outputs = {{1, 1, RigDrivenProperty::Position, 0}, {2, 2, RigDrivenProperty::Position, 0},
                                 {3, 3, RigDrivenProperty::Position, 0}, {4, 4, RigDrivenProperty::Position, 0},
                                 {5, 5, RigDrivenProperty::Position, 0}};
    const RigDriverEvaluator typedControllers(controllerRig, controllerDrivers);
    const std::vector<RigControllerValue> typedInputs{{1, {.7f, 0.f, 0.f}}, {2, {0.f, -.3f, 0.f}},
                                                     {3, {0.f, 0.f, .8f}}, {4, {.6f, 0.f, 0.f}}, {5, {2.7f, 0.f, 0.f}}};
    const auto typedPose = typedControllers.evaluate(0.f, typedInputs);
    const float expectedControls[]{.7f, -.3f, .8f, 1.f, 3.f};
    for (unsigned i = 0; i < 5; ++i) checkNear(typedPose[i].local.position.x, expectedControls[i], 1e-6f, "typed controller component clamp/quantization");
    auto invalidInputs = typedInputs; invalidInputs[0].value.y = 1.f;
    rejects([&] { (void) typedControllers.evaluate(0.f, invalidInputs); }, "scalar inactive components rejected");
    for (const auto kind : {RigDriverKind::Add, RigDriverKind::Multiply, RigDriverKind::Min, RigDriverKind::Max,
                           RigDriverKind::Clamp, RigDriverKind::Smoothstep, RigDriverKind::Time,
                           RigDriverKind::Distance, RigDriverKind::Angle}) {
        RigDriverGraph g;
        RigDriverNode a; a.id = 1; a.value = .5f;
        RigDriverNode b; b.id = 2; b.value = .25f;
        RigDriverNode operation; operation.id = 3; operation.kind = kind;
        const auto arity = rigOperator(kind).inputArity;
        if (arity) operation.inputs[0] = 1;
        if (arity == 2) operation.inputs[1] = 2;
        operation.first = 6; operation.second = 7;
        operation.inMin = 0.f; operation.inMax = .4f;
        g.nodes = {a, b, operation}; g.outputs = {{3, 1, RigDrivenProperty::Position, 0}};
        const auto actual = RigDriverEvaluator(controllerRig, g).evaluate(2.f)[0].local.position.x;
        const float expected = kind == RigDriverKind::Add ? .75f : kind == RigDriverKind::Multiply ? .125f :
                               kind == RigDriverKind::Min ? .25f : kind == RigDriverKind::Max ? .5f :
                               kind == RigDriverKind::Clamp ? .4f : kind == RigDriverKind::Smoothstep ? 1.f :
                               kind == RigDriverKind::Time ? 2.f : kind == RigDriverKind::Distance ? 5.f : .5f;
        checkNear(actual, expected, 1e-5f, "driver arithmetic/time/spatial semantics");
    }
    section("TestDampedAim / SurfaceAttach / LookAtWithRoll");
    ConstraintSpec aim; aim.kind = ConstraintKind::DampedAim; aim.damping = 2.f;
    Transform aimed;
    check(applyConstraint(aim, aimed, {1.f, 0.f, 0.f}, Quaternion{}, {1.f, 1.f, 1.f}, 0.f), "DampedAim at zero time valid");
    check(aimed.localQuaternion() == Quaternion{}, "DampedAim starts at authored orientation");
    Transform later;
    check(applyConstraint(aim, later, {1.f, 0.f, 0.f}, Quaternion{}, {1.f, 1.f, 1.f}, 2.f), "DampedAim response evaluates directly");
    check(later.localQuaternion().angleTo(MotionPath::directionToQuaternion({1.f, 0.f, 0.f})) < .04f,
          "DampedAim exponential response approaches target without history");
    aim.kind = ConstraintKind::LookAtWithRoll; aim.roll = 1.57079632679f;
    Transform rolled;
    check(applyConstraint(aim, rolled, {0.f, 0.f, 1.f}, Quaternion{}, {1.f, 1.f, 1.f}, 0.f), "LookAt roll applies");
    Vector3 rolledX = Vector3::X(); rolledX.applyQuaternion(rolled.localQuaternion());
    checkNear(rolledX, {0.f, 1.f, 0.f}, 1e-5f, "roll rotates tangent axes without changing facing");
    ConstraintSpec attach; attach.kind = ConstraintKind::SurfaceAttach;
    attach.surfaceOffset = {1.f, 0.f, 0.f};
    Transform attached; attached.position = {2.f, 3.f, 10.f};
    check(applyConstraint(attach, attached, {0.f, 0.f, 2.f}, Quaternion{}, {1.f, 1.f, 1.f}, 0.f), "SurfaceAttach projects on target plane");
    checkNear(attached.position, {3.f, 3.f, 2.f}, 1e-5f, "plane projection retains tangential position and adds offset");
    attach.surfaceNormal = {};
    rejects([&] { validateRigOperator(attach); }, "invalid surface normal fails closed");
    return report();
}
