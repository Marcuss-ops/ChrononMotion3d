#include "chrononmotion/motion/RigOperators.hpp"

#include "chrononmotion/math/Finite.hpp"#include <array>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion {
    namespace {
        const auto catalog = std::to_array<RigOperatorDescriptor>({
            {ConstraintKind::Billboard, "Billboard", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::Aim, "Aim", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::KeepDistance, "KeepDistance", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::FollowPath, "FollowPath", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::CopyPosition, "CopyPosition", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::CopyRotation, "CopyRotation", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::CopyScale, "CopyScale", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::Parent, "Parent", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::Orient, "Orient", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::Point, "Point", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::StretchTo, "StretchTo", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::LimitPosition, "LimitPosition", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::LimitRotation, "LimitRotation", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::LimitScale, "LimitScale", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::DampedAim, "DampedAim", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::SurfaceAttach, "SurfaceAttach", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {ConstraintKind::LookAtWithRoll, "LookAtWithRoll", RigOperatorCategory::Constraint, RigEvaluationStage::Constraints},
            {RigDriverKind::Constant, "Constant", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 0},
            {RigDriverKind::Controller, "Controller", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 0},
            {RigDriverKind::Time, "Time", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 0},
            {RigDriverKind::Track, "Track", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 0},
            {RigDriverKind::Distance, "Distance", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 0},
            {RigDriverKind::Angle, "Angle", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 0},
            {RigDriverKind::Remap, "Remap", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 1},
            {RigDriverKind::Clamp, "Clamp", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 1},
            {RigDriverKind::Smoothstep, "Smoothstep", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 1},
            {RigDriverKind::Add, "Add", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 2},
            {RigDriverKind::Multiply, "Multiply", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 2},
            {RigDriverKind::Min, "Min", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 2},
            {RigDriverKind::Max, "Max", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 2},
            {RigDriverKind::Noise, "Noise", RigOperatorCategory::Driver, RigEvaluationStage::Drivers, 1},
            {RigDeformerKind::Bend, "Bend", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::Wave, "Wave", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::Twist, "Twist", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::Bulge, "Bulge", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::Taper, "Taper", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::Lattice, "Lattice", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::PinWarp, "PinWarp", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::PathWarp, "PathWarp", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::NoiseWarp, "NoiseWarp", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigDeformerKind::SquashStretch, "SquashStretch", RigOperatorCategory::Deformer, RigEvaluationStage::Geometry},
            {RigSolverKind::IKChain, "IKChain", RigOperatorCategory::Solver, RigEvaluationStage::Solve},
            {RigSolverKind::SpaceSwitch, "SpaceSwitch", RigOperatorCategory::Solver, RigEvaluationStage::Spaces},
            {RigSolverKind::SecondaryMotion, "SecondaryMotion", RigOperatorCategory::Solver, RigEvaluationStage::Secondary, 0, true, false, true},
            {RigSolverKind::SplineIK, "SplineIK", RigOperatorCategory::Solver, RigEvaluationStage::Solve},
            {RigSolverKind::Skin, "Skin", RigOperatorCategory::Solver, RigEvaluationStage::Geometry},
            {RigSolverKind::Morph, "Morph", RigOperatorCategory::Solver, RigEvaluationStage::Geometry},
            {RigSolverKind::PoseMixer, "PoseMixer", RigOperatorCategory::Solver, RigEvaluationStage::PoseMix}
        });
    }
    std::span<const RigOperatorDescriptor> rigOperatorRegistry() { return catalog; }
    const RigOperatorDescriptor& rigOperator(RigOperatorKind kind) {
        for (const auto& entry : catalog) if (entry.kind == kind) return entry;
        throw std::invalid_argument("RigOperators: unregistered kind");
    }
    void validateRigOperator(const ConstraintSpec& spec) {
        (void) rigOperator(spec.kind);
        if (!std::isfinite(spec.amount) || spec.amount < 0.f || spec.amount > 1.f ||
            !std::isfinite(spec.distance) || spec.distance < 0.f || !std::isfinite(spec.progress) ||
            !math::isFinite(spec.axis) || !math::isFinite(spec.minimum) || !math::isFinite(spec.maximum) ||
            !std::isfinite(spec.restLength) || spec.restLength <= 0.f || !std::isfinite(spec.damping) || spec.damping < 0.f ||
            !std::isfinite(spec.roll) || !math::isFinite(spec.surfaceNormal) || !math::isFinite(spec.surfaceOffset) ||
            (spec.kind == ConstraintKind::SurfaceAttach &&
             (std::abs(spec.surfaceNormal.lengthSq() - 1.f) > 1e-4f || std::abs(spec.surfaceNormal.dot(spec.surfaceOffset)) > 1e-4f)) ||
            (spec.kind == ConstraintKind::FollowPath && !spec.path))
            throw std::invalid_argument("RigOperators: invalid constraint parameters");
        for (unsigned axis = 0; axis < 3; ++axis)
            if (spec.minimum[axis] > spec.maximum[axis] ||
                (spec.kind == ConstraintKind::LimitScale && spec.minimum[axis] <= 0.f))
                throw std::invalid_argument("RigOperators: invalid constraint range");
    }
    void validateRigOperator(const RigDriverNode& spec) {
        (void) rigOperator(spec.kind);
        if (!std::isfinite(spec.value) || !std::isfinite(spec.inMin) || !std::isfinite(spec.inMax) ||
            !std::isfinite(spec.outMin) || !std::isfinite(spec.outMax))
            throw std::invalid_argument("RigOperators: nonfinite driver parameters");
    }
    void validateRigOperator(const RigDeformer& spec) {
        (void) rigOperator(spec.kind);
        // The geometry evaluator remains the canonical deformer validator.
        DeformerStack stack; stack.operators.push_back(spec);
        (void) stack.evaluate({});
    }
}
