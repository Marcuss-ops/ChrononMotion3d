// ChrononMotion — FK retarget tests: verbatim rotations, length-ratio
// position scaling, bind fallback, and fail-closed validation.

#include "motion_check.hpp"

#include "chrononmotion/motion/Motion.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace {

    using namespace chrononmotion;
    using namespace chrononmotion::motion;
    using chrononmotion_test::check;
    using chrononmotion_test::checkNear;

    RigNode bone(RigNodeId id, RigNodeId parent, float length) {
        RigNode node;
        node.id = id;
        node.parent = parent;
        node.kind = RigNodeKind::Bone;
        node.length = length;
        return node;
    }

    RigEvaluator sourceRig() {
        RigGraph graph;
        graph.nodes.push_back(bone(1, 0, 2.f));
        graph.nodes.push_back(bone(2, 1, 4.f));
        graph.nodes.push_back(bone(3, 2, 1.f));
        return graph.compile();
    }

    RigEvaluator targetRig() {
        RigGraph graph;
        graph.nodes.push_back(bone(10, 0, 1.f));
        graph.nodes.push_back(bone(20, 10, 2.f));
        graph.nodes.push_back(bone(30, 20, 0.5f));
        graph.nodes.push_back(bone(40, 20, 0.5f));
        return graph.compile();
    }

    std::vector<RetargetPair> fullMap() {
        return {{1, 10}, {2, 20}, {3, 30}};
    }

    RigPoseValue posed(RigNodeId id, const Vector3& position, const Quaternion& orientation) {
        RigPoseValue value;
        value.id = id;
        value.local.position = position;
        value.local.orientation = orientation;
        return value;
    }

    bool throwsInvalid(const RigEvaluator& source, const std::vector<RigPoseValue>& pose,
                       const RigEvaluator& target, const std::vector<RetargetPair>& map,
                       const RetargetOptions& options = RetargetOptions{}) {
        // NB: spans do not construct from braced lists, so every call site
        // names its vectors; that is also why the API takes spans (any
        // caller-side storage works) rather than vectors.
        try {
            (void)retargetPose(source, pose, target, map, options);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    }

    const Transform& localOf(const RigPose& pose, RigNodeId id) {
        for (const auto& value : pose)
            if (value.id == id) return value.local;
        throw std::invalid_argument("test: retarget dropped a joint");
    }

    void transfer() {
        chrononmotion_test::section("retarget transfer");
        const RigEvaluator source = sourceRig();
        const RigEvaluator target = targetRig();
        const Quaternion turn(0.f, 0.70710678f, 0.f, 0.70710678f);
        const std::vector<RigPoseValue> pose = {posed(1, Vector3(0.f, 10.f, 0.f), Quaternion()),
                                                posed(2, Vector3(0.f, 4.f, 0.f), turn),
                                                posed(3, Vector3(0.f, 1.f, 0.f), Quaternion())};

        const RigPose result = retargetPose(source, pose, target, fullMap());
        check(result.size() == 4, "retarget covers every target joint");

        // Rotations copy verbatim; offsets scale by the bind-length ratio.
        checkNear(localOf(result, 20).orientation.y, turn.y, 1e-6f, "joint rotation copies verbatim");
        checkNear(localOf(result, 20).position, Vector3(0.f, 2.f, 0.f), 1e-5f,
                  "joint offset scales by the length ratio");
        checkNear(localOf(result, 30).position, Vector3(0.f, 0.5f, 0.f), 1e-5f,
                  "tip offset scales by the length ratio");
        // Root offsets take the root scale (identity here).
        checkNear(localOf(result, 10).position, Vector3(0.f, 10.f, 0.f), 1e-5f,
                  "root offset takes the root scale");

        // Unmapped joints keep their bind pose.
        checkNear(localOf(result, 40).position, Vector3(0.f, 0.f, 0.f), 1e-6f,
                  "unmapped joint keeps its bind pose");

        // A joint missing from the pose reads its source bind, then scales.
        const std::vector<RigPoseValue> sparse = {posed(1, Vector3(0.f, 8.f, 0.f), Quaternion()),
                                                  posed(2, Vector3(0.f, 4.f, 0.f), turn)};
        const RigPose sparseResult = retargetPose(source, sparse, target, fullMap());
        checkNear(localOf(sparseResult, 30).orientation.w, 1.f, 1e-6f,
                  "missing joint falls back to source bind");

        // Root translation scale applies to root pairs only.
        RetargetOptions tall;
        tall.rootTranslationScale = 2.f;
        const RigPose tallResult = retargetPose(source, pose, target, fullMap(), tall);
        checkNear(localOf(tallResult, 10).position, Vector3(0.f, 20.f, 0.f), 1e-5f,
                  "root scale applies to the root");
        checkNear(localOf(tallResult, 20).position, Vector3(0.f, 2.f, 0.f), 1e-5f,
                  "root scale leaves other joints alone");

        // Raw scaling off: offsets copy verbatim.
        RetargetOptions raw;
        raw.scalePositionsToTarget = false;
        const RigPose rawResult = retargetPose(source, pose, target, fullMap(), raw);
        checkNear(localOf(rawResult, 20).position, Vector3(0.f, 4.f, 0.f), 1e-5f,
                  "offsets copy verbatim without scaling");

        // Output follows the target evaluation order (deterministic).
        const std::vector<RigNodeId> order = target.evaluationOrder();
        check(result.size() == order.size(), "output covers the evaluation order");
        for (std::size_t i = 0; i < order.size(); ++i)
            check(result[i].id == order[i], "output is in evaluation order");
    }

    void rejections() {
        chrononmotion_test::section("retarget rejections");
        const RigEvaluator source = sourceRig();
        const RigEvaluator target = targetRig();
        const std::vector<RigPoseValue> pose = {posed(1, Vector3(0.f, 10.f, 0.f), Quaternion())};

        const std::vector<RetargetPair> emptyMap{};
        const std::vector<RetargetPair> dupTarget{{1, 10}, {2, 10}};
        const std::vector<RetargetPair> dupSource{{1, 10}, {1, 20}};
        const std::vector<RetargetPair> straySource{{99, 10}};
        const std::vector<RetargetPair> strayTarget{{1, 99}};
        const std::vector<RetargetPair> zeroId{{0, 10}};
        const std::vector<RetargetPair> single{{1, 10}};
        check(throwsInvalid(source, pose, target, emptyMap), "empty map rejected");
        check(throwsInvalid(source, pose, target, dupTarget), "duplicate target rejected");
        check(throwsInvalid(source, pose, target, dupSource), "duplicate source rejected");
        check(throwsInvalid(source, pose, target, straySource), "unknown source joint rejected");
        check(throwsInvalid(source, pose, target, strayTarget), "unknown target joint rejected");
        check(throwsInvalid(source, pose, target, zeroId), "zero id rejected");

        std::vector<RigPoseValue> nanPose = {posed(1, Vector3(0.f, 10.f, 0.f), Quaternion())};
        nanPose.front().local.position.x = std::numeric_limits<float>::quiet_NaN();
        check(throwsInvalid(source, nanPose, target, single), "NaN pose rejected");

        std::vector<RigPoseValue> strayPose = {posed(77, Vector3(0.f, 0.f, 0.f), Quaternion())};
        check(throwsInvalid(source, strayPose, target, single), "stray pose joint rejected");

        RetargetOptions bad;
        bad.rootTranslationScale = std::numeric_limits<float>::quiet_NaN();
        check(throwsInvalid(source, pose, target, single, bad), "NaN root scale rejected");
    }

    void bakeIntegration() {
        chrononmotion_test::section("retarget bake integration");
        const RigEvaluator source = sourceRig();
        const RigEvaluator target = targetRig();
        std::vector<RigPose> frames;
        for (int frame = 0; frame < 3; ++frame) {
            const float t = static_cast<float>(frame);
            const std::vector<RigPoseValue> sourcePose = {
                posed(1, Vector3(0.f, 10.f + t, 0.f), Quaternion()),
                posed(2, Vector3(0.f, 4.f, 0.f), Quaternion()),
                posed(3, Vector3(0.f, 1.f, 0.f), Quaternion())};
            frames.push_back(retargetPose(source, sourcePose, target, fullMap()));
        }
        const BakedRig baked = target.bake(frames, 0, 30.f);
        check(baked.firstFrame == 0, "bake keeps the first frame");
        check(!baked.nodes.empty(), "bake emits bound nodes");
        // The retargeted root climbs one unit per frame, scaled by identity.
        check(baked.nodes.front().tracks.position.keys().size() == 3, "bake keeps every frame");
    }

}// namespace

int main() {
    transfer();
    rejections();
    bakeIntegration();
    return chrononmotion_test::report();
}
