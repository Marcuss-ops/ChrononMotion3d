#ifndef CHRONONMOTION_MOTION_LIGHT_RIG_HPP
#define CHRONONMOTION_MOTION_LIGHT_RIG_HPP

#include "chrononmotion/motion/Lighting.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace chrononmotion::motion {

    struct LightRigSource {
        std::uint32_t id{0};
        std::string name{};
        LightKind kind{LightKind::Spot};
        Transform local{};
        Vector3 direction{0.f, 0.f, -1.f};
        Color color{};
        float intensity{1.f};
        float range{0.f};
        float decay{1.f};
        float angle{0.7853981633974483f};
        float penumbra{0.f};
        bool castsShadow{false};
    };

    /// Renderer-neutral authored collection of lights sharing one parent pose.
    /// V1 caps a rig at 16 uniquely identified, non-ambient sources.
    class LightRig {
    public:
        static constexpr std::size_t kMaxSources = 16;

        std::string id{};
        std::string name{};
        Transform transform{};
        Track<Vector3> positionTrack{};
        Track<Vector3> rotationTrack{};
        std::vector<LightRigSource> sources{};

        void validate() const;
        [[nodiscard]] std::vector<LightSample> sample(float seconds) const;
        [[nodiscard]] std::string serialize() const;
    };

    namespace lightRigPresets {
        [[nodiscard]] const std::vector<std::string_view>& catalog();
        [[nodiscard]] LightRig make(std::string_view id);
        void animateRotation(LightRig& rig, float startSeconds, float endSeconds,
                             const Vector3& fromRadians, const Vector3& toRadians,
                             const Easing& easing = Easing::easeInOut());
    }

} // namespace chrononmotion::motion

#endif
