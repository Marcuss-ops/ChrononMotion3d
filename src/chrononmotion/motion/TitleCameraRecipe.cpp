#include "chrononmotion/motion/TitleCameraRecipe.hpp"

#include "chrononmotion/math/MathUtils.hpp"

#include <algorithm>
#include <cmath>

namespace chrononmotion::motion::camera {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        float intensityScale(TitleIntensity intensity) {
            switch (intensity) {
                case TitleIntensity::Subtle: return 0.55f;
                case TitleIntensity::Editorial: return 1.f;
                case TitleIntensity::Cinematic: return 1.9f;
            }
            return 1.f;
        }
    }

    float titleFramingDistance(TitleFraming framing, float fovDegrees) {
        constexpr float width = 900.f;
        float coverage = 0.9f;
        switch (framing) {
            case TitleFraming::Hero: coverage = 0.72f; break;
            case TitleFraming::Medium: coverage = 0.55f; break;
            case TitleFraming::Wide: coverage = 0.40f; break;
            case TitleFraming::ExtremeClose: coverage = 0.88f; break;
        }
        const float tanHalf = std::tan(std::max(fovDegrees * 0.5f * kPi / 180.f, 1e-4f));
        return width / (2.f * tanHalf * (1920.f / 1080.f) * coverage);
    }

    TitleCameraPlan resolveTitleCameraPlan(TitleMove move, const TitleRecipeShot& shot) {
        const float loud = intensityScale(shot.intensity);
        TitleCameraPlan p;
        p.fov = shot.fov;
        p.distance = titleFramingDistance(shot.framing, p.fov);
        using math::degToRad;
        switch (move) {
            case TitleMove::SlowPush: p.dolly=p.distance*.34f*loud; break;
            case TitleMove::SlowPullOut: p.dolly=-p.distance*.30f*loud; break;
            case TitleMove::LeftDrift: p.panX=320.f*loud; break;
            case TitleMove::RightDrift: p.panX=-320.f*loud; break;
            case TitleMove::VerticalRise: p.heightOffset=-110.f*loud; break;
            case TitleMove::VerticalDescend: p.heightOffset=110.f*loud; break;
            case TitleMove::MicroOrbitLeft: p.yawStart=degToRad(8.f)*loud; p.yawEnd=degToRad(-4.f)*loud; break;
            case TitleMove::MicroOrbitRight: p.yawStart=degToRad(-8.f)*loud; p.yawEnd=degToRad(4.f)*loud; break;
            case TitleMove::ArcPush: p.restLateral=-140.f*loud; p.arcBow=150.f*loud; p.dolly=p.distance*.26f*loud; break;
            case TitleMove::ArcPull: p.restLateral=140.f*loud; p.arcBow=-150.f*loud; p.dolly=-p.distance*.24f*loud; break;
            case TitleMove::LowAnglePush: p.heightOffset=-70.f*loud; p.heightEnd=p.heightOffset; p.dolly=p.distance*.24f*loud; break;
            case TitleMove::HighAngleSettle: p.heightOffset=100.f*loud; p.heightEnd=p.heightOffset*.25f; break;
            case TitleMove::RollSettle: p.rollStart=degToRad(-3.f)*loud; p.dolly=p.distance*.08f*loud; break;
            case TitleMove::RollPass: p.rollStart=degToRad(-2.5f)*loud; p.rollPeak=degToRad(2.5f)*loud; p.dolly=p.distance*.10f*loud; break;
            case TitleMove::DollyZoomSubtle: { p.fovEnd=p.fov+9.f*loud; const float ratio=std::tan(p.fov*.5f*kPi/180.f)/std::tan(p.fovEnd*.5f*kPi/180.f); p.dolly=p.distance*(1.f-ratio); break; }
            case TitleMove::FocusPush: p.dolly=p.distance*.20f*loud; p.focusPull=p.distance*.20f*loud; p.fStop=12.f; break;
            case TitleMove::ParallaxSide: p.panX=260.f*loud; break;
            case TitleMove::ParallaxPush: p.dolly=p.distance*.30f*loud; break;
            case TitleMove::WhipSettle: p.whipAcquisition=.25f; p.yawStart=degToRad(-24.f); p.dolly=p.distance*.12f*loud; break;
            case TitleMove::CornerReveal: p.restLateral=-240.f*loud; p.arcBow=-170.f*loud; p.panX=300.f*loud; p.dolly=p.distance*.16f*loud; break;
            case TitleMove::CrashPush: p.dolly=p.distance*.44f*loud; break;
            case TitleMove::TopDownSettle: p.heightOffset=230.f*loud; p.dolly=p.distance*.06f*loud; break;
            case TitleMove::HandheldMicro: p.panX=64.f*loud; p.rollStart=degToRad(.9f)*loud; p.dolly=p.distance*.04f*loud; break;
            case TitleMove::OrbitWide: p.yawStart=degToRad(15.f)*loud; p.yawEnd=degToRad(-7.f)*loud; break;
            case TitleMove::FloorRise: p.heightOffset=-165.f*loud; p.dolly=p.distance*.18f*loud; break;
        }
        return p;
    }
}
