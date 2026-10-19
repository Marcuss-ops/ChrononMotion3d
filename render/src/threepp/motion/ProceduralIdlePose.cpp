#include "threepp/motion/ProceduralIdlePose.hpp"

#include "threepp/core/Object3D.hpp"

#include <cmath>
#include <stdexcept>

namespace threepp::motion {

    ProceduralIdlePose::ProceduralIdlePose(Object3D& subject)
        : ProceduralIdlePose(subject, Options{}) {}

    ProceduralIdlePose::ProceduralIdlePose(Object3D& subject, Options options)
        : subject_(&subject), basePosition_(subject.position), baseRotation_(subject.rotation), options_(options) {
        if (!std::isfinite(options_.cycleSeconds) || options_.cycleSeconds <= 0.f ||
            !std::isfinite(options_.yawRadians) || options_.yawRadians < 0.f ||
            !std::isfinite(options_.leanRadians) || options_.leanRadians < 0.f ||
            !std::isfinite(options_.bobAmplitude) || options_.bobAmplitude < 0.f ||
            !std::isfinite(options_.phaseRadians)) {
            throw std::invalid_argument("ProceduralIdlePose: options must be finite and amplitudes non-negative");
        }
    }

    void ProceduralIdlePose::apply(float timelineSeconds) const {
        if (!std::isfinite(timelineSeconds)) {
            throw std::invalid_argument("ProceduralIdlePose::apply: timelineSeconds must be finite");
        }

        constexpr float tau = 6.2831853071795864769f;
        const float phase = tau * timelineSeconds / options_.cycleSeconds + options_.phaseRadians;
        subject_->position.set(basePosition_.x,
                               basePosition_.y + options_.bobAmplitude * std::sin(phase * 0.5f),
                               basePosition_.z);
        subject_->rotation.set(baseRotation_.x + options_.leanRadians * std::sin(phase),
                               baseRotation_.y + options_.yawRadians * std::sin(phase * 0.63f + 0.8f),
                               baseRotation_.z);
    }

}// namespace threepp::motion
