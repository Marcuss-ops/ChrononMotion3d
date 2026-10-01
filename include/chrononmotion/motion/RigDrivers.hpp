#ifndef CHRONONMOTION_MOTION_RIG_DRIVERS_HPP
#define CHRONONMOTION_MOTION_RIG_DRIVERS_HPP

#include "chrononmotion/motion/RigGraph.hpp"
#include <array>

namespace chrononmotion::motion {
    enum class RigControllerKind { Scalar, Joystick2D, Spatial3D, Boolean, Enum };
    struct RigController {
        RigNodeId id{0}; // Refers to a Controller node in the RigGraph.
        RigControllerKind kind{RigControllerKind::Scalar};
        Vector3 neutral{};
        Vector3 minimum{-1.f, -1.f, -1.f};
        Vector3 maximum{1.f, 1.f, 1.f};
    };
    struct RigControllerValue { RigNodeId id{0}; Vector3 value{}; };
    enum class RigDriverKind {
        Constant, Controller, Time, Track, Distance, Angle,
        Remap, Clamp, Smoothstep, Add, Multiply, Min, Max, Noise
    };
    struct RigDriverNode {
        std::uint32_t id{0};
        RigDriverKind kind{RigDriverKind::Constant};
        // Arity: arithmetic 2; remap/clamp/smoothstep/noise 1; others 0.
        std::array<std::uint32_t, 2> inputs{};
        RigNodeId controller{0};
        unsigned component{0};
        RigNodeId first{0};
        RigNodeId second{0};
        float value{0.f};
        float inMin{0.f}, inMax{1.f}, outMin{0.f}, outMax{1.f};
        std::uint32_t seed{0};
        Track<float> track{};
    };
    enum class RigDrivenProperty { Position, Rotation, Scale, Opacity };
    struct RigDriverOutput {
        std::uint32_t driver{0};
        RigNodeId target{0};
        RigDrivenProperty property{RigDrivenProperty::Rotation};
        unsigned component{0};
    };
    struct RigDriverGraph {
        std::vector<RigController> controllers{};
        std::vector<RigDriverNode> nodes{};
        std::vector<RigDriverOutput> outputs{};
    };
    // The evaluator owns one immutable RigEvaluator snapshot. Driver inputs read
    // authored FK world state, then outputs overwrite local channels simultaneously.
    // No output can feed an input implicitly; dependencies are explicit DAG edges.
    class RigDriverEvaluator {
    public:
        RigDriverEvaluator(const RigEvaluator& rig, const RigDriverGraph& graph);
        [[nodiscard]] RigPose evaluate(float seconds,
                                      std::span<const RigControllerValue> controls = {},
                                      std::span<const RigPoseValue> pose = {}) const;
    private:
        RigEvaluator rig_;
        RigDriverGraph graph_;
        std::vector<std::size_t> order_{};
        std::vector<std::array<std::size_t, 2>> inputs_{};
        std::vector<std::size_t> controllerIndices_{};
        std::vector<std::size_t> firstIndices_{}, secondIndices_{};
        std::vector<std::size_t> outputDrivers_{}, outputTargets_{};
    };

    enum class RigPoseBlendMode { Override, Additive, WeightedBlend, MaskedBlend };
    struct RigPoseLayer {
        RigPose pose{}; // sorted unique IDs, sparse allowed
        RigPoseBlendMode mode{RigPoseBlendMode::WeightedBlend};
        float weight{1.f};
        std::vector<RigNodeId> mask{}; // sorted unique; empty masked blend affects nothing
    };
    // Layers apply in explicit authored order. Additive values are deltas from
    // rest pose, including quaternion delta and relative multiplicative scale.
    [[nodiscard]] RigPose mixRigPoses(const RigEvaluator& rig,
                                     std::span<const RigPoseValue> base,
                                     std::span<const RigPoseLayer> layers);
}
#endif
