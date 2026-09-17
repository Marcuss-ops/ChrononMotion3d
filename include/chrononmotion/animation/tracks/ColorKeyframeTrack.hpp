
#ifndef CHRONONMOTION_COLORKEYFRAMETRACK_HPP
#define CHRONONMOTION_COLORKEYFRAMETRACK_HPP

#include "chrononmotion/animation/KeyframeTrack.hpp"

namespace chrononmotion {

    class ColorKeyframeTrack : public KeyframeTrack {

    public:
        ColorKeyframeTrack(const std::string& name,
                           const std::vector<float>& times,
                           const std::vector<float>& values,
                           std::optional<Interpolation> interpolation = {})
            : KeyframeTrack(name, times, values, interpolation) {}

        [[nodiscard]] std::string ValueTypeName() const override { return "color"; }

        static std::shared_ptr<ColorKeyframeTrack> create(
                const std::string& name,
                const std::vector<float>& times,
                const std::vector<float>& values,
                std::optional<Interpolation> interpolation = {}) {
            return std::make_shared<ColorKeyframeTrack>(name, times, values, interpolation);
        }
    };

}// namespace chrononmotion

#endif//CHRONONMOTION_COLORKEYFRAMETRACK_HPP
