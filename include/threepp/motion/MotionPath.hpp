// ChrononMotion — motion paths.
//
// A spatial trajectory a layer can travel along. Wraps the kept three.js curve
// library so the motion side has one sampling authority: `sample()` is uniform
// in the curve parameter (what a timing curve wants), `sampleUniform()` is
// uniform in arc length (constant-speed travel).

#ifndef THREEPP_MOTION_MOTIONPATH_HPP
#define THREEPP_MOTION_MOTIONPATH_HPP

#include "threepp/extras/core/Curve.hpp"
#include "threepp/math/Quaternion.hpp"
#include "threepp/math/Vector3.hpp"

#include <memory>
#include <utility>
#include <vector>

namespace threepp::motion {

    /// A spatial trajectory, sampled by the motion timeline.
    class MotionPath {
    public:
        /// Straight segment.
        [[nodiscard]] static MotionPath line(const Vector3& from, const Vector3& to);

        /// Cubic Bézier: `from` and `to` are on the curve, `control1`/`control2`
        /// pull it. This is the shape behind arc and ease-through-space moves.
        [[nodiscard]] static MotionPath cubicBezier(const Vector3& from,
                                                    const Vector3& control1,
                                                    const Vector3& control2,
                                                    const Vector3& to);

        /// Quadratic Bézier: one control point.
        [[nodiscard]] static MotionPath quadraticBezier(const Vector3& from,
                                                        const Vector3& control,
                                                        const Vector3& to);

        /// Catmull-Rom through every point — the smooth path for more than two
        /// waypoints, without the author having to place control handles.
        [[nodiscard]] static MotionPath catmullRom(std::vector<Vector3> points,
                                                   bool closed = false,
                                                   float tension = 0.5f);

        explicit MotionPath(std::shared_ptr<Curve3> curve, bool closed = false);

        [[nodiscard]] const Curve3& curve() const { return *curve_; }
        [[nodiscard]] bool closed() const { return closed_; }

        /// Uniform in the curve parameter t (0..1).
        void sample(float t, Vector3& out) const;
        [[nodiscard]] Vector3 sample(float t) const;

        /// Uniform in arc length u (0..1): constant speed along the path.
        void sampleUniform(float u, Vector3& out) const;
        [[nodiscard]] Vector3 sampleUniform(float u) const;

        /// Unit tangent at arc-length u.
        void tangent(float u, Vector3& out) const;
        [[nodiscard]] Vector3 tangent(float u) const;

        [[nodiscard]] float length() const;
        [[nodiscard]] std::vector<Vector3> points(unsigned int divisions = 32) const;

        /// Orientation that turns a node's local +Z along the path at arc-length u
        /// ("orient to path"). Shortest-path, so it cannot flip the node.
        void orientToPath(float u, Quaternion& out) const;
        [[nodiscard]] Quaternion orientToPath(float u) const;

        /// The shortest-path rotation that turns local +Z into `direction`.
        [[nodiscard]] static Quaternion directionToQuaternion(const Vector3& direction);

    private:
        std::shared_ptr<Curve3> curve_;
        bool closed_{false};
    };

}// namespace threepp::motion

#endif//THREEPP_MOTION_MOTIONPATH_HPP
