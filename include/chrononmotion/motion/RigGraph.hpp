#ifndef CHRONONMOTION_MOTION_RIG_GRAPH_HPP
#define CHRONONMOTION_MOTION_RIG_GRAPH_HPP

#include "chrononmotion/motion/Layer.hpp"

#include <span>
#include <vector>
#include <string>
#include <string_view>

namespace chrononmotion::motion {

    using RigNodeId = std::uint32_t;
    enum class RigNodeKind { Bone, Controller, LayerBinding, PathPoint, MeshBinding, Pin };

    struct RigNode {
        RigNodeId id{0};
        RigNodeId parent{0};
        RigNodeKind kind{RigNodeKind::Bone};
        Transform bind{};
        Transform rest{};
        float length{1.f};
        Vector3 primaryAxis{1.f, 0.f, 0.f};
        // Additional read dependencies; ordering only, not a second hierarchy.
        std::vector<RigNodeId> dependencies{};
        Layer::Id layerId{0};
        std::vector<ConstraintSpec> constraints{};
    };

    struct RigPoseValue {
        RigNodeId id{0};
        Transform local{};
    };
    using RigPose = std::vector<RigPoseValue>;

    struct RigEvaluation {
        // Sorted by stable ID, independent of authoring insertion order.
        std::vector<RigNodeId> ids{};
        std::vector<Transform> local{};
        std::vector<Matrix4> world{};
        [[nodiscard]] std::size_t index(RigNodeId id) const;
        [[nodiscard]] Vector3 localToWorld(RigNodeId id, const Vector3& point) const;
        [[nodiscard]] Vector3 worldToLocal(RigNodeId id, const Vector3& point) const;
    };

    struct RigConstraintWeight { RigNodeId node{0}; unsigned constraint{0}; float amount{1.f}; };

    /// Which solver a chain is handed to. `Fabrik` is the general n-joint
    /// walker; `TwoBone` is the closed-form three-point solve that can hold a
    /// knee or elbow at a minimum fold angle. Defaults to Fabrik so every
    /// existing schedule keeps its exact behaviour.
    enum class RigIKSolverKind { Fabrik, TwoBone };

    struct RigIKChain {
        // Root first, end effector last. Each joint must parent the next one.
        std::vector<RigNodeId> joints{};
        Vector3 target{};
        Vector3 pole{};
        float amount{1.f}; // FK/IK blend, endpoints exact.
        unsigned iterations{256};
        float tolerance{1e-5f};
        RigIKSolverKind solver{RigIKSolverKind::Fabrik};
        // TwoBone only: bend-plane rotation and the interior mid-joint limits
        // (radians, pi = straight). Ignored by the Fabrik path.
        float twist{0.f};
        float minMidAngle{0.f};
        float maxMidAngle{3.14159265358979323846f};
    };

    struct RigBakedNode {
        RigNodeId id{0};
        Layer::Id layerId{0};
        LayerTracks tracks{}; // Local tracks; parents are retained at lowering.
    };
    struct BakedRig {
        std::vector<RigBakedNode> nodes{};
        int firstFrame{0};
        int lastFrame{0};
        float fps{60.f};
    };

    class MotionScene;
    class RigEvaluator;

    class RigGraph {
    public:
        std::vector<RigNode> nodes{};
        // Fail closed by constructing the immutable, ID-resolved evaluator.
        [[nodiscard]] RigEvaluator compile() const;
    };

    class RigEvaluator {
    public:
        [[nodiscard]] const std::vector<RigNode>& nodes() const { return nodes_; }
        [[nodiscard]] const std::vector<RigNodeId>& evaluationOrder() const { return orderIds_; }
        [[nodiscard]] RigEvaluation evaluate(std::span<const RigPoseValue> pose = {}) const;
        // Reuses caller buffers: graph size is fixed, no per-frame graph rebuild.
        void evaluate(std::span<const RigPoseValue> pose, RigEvaluation& output) const;
        [[nodiscard]] RigEvaluation evaluateAt(float seconds, std::span<const RigPoseValue> pose = {},
                                               std::span<const RigConstraintWeight> weights = {}) const;
        // Raw FK is used for bind matrices and solver/matching input. Operators
        // must never alter the authored bind pose.
        [[nodiscard]] RigEvaluation evaluateFK(std::span<const RigPoseValue> pose = {}) const;
        [[nodiscard]] RigEvaluation bindPose() const;
        // Adapter only: canonical solveIKChain3D remains the solver authority.
        // Returns a complete sorted pose; unaffected joints retain authored FK.
        [[nodiscard]] RigPose solveIK(const RigIKChain& chain,
                                      std::span<const RigPoseValue> pose = {}) const;
        // Match a source world pose into this graph (e.g. a changed parent).
        // Anchors are retained; non-representable shear/reflections fail closed.
        [[nodiscard]] RigPose matchWorldPose(const RigEvaluation& source) const;
        // Return an immutable raw-FK view for staged authoring adapters.
        [[nodiscard]] RigEvaluator withoutConstraints() const;
        // FK→IK matching sets the effector and bend-plane controls to the source
        // pose; solving from that same pose keeps all joints unchanged.
        [[nodiscard]] RigIKChain matchIK(const RigIKChain& chain,
                                        std::span<const RigPoseValue> pose) const;
        [[nodiscard]] BakedRig bake(std::span<const RigPose> frames,
                                   int firstFrame, float fps) const;
        // Transactional validation before mutating the scene. All bound parents
        // must also be bound, so the local/world meaning cannot silently change.
        void applyBake(const BakedRig& bake, MotionScene& scene) const;

    private:
        friend class RigGraph;
        std::vector<RigNode> nodes_{};
        std::vector<std::size_t> parents_{};
        std::vector<std::size_t> order_{};
        std::vector<RigNodeId> orderIds_{};
        std::vector<std::vector<std::size_t>> constraintTargets_{};
        [[nodiscard]] std::size_t index(RigNodeId id) const;
        void evaluateImpl(std::span<const RigPoseValue> pose, RigEvaluation& output, bool constraints, float seconds = 0.f,
                          std::span<const RigConstraintWeight> weights = {}) const;
    };

    // Strict authoring graph format, not a renderer contract. Unknown fields,
    // duplicate keys, malformed transforms and dependency cycles fail closed.
    // Runtime tracks/controllers/deformers belong to RigProgram, not this graph.
    [[nodiscard]] std::string serializeRigGraph(const RigGraph& graph);
    [[nodiscard]] RigGraph deserializeRigGraph(std::string_view json);

} // namespace chrononmotion::motion
#endif
