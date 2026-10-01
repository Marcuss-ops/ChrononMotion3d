#ifndef CHRONONMOTION_MOTION_CONSTRAINTS_HPP
#define CHRONONMOTION_MOTION_CONSTRAINTS_HPP

#include "chrononmotion/motion/MotionPath.hpp"
#include "chrononmotion/motion/Track.hpp"
#include "chrononmotion/motion/Transform.hpp"

#include <cstdint>
#include <memory>

namespace chrononmotion::motion {

    enum class ConstraintKind {
        Billboard,
        Aim,
        KeepDistance,
        FollowPath,
        CopyPosition,
        CopyRotation,
        CopyScale,
        Parent,
        Orient,
        Point,
        StretchTo,
        LimitPosition,
        LimitRotation,
        LimitScale,
        DampedAim,
        SurfaceAttach,
        LookAtWithRoll
    };

    struct ConstraintSpec {
        ConstraintKind kind{ConstraintKind::Aim};
        std::uint32_t targetId{0};
        std::shared_ptr<MotionPath> path{};
        Vector3 axis{1.f, 1.f, 1.f};
        float distance{0.f};
        float amount{1.f};
        float progress{0.f};
        bool orientToPath{false};
        Vector3 minimum{-1000000.f, -1000000.f, -1000000.f};
        Vector3 maximum{1000000.f, 1000000.f, 1000000.f};
        float restLength{1.f}; // StretchTo local +Z length
        float damping{1.f}; // DampedAim exponential response rate from authored pose.
        float roll{0.f}; // LookAtWithRoll local +Z roll, radians.
        Vector3 surfaceNormal{0.f, 0.f, 1.f}; // SurfaceAttach plane normal in target space.
        Vector3 surfaceOffset{}; // SurfaceAttach tangent offset in target space.

        std::shared_ptr<Track<float>> amountTrack{};
    };

    [[nodiscard]] bool applyConstraint(const ConstraintSpec& spec,
                                       Transform& local,
                                       const Vector3& targetPosition,
                                       const Quaternion& targetRotation,
                                       const Vector3& targetScale,
                                       float seconds) noexcept;

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_CONSTRAINTS_HPP
