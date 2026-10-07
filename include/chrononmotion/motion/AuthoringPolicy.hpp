#ifndef CHRONONMOTION_MOTION_AUTHORING_POLICY_HPP
#define CHRONONMOTION_MOTION_AUTHORING_POLICY_HPP

#include <cstddef>
#include <limits>
#include <string>
#include <vector>

namespace chrononmotion::motion {

    /// Limits and allowlists enforced by MotionScene's authoring entry points.
    /// Empty allowlists mean unrestricted; zero budgets deny that resource.
    struct AuthoringPolicy {
        bool allowAnimations{true};
        bool allowStaticGltfImport{true};
        bool allowMotionGltfImport{true};
        bool allowNewCustomPresets{false};
        std::vector<std::string> allowedPresets{};
        std::vector<std::string> allowedGeometryTypes{};
        std::size_t maxPrimitiveSegments{65536};
        std::size_t maxLayers{std::numeric_limits<std::size_t>::max()};
        std::size_t maxLights{std::numeric_limits<std::size_t>::max()};
        std::size_t maxKeyframes{std::numeric_limits<std::size_t>::max()};
        std::size_t maxGeometryVertices{std::numeric_limits<std::size_t>::max()};
    };

    struct AuthoringAuditEntry {
        std::string action{};
        std::string subject{};
        bool accepted{false};
        std::string reason{};
    };

}// namespace chrononmotion::motion

#endif//CHRONONMOTION_MOTION_AUTHORING_POLICY_HPP