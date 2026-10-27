#pragma once

#include "chrononmotion/math/Euler.hpp"
#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/math/Matrix4.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/Layer.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <chronon3d/core/types/time.hpp>
#include <chronon3d/render_plan/render_plan.hpp>

namespace chrononmotion::motion::lowering_detail {
using chronon3d::render_plan::CameraPlan;
struct ResolvedPose {
    Vector3 position{};
    Vector3 scale{1.f, 1.f, 1.f};
    Vector3 rotation{};
    float opacity{1.f};
};
inline constexpr std::size_t kMaximumBakedTransformSamples = 65'536;

inline void ensureFinite(float value, const char* name) {
    if (!std::isfinite(value))
        throw std::invalid_argument(std::string("lowerToRenderPlan: non-finite ") + name);
}

inline bool matrixFinite(const Matrix4& matrix) {
    return std::all_of(matrix.elements.begin(), matrix.elements.end(),
                       [](float value) { return std::isfinite(value); });
}

// Chronon3D reconstructs camera rotation with glm::quat(euler), whose order is
// qZ * qY * qX. The motion library's Euler extraction uses a different order;
// serialize the inverse of Chronon3D's actual constructor instead.
inline std::array<float, 3> chrononCameraEuler(const Matrix4& rotation) {
    const auto& m = rotation.elements; // column-major storage
    const float sinY = std::clamp(-m[2], -1.f, 1.f); // -row 2, column 0
    const float y = std::asin(sinY);
    const float cosY = std::cos(y);
    float x = 0.f;
    float z = 0.f;
    if (std::abs(cosY) > 1e-6f) {
        x = std::atan2(m[6], m[10]); // row 2, column 1 / row 2, column 2
        z = std::atan2(m[1], m[0]);  // row 1, column 0 / row 0, column 0
    } else {
        x = std::atan2(sinY * m[4], m[5]); // deterministic solution with z = 0
    }
    return {x, y, z};
}

inline bool close(float a, float b) {
    return std::abs(a - b) <= 1e-4f * std::max({1.f, std::abs(a), std::abs(b)});
}

inline Vector3 transformPoint(const Matrix4& matrix, const Vector3& point) {
    Vector3 result = point;
    result.applyMatrix4(matrix);
    return result;
}

inline chronon3d::render_plan::AnimationKeyframePlan makePlanKeyframe(chronon3d::Frame frame, std::vector<float> value) {
    chronon3d::render_plan::AnimationKeyframePlan kf;
    kf.frame = frame;
    kf.value = std::move(value);
    return kf;
}

[[nodiscard]] chronon3d::render_plan::RenderPlan lowerSamplesToRenderPlan(
    float fps, std::size_t frameCount, const RenderPlanLoweringOptions& options,
    const std::vector<Layer>& metadata,
    const std::vector<std::vector<ResolvedPose>>& samples);


} // namespace chrononmotion::motion::lowering_detail

namespace chrononmotion::motion {
using lowering_detail::close;
using lowering_detail::chrononCameraEuler;
using lowering_detail::ensureFinite;
using lowering_detail::makePlanKeyframe;
using lowering_detail::matrixFinite;
using lowering_detail::transformPoint;
}
