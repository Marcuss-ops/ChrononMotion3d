#include "motion_authoring_shared.hpp"

namespace chrononmotion_test::authoring {
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
}
