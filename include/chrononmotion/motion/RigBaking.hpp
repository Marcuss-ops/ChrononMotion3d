#ifndef CHRONONMOTION_MOTION_RIG_BAKING_HPP
#define CHRONONMOTION_MOTION_RIG_BAKING_HPP
#include "chrononmotion/motion/RigGraph.hpp"
#include "chrononmotion/motion/OfflineBaking.hpp"
#include "chrononmotion/motion/RigDrivers.hpp"
#include "chrononmotion/motion/RigDeformers.hpp"

namespace chrononmotion::motion {
    struct RigSpaceSwitchPoint {
        float seconds{0.f};
        unsigned activeSpace{0};
    };
    struct RigSpaceSwitch {
        RigNodeId node{0};
        std::vector<RigNodeId> spaces{}; // 0 means world
        std::vector<RigSpaceSwitchPoint> switches{};
        bool preserveWorldTransform{true};
    };
    struct RigAnimatedNode { RigNodeId id{0}; LayerTracks tracks{}; };
    // Adapter to MotionScene's existing deterministic space-switch authority.
    // Returns locals expressed against the original rig hierarchy, ready to bake.
    [[nodiscard]] RigPose evaluateRigSpaces(const RigEvaluator& rig,
                                            std::span<const RigAnimatedNode> animation,
                                            std::span<const RigSpaceSwitch> switches,
                                            float seconds);

    struct RigGeometryBinding {
        std::uint32_t id{0};
        RigNodeId rigidNode{0}; // Rigid SVG group/layer binding; zero means world geometry.
        std::vector<Vector3> source{};
        std::vector<RigMorphTarget> morphs{};
        std::vector<Track<float>> morphTracks{}; // Empty entries retain static weights.
        std::vector<RigSkinVertex> skin{};
        std::vector<RigSkinMatrixBinding> inverseBinds{};
        std::vector<std::uint32_t> indices{}; // Optional supplied triangle topology; deformation preserves it.
        DeformerStack deformers{};
    };
    struct RigFrameGeometry { std::uint32_t id{0}; std::vector<Vector3> positions{}; };
    enum class RigEvaluationStage { Authored, Spaces, Drivers, PoseMix, Solve, Constraints, Secondary, Geometry, Final };
    struct RigScheduledIK {
        std::uint32_t id{0};
        RigIKChain chain{};
        RigNodeId targetNode{0}, poleNode{0};
        Track<Vector3> target{}, pole{};
        Track<float> amount{};
    };
    enum class RigWeightKind { IK, Constraint, Deformer, Pose, Morph };
    struct RigWeightBinding {
        // A scalar driver is routed through a declared Controller node position.x.
        // This reuses the resolved DriverGraph instead of a second expression engine.
        RigNodeId source{0};
        RigWeightKind kind{RigWeightKind::IK};
        std::uint32_t target{0}; // IK/geometry id, rig node id, or pose layer index
        unsigned component{0}; // Constraint/deformer/morph index
    };
    struct RigProgram {
        RigGraph graph{};
        RigDriverGraph drivers{};
        std::vector<RigAnimatedNode> animation{};
        std::vector<RigSpaceSwitch> spaces{};
        std::vector<RigPoseLayer> poses{};
        std::vector<RigScheduledIK> ik{};
        std::vector<RigGeometryBinding> geometry{};
        std::vector<RigWeightBinding> weights{};
        // Offline secondary samples, sparse sorted poses, same origin as firstFrame.
        std::vector<RigPose> secondary{};
        int secondaryFirstFrame{0};
        float secondaryFps{60.f};
    };
    struct RigProgramFrame {
        RigEvaluation pose{};
        std::vector<RigFrameGeometry> geometry{};
    };
    struct BakedRigProgram {
        BakedRig transforms{};
        std::vector<RigProgramFrame> frames{};
        std::uint64_t signature{0}; // FNV-1a of explicit float bits, IDs, counts and all channels
    };
    class RigProgramEvaluator {
    public:
        explicit RigProgramEvaluator(const RigProgram& program);
        [[nodiscard]] RigProgramFrame evaluate(int frame, float fps,
                                               std::span<const RigControllerValue> controls = {}) const;
        [[nodiscard]] BakedRigProgram bake(int firstFrame, int lastFrame, float fps) const;
    private:
        RigProgram program_;
        RigEvaluator rig_, raw_;
        RigDriverEvaluator drivers_;
        std::vector<std::size_t> animationNodes_{}, ikTargets_{}, ikPoles_{};
        struct ResolvedWeight { std::size_t source{0}, target{0}; RigWeightKind kind{}; unsigned component{0}; };
        std::vector<ResolvedWeight> weights_{};
        std::vector<std::vector<std::size_t>> secondaryNodes_{};
    };
    [[nodiscard]] std::uint64_t rigFrameSignature(const RigProgramFrame& frame);

    struct RigSecondaryMotion {
        std::vector<RigNodeId> joints{};
        Vector2 gravity{};
        float damping{.02f};
        unsigned iterations{8};
        float stiffness{0.f};
        float mass{1.f};
        Vector2 force{};
        float drag{0.f};
        float maxAngle{3.14159265358979323846f};
    };
    // Planar semantic frontend to the canonical Verlet baker. No new simulation
    // state; absolute frame origin is retained and baked samples are seekable.
    [[nodiscard]] std::vector<RigPose> bakeRigSecondaryMotion(
        const RigEvaluator& rig, const RigSecondaryMotion& spec,
        int firstFrame, int lastFrame, float fps = 60.f);

    struct RigSplineIK {
        std::vector<RigNodeId> joints{};
        std::shared_ptr<MotionPath> path{};
        float start{0.f};
        float end{1.f};
        // Rigid chord resolution: the root is searched forward from `start`
        // until the whole chain of exact rest-length chords enters the path.
        // Curvature causes backward distribution only where the chord arc
        // length exceeds the straight chord; segments never stretch.
        unsigned resolutionSteps{512};
    };
    // Rigid-chord SplineIK on the existing MotionPath authority: joints sit on
    // the path at strictly increasing arc-length parameters and every chord
    // equals its rest length. A chain that cannot fit the interval fails closed.
    [[nodiscard]] RigPose solveRigSplineIK(const RigEvaluator& rig, const RigSplineIK& spec);
}
#endif
