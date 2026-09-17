
#ifndef CHRONONMOTION_NUMBERKEYFRAMETRACK_HPP
#define CHRONONMOTION_NUMBERKEYFRAMETRACK_HPP

#include "chrononmotion/animation/KeyframeTrack.hpp"

namespace chrononmotion {

    class NumberKeyframeTrack : public KeyframeTrack {

    public:
        NumberKeyframeTrack(const std::string& name,
                            const std::vector<float>& times,
                            const std::vector<float>& values,
                            std::optional<Interpolation> interpolation = {})
            : KeyframeTrack(name, times, values, interpolation) {}

        [[nodiscard]] std::string ValueTypeName() const override { return "number"; }

        static std::shared_ptr<NumberKeyframeTrack> create(
                const std::string& name,
                const std::vector<float>& times,
                const std::vector<float>& values,
                std::optional<Interpolation> interpolation = {}) {
            return std::make_shared<NumberKeyframeTrack>(name, times, values, interpolation);
        }
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_NUMBERKEYFRAMETRACK_HPP
