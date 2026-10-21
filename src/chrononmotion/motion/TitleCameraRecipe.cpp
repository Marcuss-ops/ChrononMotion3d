#include "chrononmotion/motion/TitleCameraRecipe.hpp"

#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

namespace chrononmotion::motion::camera {
    namespace {
    constexpr std::array<TitleMove, 25> kTitleMoves{
        TitleMove::SlowPush, TitleMove::SlowPullOut, TitleMove::LeftDrift,
        TitleMove::RightDrift, TitleMove::VerticalRise, TitleMove::VerticalDescend,
        TitleMove::MicroOrbitLeft, TitleMove::MicroOrbitRight, TitleMove::ArcPush,
        TitleMove::ArcPull, TitleMove::LowAnglePush, TitleMove::HighAngleSettle,
        TitleMove::RollSettle, TitleMove::RollPass, TitleMove::DollyZoomSubtle,
        TitleMove::FocusPush, TitleMove::ParallaxSide, TitleMove::ParallaxPush,
        TitleMove::WhipSettle, TitleMove::CornerReveal, TitleMove::CrashPush,
        TitleMove::TopDownSettle, TitleMove::HandheldMicro, TitleMove::OrbitWide,
        TitleMove::FloorRise
    };
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

std::span<const TitleMove> titleCameraMoves() noexcept { return kTitleMoves; }

std::string_view titleCameraMoveId(TitleMove move) noexcept {
    switch (move) {
        case TitleMove::SlowPush: return "title_camera_slow_push";
        case TitleMove::SlowPullOut: return "title_camera_slow_pull_out";
        case TitleMove::LeftDrift: return "title_camera_left_drift";
        case TitleMove::RightDrift: return "title_camera_right_drift";
        case TitleMove::VerticalRise: return "title_camera_vertical_rise";
        case TitleMove::VerticalDescend: return "title_camera_vertical_descend";
        case TitleMove::MicroOrbitLeft: return "title_camera_micro_orbit_left";
        case TitleMove::MicroOrbitRight: return "title_camera_micro_orbit_right";
        case TitleMove::ArcPush: return "title_camera_arc_push";
        case TitleMove::ArcPull: return "title_camera_arc_pull";
        case TitleMove::LowAnglePush: return "title_camera_low_angle_push";
        case TitleMove::HighAngleSettle: return "title_camera_high_angle_settle";
        case TitleMove::RollSettle: return "title_camera_roll_settle";
        case TitleMove::RollPass: return "title_camera_roll_pass";
        case TitleMove::DollyZoomSubtle: return "title_camera_dolly_zoom_subtle";
        case TitleMove::FocusPush: return "title_camera_focus_push";
        case TitleMove::ParallaxSide: return "title_camera_parallax_side";
        case TitleMove::ParallaxPush: return "title_camera_parallax_push";
        case TitleMove::WhipSettle: return "title_camera_whip_settle";
        case TitleMove::CornerReveal: return "title_camera_corner_reveal";
        case TitleMove::CrashPush: return "title_camera_crash_push";
        case TitleMove::TopDownSettle: return "title_camera_top_down_settle";
        case TitleMove::HandheldMicro: return "title_camera_handheld_micro";
        case TitleMove::OrbitWide: return "title_camera_orbit_wide";
        case TitleMove::FloorRise: return "title_camera_floor_rise";
    }
    return "title_camera_unknown";
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

    void applyTitleCameraRequest(CameraRig& rig, const TitleCameraRequest& request) {
        if (request.duration < 12 || request.inFrame < 0 || !(request.fps > 0.f) ||
            !std::isfinite(request.fps) || !(request.viewport.x > 0.f) || !(request.viewport.y > 0.f) ||
            !std::isfinite(request.anchor.x) || !std::isfinite(request.anchor.y) ||
            !std::isfinite(request.anchor.z))
            throw std::invalid_argument("applyTitleCameraRequest: invalid timing, viewport or anchor");

        const TitleCameraPlan plan = resolveTitleCameraPlan(request.move, request.shot);
        const float fps = request.fps;
        const int in = request.inFrame;
        const int settle = std::clamp(request.duration * 2 / 10, 6, 12);
        const int travelEnd = in + request.duration - settle;
        const int end = in + request.duration;
        const float t0 = static_cast<float>(in) / fps;
        const float t1 = static_cast<float>(travelEnd) / fps;
        const float t2 = static_cast<float>(end) / fps;
        const float tMid = static_cast<float>((in + travelEnd) / 2) / fps;
        const bool whip = plan.whipAcquisition > 0.f;
        const bool orbits = whip || plan.yawStart != 0.f || plan.yawEnd != 0.f;
        const bool hasRoll = plan.rollStart != 0.f || plan.rollPeak != 0.f || plan.rollEnd != 0.f;
        const bool hasFov = plan.fovEnd != 0.f;
        const bool hasFocus = plan.focusPull != 0.f;
        const Vector3& anchor = request.anchor;
        CameraPose rest;
        rest.position = {anchor.x, anchor.y, anchor.z + plan.distance};
        rest.hasTarget = true;
        rest.target = anchor;
        rest.fov = plan.fov;

        CameraPose start = rest;
        if (orbits) CameraRig::orbitAround(start, anchor, plan.yawStart, 0.f);
        else if (plan.arcBow != 0.f) CameraRig::pan(start, Vector2(plan.restLateral, 0.f));
        else CameraRig::pan(start, Vector2(-plan.panX * .5f, plan.heightOffset));

        CameraPose finish = rest;
        if (orbits) CameraRig::orbitAround(finish, anchor, plan.yawEnd, 0.f);
        else if (plan.arcBow == 0.f) CameraRig::pan(finish, Vector2(plan.panX * .5f, plan.heightEnd));
        CameraRig::dolly(finish, plan.dolly);
        if (plan.arcBow != 0.f) CameraRig::pan(finish, Vector2(plan.restLateral * -.35f, 0.f));

        CameraPose mid = rest;
        CameraRig::pan(mid, Vector2(plan.arcBow != 0.f ? plan.restLateral + plan.arcBow * .5f : plan.panX * .5f,
                                    (plan.heightOffset + plan.heightEnd) * .5f));
        if (orbits) CameraRig::orbitAround(mid, anchor, (plan.yawStart + plan.yawEnd) * .5f, 0.f);
        CameraRig::dolly(mid, plan.dolly * .5f);

        auto& position = rig.positionTrack();
        auto& target = rig.targetTrack();
        if (whip) {
            position.add(t0, start.position, Easing::expoOut()).add(tMid, mid.position, Easing::easeInOut())
                .add(t1, finish.position, Easing::easeInOut());
            target.add(t0, anchor, Easing::easeInOut()).add(t1, anchor, Easing::linear());
        } else if (plan.arcBow != 0.f || orbits) {
            position.add(t0, start.position, Easing::easeIn()).add(tMid, mid.position, Easing::easeOut())
                .add(t1, finish.position, Easing::easeInOut());
            target.add(t0, anchor, Easing::easeInOut()).add(t1, anchor, Easing::linear());
        } else {
            position.add(t0, start.position, Easing::easeInOut()).add(t1, finish.position, Easing::easeInOut());
            target.add(t0, anchor, Easing::easeInOut()).add(t1, anchor, Easing::linear());
        }
        position.add(t2, finish.position, Easing::linear());
        target.add(t2, anchor, Easing::linear());

        if (hasRoll) {
            auto& roll = rig.rollTrack();
            roll.add(t0, plan.rollStart, Easing::easeInOut());
            if (plan.rollPeak != 0.f) roll.add(tMid, plan.rollPeak, Easing::easeInOut());
            roll.add(t1, plan.rollEnd, Easing::easeInOut()).add(t2, plan.rollEnd, Easing::linear());
        }
        if (hasFov) rig.fovTrack().add(t0, plan.fov, Easing::easeInOut())
            .add(t1, plan.fovEnd, Easing::easeInOut()).add(t2, plan.fovEnd, Easing::linear());
        if (hasFocus) rig.focusDistanceTrack().add(t0, plan.distance, Easing::easeInOut())
            .add(t1, plan.distance - plan.focusPull, Easing::easeInOut())
            .add(t2, plan.distance - plan.focusPull, Easing::linear());
        if (plan.fStop != 0.f) rig.fStopTrack().add(t1, plan.fStop, Easing::easeInOut())
            .add(t2, plan.fStop, Easing::linear());

        rig.setTarget(anchor).setPosition(finish.position).setFov(hasFov ? plan.fovEnd : plan.fov);
        ScreenCameraIntrinsics intrinsics;
        intrinsics.viewport = request.viewport;
        for (int frame = in; frame <= end; ++frame) {
            const CameraPose pose = rig.sample(static_cast<float>(frame) / fps);
            intrinsics.fovDegrees = pose.fov;
            const Vector2 screen = projectToScreen(anchor, pose, intrinsics);
            if (screen.x < .05f * intrinsics.viewport.x || screen.x > .95f * intrinsics.viewport.x ||
                screen.y < .08f * intrinsics.viewport.y || screen.y > .92f * intrinsics.viewport.y)
                throw std::invalid_argument("applyTitleCameraRequest: title anchor leaves safe area");
        }
    }

    PushSettlePlan resolvePushSettlePlan(PushSettlePreset preset, int durationFrames) {
        PushSettlePlan plan;
        plan.duration = durationFrames > 0 ? durationFrames : 90;
        plan.settleFrames = std::min(plan.settleFrames, plan.duration);
        switch (preset) {
            case PushSettlePreset::Tight:
                plan.startDistance = 1100.f; plan.endDistance = 1030.f;
                plan.startYawDegrees = 0.f;
                break;
            case PushSettlePreset::Quiet:
                plan.startDistance = 1150.f; plan.endDistance = 1130.f;
                plan.startYawDegrees = 0.f;
                break;
            case PushSettlePreset::GentleAngle:
                plan.startDistance = 1200.f; plan.endDistance = 1120.f;
                plan.startYawDegrees = -0.8f;
                break;
            case PushSettlePreset::Documentary:
                plan.startDistance = 1250.f; plan.endDistance = 1100.f;
                plan.startYawDegrees = -1.5f;
                break;
        }
        return plan;
    }

    void applyPushSettlePlan(CameraRig& rig, const PushSettlePlan& plan, float fps) {
        if (!std::isfinite(fps) || fps <= 0.f || plan.inFrame < 0 || plan.duration <= 0 ||
            plan.settleFrames < 0 || plan.settleFrames > plan.duration ||
            !std::isfinite(plan.startDistance) || !std::isfinite(plan.endDistance) ||
            !std::isfinite(plan.startYawDegrees) || !std::isfinite(plan.endYawDegrees))
            throw std::invalid_argument("applyPushSettlePlan: invalid camera motion values");
        const float t0 = static_cast<float>(plan.inFrame) / fps;
        const float tSettle = static_cast<float>(plan.inFrame + plan.duration - plan.settleFrames) / fps;
        const float t1 = static_cast<float>(plan.inFrame + plan.duration) / fps;
        const float midDistance = plan.startDistance + (plan.endDistance - plan.startDistance) * 0.85f;
        rig.positionTrack().add(t0, Vector3(0.f, 0.f, plan.startDistance), Easing::easeInOut())
            .add(tSettle, Vector3(0.f, 0.f, midDistance), Easing::easeOut())
            .add(t1, Vector3(0.f, 0.f, plan.endDistance), Easing::linear());
        if (std::fabs(plan.startYawDegrees - plan.endYawDegrees) > 0.0001f) {
            Quaternion start, end;
            start.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(plan.startYawDegrees));
            end.setFromAxisAngle(Vector3(0.f, 1.f, 0.f), math::degToRad(plan.endYawDegrees));
            rig.orientationTrack().add(t0, start, Easing::easeOut())
                .add(t1, end, Easing::linear());
        }
    }
}
