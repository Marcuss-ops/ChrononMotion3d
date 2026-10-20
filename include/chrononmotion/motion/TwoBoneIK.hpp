// ChrononMotion — analytic two-bone IK.
//
// The chain every limb actually is: start -> mid -> end (hip/knee/ankle,
// shoulder/elbow/wrist). The general solver in OfflineBaking is FABRIK, which
// walks an n-joint chain iteratively and has no notion of a joint that cannot
// fold any further. A knee is exactly that joint, and so is an elbow that must
// stay on one side of straight.
//
// This header is the closed-form solve for three points. It is a pure function
// of its arguments — no iteration, no state from the previous frame — so the
// same inputs always produce the same pose, which is what lets it sit inside
// `RigEvaluator::solveIK` without weakening the determinism contract.
//
// The rules it implements, stated once:
//
//   * Bone lengths are inputs and are never changed. A target out of reach
//     straightens the chain toward it and reports `reached == false`; it does
//     not scale the limb to fake the distance.
//   * `midAngle` is the interior angle at the middle joint: `pi` is a straight
//     limb, smaller values are a folded one. `minMidAngle` is the limit that
//     stops a knee/elbow folding through itself; the solve clamps the angle and
//     lets the end joint fall short of the target rather than break the joint.
//   * The bend plane is `dir × pole`. A pole parallel to the target direction
//     falls back to the chain's current bend, and then to the world axis least
//     aligned with `dir`, so the solve stays deterministic instead of picking
//     an arbitrary side.
//   * `twist` rotates the bend plane about the start->target axis and leaves the
//     end joint exactly where it was.
//   * `weight` blends the *directions* of both segments from the authored pose
//     to the solved one, so a partial solve keeps every bone at its authored
//     length instead of shrinking the limb the way a position lerp would.
//
// What this is not: a joint-limit system for arbitrary axes, a collision solve,
// or a chain solver. Three points in, three points out.

#ifndef CHRONONMOTION_MOTION_TWOBONEIK_HPP
#define CHRONONMOTION_MOTION_TWOBONEIK_HPP

#include "chrononmotion/math/Quaternion.hpp"
#include "chrononmotion/math/Vector3.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion {

    namespace two_bone_detail {

        inline constexpr float kPi = 3.14159265358979323846f;
        inline constexpr float kEpsilon = 1e-6f;

        inline bool finite(const Vector3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        /// The world axis that `dir` is least aligned with. Used only when both
        /// the pole and the current bend are parallel to the target direction.
        inline Vector3 anyPerpendicular(const Vector3& direction) {
            const float ax = std::fabs(direction.x);
            const float ay = std::fabs(direction.y);
            const float az = std::fabs(direction.z);
            if (ax <= ay && ax <= az) return Vector3(1.f, 0.f, 0.f);
            if (ay <= az) return Vector3(0.f, 1.f, 0.f);
            return Vector3(0.f, 0.f, 1.f);
        }

        /// Rotate `from` toward `to` by `weight`, angularly. Both must be unit
        /// length. A zero-length input returns the other side unchanged.
        inline Vector3 slerpDirection(const Vector3& from, const Vector3& to, float weight) {
            if (weight >= 1.f) return to;
            if (weight <= 0.f) return from;
            Quaternion rotation;
            rotation.setFromUnitVectors(from, to);
            Quaternion partial;
            partial.identity();
            partial.slerpQuaternions(Quaternion{}, rotation, weight);
            Vector3 blended = from;
            blended.applyQuaternion(partial);
            return blended;
        }

    }// namespace two_bone_detail

    /// Parameters of one analytic two-bone solve. Everything is in the same
    /// space as the three joint positions (world space when called from
    /// `RigEvaluator`, model space when a caller solves its own chain).
    struct TwoBoneIKSpec {
        /// The point the end joint should reach.
        Vector3 target{};
        /// Direction the middle joint should bend toward. Not required to be
        /// normalized; a zero vector is treated as "keep the current bend".
        Vector3 pole{0.f, 1.f, 0.f};
        /// Rotation of the bend plane about the start->target axis, radians.
        float twist{0.f};
        /// Interior mid-joint angle at which the chain stops folding, radians.
        float minMidAngle{0.f};
        /// Interior mid-joint angle at which the chain stops opening, radians.
        /// `pi` (the default) is a straight limb.
        float maxMidAngle{two_bone_detail::kPi};
        /// Blend from the authored pose (0) to the solved pose (1).
        float weight{1.f};
    };

    /// The solved three points plus the facts a caller needs to judge the
    /// result: the interior mid-joint angle actually produced, and whether the
    /// end joint landed on the target.
    struct TwoBoneIKResult {
        Vector3 start{};
        Vector3 mid{};
        Vector3 end{};
        float midAngle{0.f};
        bool reached{false};
    };

    /// Solve one two-bone chain analytically.
    ///
    /// Throws `std::invalid_argument` on non-finite input, a bone of zero
    /// length, a weight outside [0, 1] or limits outside [0, pi] with
    /// `minMidAngle > maxMidAngle`. A degenerate *target* is not an error: the
    /// solve keeps the chain's current direction instead of dividing by zero.
    [[nodiscard]] inline TwoBoneIKResult solveTwoBoneIK(const Vector3& start, const Vector3& mid,
                                                        const Vector3& end, const TwoBoneIKSpec& spec) {
        using namespace two_bone_detail;

        if (!finite(start) || !finite(mid) || !finite(end) || !finite(spec.target) || !finite(spec.pole) ||
            !std::isfinite(spec.twist) || !std::isfinite(spec.minMidAngle) ||
            !std::isfinite(spec.maxMidAngle) || !std::isfinite(spec.weight))
            throw std::invalid_argument("solveTwoBoneIK: non-finite input");
        if (spec.weight < 0.f || spec.weight > 1.f)
            throw std::invalid_argument("solveTwoBoneIK: weight must be in [0, 1]");
        if (spec.minMidAngle < 0.f || spec.maxMidAngle > kPi || spec.minMidAngle > spec.maxMidAngle)
            throw std::invalid_argument("solveTwoBoneIK: mid-angle limits must satisfy 0 <= min <= max <= pi");

        const float upper = start.distanceTo(mid);
        const float lower = mid.distanceTo(end);
        if (upper <= kEpsilon || lower <= kEpsilon)
            throw std::invalid_argument("solveTwoBoneIK: both bones must have positive length");

        TwoBoneIKResult result;
        result.start = start;

        // Direction to the target. A target sitting on the start joint has no
        // direction; keep the authored one so the solve stays finite.
        Vector3 towardTarget = spec.target - start;
        if (towardTarget.lengthSq() <= kEpsilon * kEpsilon) {
            towardTarget = end - start;
            if (towardTarget.lengthSq() <= kEpsilon * kEpsilon)
                throw std::invalid_argument("solveTwoBoneIK: target and chain direction are both degenerate");
        }
        Vector3 direction = towardTarget;
        direction.normalize();

        const float requested = start.distanceTo(spec.target);
        const float reachMax = upper + lower;
        const float reachMin = std::fabs(upper - lower);
        // Both ends of the reachable band are singular (folded or straight), so
        // the working distance is clamped inside it, never onto it.
        const float distance = std::clamp(requested, reachMin, reachMax);

        float cosMid = (upper * upper + lower * lower - distance * distance) / (2.f * upper * lower);
        cosMid = std::clamp(cosMid, -1.f, 1.f);
        const float requestedAngle = std::acos(cosMid);
        const float solvedAngle = std::clamp(requestedAngle, spec.minMidAngle, spec.maxMidAngle);
        // A limit that moves the angle also moves the reachable distance: the
        // end joint falls short rather than the bone being stretched.
        const float solvedDistance = solvedAngle == requestedAngle
                                             ? distance
                                             : std::sqrt(std::max(0.f, upper * upper + lower * lower -
                                                                              2.f * upper * lower * std::cos(solvedAngle)));

        // Bend plane. `normal = dir x pole`, and the bend direction is
        // `normal x dir`, which points toward the pole side.
        Vector3 pole = spec.pole;
        if (pole.lengthSq() <= kEpsilon * kEpsilon) {
            pole = mid - start;
            pole.addScaledVector(direction, -pole.dot(direction));
        }
        Vector3 normal;
        if (pole.lengthSq() > kEpsilon * kEpsilon) {
            pole.normalize();
            normal.crossVectors(direction, pole);
        }
        if (normal.lengthSq() <= kEpsilon * kEpsilon) {
            // Pole parallel to the target direction: fall back to the current
            // bend, then to a world axis the direction cannot be parallel to.
            Vector3 bend = mid - start;
            bend.addScaledVector(direction, -bend.dot(direction));
            if (bend.lengthSq() > kEpsilon * kEpsilon) {
                bend.normalize();
                normal.crossVectors(direction, bend);
            } else {
                normal.crossVectors(direction, anyPerpendicular(direction));
            }
        }
        normal.normalize();
        Vector3 bendDirection;
        bendDirection.crossVectors(normal, direction);
        bendDirection.normalize();

        // Standard two-bone construction: place the middle joint on the circle
        // where both bones keep their lengths.
        const float along = (upper * upper - lower * lower + solvedDistance * solvedDistance) /
                            (2.f * solvedDistance);
        const float height = std::sqrt(std::max(0.f, upper * upper - along * along));
        Vector3 solvedMid = start + direction * along + bendDirection * height;
        const Vector3 solvedEnd = start + direction * solvedDistance;

        if (spec.twist != 0.f) {
            Quaternion twistRotation;
            twistRotation.setFromAxisAngle(direction, spec.twist);
            Vector3 offset = solvedMid - start;
            offset.applyQuaternion(twistRotation);
            solvedMid = start + offset;
        }

        // Partial solve: blend the segment directions, never the points, so a
        // half-applied IK still carries two full-length bones.
        Vector3 firstAuthored = mid - start;
        Vector3 secondAuthored = end - mid;
        firstAuthored.normalize();
        secondAuthored.normalize();

        if (spec.weight >= 1.f) {
            result.mid = solvedMid;
        } else {
            Vector3 firstSolved = solvedMid - start;
            firstSolved.normalize();
            const Vector3 first = slerpDirection(firstAuthored, firstSolved, spec.weight);
            result.mid = start + first * upper;
        }

        Vector3 secondSolved = solvedEnd - result.mid;
        if (secondSolved.lengthSq() <= kEpsilon * kEpsilon)
            secondSolved = direction;
        secondSolved.normalize();
        const Vector3 second = slerpDirection(secondAuthored, secondSolved, spec.weight);
        result.end = result.mid + second * lower;

        // Report the angle the blend actually produced, not the requested one.
        Vector3 toStart = start - result.mid;
        Vector3 toEnd = result.end - result.mid;
        if (toStart.lengthSq() > kEpsilon * kEpsilon && toEnd.lengthSq() > kEpsilon * kEpsilon) {
            toStart.normalize();
            toEnd.normalize();
            result.midAngle = std::acos(std::clamp(toStart.dot(toEnd), -1.f, 1.f));
        } else {
            result.midAngle = requestedAngle;
        }

        const float tolerance = 1e-4f * std::max(1.f, reachMax);
        result.reached = spec.weight >= 1.f && solvedAngle == requestedAngle &&
                         requested <= reachMax + tolerance && requested >= reachMin - tolerance &&
                         result.end.distanceTo(spec.target) <= tolerance;
        return result;
    }

}// namespace chrononmotion::motion

#endif// CHRONONMOTION_MOTION_TWOBONEIK_HPP
