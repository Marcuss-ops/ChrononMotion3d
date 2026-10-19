// ChrononMotion — FK motion retargeting between rigs.
//
// Transfers authored or baked motion from a source rig to a differently
// proportioned target rig: joint rotations copy verbatim, joint offsets scale
// by the target/source bind-length ratio, and unmapped target joints keep
// their bind pose. The correspondence is an explicit caller-supplied map —
// there are no name heuristics, no silent fuzzy matching, and no second
// solver: the target evaluates the resulting pose with its own constraints,
// limits and deformers exactly as if it had been authored there.
//
// What it deliberately is not: weight painting, skin transfer, IK
// re-solving, or full retargeting with foot locking. Painting stays in the
// DCC (Blender as a baker, glTF as the interchange); this is the
// deterministic joint-motion half a compiler can promise.

#ifndef CHRONONMOTION_MOTION_RIGRETARGET_HPP
#define CHRONONMOTION_MOTION_RIGRETARGET_HPP

#include "chrononmotion/motion/RigGraph.hpp"

#include <span>

namespace chrononmotion::motion {

    /// One joint correspondence: `source` names a node in the source graph,
    /// `target` a node in the target graph.
    struct RetargetPair {
        RigNodeId source{0};
        RigNodeId target{0};
    };

    struct RetargetOptions {
        /// Scale each mapped non-root joint offset by
        /// target bind length / source bind length, so a walk authored on
        /// long legs lands on short legs instead of floating. Root offsets
        /// always use `rootTranslationScale`.
        bool scalePositionsToTarget{true};
        /// Uniform scale for mapped root-joint positions (e.g. the
        /// characters' height ratio). Must be finite.
        float rootTranslationScale{1.f};
    };

    /// Retarget one source pose onto the target graph.
    ///
    /// `sourcePose` carries the source joints' local transforms (sparse is
    /// fine: a joint missing from the span reads its source bind pose).
    /// Returns a complete target pose in the target's evaluation order, ready
    /// for `RigEvaluator::evaluate` or per-frame collection into
    /// `RigEvaluator::bake`. Unmapped target joints keep their bind pose.
    ///
    /// Fail-closed: empty map, duplicate source/target ids, ids missing from
    /// either graph, non-finite inputs, and non-positive bind lengths under
    /// scaling all throw std::invalid_argument.
    [[nodiscard]] RigPose retargetPose(const RigEvaluator& source,
                                       std::span<const RigPoseValue> sourcePose,
                                       const RigEvaluator& target, std::span<const RetargetPair> map,
                                       const RetargetOptions& options = RetargetOptions{});

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_RIGRETARGET_HPP
