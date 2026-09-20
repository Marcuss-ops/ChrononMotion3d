#ifndef CHRONONMOTION_MOTION_CONSTRAINTS_HPP
#define CHRONONMOTION_MOTION_CONSTRAINTS_HPP

#include "chrononmotion/motion/MotionPath.hpp"
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
        CopyScale
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
    };

    [[nodiscard]] bool applyConstraint(const ConstraintSpec& spec,
                                       Transform& local,
                                       const Vector3& targetPosition,
                                       const Quaternion& targetRotation,
                                       const Vector3& targetScale,
                                       float seconds) noexcept;

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_CONSTRAINTS_HPP
