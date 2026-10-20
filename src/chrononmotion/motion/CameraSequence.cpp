#include "chrononmotion/motion/CameraSequence.hpp"

#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/motion/CameraScreenSpace.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion::camera {
    namespace {
        constexpr float kPi = 3.14159265358979323846f;
        using math::degToRad;

        float at(int frame, float fps) { return static_cast<float>(frame) / fps; }
        bool finiteSubject(const SequenceSubject& s) {
            return std::isfinite(s.center.x) && std::isfinite(s.center.y) &&
                   std::isfinite(s.center.z) && std::isfinite(s.halfWidth) &&
                   std::isfinite(s.halfHeight) && std::isfinite(s.fov);
        }

        struct LegPlan {
            CameraPose restA{}, restB{};
            float lateral{0.f}, height{0.f}, dollyApex{0.f}, yaw{0.f}, rollPeak{0.f};
            float fovPeak{0.f}, focusShift{0.f}, fStop{0.f};
            bool aimBoth{true};
            float travelEase{0.f};
        };

        CameraPose restPoseFor(const SequenceSubject& subject, const Vector2& viewport, float intensity) {
            const float fill = 0.62f + 0.10f * intensity;
            const float distance = framingDistance(subject, viewport, fill);
            CameraPose pose;
            pose.position = Vector3(subject.center.x, subject.center.y, subject.center.z + distance);
            pose.hasTarget = true;
            pose.target = subject.center;
            pose.fov = subject.fov;
            return pose;
        }

        LegPlan planLeg(Handoff handoff, const CameraPose& restA, const CameraPose& restB,
                        float intensity) {
            LegPlan plan;
            plan.restA = restA;
            plan.restB = restB;
            switch (handoff) {
                case Handoff::PushThrough:
                    plan.dollyApex = -(restA.position - restA.target).length() * 0.10f * intensity;
                    plan.fovPeak = restA.fov + 4.f * intensity;
                    break;
                case Handoff::LateralSwipe:
                    plan.lateral = 190.f * intensity;
                    plan.aimBoth = false;
                    break;
                case Handoff::ArcCarry:
                    plan.lateral = 230.f * intensity;
                    plan.height = 60.f * intensity;
                    break;
                case Handoff::OrbitHandoff:
                    plan.yaw = degToRad(26.f) * intensity;
                    break;
                case Handoff::RiseAndLand:
                    plan.height = 150.f * intensity;
                    plan.dollyApex = -(restA.position - restA.target).length() * 0.06f * intensity;
                    break;
                case Handoff::FocusRack:
                    plan.lateral = 26.f * intensity;
                    plan.focusShift = 1.f;
                    plan.fStop = 14.f;
                    break;
                case Handoff::PullBackReveal:
                    plan.dollyApex = -(restA.position - restA.target).length() * 0.22f * intensity;
                    plan.rollPeak = degToRad(-2.5f) * intensity;
                    plan.fovPeak = std::max(restA.fov, restB.fov) + 6.f * intensity;
                    break;
                case Handoff::WhipReframe:
                    plan.lateral = 120.f * intensity;
                    plan.travelEase = 1.f;
                    break;
            }
            return plan;
        }

        CameraPose apexPose(const LegPlan& plan, float progress) {
            CameraPose pose = plan.restA;
            CameraRig::pan(pose, Vector2(plan.lateral * 0.5f, plan.height * 0.5f));
            if (plan.dollyApex != 0.f) CameraRig::dolly(pose, plan.dollyApex);
            if (plan.yaw != 0.f) {
                CameraRig::orbitAround(pose, (plan.restA.target + plan.restB.target) * 0.5f,
                                       plan.yaw * progress, 0.f);
            }
            if (plan.rollPeak != 0.f) CameraRig::roll(pose, plan.rollPeak);
            pose.target = plan.aimBoth ? (plan.restA.target + plan.restB.target) * 0.5f : plan.restB.target;
            if (plan.focusShift != 0.f) {
                const float distA = (plan.restA.position - plan.restA.target).length();
                const float distB = (plan.restB.position - plan.restB.target).length();
                pose.focusDistance = distA + (distB - distA) * progress;
            }
            if (plan.fovPeak != 0.f) pose.fov = plan.fovPeak;
            return pose;
        }

        void keyLeg(CameraRig& rig, const LegPlan& plan, int inFrame, int travelFrames, float fps) {
            const float t0 = at(inFrame, fps);
            const float tApex = at(inFrame + travelFrames / 2, fps);
            const float t1 = at(inFrame + travelFrames, fps);
            const CameraPose apex = apexPose(plan, 0.5f);
            auto& position = rig.positionTrack();
            auto& target = rig.targetTrack();
            const Easing approach = plan.travelEase > 0.f
                    ? Easing::cubicBezier(0.7f, 0.9f, 0.7f, 0.9f)
                    : Easing::cubicBezier(0.5f, 0.f, 0.7f, 0.9f);
            const Easing land = Easing::cubicBezier(0.3f, 0.1f, 0.5f, 1.f);
            position.add(t0, plan.restA.position, approach);
            position.add(tApex, apex.position, land);
            position.add(t1, plan.restB.position, Easing::easeInOut());
            target.add(t0, plan.restA.target, approach);
            target.add(tApex, apex.target, land);
            target.add(t1, plan.restB.target, Easing::easeInOut());
            if (plan.rollPeak != 0.f) {
                rig.rollTrack().add(t0, 0.f, Easing::easeInOut())
                    .add(tApex, plan.rollPeak, Easing::easeOut()).add(t1, 0.f, Easing::easeInOut());
            }
            if (plan.fovPeak != 0.f) {
                rig.fovTrack().add(t0, plan.restA.fov, Easing::easeInOut())
                    .add(tApex, plan.fovPeak, Easing::easeOut())
                    .add(t1, plan.restB.fov, Easing::easeInOut());
            }
            if (plan.focusShift != 0.f || plan.fStop != 0.f) {
                const float distA = (plan.restA.position - plan.restA.target).length();
                const float distB = (plan.restB.position - plan.restB.target).length();
                rig.focusDistanceTrack().add(t0, distA, Easing::easeInOut())
                    .add(tApex, (distA + distB) * 0.5f, Easing::easeInOut())
                    .add(t1, distB, Easing::easeInOut());
                if (plan.fStop != 0.f)
                    rig.fStopTrack().add(t0, plan.fStop, Easing::easeInOut())
                        .add(t1, plan.fStop, Easing::easeInOut());
            }
        }

        bool projectInside(const SequenceSubject& subject, const CameraPose& pose, const Vector2& viewport) {
            ScreenCameraIntrinsics intrinsics;
            intrinsics.viewport = viewport;
            intrinsics.fovDegrees = pose.fov;
            const Vector2 screen = projectToScreen(subject.center, pose, intrinsics);
            return screen.x >= 0.05f * viewport.x && screen.x <= 0.95f * viewport.x &&
                   screen.y >= 0.08f * viewport.y && screen.y <= 0.92f * viewport.y;
        }
    }

    float framingDistance(const SequenceSubject& subject, const Vector2& viewport, float fill) {
        if (!finiteSubject(subject) || !(subject.halfWidth > 0.f) || !(subject.halfHeight > 0.f) ||
            !(subject.fov > 0.f) || !(viewport.x > 0.f) || !(viewport.y > 0.f))
            throw std::invalid_argument("framingDistance: subject, FOV and viewport must be positive and finite");
        const float aspect = viewport.x / std::max(viewport.y, 1.f);
        const float clampedFill = std::clamp(fill, 0.1f, 0.98f);
        return framingDistanceForBounds(subject.halfWidth, subject.halfHeight, subject.fov,
                                        aspect, clampedFill);
    }

    void applySequence(CameraRig& rig, const CameraSequence& sequence, float fps, const Vector2& viewport) {
        if (sequence.beats.size() < 2) throw std::invalid_argument("applySequence: at least two beats required");
        if (sequence.travelFrames <= 0) throw std::invalid_argument("applySequence: travelFrames must be positive");
        if (!(sequence.intensity > 0.f) || !std::isfinite(sequence.intensity))
            throw std::invalid_argument("applySequence: intensity must be positive and finite");
        if (!(fps > 0.f) || !std::isfinite(fps) || !(viewport.x > 0.f) || !(viewport.y > 0.f))
            throw std::invalid_argument("applySequence: fps and viewport must be positive and finite");
        if (sequence.inFrame < 0) throw std::invalid_argument("applySequence: inFrame must be non-negative");
        for (const auto& beat : sequence.beats) {
            if (beat.hold < 6) throw std::invalid_argument("applySequence: each hold must be at least six frames");
            if (!finiteSubject(beat.subject)) throw std::invalid_argument("applySequence: subjects must be finite");
        }

        int frame = sequence.inFrame;
        CameraPose firstRest{};
        bool first = true;
        for (std::size_t i = 0; i + 1 < sequence.beats.size(); ++i) {
            const auto& a = sequence.beats[i];
            const auto& b = sequence.beats[i + 1];
            const CameraPose restA = restPoseFor(a.subject, viewport, sequence.intensity);
            const CameraPose restB = restPoseFor(b.subject, viewport, sequence.intensity);
            if (first) { firstRest = restA; first = false; }

            const int holdEnd = frame + a.hold;
            const float tIn = at(frame, fps);
            const float tSettle = at(frame + std::min(a.hold - 2, 8), fps);
            const float tOut = at(holdEnd, fps);
            rig.positionTrack().add(tIn, restA.position, Easing::linear())
                .add(tSettle, restA.position, Easing::easeInOut()).add(tOut, restA.position, Easing::linear());
            rig.targetTrack().add(tIn, restA.target, Easing::linear())
                .add(tSettle, restA.target, Easing::easeInOut()).add(tOut, restA.target, Easing::linear());
            rig.fovTrack().add(tIn, restA.fov, Easing::linear()).add(tOut, restA.fov, Easing::linear());

            const LegPlan plan = planLeg(sequence.handoff, restA, restB, sequence.intensity);
            keyLeg(rig, plan, holdEnd, sequence.travelFrames, fps);
            frame = holdEnd + sequence.travelFrames;
        }

        const auto& last = sequence.beats.back();
        const CameraPose rest = restPoseFor(last.subject, viewport, sequence.intensity);
        const float tIn = at(frame, fps), tOut = at(frame + last.hold, fps);
        rig.positionTrack().add(tIn, rest.position, Easing::linear())
            .add(tIn + 8.f / fps, rest.position, Easing::easeInOut()).add(tOut, rest.position, Easing::linear());
        rig.targetTrack().add(tIn, rest.target, Easing::linear())
            .add(tIn + 8.f / fps, rest.target, Easing::easeInOut()).add(tOut, rest.target, Easing::linear());
        rig.fovTrack().add(tIn, rest.fov, Easing::linear()).add(tOut, rest.fov, Easing::linear());

        rig.setTarget(firstRest.target).setPosition(firstRest.position).setFov(firstRest.fov)
            .setAspect(viewport.x / std::max(viewport.y, 1.f));
        for (const auto& beat : sequence.beats) {
            const CameraPose pose = restPoseFor(beat.subject, viewport, sequence.intensity);
            if (!projectInside(beat.subject, pose, viewport))
                throw std::invalid_argument("applySequence: a rest framing leaves a subject outside the safe area");
        }
    }
}
