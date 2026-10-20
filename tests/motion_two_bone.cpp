// Analytic two-bone IK: the solver contract and its RigGraph integration.
//
// Each check pins one defining property rather than a sampled pose: lengths are
// never changed, the pole picks the bend side, a joint limit makes the effector
// fall short instead of breaking the joint, and a partial weight keeps both
// bones at their authored length. The integration checks prove that the Fabrik
// path is untouched and that a TwoBone chain reaches through `solveIK`.

#include "motion_check.hpp"
#include "chrononmotion/motion/Motion.hpp"

#include <cmath>
#include <stdexcept>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using namespace chrononmotion_test;

namespace {

    template<class F>
    void rejects(F&& action, const char* what) {
        bool rejected = false;
        try {
            action();
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        check(rejected, what);
    }

    TwoBoneIKSpec specFor(const Vector3& target, const Vector3& pole = Vector3(0.f, 1.f, 0.f)) {
        TwoBoneIKSpec spec;
        spec.target = target;
        spec.pole = pole;
        return spec;
    }

    float segmentLength(const TwoBoneIKResult& r) {
        return r.start.distanceTo(r.mid) + r.mid.distanceTo(r.end);
    }

    void testReachable() {
        section("TwoBone / reachable target");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);
        const auto result = solveTwoBoneIK(start, mid, end, specFor(Vector3(1.f, 0.8f, 0.f)));

        check(result.reached, "an in-reach target is reported reached");
        checkNear(result.end, Vector3(1.f, 0.8f, 0.f), 1e-4f, "the end joint lands on the target");
        checkNear(start.distanceTo(result.mid), 1.f, 1e-4f, "the upper bone keeps its length");
        checkNear(result.mid.distanceTo(result.end), 1.f, 1e-4f, "the lower bone keeps its length");
        // The pole is +Y, so the middle joint must bend toward +Y.
        check(result.mid.y > 0.f, "the pole selects the bend side");
    }

    void testPoleSide() {
        section("TwoBone / pole side");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);
        const Vector3 target(0.8f, 0.f, 0.f);

        const auto up = solveTwoBoneIK(start, mid, end, specFor(target, Vector3(0.f, 1.f, 0.f)));
        const auto down = solveTwoBoneIK(start, mid, end, specFor(target, Vector3(0.f, -1.f, 0.f)));

        check(up.mid.y > 0.f, "a +Y pole bends the middle joint to +Y");
        check(down.mid.y < 0.f, "a -Y pole bends the middle joint to -Y");
        checkNear(up.midAngle, down.midAngle, 1e-5f, "the bend side does not change the mid angle");
    }

    void testUnreachable() {
        section("TwoBone / unreachable target");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);
        const auto result = solveTwoBoneIK(start, mid, end, specFor(Vector3(10.f, 0.f, 0.f)));

        check(!result.reached, "an out-of-reach target is not reported reached");
        checkNear(result.end.distanceTo(start), 2.f, 1e-4f, "the chain straightens to its full length");
        checkNear(result.midAngle, 3.14159265f, 1e-3f, "a straightened chain reports a straight mid angle");
    }

    void testMinAngleLimit() {
        section("TwoBone / minimum mid angle");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);

        auto spec = specFor(Vector3(0.2f, 0.f, 0.f));
        spec.minMidAngle = 1.2f;
        const auto result = solveTwoBoneIK(start, mid, end, spec);

        check(result.midAngle >= 1.2f - 1e-3f, "the knee cannot fold past the minimum angle");
        check(!result.reached, "a limited solve does not claim the target was reached");
        check(result.end.x > 1.f, "the effector falls short of the target instead of shrinking the bone");
        checkNear(start.distanceTo(result.mid), 1.f, 1e-4f, "the limit does not change the upper bone length");
    }

    void testMaxAngleLimit() {
        section("TwoBone / maximum mid angle");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);

        auto spec = specFor(Vector3(2.f, 0.f, 0.f));
        spec.maxMidAngle = 2.5f;
        const auto result = solveTwoBoneIK(start, mid, end, spec);

        check(result.midAngle <= 2.5f + 1e-3f, "the chain cannot open past the maximum angle");
        check(!result.reached, "an opened chain that falls short is not reached");
    }

    void testTwist() {
        section("TwoBone / twist");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);
        const Vector3 target(0.8f, 0.f, 0.f);

        auto spec = specFor(target, Vector3(0.f, 1.f, 0.f));
        const auto plain = solveTwoBoneIK(start, mid, end, spec);
        spec.twist = 1.57079633f;
        const auto twisted = solveTwoBoneIK(start, mid, end, spec);

        checkNear(plain.midAngle, twisted.midAngle, 1e-4f, "twist does not change the mid angle");
        checkNear(plain.end, twisted.end, 1e-5f, "twist leaves the end joint in place");
        check(std::fabs(twisted.mid.z) > 1e-3f, "twist moves the middle joint out of the original plane");
        checkNear(start.distanceTo(twisted.mid), 1.f, 1e-4f, "twist does not change the upper bone length");
    }

    void testWeight() {
        section("TwoBone / weight blending");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);
        const Vector3 target(0.8f, 0.f, 0.f);

        auto zero = specFor(target);
        zero.weight = 0.f;
        const auto authored = solveTwoBoneIK(start, mid, end, zero);
        checkNear(authored.mid, mid, 1e-5f, "weight 0 keeps the authored middle joint");
        checkNear(authored.end, end, 1e-5f, "weight 0 keeps the authored end joint");
        check(!authored.reached, "weight 0 does not claim a solve");

        auto half = specFor(target);
        half.weight = 0.5f;
        const auto blended = solveTwoBoneIK(start, mid, end, half);
        checkNear(start.distanceTo(blended.mid), 1.f, 1e-4f, "a partial solve keeps the upper bone length");
        checkNear(blended.mid.distanceTo(blended.end), 1.f, 1e-4f, "a partial solve keeps the lower bone length");
        checkNear(segmentLength(blended), 2.f, 1e-4f, "a partial solve never shortens the limb");
        check(blended.mid.y > authored.mid.y && blended.mid.y < solveTwoBoneIK(start, mid, end, specFor(target)).mid.y,
              "a partial solve sits between the authored and solved poses");
    }

    void testDeterminism() {
        section("TwoBone / determinism");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);
        auto spec = specFor(Vector3(1.f, 0.8f, 0.3f));
        spec.twist = 0.4f;
        const auto a = solveTwoBoneIK(start, mid, end, spec);
        const auto b = solveTwoBoneIK(start, mid, end, spec);
        check(a.mid.x == b.mid.x && a.mid.y == b.mid.y && a.mid.z == b.mid.z,
              "the middle joint is bit-identical across calls");
        check(a.end.x == b.end.x && a.end.y == b.end.y && a.end.z == b.end.z,
              "the end joint is bit-identical across calls");
        check(a.midAngle == b.midAngle && a.reached == b.reached,
              "the reported facts are bit-identical across calls");
    }

    void testValidation() {
        section("TwoBone / validation");
        const Vector3 start(0.f, 0.f, 0.f);
        const Vector3 mid(1.f, 0.f, 0.f);
        const Vector3 end(2.f, 0.f, 0.f);

        rejects([&] { (void) solveTwoBoneIK(start, start, end, specFor(Vector3(1.f, 1.f, 0.f))); },
                "a zero-length upper bone is rejected");
        rejects([&] { (void) solveTwoBoneIK(start, mid, mid, specFor(Vector3(1.f, 1.f, 0.f))); },
                "a zero-length lower bone is rejected");

        auto badWeight = specFor(Vector3(1.f, 1.f, 0.f));
        badWeight.weight = 1.5f;
        rejects([&] { (void) solveTwoBoneIK(start, mid, end, badWeight); }, "a weight above 1 is rejected");

        auto badLimits = specFor(Vector3(1.f, 1.f, 0.f));
        badLimits.minMidAngle = 2.f;
        badLimits.maxMidAngle = 1.f;
        rejects([&] { (void) solveTwoBoneIK(start, mid, end, badLimits); }, "min > max is rejected");

        auto badRange = specFor(Vector3(1.f, 1.f, 0.f));
        badRange.maxMidAngle = 4.f;
        rejects([&] { (void) solveTwoBoneIK(start, mid, end, badRange); }, "a limit above pi is rejected");
    }

    RigGraph chainGraph() {
        RigNode root;
        root.id = 1;
        root.rest.position = Vector3(0.f, 0.f, 0.f);
        root.bind = root.rest;

        RigNode knee;
        knee.id = 2;
        knee.parent = 1;
        knee.rest.position = Vector3(1.f, 0.f, 0.f);
        knee.bind = knee.rest;

        RigNode ankle;
        ankle.id = 3;
        ankle.parent = 2;
        ankle.rest.position = Vector3(1.f, 0.f, 0.f);
        ankle.bind = ankle.rest;

        RigGraph graph;
        graph.nodes = {root, knee, ankle};
        return graph;
    }

    void testGraphIntegration() {
        section("TwoBone / RigGraph integration");
        const auto evaluator = chainGraph().compile();

        RigIKChain chain;
        chain.joints = {1, 2, 3};
        chain.target = Vector3(0.6f, 0.5f, 0.f);
        chain.pole = Vector3(0.f, 1.f, 0.f);
        chain.solver = RigIKSolverKind::TwoBone;

        const auto solved = evaluator.evaluate(evaluator.solveIK(chain));
        checkNear(solved.localToWorld(3, {}), chain.target, 1e-4f, "a TwoBone chain reaches its target");
        checkNear(solved.localToWorld(1, {}).distanceTo(solved.localToWorld(2, {})), 1.f, 1e-4f,
                  "the solved chain keeps the upper bone length");

        // The same graph still solves through Fabrik: the new solver is opt-in.
        chain.solver = RigIKSolverKind::Fabrik;
        const auto fabrik = evaluator.evaluate(evaluator.solveIK(chain));
        checkNear(fabrik.localToWorld(3, {}), chain.target, 1e-3f, "the Fabrik path still reaches its target");

        // `RigIKChain::pole` is a world point, not a direction: a pole above the
        // chain root must bend the middle joint up and one below must bend it
        // down, with the same target.
        RigIKChain up = chain;
        up.solver = RigIKSolverKind::TwoBone;
        up.target = Vector3(0.6f, 0.2f, 0.f);
        up.pole = Vector3(0.f, 1.5f, 0.f);
        RigIKChain down = up;
        down.pole = Vector3(0.f, -1.5f, 0.f);
        check(evaluator.evaluate(evaluator.solveIK(up)).localToWorld(2, {}).y > 0.f,
              "a pole point above the root bends the middle joint up");
        check(evaluator.evaluate(evaluator.solveIK(down)).localToWorld(2, {}).y < 0.f,
              "a pole point below the root bends the middle joint down");

        // A limited TwoBone chain folds short of a target inside its limit.
        RigIKChain limited = chain;
        limited.solver = RigIKSolverKind::TwoBone;
        limited.target = Vector3(0.2f, 0.f, 0.f);
        limited.minMidAngle = 1.2f;
        const auto clamped = evaluator.evaluate(evaluator.solveIK(limited));
        check(clamped.localToWorld(3, {}).x > 1.f, "the limited chain stops at the joint limit, not at the target");
        checkNear(clamped.localToWorld(1, {}).distanceTo(clamped.localToWorld(2, {})), 1.f, 1e-4f,
                  "the limited chain still carries full-length bones");
    }

    void testGraphRejectsWrongChain() {
        section("TwoBone / RigGraph chain size");
        const auto evaluator = chainGraph().compile();
        RigIKChain chain;
        chain.joints = {1, 2};
        // Two joints with a TwoBone solver is a chain the closed-form solve
        // cannot honour; the graph must say so rather than pick a solver.
        chain.target = Vector3(1.f, 0.5f, 0.f);
        chain.solver = RigIKSolverKind::TwoBone;
        rejects([&] { (void) evaluator.solveIK(chain); }, "a TwoBone chain with two joints is rejected");
    }

}// namespace

int main() {
    testReachable();
    testPoleSide();
    testUnreachable();
    testMinAngleLimit();
    testMaxAngleLimit();
    testTwist();
    testWeight();
    testDeterminism();
    testValidation();
    testGraphIntegration();
    testGraphRejectsWrongChain();
    return report();
}
