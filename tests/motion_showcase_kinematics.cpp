#include "motion_showcase_shared.hpp"

namespace chrononmotion_test::showcase {
void addKinematicsCases(std::vector<ShowcaseCase>& cases) {
    const auto add = [&](unsigned number, const char* id, const char* description, std::function<void(Checks&, Snapshot&)> run) {
        cases.push_back({number, id, description, {}, std::move(run)});
    };
    add(16, "two_bone_arm", "two-segment IK follows a circular reachable target", [](Checks& c, Snapshot& s) {
        for (int frame = 0; frame <= 60; ++frame) {
            const float angle = static_cast<float>(frame) / 60.f * 2.f * kPi;
            const Vector2 target(1.4f * std::cos(angle), 1.4f * std::sin(angle));
            const auto solved = solveTwoSegmentIK2D({}, target, 1.f, 1.f);
            c.that(solved.targetReachable, "circular arm target remains reachable");
            c.near(solved.end, target, 1e-5f, "arm endpoint meets its target");
            c.near(solved.elbow.distanceTo({}), 1.f, 1e-5f, "upper bone retains its length");
            c.near(solved.end.distanceTo(solved.elbow), 1.f, 1e-5f, "lower bone retains its length");
            s.vector(Vector3(solved.elbow.x, solved.elbow.y, 0.f));
        }
    });

    add(17, "ik_reach_limits", "2D IK clamps inner and outer unreachable targets", [](Checks& c, Snapshot& s) {
        const auto far = solveTwoSegmentIK2D({}, {8.f, 0.f}, 2.f, 1.f);
        const auto near = solveTwoSegmentIK2D({}, {.1f, 0.f}, 2.f, 1.f);
        const auto maxEdge = solveTwoSegmentIK2D({}, {3.f, 0.f}, 2.f, 1.f);
        const auto minEdge = solveTwoSegmentIK2D({}, {1.f, 0.f}, 2.f, 1.f);
        c.that(!far.targetReachable && !near.targetReachable, "targets outside either side of the reachable annulus are reported");
        c.near(far.end.distanceTo({}), 3.f, 1e-5f, "outer target clamps to total limb length");
        c.near(near.end.distanceTo({}), 1.f, 1e-5f, "inner target clamps to the minimum limb radius");
        for (const auto* solved : {&far, &near, &maxEdge, &minEdge}) {
            c.near(solved->elbow.distanceTo({}), 2.f, 1e-5f, "upper segment length survives reach clamping");
            c.near(solved->end.distanceTo(solved->elbow), 1.f, 1e-5f,
                   "lower segment length survives reach clamping");
        }
        c.that(maxEdge.targetReachable && minEdge.targetReachable, "both annulus boundaries are reachable");
        s.vector(Vector3(far.end.x, far.end.y, 0.f)); s.vector(Vector3(near.end.x, near.end.y, 0.f));
    });

    add(18, "general_ik_snake", "eight-segment FABRIK chain tracks a moving reachable target", [](Checks& c, Snapshot& s) {
        IK3BakeSpec spec;
        for (int i = 0; i <= 8; ++i) spec.initialJoints.emplace_back(static_cast<float>(i), 0.f, 0.f);
        spec.pole = {4.f, 0.f, 8.f}; spec.fps = 30.f; spec.iterations = 64; spec.tolerance = 1e-5f;
        spec.targetTrack.add(0.f, {8.f, 0.f, 0.f}).add(1.f, {5.f, 4.f, 0.f});
        const auto baked = bakeIKChain3D(spec, 0, 30);
        c.that(baked.samples.size() == 31 && baked.jointPositions.size() == 9,
               "general IK bakes every joint in the eight-segment chain");
        for (std::size_t frame = 0; frame < baked.samples.size(); ++frame) {
            const auto& pose = baked.samples[frame];
            c.near(pose.front(), spec.initialJoints.front(), 1e-6f, "snake root stays pinned");
            for (std::size_t joint = 1; joint < pose.size(); ++joint)
                c.near(pose[joint].distanceTo(pose[joint - 1]), 1.f, 3e-3f, "snake segment length remains one unit");
            c.that(baked.targetReachable[frame], "snake target remains within total chain reach");
            c.near(pose.back(), spec.targetTrack.sample(secondsAt(static_cast<int>(frame))), 3e-3f,
                   "snake end effector converges to the sampled target");
        }
        s.vector(baked.samples.back().back());
    });

    add(19, "pole_vector_knee", "pole position selects consistent opposite knee planes", [](Checks& c, Snapshot& s) {
        const std::array<Vector3, 3> rest{{{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {2.f, 0.f, 0.f}}};
        float positive = 0.f, negative = 0.f;
        for (int i = 0; i < 11; ++i) {
            const Vector3 target(1.f + .2f * static_cast<float>(i) / 10.f, .7f, 0.f);
            const auto a = solveIKChain3D(rest, target, {0.f, 0.f, 3.f});
            const auto b = solveIKChain3D(rest, target, {0.f, 0.f, -3.f});
            positive += a.joints[1].z;
            negative += b.joints[1].z;
            c.that(a.joints[1].z > 0.f && b.joints[1].z < 0.f,
                   "each solved knee stays on its requested pole side");
            s.vector(a.joints[1]); s.vector(b.joints[1]);
        }
        c.that(positive > 0.f && negative < 0.f, "pole vector holds the knee on the requested side");
    });

    add(20, "space_switch", "hand switches world/torso/object space without a pop", [](Checks& c, Snapshot& s) {
        MotionScene scene("20_space_switch", kFps);
        Layer& torso = scene.addNull(1, "torso");
        torso.tracks.position.add(0.f, {}).add(1.f, {10.f, 0.f, 0.f});
        Layer& object = scene.addNull(2, "object");
        object.tracks.position.add(0.f, {}).add(1.f, {0.f, 10.f, 0.f});
        Layer& hand = addCard(scene, 3, "hand", {2.f, 0.f, 0.f});
        hand.spaceSwitches = {{.5f, 1}, {.75f, 2}};
        scene.evaluatePtsExact(.49f);
        const Matrix4 beforeTorsoHandoff = scene.worldMatrix(3);
        scene.evaluatePtsExact(.5f);
        const Vector3 worldAtTorsoHandoff = worldPosition(scene, 3);
        c.near(worldAtTorsoHandoff, Vector3(2.f, 0.f, 0.f), 1e-4f, "first handoff preserves world position");
        for (std::size_t element = 0; element < beforeTorsoHandoff.elements.size(); ++element)
            c.near(scene.worldMatrix(3).elements[element], beforeTorsoHandoff.elements[element], .011f,
                   "first handoff preserves every world-matrix component");
        scene.evaluatePtsExact(.749f);
        const Matrix4 immediatelyBeforeObject = scene.worldMatrix(3);
        scene.evaluatePtsExact(.75f);
        for (std::size_t element = 0; element < immediatelyBeforeObject.elements.size(); ++element)
            c.near(scene.worldMatrix(3).elements[element], immediatelyBeforeObject.elements[element], .011f,
                   "second handoff preserves every world-matrix component");
        scene.evaluatePtsExact(1.f);
        c.near(worldPosition(scene, 3), Vector3(4.5f, 2.5f, 0.f), 1e-4f,
               "hand follows its final object space with maintained offset");
        capture(scene, s, {0.f, .5f, .75f, 1.f});
    });

    add(21, "aim_turret", "Aim constraint tracks a moving target", [](Checks& c, Snapshot& s) {
        MotionScene scene("21_aim_turret", kFps);
        addCard(scene, 1, "target", {3.f, 4.f, 1.f}).tracks.position.add(0.f, {3.f, 4.f, 1.f}).add(1.f, {-2.f, 5.f, 1.f});
        Layer& turret = addCard(scene, 2, "turret");
        ConstraintSpec aim; aim.kind = ConstraintKind::Aim; aim.targetId = 1;
        turret.constraints.push_back(aim);
        for (const float time : {0.f, .25f, .5f, .75f, 1.f}) {
            scene.evaluatePtsExact(time);
            Vector3 desired = worldPosition(scene, 1) - worldPosition(scene, 2); desired.normalize();
            Vector3 actual = Vector3::Z(); actual.applyQuaternion(scene.state(2).local.orientation);
            c.near(actual, desired, 2e-4f, "turret local +Z aims at the target");
            s.vector(actual);
        }
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(22, "billboard_cards", "billboard orientation faces the camera position", [](Checks& c, Snapshot& s) {
        MotionScene scene("22_billboard_cards", kFps);
        addCard(scene, 1, "camera", {0.f, 0.f, 10.f});
        Layer& card = addCard(scene, 2, "billboard", {2.f, 1.f, 0.f});
        ConstraintSpec billboard; billboard.kind = ConstraintKind::Billboard; billboard.targetId = 1;
        card.constraints.push_back(billboard);
        scene.evaluate(0);
        Vector3 expected = worldPosition(scene, 1) - worldPosition(scene, 2); expected.normalize();
        Vector3 facing = Vector3::Z(); facing.applyQuaternion(scene.state(2).local.orientation);
        c.near(facing, expected, 2e-4f, "billboard normal points toward the camera");
        capture(scene, s, {0.f});
    });

    add(23, "keep_distance_orbit", "follower preserves target radius while target moves", [](Checks& c, Snapshot& s) {
        MotionScene scene("23_keep_distance_orbit", kFps);
        Layer& target = addCard(scene, 1, "orbit_target");
        target.tracks.position.add(0.f, {}).add(1.f, {5.f, 4.f, 0.f});
        Layer& satellite = addCard(scene, 2, "satellite", {5.f, 0.f, 0.f});
        ConstraintSpec keep; keep.kind = ConstraintKind::KeepDistance; keep.targetId = 1; keep.distance = 5.f;
        satellite.constraints.push_back(keep);
        for (const float time : {0.f, .2f, .4f, .6f, .8f, 1.f}) {
            scene.evaluatePtsExact(time);
            c.near(worldPosition(scene, 2).distanceTo(worldPosition(scene, 1)), 5.f, 2e-4f,
                   "satellite remains at the authored distance");
            s.vector(worldPosition(scene, 2));
        }
        capture(scene, s, {0.f, .5f, 1.f});
    });

    add(24, "constraint_blend", "Aim constraint blends continuously from free motion to target", [](Checks& c, Snapshot& s) {
        MotionScene scene("24_constraint_blend", kFps);
        addCard(scene, 1, "aim_target", {0.f, 10.f, 0.f});
        Layer& follower = addCard(scene, 2, "aim_follower");
        ConstraintSpec aim; aim.kind = ConstraintKind::Aim; aim.targetId = 1;
        aim.amountTrack = std::make_shared<Track<float>>();
        aim.amountTrack->add(0.f, 0.f).add(1.f, 1.f);
        follower.constraints.push_back(aim);
        scene.evaluatePtsExact(0.f);
        const Quaternion free = scene.state(2).local.orientation;
        scene.evaluatePtsExact(.5f);
        const Quaternion halfway = scene.state(2).local.orientation;
        scene.evaluatePtsExact(1.f);
        const Quaternion aimed = scene.state(2).local.orientation;
        c.near(aimed.angleTo(free), kPi * .5f, 2e-4f, "amount 1 reaches the fully aimed orientation");
        c.near(halfway.angleTo(free), kPi * .25f, 2e-4f, "amount .5 slerps halfway from free motion");
        scene.evaluatePtsExact(.499f);
        const Quaternion before = scene.state(2).local.orientation;
        scene.evaluatePtsExact(.501f);
        c.that(before.angleTo(scene.state(2).local.orientation) < .01f, "animated influence crosses its midpoint without a pop");
        capture(scene, s, {0.f, .25f, .5f, .75f, 1.f});
    });
}
} // namespace chrononmotion_test::showcase
