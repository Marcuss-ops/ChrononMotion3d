#ifndef CHRONONMOTION_MOTION_RIG_OPERATORS_HPP
#define CHRONONMOTION_MOTION_RIG_OPERATORS_HPP

#include "chrononmotion/motion/RigBaking.hpp"
#include <string_view>
#include <variant>

namespace chrononmotion::motion {
    enum class RigSolverKind { IKChain, SpaceSwitch, SecondaryMotion, SplineIK, Skin, Morph, PoseMixer };
    enum class RigOperatorCategory { Constraint, Driver, Deformer, Solver };
    using RigOperatorKind = std::variant<ConstraintKind, RigDriverKind, RigDeformerKind, RigSolverKind>;
    struct RigOperatorDescriptor {
        RigOperatorKind kind;
        std::string_view name;
        RigOperatorCategory category;
        RigEvaluationStage stage;
        unsigned inputArity{0};
        bool supports2D{true};
        bool supports3D{true};
        bool offlineOnly{false};
    };
    // Immutable built-in registry. Typed evaluator/validator overloads are the
    // dispatch functions; there are no mutable per-category maps or plug-ins.
    [[nodiscard]] std::span<const RigOperatorDescriptor> rigOperatorRegistry();
    [[nodiscard]] const RigOperatorDescriptor& rigOperator(RigOperatorKind kind);
    void validateRigOperator(const ConstraintSpec& spec);
    void validateRigOperator(const RigDriverNode& spec);
    void validateRigOperator(const RigDeformer& spec);
}
#endif
