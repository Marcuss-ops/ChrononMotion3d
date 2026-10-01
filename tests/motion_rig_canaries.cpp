#include "motion_check.hpp"
#include "chrononmotion/motion/Motion.hpp"
#include "chrononmotion/assets/GltfReader.hpp"
#include <array>
#include <cstdio>
#include <string_view>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using namespace chrononmotion_test;
namespace {
    const std::array<const char*, 20> names{{"fk_arm", "two_bone_ik", "eight_bone_snake", "pole_knee", "ik_reach_limits",
        "fk_ik_switch", "space_switch", "aim_turret", "rotation_limit", "stretch_arm", "spline_tail", "secondary_tail",
        "svg_rigid", "svg_puppet", "svg_lattice", "skin_mesh", "pose_mixer", "controller_joystick", "gltf_skeleton", "mega_character"}};
    RigGraph chain(unsigned segments = 2) {
        RigGraph graph;
        for (unsigned i = 0; i <= segments; ++i) {
            RigNode n; n.id = i + 1; n.parent = i; if (i) n.rest.position = {1.f, 0.f, 0.f};
            n.bind = n.rest; graph.nodes.push_back(n);
        }
        return graph;
    }
    void compareWorld(const RigEvaluation& a, const RigEvaluation& b, const char* what) {
        check(a.ids == b.ids, what);
        for (std::size_t i = 0; i < a.world.size(); ++i) for (unsigned c = 0; c < 16; ++c)
            checkNear(a.world[i].elements[c], b.world[i].elements[c], 1e-4f, what);
    }
    RigProgram mega() {
        RigProgram p; p.graph = chain();
        RigNode target; target.id = 4; target.rest.position = {2.f, 0.f, 0.f}; target.bind = target.rest;
        RigNode pole; pole.id = 5; pole.rest.position = {0.f, 0.f, 3.f}; pole.bind = pole.rest;
        RigNode head; head.id = 6; head.kind = RigNodeKind::Controller;
        RigNode object; object.id = 7; object.rest.position = {3.f, 3.f, 0.f}; object.bind = object.rest;
        RigNode badge; badge.id = 8; badge.rest.position = {3.f, 4.f, 0.f}; badge.bind = badge.rest;
        RigNode tail; tail.id = 9;
        RigNode tailTip; tailTip.id = 10; tailTip.parent = 9; tailTip.rest.position = {1.f, 0.f, 0.f}; tailTip.bind = tailTip.rest;
        RigNode energy; energy.id = 11; energy.kind = RigNodeKind::Controller;
        p.graph.nodes.insert(p.graph.nodes.end(), {target, pole, head, object, badge, tail, tailTip, energy});
        RigAnimatedNode t; t.id = 4;
        t.tracks.position.add(0.f, {2.f, 0.f, 0.f}).add(2.f, {2.f, 0.f, 0.f}).add(3.f, {1.f, 1.f, 0.f})
            .add(5.f, {1.f, 1.f, 0.f}).add(6.f, {2.f, 0.f, 0.f}).add(10.f, {2.f, 0.f, 0.f});
        RigAnimatedNode poleAnimation; poleAnimation.id = 5;
        poleAnimation.tracks.position.add(0.f, {0.f, 0.f, 3.f}).add(3.f, {0.f, 0.f, 3.f})
            .add(4.f, {0.f, 0.f, -3.f}).add(5.f, {0.f, 0.f, -3.f}).add(6.f, {0.f, 0.f, 3.f});
        RigAnimatedNode objectAnimation; objectAnimation.id = 7;
        objectAnimation.tracks.position.add(0.f, {3.f, 3.f, 0.f}).add(4.f, {3.f, 3.f, 0.f})
            .add(5.f, {4.f, 3.f, 0.f}).add(6.f, {3.f, 3.f, 0.f});
        p.animation = {t, poleAnimation, objectAnimation};
        RigSpaceSwitch space; space.node = 8; space.spaces = {0, 7}; space.switches = {{4.f, 1}, {6.f, 0}}; p.spaces.push_back(space);
        RigScheduledIK ik; ik.id = 1; ik.chain.joints = {1, 2, 3}; ik.chain.target = {2.f, 0.f, 0.f}; ik.chain.pole = {0.f, 0.f, 3.f};
        ik.targetNode = 4; ik.poleNode = 5;
        ik.amount.add(0.f, 0.f).add(2.f, 0.f).add(3.f, 1.f).add(5.f, 1.f).add(6.f, 0.f); p.ik.push_back(ik);
        RigController controller; controller.id = 6; controller.kind = RigControllerKind::Joystick2D; p.drivers.controllers.push_back(controller);
        RigDriverNode look; look.id = 1; look.kind = RigDriverKind::Track;
        look.track.add(0.f, 0.f).add(1.f, 0.f).add(2.f, .4f).add(3.f, 0.f);
        RigDriverNode bend; bend.id = 2; bend.kind = RigDriverKind::Track;
        bend.track.add(0.f, 0.f).add(6.f, 0.f).add(7.f, 1.f).add(8.f, 0.f);
        RigDriverNode wave; wave.id = 3; wave.kind = RigDriverKind::Track;
        wave.track.add(0.f, 0.f).add(8.f, 0.f).add(9.f, 1.f).add(10.f, 0.f);
        p.drivers.nodes = {look, bend, wave};
        p.drivers.outputs = {{1, 6, RigDrivenProperty::Rotation, 1}, {2, 11, RigDrivenProperty::Position, 0},
                            {3, 6, RigDrivenProperty::Position, 0}};
        RigPoseLayer waving; waving.mode = RigPoseBlendMode::Additive; waving.weight = 0.f;
        Transform lean; lean.rotation.z = .3f; waving.pose = {{6, lean}}; p.poses.push_back(waving);
        RigGeometryBinding body; body.id = 1; body.rigidNode = 6;
        body.source = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 1.f, 0.f}};
        RigDeformer deformation; deformation.kind = RigDeformerKind::Bend; deformation.strength = .3f; deformation.amount = 0.f;
        body.deformers.operators.push_back(deformation); p.geometry.push_back(body);
        RigGeometryBinding hand; hand.id = 2; hand.rigidNode = 3; hand.source = {{0.f, 0.f, 0.f}, {.2f, .1f, 0.f}}; p.geometry.push_back(hand);
        RigGeometryBinding tailShape; tailShape.id = 3; tailShape.rigidNode = 10; tailShape.source = {{0.f, 0.f, 0.f}, {.3f, 0.f, 0.f}}; p.geometry.push_back(tailShape);
        p.weights = {{11, RigWeightKind::Deformer, 1, 0}, {6, RigWeightKind::Pose, 0, 0}};
        const auto raw = p.graph.compile(); RigSecondaryMotion secondary; secondary.joints = {9, 10};
        secondary.gravity = {0.f, -1.f}; secondary.stiffness = 10.f; secondary.damping = .15f; secondary.maxAngle = .5f;
        const auto samples = bakeRigSecondaryMotion(raw, secondary, 0, 60, 30.f);
        p.secondary.resize(301); p.secondaryFps = 30.f;
        for (unsigned frame = 0; frame <= 300; ++frame) {
            const unsigned sample = frame < 210 ? 0 : std::min(frame - 210, 60U);
            Transform rootPose = samples[sample][8].local;
            Transform tipPose = samples[sample][9].local;
            if (frame > 270) {
                const float weight = static_cast<float>(frame - 270) / 30.f;
                rootPose = Transform::blend(rootPose, raw.nodes()[8].rest, weight);
                tipPose = Transform::blend(tipPose, raw.nodes()[9].rest, weight);
            }
            p.secondary[frame] = {{9, rootPose}, {10, tipPose}};
        }
        return p;
    }
    void run(unsigned number) {
        section(names[number - 1]);
        const auto rig = chain(number == 3 ? 8 : 2).compile();
        if (number == 1) {
            Transform root; root.rotation.z = 1.57079632679f;
            checkNear(rig.evaluate(RigPose{{1, root}}).localToWorld(3, {}), {0.f, 2.f, 0.f}, 1e-5f, "FK hierarchy follows parent");
        } else if (number >= 2 && number <= 5) {
            RigIKChain ik; for (const auto& node : rig.nodes()) ik.joints.push_back(node.id);
            const float reach = static_cast<float>(rig.nodes().size() - 1);
            ik.target = {reach * .6f, .5f, 0.f}; ik.pole = {0.f, 0.f, 3.f};
            if (number == 5) ik.target = {reach + 5.f, 0.f, 0.f};
            const auto pose = rig.evaluate(rig.solveIK(ik));
            checkNear(pose.localToWorld(ik.joints.back(), {}), number == 5 ? Vector3{reach, 0.f, 0.f} : ik.target, 1e-4f, "IK reach contract");
            if (number == 4) {
                ik.pole.z = -3.f; const auto opposite = rig.evaluate(rig.solveIK(ik));
                check(pose.localToWorld(2, {}).z * opposite.localToWorld(2, {}).z < 0.f, "pole changes knee side");
            }
        } else if (number == 6) {
            RigIKChain ik; ik.joints = {1, 2, 3}; ik.pole = {0.f, 0.f, 3.f};
            Transform elbow = rig.nodes()[1].rest; elbow.rotation.z = .5f; RigPose fk{{2, elbow}};
            const auto matched = rig.matchIK(ik, fk); const auto solved = rig.evaluate(rig.solveIK(matched, fk));
            compareWorld(solved, rig.evaluate(fk), "FK→IK no pop");
            compareWorld(solved, rig.evaluate(rig.matchWorldPose(solved)), "IK→FK no pop");
        } else if (number == 7) {
            RigSpaceSwitch space; space.node = 3; space.spaces = {0, 1}; space.switches = {{20.f / 30.f, 0}, {1.f, 1}};
            const std::vector<RigSpaceSwitch> switches{space};
            compareWorld(rig.evaluate(evaluateRigSpaces(rig, {}, switches, 20.f / 30.f)), rig.evaluate(), "SpaceSwitch no pop");
        } else if (number >= 8 && number <= 10) {
            Transform t; ConstraintSpec c;
            c.kind = number == 8 ? ConstraintKind::Aim : number == 9 ? ConstraintKind::LimitRotation : ConstraintKind::StretchTo;
            if (number == 9) { t.rotation.z = 2.f; c.minimum = {-.2f, -.2f, -.2f}; c.maximum = {.2f, .2f, .2f}; }
            check(applyConstraint(c, t, {0.f, 0.f, 3.f}, Quaternion{}, {1.f, 1.f, 1.f}, 0.f), "constraint executes");
            if (number == 9) checkNear(t.rotation.z, .2f, 0.f, "rotation bounded");
            if (number == 10) checkNear(t.applyPoint({0.f, 0.f, 1.f}), {0.f, 0.f, 3.f}, 1e-5f, "stretch endpoint");
        } else if (number == 11) {
            RigSplineIK s; s.joints = {1, 2, 3}; s.path = std::make_shared<MotionPath>(MotionPath::line({}, {0.f, 2.f, 0.f}));
            checkNear(rig.evaluate(solveRigSplineIK(rig, s)).localToWorld(3, {}), {0.f, 2.f, 0.f}, 1e-5f, "SplineIK endpoint");
        } else if (number == 12) {
            RigSecondaryMotion s; s.joints = {1, 2, 3}; s.gravity = {0.f, -1.f};
            const auto bake = bakeRigSecondaryMotion(rig, s, 0, 60, 30.f);
            check(rig.evaluate(bake.back()).localToWorld(3, {}).y < 0.f, "secondary tail responds to gravity");
        } else if (number >= 13 && number <= 16) {
            RigProgram p; p.graph = chain(); RigGeometryBinding g; g.id = 1; g.source = {{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}};
            if (number == 13) g.rigidNode = 3;
            if (number == 14) { RigDeformer d; d.kind = RigDeformerKind::PinWarp; RigPin pin; pin.target = {0.f, 2.f, 0.f}; d.pins.push_back(pin); g.deformers.operators.push_back(d); }
            if (number == 15) { RigDeformer d; d.kind = RigDeformerKind::Lattice; d.lattice.offsets.resize(16, {0.f, 1.f, 0.f}); g.deformers.operators.push_back(d); }
            if (number == 16) { g.skin.resize(3); for (auto& vertex : g.skin) { vertex.joints[0] = 1; vertex.weights[0] = 1.f; } }
            p.geometry.push_back(g); const auto frame = RigProgramEvaluator(p).evaluate(0, 30.f);
            checkNear(frame.geometry[0].positions[0], number == 13 ? Vector3{2.f, 0.f, 0.f} : number == 14 ? Vector3{0.f, 2.f, 0.f} :
                      number == 15 ? Vector3{0.f, 1.f, 0.f} : Vector3{}, 1e-5f, "supplied SVG/mesh geometry binding contract");
        } else if (number == 17) {
            RigPoseLayer p; Transform transform; transform.position.x = 2.f; p.pose = {{1, transform}}; p.weight = .5f;
            checkNear(mixRigPoses(rig, {}, std::vector<RigPoseLayer>{p})[0].local.position.x, 1.f, 0.f, "pose midpoint");
        } else if (number == 18) {
            auto graph = chain(); graph.nodes[0].kind = RigNodeKind::Controller;
            RigDriverGraph d; RigController c; c.id = 1; c.kind = RigControllerKind::Joystick2D; d.controllers.push_back(c);
            RigDriverNode n; n.id = 1; n.kind = RigDriverKind::Controller; n.controller = 1; n.component = 1; d.nodes.push_back(n);
            d.outputs.push_back({1, 2, RigDrivenProperty::Rotation, 2});
            checkNear(RigDriverEvaluator(graph.compile(), d).evaluate(0.f, std::vector<RigControllerValue>{{1, {0.f, .5f, 0.f}}})[1].local.rotation.z,
                      .5f, 0.f, "joystick maps typed component to bone");
        } else if (number == 19) {
            const auto asset = loadGltfMotion(R"({"asset":{"version":"2.0"},"buffers":[],"bufferViews":[],"accessors":[],"nodes":[{"children":[1]},{"translation":[1,0,0]}],"skins":[{"joints":[0,1],"skeleton":0}]})");
            check(asset.skins.size() == 1 && asset.skins[0].joints.size() == 2, "glTF skeleton and joints imported");
            checkNear(asset.graph.compile().evaluate().localToWorld(2, {}), {1.f, 0.f, 0.f}, 0.f, "glTF skeleton hierarchy");
        } else {
            const RigProgramEvaluator evaluator(mega());
            const auto a = evaluator.bake(0, 300, 30.f), b = evaluator.bake(0, 300, 30.f);
            check(a.signature == b.signature, "Mega Rig pose/track/geometry repeat signature");
            const auto direct = rigFrameSignature(evaluator.evaluate(197, 30.f));
            for (int frame = 0; frame <= 300; ++frame) {
                const auto& pose = a.frames[frame].pose;
                checkNear(pose.localToWorld(1, {}).distanceTo(pose.localToWorld(2, {})), 1.f, 1e-4f, "Mega upper bone length stable");
                checkNear(pose.localToWorld(2, {}).distanceTo(pose.localToWorld(3, {})), 1.f, 1e-4f, "Mega lower bone length stable");
                check(rigFrameSignature(evaluator.evaluate(frame, 30.f)) == rigFrameSignature(a.frames[frame]), "Mega random access equals bake");
            }
            check(direct == rigFrameSignature(a.frames[197]), "Mega direct197 matches sequential");
            compareWorld(a.frames.front().pose, a.frames.back().pose, "Mega returns neutral without drift");
            check(a.frames[60].pose.local[a.frames[60].pose.index(6)].rotation.y > .3f, "1–2s controller moves head");
            checkNear(a.frames[90].pose.localToWorld(3, {}), {1.f, 1.f, 0.f}, 1e-4f, "2–3s arm follows IK");
            check(a.frames[210].geometry[0].positions != a.frames[180].geometry[0].positions, "6–7s body bends");
            check(a.frames[240].pose.localToWorld(10, {}).y < 0.f, "7–8s secondary tail moves");
            check(a.frames[270].pose.local[a.frames[270].pose.index(6)].localQuaternion().angleTo(Quaternion{}) > .2f, "8–9s additive wave");
        }
    }
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--list") {
        for (unsigned i = 0; i < names.size(); ++i) std::printf("%02u %s\n", i + 1, names[i]);
        return 0;
    }
    if (argc == 3 && std::string_view(argv[1]) == "--case") {
        for (unsigned i = 0; i < names.size(); ++i) if (std::string_view(argv[2]) == names[i]) { run(i + 1); return report(); }
        std::fprintf(stderr, "unknown rig canary\n"); return 2;
    }
    if (argc != 1 && !(argc == 2 && std::string_view(argv[1]) == "--all")) return 2;
    for (unsigned i = 1; i <= names.size(); ++i) run(i);
    return report();
}
