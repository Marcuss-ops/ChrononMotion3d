#include "motion_check.hpp"
#include "chrononmotion/motion/Motion.hpp"

#include <algorithm>
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
    RigGraph arm() {
        RigGraph graph;
        RigNode root; root.id = 10; root.layerId = 1;
        root.rest.position = {3.f, 4.f, 2.f}; root.bind = root.rest;
        RigNode torso; torso.id = 20; torso.parent = 10; torso.layerId = 2;
        torso.rest.position = {0.f, 2.f, 0.f}; torso.bind = torso.rest;
        RigNode left; left.id = 30; left.parent = 20; left.layerId = 3;
        left.rest.position = {-2.f, 0.f, 0.f}; left.bind = left.rest;
        RigNode hand; hand.id = 40; hand.parent = 30; hand.layerId = 4;
        hand.rest.position = {-1.f, 0.f, 0.f}; hand.bind = hand.rest;
        RigNode right = left; right.id = 50; right.layerId = 5; right.rest.position.x = 2.f; right.bind = right.rest;
        RigNode rightHand = hand; rightHand.id = 60; rightHand.parent = 50;
        rightHand.layerId = 6; rightHand.rest.position.x = 1.f; rightHand.bind = rightHand.rest;
        graph.nodes = {rightHand, hand, root, right, torso, left};
        return graph;
    }
}

int main() {
    section("TestRigGraphStableOrdering / Parenting / RestPose");
    auto graph = arm();
    const auto rig = graph.compile();
    auto permuted = graph; std::reverse(permuted.nodes.begin(), permuted.nodes.end());
    check(rig.evaluationOrder() == permuted.compile().evaluationOrder(), "stable parent-first ordering independent of insertion");
    check(rig.evaluationOrder() == std::vector<RigNodeId>({10, 20, 30, 40, 50, 60}), "canonical ready-set stable ID ordering");
    const auto rest = rig.evaluate();
    checkNear(rest.localToWorld(40, {}), {0.f, 6.f, 2.f}, 1e-6f, "left hand follows hierarchy");
    checkNear(rest.localToWorld(60, {}), {6.f, 6.f, 2.f}, 1e-6f, "right hand follows hierarchy");
    Transform rotated = rig.nodes().front().rest;
    rotated.rotation.z = 1.f;
    RigPose pose{{10, rotated}};
    const auto posed = rig.evaluate(pose);
    checkNear(posed.localToWorld(30, {}).distanceTo(posed.localToWorld(40, {})), 1.f, 1e-5f, "FK segment length stable");
    check(rig.bindPose().world == rest.world, "bind recovery exact");
    graph.nodes.clear();
    check(rig.evaluate().world == rest.world, "compiled rig independent of mutable authoring graph");

    section("TestRigGraphLocalWorldRoundTrip / RandomAccessDeterminism");
    const Vector3 point{1.f, -3.f, 2.f};
    checkNear(posed.worldToLocal(40, posed.localToWorld(40, point)), point, 2e-5f, "local/world round trip");
    for (int i = 0; i < 100; ++i) {
        (void) rig.evaluate();
        check(rig.evaluate(pose).world == posed.world, "100 evaluations bit-stable regardless of history");
    }
    RigEvaluation reused;
    rig.evaluate(pose, reused);
    const auto* worlds = reused.world.data();
    const auto* locals = reused.local.data();
    for (int i = 0; i < 1000; ++i) rig.evaluate(pose, reused);
    check(reused.world.data() == worlds && reused.local.data() == locals, "fixed-size evaluation buffers reused");

    section("TestRigGraphRejectsCycle / InvalidData");
    auto invalid = arm(); invalid.nodes[2].parent = 40;
    rejects([&] { (void) invalid.compile(); }, "hierarchy cycle rejected");
    invalid = arm(); invalid.nodes[2].dependencies = {60};
    rejects([&] { (void) invalid.compile(); }, "dependency cycle rejected");
    invalid = arm(); invalid.nodes[0].parent = 999;
    rejects([&] { (void) invalid.compile(); }, "missing parent rejected");
    invalid = arm(); invalid.nodes[0].dependencies = {999};
    rejects([&] { (void) invalid.compile(); }, "missing dependency rejected");
    invalid = arm(); invalid.nodes[0].id = 40;
    rejects([&] { (void) invalid.compile(); }, "duplicate node rejected");
    invalid = arm(); invalid.nodes[0].id = 0;
    rejects([&] { (void) invalid.compile(); }, "zero ID rejected");
    invalid = arm(); invalid.nodes[0].rest.position.x = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { (void) invalid.compile(); }, "NaN rejected");
    invalid = arm(); invalid.nodes[0].rest.scale.x = 0.f;
    rejects([&] { (void) invalid.compile(); }, "singular pose rejected");
    invalid = arm(); invalid.nodes[0].rest.orientation.set(0.f, 0.f, 0.f, 0.f);
    rejects([&] { (void) invalid.compile(); }, "invalid quaternion rejected");
    invalid = arm(); invalid.nodes[0].length = 0.f;
    rejects([&] { (void) invalid.compile(); }, "zero bone length rejected");
    invalid = arm(); invalid.nodes[0].primaryAxis = {};
    rejects([&] { (void) invalid.compile(); }, "invalid primary axis rejected");
    invalid = arm(); invalid.nodes[0].layerId = 1;
    rejects([&] { (void) invalid.compile(); }, "duplicate binding rejected");
    rejects([&] { (void) rig.evaluate(RigPose{{10, rotated}, {10, rotated}}); }, "duplicate pose ID rejected");
    rejects([&] { (void) rig.evaluate(RigPose{{999, rotated}}); }, "unknown pose ID rejected");
    rejects([&] { (void) rig.evaluate(RigPose{{20, rotated}, {10, rotated}}); }, "noncanonical pose ordering rejected");
    check(RigGraph{}.compile().evaluate().world.empty(), "empty rig valid");

    section("TestRigBakeRepeat / RandomAccess197 / LayerBinding");
    std::vector<RigPose> frames(301);
    for (int frame = 0; frame <= 300; ++frame) {
        Transform root = rig.nodes().front().rest;
        root.position.x += static_cast<float>(frame) * .01f;
        frames[frame] = {{10, root}};
    }
    const auto baked = rig.bake(frames, 0, 30.f);
    const auto repeated = rig.bake(frames, 0, 30.f);
    check(baked.lastFrame == 300 && baked.nodes.size() == 6, "inclusive bake range");
    MotionScene scene("rig", 30.f);
    for (const auto& node : rig.nodes()) {
        auto& layer = scene.addNull(node.layerId, "bound");
        if (node.parent) {
            const auto it = std::find_if(rig.nodes().begin(), rig.nodes().end(), [&](const RigNode& n) { return n.id == node.parent; });
            layer.parentId = it->layerId;
        }
    }
    rig.applyBake(baked, scene);
    for (int frame = 0; frame <= 300; ++frame) {
        const float seconds = static_cast<float>(frame) / 30.f;
        for (std::size_t i = 0; i < rig.nodes().size(); ++i)
            check(baked.nodes[i].tracks.position.sample(seconds) == repeated.nodes[i].tracks.position.sample(seconds), "repeat bake exact track values");
        scene.evaluate(frame);
    }
    const Matrix4 sequential = scene.worldMatrix(4);
    scene.evaluate(197);
    const auto direct = rig.evaluate(frames[197]);
    check(scene.worldMatrix(4) == direct.world[direct.index(40)], "frame 197 tracks equal direct rig evaluation");
    scene.evaluate(300);
    check(scene.worldMatrix(4) == sequential, "random seek does not affect later bake samples");
    rejects([&] { (void) rig.bake({}, 0, 30.f); }, "empty bake rejected");
    rejects([&] { (void) rig.bake(frames, -1, 30.f); }, "negative start rejected");
    rejects([&] { (void) rig.bake(frames, 0, 0.f); }, "invalid fps rejected");
    scene.findLayer(6)->parentId = 1;
    const auto before = scene.findLayer(1)->tracks.position.sample(1.f);
    rejects([&] { rig.applyBake(baked, scene); }, "incompatible hierarchy rejected before mutation");
    check(scene.findLayer(1)->tracks.position.sample(1.f) == before, "rejected lowering preserves scene");
    section("TestRigIKCanonicalAdapter / FkIkMatching / SpaceSwitchNoPop");
    for (const unsigned segments : {2U, 4U, 8U, 32U}) {
        RigGraph chainGraph;
        RigIKChain ik;
        for (unsigned joint = 0; joint <= segments; ++joint) {
            RigNode node; node.id = joint + 1; node.parent = joint;
            if (joint) node.rest.position = {1.f, 0.f, 0.f};
            node.bind = node.rest;
            chainGraph.nodes.push_back(node);
            ik.joints.push_back(node.id);
        }
        const auto solver = chainGraph.compile();
        ik.target = {static_cast<float>(segments) * .6f, .5f, 0.f};
        ik.pole = {0.f, 0.f, 4.f};
        const auto solvedPose = solver.solveIK(ik);
        const auto solved = solver.evaluate(solvedPose);
        checkNear(solved.localToWorld(segments + 1, {}), ik.target, 1e-4f, "canonical IK reachable endpoint");
        for (unsigned joint = 0; joint < segments; ++joint)
            checkNear(solved.localToWorld(joint + 1, {}).distanceTo(solved.localToWorld(joint + 2, {})),
                      1.f, 2e-5f, "IK length preserved");
        check(solver.evaluate(solver.solveIK(ik)).world == solved.world, "IK repeat exact");
        const auto fkMatched = solver.matchWorldPose(solved);
        const auto fkResult = solver.evaluate(fkMatched);
        for (std::size_t joint = 0; joint < solved.world.size(); ++joint)
            for (std::size_t component = 0; component < 16; ++component)
                checkNear(fkResult.world[joint].elements[component], solved.world[joint].elements[component],
                          1e-4f, "IK to FK matching no pop");
        ik.amount = 0.f;
        check(solver.evaluate(solver.solveIK(ik)).world == solver.evaluate().world, "FK endpoint exact");
        ik.amount = 1.f;
        ik.target = {static_cast<float>(segments) + 4.f, 0.f, 0.f};
        checkNear(solver.evaluate(solver.solveIK(ik)).localToWorld(segments + 1, {}),
                  {static_cast<float>(segments), 0.f, 0.f}, 1e-4f, "unreachable target clamped");
        ik.target = {static_cast<float>(segments), 0.f, 0.f};
        checkNear(solver.evaluate(solver.solveIK(ik)).localToWorld(segments + 1, {}),
                  ik.target, 1e-4f, "exact boundary reached");
    }
    auto switchedGraph = arm();
    switchedGraph.nodes[2].rest.rotation.z = .4f;
    switchedGraph.nodes[2].rest.scale = {2.f, 2.f, 2.f};
    const auto originalRig = switchedGraph.compile();
    const auto beforeSwitch = originalRig.evaluate();
    switchedGraph.nodes[1].parent = 50;
    const auto switchedRig = switchedGraph.compile();
    const auto switchPose = switchedRig.matchWorldPose(beforeSwitch);
    const auto afterSwitch = switchedRig.evaluate(switchPose);
    for (std::size_t joint = 0; joint < beforeSwitch.world.size(); ++joint)
        for (std::size_t component = 0; component < 16; ++component)
            checkNear(afterSwitch.world[joint].elements[component], beforeSwitch.world[joint].elements[component],
                      1e-4f, "space switch preserves world pose with rotated/scaled parents");
    RigIKChain badIK; badIK.joints = {10, 40};
    rejects([&] { (void) rig.solveIK(badIK); }, "IK rejects discontinuous chain");
    section("TestRigControllers / DriverGraph / PoseMixer");
    RigGraph drivenGraph;
    RigNode control; control.id = 1; control.kind = RigNodeKind::Controller;
    RigNode head; head.id = 2;
    RigNode leg; leg.id = 3;
    drivenGraph.nodes = {head, control, leg};
    const auto drivenRig = drivenGraph.compile();
    RigDriverGraph drivers;
    RigController joystick; joystick.id = 1; joystick.kind = RigControllerKind::Joystick2D;
    drivers.controllers.push_back(joystick);
    RigDriverNode input; input.id = 1; input.kind = RigDriverKind::Controller; input.controller = 1;
    RigDriverNode multiplier; multiplier.id = 2; multiplier.value = .5f;
    RigDriverNode multiply; multiply.id = 3; multiply.kind = RigDriverKind::Multiply; multiply.inputs = {1, 2};
    drivers.nodes = {multiply, input, multiplier};
    drivers.outputs.push_back({3, 2, RigDrivenProperty::Rotation, 1});
    const RigDriverEvaluator evaluator(drivenRig, drivers);
    checkNear(evaluator.evaluate(0.f)[1].local.rotation.y, 0.f, 0.f, "neutral controller neutral pose");
    const std::vector<RigControllerValue> rightControl{{1, {1.f, 0.f, 0.f}}};
    const std::vector<RigControllerValue> leftControl{{1, {-1.f, 0.f, 0.f}}};
    checkNear(evaluator.evaluate(0.f, rightControl)[1].local.rotation.y, .5f, 0.f, "right controller pose");
    checkNear(evaluator.evaluate(0.f, leftControl)[1].local.rotation.y, -.5f, 0.f, "left controller pose");
    const std::vector<RigControllerValue> outside{{1, {100.f, 0.f, 0.f}}};
    checkNear(evaluator.evaluate(0.f, outside)[1].local.rotation.y, .5f, 0.f, "controller clamped");
    check(drivenRig.evaluate(evaluator.evaluate(197.f, rightControl)).world ==
          drivenRig.evaluate(evaluator.evaluate(197.f, rightControl)).world, "driver repeat stable");
    auto cyclic = drivers; cyclic.nodes[1].kind = RigDriverKind::Add; cyclic.nodes[1].inputs = {3, 2};
    rejects([&] { RigDriverEvaluator bad(drivenRig, cyclic); }, "driver graph cycle rejected");
    auto nonfinite = drivers; nonfinite.nodes[2].value = std::numeric_limits<float>::infinity();
    rejects([&] { RigDriverEvaluator bad(drivenRig, nonfinite); }, "nonfinite driver rejected");
    auto ambiguous = drivers; ambiguous.outputs.push_back(ambiguous.outputs.front());
    rejects([&] { RigDriverEvaluator bad(drivenRig, ambiguous); }, "ambiguous output rejected");
    auto remapGraph = drivers;
    remapGraph.nodes[0].kind = RigDriverKind::Remap; remapGraph.nodes[0].inputs = {1, 0};
    remapGraph.nodes[0].inMin = -1.f; remapGraph.nodes[0].inMax = 1.f;
    RigDriverEvaluator remapper(drivenRig, remapGraph);
    checkNear(remapper.evaluate(0.f, leftControl)[1].local.rotation.y, 0.f, 0.f, "bounded remap lower bound");
    checkNear(remapper.evaluate(0.f, rightControl)[1].local.rotation.y, 1.f, 0.f, "bounded remap upper bound");
    auto noiseGraph = drivers; noiseGraph.nodes[0].kind = RigDriverKind::Noise; noiseGraph.nodes[0].inputs = {1, 0};
    noiseGraph.nodes[0].seed = 197;
    RigDriverEvaluator noisy(drivenRig, noiseGraph);
    check(drivenRig.evaluate(noisy.evaluate(0.f, rightControl)).world ==
          drivenRig.evaluate(noisy.evaluate(0.f, rightControl)).world, "seeded noise repeat stable");
    Transform target; target.position.x = 10.f;
    Transform legTarget; legTarget.position.y = 5.f;
    RigPoseLayer layer; layer.pose = {{2, target}, {3, legTarget}};
    for (const float weight : {0.f, .5f, 1.f}) {
        layer.weight = weight;
        const std::vector<RigPoseLayer> layers{layer};
        checkNear(mixRigPoses(drivenRig, {}, layers)[1].local.position.x, 10.f * weight, 0.f, "pose weights 0/.5/1");
    }
    layer.mode = RigPoseBlendMode::MaskedBlend; layer.mask = {2};
    checkNear(mixRigPoses(drivenRig, {}, std::vector<RigPoseLayer>{layer})[2].local.position.y,
              0.f, 0.f, "upper body mask preserves legs");
    layer.mode = RigPoseBlendMode::Additive; layer.weight = .5f;
    Transform base; base.position.x = 3.f;
    checkNear(mixRigPoses(drivenRig, RigPose{{2, base}}, std::vector<RigPoseLayer>{layer})[1].local.position.x,
              8.f, 0.f, "additive preserves base and adds weighted rest delta");
    layer.weight = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { (void) mixRigPoses(drivenRig, {}, std::vector<RigPoseLayer>{layer}); }, "NaN pose weight rejected");
    section("TestRigConstraintStages / ParentSpace / Limits / StretchTo");
    RigGraph constrained;
    RigNode parent; parent.id = 1; parent.rest.position = {10.f, 0.f, 0.f}; parent.bind = parent.rest;
    RigNode aimTarget; aimTarget.id = 2; aimTarget.rest.position = {10.f, 0.f, 4.f}; aimTarget.bind = aimTarget.rest;
    RigNode pointer; pointer.id = 3; pointer.parent = 1;
    ConstraintSpec limit; limit.kind = ConstraintKind::LimitRotation;
    limit.minimum = {-.2f, -.2f, -.2f}; limit.maximum = {.2f, .2f, .2f};
    ConstraintSpec stretch; stretch.kind = ConstraintKind::StretchTo; stretch.targetId = 2; stretch.restLength = 2.f;
    pointer.rest.rotation.z = 1.f;
    pointer.constraints = {limit, stretch}; // Limits stage still applies last.
    constrained.nodes = {pointer, aimTarget, parent};
    const auto constrainedRig = constrained.compile();
    const auto constraintPose = constrainedRig.evaluate();
    checkNear(constraintPose.local[2].scale.z, 2.f, 0.f, "StretchTo accounts for parent coordinate space");
    checkNear(constraintPose.local[2].rotation.z, 0.f, 0.f, "StretchTo replaces combined rotation before limits");
    checkNear(constraintPose.localToWorld(3, {0.f, 0.f, 2.f}), {10.f, 0.f, 4.f}, 1e-5f,
              "StretchTo endpoint reaches target without leftover Euler rotation");
    pointer.constraints = {stretch};
    ConstraintSpec pointConstraint; pointConstraint.kind = ConstraintKind::Point; pointConstraint.targetId = 2;
    pointer.constraints = {pointConstraint}; constrained.nodes[0] = pointer;
    const auto pointPose = constrained.compile().evaluate();
    checkNear(pointPose.localToWorld(3, {}), {10.f, 0.f, 4.f}, 1e-5f, "Point target converted to parent local space");
    auto constraintCycle = constrained; constraintCycle.nodes[1].constraints = {pointConstraint};
    rejects([&] { (void) constraintCycle.compile(); }, "constraint dependency cycle rejected");
    MotionScene limitedScene("limit", 30.f);
    auto& limitedLayer = limitedScene.addNull(1, "limited");
    limitedLayer.transform.rotation.z = 1.f; limitedLayer.constraints = {limit};
    check(limitedScene.validate().empty(), "root-target limits validate");
    limitedScene.evaluate(0);
    checkNear(limitedScene.state(1).local.rotation.z, .2f, 0.f, "root-target limits execute on MotionScene");
    section("TestDeformerStack / PuppetPins / Lattice / Skin / Morph");
    const std::vector<Vector3> geometry{{0.f, 0.f, 0.f}, {.5f, .5f, 0.f}, {1.f, 0.f, 0.f}};
    DeformerStack deformers;
    check(deformers.evaluate(geometry) == geometry, "empty stack exact identity");
    RigDeformer wave; wave.amount = 0.f;
    deformers.operators = {wave};
    check(deformers.evaluate(geometry) == geometry, "zero amount exact identity");
    wave.amount = 1.f; wave.wavelength = 3.f;
    RigDeformer bend; bend.kind = RigDeformerKind::Bend; bend.strength = .5f;
    deformers.operators = {wave, bend}; const auto waveBend = deformers.evaluate(geometry);
    deformers.operators = {bend, wave};
    check(deformers.evaluate(geometry) != waveBend, "stack order is contractual");
    RigDeformer lattice; lattice.kind = RigDeformerKind::Lattice;
    lattice.lattice.offsets.resize(16, {0.f, 1.f, 0.f});
    deformers.operators = {lattice};
    checkNear(deformers.evaluate(geometry)[0], {0.f, 1.f, 0.f}, 1e-6f, "4x4 lattice displacement");
    lattice.lattice.columns = 8; lattice.lattice.rows = 8;
    lattice.lattice.offsets.resize(64, {0.f, 1.f, 0.f}); deformers.operators = {lattice};
    checkNear(deformers.evaluate(geometry)[0], {0.f, 1.f, 0.f}, 1e-6f, "8x8 lattice displacement");
    lattice.lattice.offsets.pop_back(); deformers.operators = {lattice};
    rejects([&] { (void) deformers.evaluate(geometry); }, "invalid lattice rejected");
    RigDeformer puppet; puppet.kind = RigDeformerKind::PinWarp;
    RigPin pin; pin.target = {0.f, 2.f, 0.f}; pin.radius = 2.f;
    puppet.pins = {pin}; deformers.operators = {puppet};
    checkNear(deformers.evaluate(geometry)[0], {0.f, 2.f, 0.f}, 0.f, "position pin anchors artwork");
    RigPin stiff; stiff.kind = RigPinKind::Stiffness; stiff.radius = 2.f;
    puppet.pins.push_back(stiff); deformers.operators = {puppet};
    checkNear(deformers.evaluate(geometry)[0], {}, 0.f, "stiffness pin preserves rest point");
    pin.kind = RigPinKind::Rotation; pin.rotation.setFromAxisAngle(Vector3::Z(), 1.57079632679f);
    pin.radius = 4.f; puppet.pins = {pin}; deformers.operators = {puppet};
    check(deformers.evaluate(geometry)[2].y > .5f, "rotation pin rotates influenced geometry");
    pin.kind = RigPinKind::Scale; pin.scale = {2.f, 2.f, 1.f};
    puppet.pins = {pin}; deformers.operators = {puppet};
    check(deformers.evaluate(geometry)[2].x > 1.5f, "scale pin expands influenced geometry");
    for (const auto kind : {RigDeformerKind::Bend, RigDeformerKind::Wave, RigDeformerKind::Twist,
                           RigDeformerKind::Bulge, RigDeformerKind::Taper, RigDeformerKind::NoiseWarp,
                           RigDeformerKind::SquashStretch}) {
        RigDeformer op; op.kind = kind; op.seed = 197;
        deformers.operators = {op};
        check(deformers.evaluate(geometry) == deformers.evaluate(geometry), "deformer repeated output exact");
    }
    RigDeformer warp; warp.kind = RigDeformerKind::PathWarp;
    warp.path = std::make_shared<MotionPath>(MotionPath::line({0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}));
    deformers.operators = {warp};
    checkNear(deformers.evaluate(geometry).back(), {4.f, 0.f, 0.f}, 1e-5f, "PathWarp consumes canonical MotionPath");
    std::vector<RigSkinVertex> skins(geometry.size());
    for (auto& binding : skins) { binding.joints[0] = 2; binding.weights[0] = 1.f; }
    check(skinRigGeometry(drivenRig, drivenRig.bindPose(), geometry, skins) == geometry, "skin bind identity");
    Transform moved; moved.position = {2.f, 0.f, 0.f};
    checkNear(skinRigGeometry(drivenRig, drivenRig.evaluate(RigPose{{2, moved}}), geometry, skins)[0],
              {2.f, 0.f, 0.f}, 0.f, "inverse bind weighted skin deformation");
    skins[0].weights[0] = .5f;
    rejects([&] { (void) skinRigGeometry(drivenRig, drivenRig.bindPose(), geometry, skins); }, "unnormalized skin rejected");
    RigMorphTarget morph; morph.positions = geometry; morph.positions[0] = {2.f, 0.f, 0.f};
    for (const float weight : {0.f, .5f, 1.f}) {
        morph.weight = weight;
        checkNear(mixRigMorphs(geometry, std::vector<RigMorphTarget>{morph})[0].x,
                  2.f * weight, 0.f, "morph weights 0/.5/1");
    }
    morph.positions.pop_back();
    rejects([&] { (void) mixRigMorphs(geometry, std::vector<RigMorphTarget>{morph}); }, "morph topology rejected");
    Transform quaternionLimited;
    quaternionLimited.orientation.setFromAxisAngle(Vector3::Z(), 1.f);
    check(applyConstraint(limit, quaternionLimited, {}, Quaternion{}, {1.f, 1.f, 1.f}, 0.f), "quaternion limit applies");
    checkNear(quaternionLimited.rotation.z, .2f, 1e-6f, "LimitRotation constrains combined quaternion/Euler orientation");
    section("TestRigBindingsFailClosed / CanonicalConstraintIntegration");
    auto malformedBake = baked;
    malformedBake.nodes[0].tracks.opacity = {};
    rejects([&] { rig.applyBake(malformedBake, scene); }, "malformed baked channels rejected");
    auto nanPose = beforeSwitch;
    nanPose.world[0].elements[0] = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { (void) switchedRig.matchWorldPose(nanPose); }, "NaN world matching rejected");
    MotionScene pointScene("point", 30.f);
    pointScene.addNull(1, "parent").transform.position = {10.f, 0.f, 0.f};
    pointScene.addNull(2, "target").tracks.position.add(0.f, {10.f, 0.f, 4.f}).add(1.f, {10.f, 0.f, 8.f});
    auto& pointLayer = pointScene.addNull(3, "point"); pointLayer.parentId = 1; pointLayer.constraints = {pointConstraint};
    pointScene.evaluate(30);
    Vector3 pointWorld; pointWorld.setFromMatrixPosition(pointScene.worldMatrix(3));
    checkNear(pointWorld, {10.f, 0.f, 8.f}, 1e-5f, "MotionScene Point uses animated world target in parent space");
    checkNear(constrainedRig.bindPose().local[2].scale.z, 1.f, 0.f, "constraint cannot alter canonical bind pose");
    section("TestRigSecondaryMotion / SplineIK");
    RigGraph flexible;
    for (unsigned i = 0; i < 4; ++i) {
        RigNode node; node.id = i + 1; node.parent = i;
        if (i) node.rest.position = {1.f, 0.f, 0.f};
        node.bind = node.rest; flexible.nodes.push_back(node);
    }
    const auto flexibleRig = flexible.compile();
    RigSecondaryMotion secondary; secondary.joints = {1, 2, 3, 4};
    const auto zeroForce = bakeRigSecondaryMotion(flexibleRig, secondary, 0, 30, 30.f);
    for (const auto& sample : zeroForce)
        check(flexibleRig.evaluate(sample).world == flexibleRig.evaluate().world, "zero force keeps exact rest pose");
    secondary.gravity = {0.f, -1.f};
    const auto secondaryBake = bakeRigSecondaryMotion(flexibleRig, secondary, 0, 60, 30.f);
    const auto secondaryRepeat = bakeRigSecondaryMotion(flexibleRig, secondary, 0, 60, 30.f);
    check(flexibleRig.evaluate(secondaryBake[30]).world == flexibleRig.evaluate(secondaryRepeat[30]).world,
          "secondary repeat exact seekable sample");
    check(flexibleRig.evaluate(secondaryBake.back()).localToWorld(4, {}).y < 0.f, "secondary gravity moves tail");
    RigSplineIK spline; spline.joints = secondary.joints;
    spline.path = std::make_shared<MotionPath>(MotionPath::line({0.f, 0.f, 0.f}, {0.f, 3.f, 0.f}));
    const auto splinePose = flexibleRig.evaluate(solveRigSplineIK(flexibleRig, spline));
    for (unsigned i = 0; i < 4; ++i)
        checkNear(splinePose.localToWorld(i + 1, {}), {0.f, static_cast<float>(i), 0.f}, 1e-5f,
                  "SplineIK rigid chords on a straight path");
    spline.path = std::make_shared<MotionPath>(MotionPath::quadraticBezier(Vector3{0.f, 0.f, 0.f}, Vector3{6.f, 0.f, 0.f}, Vector3{6.f, 6.f, 0.f}));
    const auto curvedSpline = solveRigSplineIK(flexibleRig, spline);
    const auto curvedPose = flexibleRig.evaluate(curvedSpline);
    for (unsigned i = 0; i < 3; ++i)
        checkNear(curvedPose.localToWorld(i + 1, {}).distanceTo(curvedPose.localToWorld(i + 2, {})), 1.f, 1e-4f,
                  "SplineIK chords stay rigid on a curved path");
    bool monotone = true;
    for (unsigned i = 0; i < 3; ++i) {
        const auto root = curvedPose.localToWorld(1, {});
        const auto a = curvedPose.localToWorld(i + 1, {});
        const auto b = curvedPose.localToWorld(i + 2, {});
        monotone = monotone && a.distanceTo(root) < b.distanceTo(root) + 1e-5f;
    }
    check(monotone, "SplineIK joints follow arc-length order from the root");
    check(flexibleRig.evaluate(solveRigSplineIK(flexibleRig, spline)).world == curvedPose.world,
          "SplineIK repeat exact on a curved path");
    spline.end = .3f; // Arc available (~2.9) is shorter than the 3-unit chain.
    rejects([&] { (void) solveRigSplineIK(flexibleRig, spline); }, "SplineIK rejects chains longer than the interval");
    spline.resolutionSteps = 0;
    rejects([&] { (void) solveRigSplineIK(flexibleRig, spline); }, "SplineIK rejects zero resolution steps");
    section("TestSpaceSwitchNoPopTimed / TestFkIkSwitchNoPopBothDirections");
    RigGraph spaceGraph;
    RigNode bodySpace; bodySpace.id = 1; bodySpace.rest.position = {20.f, 10.f, 0.f};
    bodySpace.rest.rotation.z = .3f; bodySpace.rest.scale = {2.f, 2.f, 2.f}; bodySpace.bind = bodySpace.rest;
    RigNode swordSpace; swordSpace.id = 2; swordSpace.rest.position = {-10.f, 5.f, 3.f};
    swordSpace.rest.rotation.z = -.5f; swordSpace.rest.scale = {.5f, .5f, .5f}; swordSpace.bind = swordSpace.rest;
    RigNode handSpace; handSpace.id = 3; handSpace.rest.position = {300.f, 400.f, 20.f}; handSpace.bind = handSpace.rest;
    spaceGraph.nodes = {handSpace, swordSpace, bodySpace};
    const auto spaceRig = spaceGraph.compile();
    RigSpaceSwitch handSwitch; handSwitch.node = 3; handSwitch.spaces = {0, 1, 2};
    handSwitch.switches = {{20.f / 30.f, 1}, {40.f / 30.f, 2}, {60.f / 30.f, 0}};
    const std::vector<RigSpaceSwitch> handSwitches{handSwitch};
    for (const float time : {19.f / 30.f, 20.f / 30.f, 21.f / 30.f, 40.f / 30.f, 60.f / 30.f})
        checkNear(spaceRig.evaluate(evaluateRigSpaces(spaceRig, {}, handSwitches, time)).localToWorld(3, {}),
                  {300.f, 400.f, 20.f}, 1e-4f, "timed hand world→body→sword→world no pop");
    RigAnimatedNode animatedSword; animatedSword.id = 2;
    animatedSword.tracks.position.add(0.f, swordSpace.rest.position).add(3.f, swordSpace.rest.position + Vector3{30.f, 0.f, 0.f});
    const std::vector<RigAnimatedNode> spaceAnimation{animatedSword};
    const auto directSpace = spaceRig.evaluate(evaluateRigSpaces(spaceRig, spaceAnimation, handSwitches, 1.97f));
    for (unsigned frame = 0; frame < 197; ++frame)
        (void) evaluateRigSpaces(spaceRig, spaceAnimation, handSwitches, static_cast<float>(frame) / 100.f);
    check(spaceRig.evaluate(evaluateRigSpaces(spaceRig, spaceAnimation, handSwitches, 1.97f)).world == directSpace.world,
          "space switches direct197 equals sequential evaluation");
    auto invalidSwitches = handSwitches; invalidSwitches[0].switches[0].activeSpace = 99;
    rejects([&] { (void) evaluateRigSpaces(spaceRig, {}, invalidSwitches, 1.f); }, "unknown active space rejected");
    RigIKChain matchingChain; matchingChain.joints = {1, 2, 3, 4}; matchingChain.pole = {0.f, 0.f, 4.f};
    Transform fkRoot; fkRoot.rotation.z = .4f;
    Transform fkElbow = flexibleRig.nodes()[1].rest; fkElbow.rotation.z = -.7f;
    const RigPose authoredFK{{1, fkRoot}, {2, fkElbow}};
    const auto matchedIK = flexibleRig.matchIK(matchingChain, authoredFK);
    const auto switchedIK = flexibleRig.evaluate(flexibleRig.solveIK(matchedIK, authoredFK));
    const auto authoredWorld = flexibleRig.evaluate(authoredFK);
    for (std::size_t i = 0; i < authoredWorld.world.size(); ++i)
        checkNear(switchedIK.localToWorld(authoredWorld.ids[i], {}), authoredWorld.localToWorld(authoredWorld.ids[i], {}),
                  1e-4f, "FK→IK matching preserves joint positions");
    const auto matchedFK = flexibleRig.matchWorldPose(switchedIK);
    for (std::size_t i = 0; i < authoredWorld.world.size(); ++i)
        for (unsigned component = 0; component < 16; ++component)
            checkNear(flexibleRig.evaluate(matchedFK).world[i].elements[component], switchedIK.world[i].elements[component],
                      1e-4f, "IK→FK matching preserves full world pose");
    return report();
}
