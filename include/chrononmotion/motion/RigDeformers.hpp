#ifndef CHRONONMOTION_MOTION_RIG_DEFORMERS_HPP
#define CHRONONMOTION_MOTION_RIG_DEFORMERS_HPP
#include "chrononmotion/motion/RigGraph.hpp"
#include <array>

namespace chrononmotion::motion {
    enum class RigDeformerKind { Bend, Wave, Twist, Bulge, Taper, Lattice, PinWarp, PathWarp, NoiseWarp, SquashStretch };
    enum class RigPinKind { Position, Rotation, Scale, Stiffness };
    struct RigPin {
        RigPinKind kind{RigPinKind::Position};
        Vector3 rest{};
        Vector3 target{};
        Quaternion rotation{};
        Vector3 scale{1.f, 1.f, 1.f};
        float radius{1.f};
        float weight{1.f};
    };
    struct RigLattice {
        unsigned columns{4}, rows{4};
        Vector3 minimum{-1.f, -1.f, 0.f};
        Vector3 maximum{1.f, 1.f, 0.f};
        // Row-major displacements; bilinear, identity outside the domain.
        std::vector<Vector3> offsets{};
    };
    struct RigDeformer {
        RigDeformerKind kind{RigDeformerKind::Wave};
        float amount{1.f};
        float strength{.1f};
        float wavelength{1.f};
        float phase{0.f};
        Vector3 origin{};
        std::uint32_t seed{0};
        RigLattice lattice{};
        std::vector<RigPin> pins{};
        std::shared_ptr<MotionPath> path{};
    };
    // A shared vertex/control-point contract, not a renderer or SVG interpreter.
    class DeformerStack {
    public:
        std::vector<RigDeformer> operators{};
        [[nodiscard]] std::vector<Vector3> evaluate(std::span<const Vector3> source) const;
    };
    struct RigSkinMatrixBinding { RigNodeId joint{0}; Matrix4 inverseBind{}; };
    struct RigSkinVertex {
        std::array<RigNodeId, 4> joints{};
        std::array<float, 4> weights{};
    };
    // Inverse bind is taken from RigEvaluator::bindPose, not a second skeleton.
    [[nodiscard]] std::vector<Vector3> skinRigGeometry(const RigEvaluator& rig,
                                                      const RigEvaluation& pose,
                                                      std::span<const Vector3> source,
                                                      std::span<const RigSkinVertex> bindings,
                                                      std::span<const RigSkinMatrixBinding> inverseBinds = {});
    struct RigMorphTarget { std::vector<Vector3> positions{}; float weight{0.f}; };
    [[nodiscard]] std::vector<Vector3> mixRigMorphs(std::span<const Vector3> base,
                                                  std::span<const RigMorphTarget> targets);
}
#endif
