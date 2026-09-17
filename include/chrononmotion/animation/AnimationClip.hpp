
#ifndef CHRONONMOTION_ANIMATIONCLIP_HPP
#define CHRONONMOTION_ANIMATIONCLIP_HPP

#include "chrononmotion/animation/KeyframeTrack.hpp"
#include "chrononmotion/math/MathUtils.hpp"

#include <memory>
#include <vector>

namespace chrononmotion {

    class Object3D;

    class AnimationClip {

    public:
        AnimationBlendMode blendMode;

        explicit AnimationClip(std::string name,
                               float duration = 1,
                               const std::vector<std::shared_ptr<KeyframeTrack>>& tracks = {},
                               AnimationBlendMode blendMode = AnimationBlendMode::Normal);

        [[nodiscard]] std::string uuid() const;

        // Only serialization round-trips (ObjectLoader) have a reason to call this.
        void setUuid(const std::string& uuid) { uuid_ = uuid; }

        [[nodiscard]] std::string name() const { return name_; }

        [[nodiscard]] float getDuration() const { return duration; }

        [[nodiscard]] const std::vector<std::shared_ptr<KeyframeTrack>>& getTracks() const { return tracks; }

        void resetDuration();

        // Convert every track to additive (delta-from-first-frame) form and mark
        // the clip Additive, so an action created from it layers on top of a base
        // animation (e.g. an upper-body reload over a walk). Call before clipAction.
        void makeAdditive();

        static std::shared_ptr<AnimationClip> findByName(const Object3D& object, const std::string& name);
        static std::shared_ptr<AnimationClip> findByName(const std::vector<std::shared_ptr<AnimationClip>>& clipArray, const std::string& name);

    private:
        std::string uuid_{math::generateUUID()};

        std::string name_;
        std::vector<std::shared_ptr<KeyframeTrack>> tracks;
        float duration;

    };

}// namespace chrononmotion

#endif//CHRONONMOTION_ANIMATIONCLIP_HPP
