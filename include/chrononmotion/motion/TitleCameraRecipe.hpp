#ifndef CHRONONMOTION_MOTION_TITLECAMERARECIPE_HPP
#define CHRONONMOTION_MOTION_TITLECAMERARECIPE_HPP

#include "chrononmotion/math/Vector2.hpp"
#include "chrononmotion/math/Vector3.hpp"
#include "chrononmotion/motion/CameraRig.hpp"

#include <cstdint>

namespace chrononmotion::motion::camera {

    enum class TitleFraming : std::uint8_t { Hero, Medium, Wide, ExtremeClose };
    enum class TitleIntensity : std::uint8_t { Subtle, Editorial, Cinematic };
    enum class TitleMove : std::uint8_t {
        SlowPush, SlowPullOut, LeftDrift, RightDrift, VerticalRise, VerticalDescend,
        MicroOrbitLeft, MicroOrbitRight, ArcPush, ArcPull, LowAnglePush,
        HighAngleSettle, RollSettle, RollPass, DollyZoomSubtle, FocusPush,
        ParallaxSide, ParallaxPush, WhipSettle, CornerReveal, CrashPush,
        TopDownSettle, HandheldMicro, OrbitWide, FloorRise
    };

    struct TitleRecipeShot {
        TitleFraming framing{TitleFraming::Medium};
        TitleIntensity intensity{TitleIntensity::Editorial};
        float fov{55.f};
    };

    struct TitleCameraPlan {
        float distance{1000.f}, fov{55.f}, dolly{0.f}, panX{0.f};
        float restLateral{0.f}, arcBow{0.f}, heightOffset{0.f}, heightEnd{0.f};
        float yawStart{0.f}, yawEnd{0.f};
        float rollStart{0.f}, rollPeak{0.f}, rollEnd{0.f};
        float fovEnd{0.f}, focusPull{0.f}, fStop{0.f}, whipAcquisition{0.f};
    };

    struct TitleCameraRequest {
        TitleMove move{TitleMove::SlowPush};
        TitleRecipeShot shot{};
        Vector3 anchor{0.f, 0.f, 0.f};
        int inFrame{0};
        int duration{135};
        float fps{30.f};
        Vector2 viewport{1920.f, 1080.f};
    };

    [[nodiscard]] float titleFramingDistance(TitleFraming framing, float fovDegrees);
    [[nodiscard]] TitleCameraPlan resolveTitleCameraPlan(TitleMove move, const TitleRecipeShot& shot);
    void applyTitleCameraRequest(CameraRig& rig, const TitleCameraRequest& request);

}

#endif
