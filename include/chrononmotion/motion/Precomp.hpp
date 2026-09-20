#ifndef CHRONONMOTION_MOTION_PRECOMP_HPP
#define CHRONONMOTION_MOTION_PRECOMP_HPP

#include <memory>

namespace chrononmotion::motion {
    class MotionScene;

    struct CompositionRef {
        std::shared_ptr<MotionScene> scene{};
        float localFps{24.f};

        [[nodiscard]] bool empty() const noexcept { return !scene; }
        [[nodiscard]] bool valid() const noexcept { return scene && localFps > 0.f; }
    };
}

#endif//CHRONONMOTION_MOTION_PRECOMP_HPP
