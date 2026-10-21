#include "chrononmotion/motion/CameraRecipe.hpp"

#include "chrononmotion/math/MathUtils.hpp"
#include "chrononmotion/motion/CameraFraming.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion::camera {
    namespace {
        using math::degToRad;
        constexpr float kNoMid = -1000.f;

        float at(int frame, float fps) { return static_cast<float>(frame) / fps; }
        bool hasMid(float value) { return value > kNoMid * 0.5f; }
        bool finiteSubject(const RecipeSubject& s) {
            return std::isfinite(s.center.x) && std::isfinite(s.center.y) &&
                   std::isfinite(s.center.z) && std::isfinite(s.halfWidth) &&
                   std::isfinite(s.halfHeight);
        }
        bool finiteVector(const Vector3& v) {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        struct LegPose { float dist{1.f}, x{0.f}, y{0.f}, yaw{0.f}, pitch{0.f}, roll{0.f}, focus{-1.f}; };
        LegPose resolveLeg(float dist, float x, float y, float yaw, float pitch,
                           float roll, float focus, float scale) {
            return {1.f + (dist - 1.f) * scale, x * scale, y * scale,
                    yaw * scale, pitch * scale, roll * scale, focus};
        }

        CameraPose poseFor(const CameraPose& rest, const RecipeSubject& subject,
                           float ref, float frameW, float frameH, const LegPose& leg) {
            CameraPose pose = rest;
            CameraRig::pan(pose, Vector2(leg.x * frameW, -leg.y * frameH));
            if (leg.yaw != 0.f || leg.pitch != 0.f)
                CameraRig::orbitAround(pose, subject.center, degToRad(leg.yaw), degToRad(leg.pitch));
            CameraRig::dolly(pose, ref * (1.f - leg.dist));
            return pose;
        }

        float focusFor(float percent, float near, float far) { return near + (far - near) * (percent / 100.f); }

        void keyLeg(CameraRig& rig, const CameraPose& pose, float roll, bool hasRoll,
                    float focus, bool hasFocus, float t, const Easing& easing) {
            rig.positionTrack().add(t, pose.position, easing);
            rig.targetTrack().add(t, pose.target, Easing::easeInOut());
            if (hasRoll) rig.rollTrack().add(t, roll, easing);
            if (hasFocus) rig.focusDistanceTrack().add(t, focus, easing);
        }

        void applySingleShot(CameraRig& rig, const CameraMotionRecipe& spec,
                             const CameraRecipeShot& shot, int duration, float fps,
                             const Vector2& viewport) {
            const float scale = intensityFactor(shot.intensity);
            const float ref = referenceDistance(shot.subject, shot.fov, viewport);
            CameraPose rest;
            rest.position = Vector3(shot.subject.center.x, shot.subject.center.y, shot.subject.center.z + ref);
            rest.hasTarget = true; rest.target = shot.subject.center; rest.fov = shot.fov;

            const LegPose start = resolveLeg(spec.distStart, spec.xStart, spec.yStart, spec.yawStart,
                                             spec.pitchStart, spec.rollStart, spec.focusStart, scale);
            const LegPose end = resolveLeg(spec.distEnd, spec.xEnd, spec.yEnd, spec.yawEnd,
                                           spec.pitchEnd, spec.rollEnd, spec.focusEnd, scale);
            const bool hasDistMid = hasMid(spec.distMid), hasPosMid = hasMid(spec.xMid) || hasMid(spec.yMid);
            const LegPose mid = resolveLeg(hasDistMid ? spec.distMid : (spec.distStart + spec.distEnd) * .5f,
                                           hasMid(spec.xMid) ? spec.xMid : (spec.xStart + spec.xEnd) * .5f,
                                           hasMid(spec.yMid) ? spec.yMid : (spec.yStart + spec.yEnd) * .5f,
                                           (spec.yawStart + spec.yawEnd) * .5f,
                                           (spec.pitchStart + spec.pitchEnd) * .5f,
                                           (spec.rollStart + spec.rollEnd) * .5f,
                                           (spec.focusStart + spec.focusEnd) * .5f, scale);
            const bool useMid = hasDistMid || hasPosMid;
            const bool hasRoll = spec.rollStart != 0.f || hasMid(spec.rollPeak) || spec.rollEnd != 0.f;
            const bool hasFocus = spec.focusStart >= 0.f && spec.focusEnd >= 0.f;
            if (hasFocus && !(shot.focusFar > shot.focusNear))
                throw std::invalid_argument("applyRecipe: focusFar must exceed focusNear");

            const int in = shot.inFrame;
            const int travel0 = in + static_cast<int>(std::round(duration * spec.travelStart));
            const int travel1 = in + static_cast<int>(std::round(duration * spec.travelEnd));
            const int endFrame = in + duration;
            const Easing travel = easingFor(spec.easing);
            const CameraPose startPose = poseFor(rest, shot.subject, ref, viewport.x, viewport.y, start);
            const CameraPose midPose = poseFor(rest, shot.subject, ref, viewport.x, viewport.y, mid);
            const CameraPose endPose = poseFor(rest, shot.subject, ref, viewport.x, viewport.y, end);
            const float rollStart = degToRad(start.roll), rollMid = degToRad(mid.roll), rollEnd = degToRad(end.roll);
            const float focusStart = hasFocus ? focusFor(start.focus, shot.focusNear, shot.focusFar) : 0.f;
            const float focusMid = hasFocus ? focusFor(mid.focus, shot.focusNear, shot.focusFar) : 0.f;
            const float focusEnd = hasFocus ? focusFor(end.focus, shot.focusNear, shot.focusFar) : 0.f;

            keyLeg(rig, startPose, rollStart, hasRoll, focusStart, hasFocus, at(in, fps), travel);
            if (travel0 != in) keyLeg(rig, startPose, rollStart, hasRoll, focusStart, hasFocus, at(travel0, fps), travel);
            if (useMid) {
                const int midFrame = in + static_cast<int>(std::round(duration * (hasDistMid ? spec.distMidAt : spec.posMidAt)));
                const float rollMidKey = hasMid(spec.rollPeak) ? degToRad(spec.rollPeak * scale) : rollMid;
                keyLeg(rig, midPose, rollMidKey, hasRoll, focusMid, hasFocus, at(midFrame, fps), travel);
            } else if (hasRoll && hasMid(spec.rollPeak)) {
                rig.rollTrack().add(at((travel0 + travel1) / 2, fps), degToRad(spec.rollPeak * scale), travel);
            }
            keyLeg(rig, endPose, rollEnd, hasRoll, focusEnd, hasFocus, at(travel1, fps), travel);
            keyLeg(rig, endPose, rollEnd, hasRoll, focusEnd, hasFocus, at(endFrame, fps), Easing::linear());
            rig.setTarget(rest.target).setPosition(endPose.position).setFov(shot.fov);
            for (float v : {ref, endPose.position.x, endPose.position.y, endPose.position.z})
                if (!std::isfinite(v)) throw std::invalid_argument("applyRecipe: non-finite resolved key");
        }

        void applyTwoShot(CameraRig& rig, const CameraMotionRecipe& spec,
                          const CameraRecipeShot& shot, int duration, float fps,
                          const Vector2& viewport) {
            const float dt = 1.f / fps, scale = intensityFactor(shot.intensity);
            const float ref = referenceDistance(shot.subject, shot.fov, viewport);
            CameraPose rest;
            rest.position = Vector3(shot.subject.center.x, shot.subject.center.y, shot.subject.center.z + ref);
            rest.hasTarget = true; rest.target = shot.subject.center; rest.fov = shot.fov;

            const LegPose outEnd = resolveLeg(spec.distEnd, spec.xEnd, spec.yEnd, spec.yawEnd,
                                              spec.pitchEnd, spec.rollEnd, spec.focusEnd, scale);
            LegPose inStart = outEnd;
            inStart.x = -outEnd.x; inStart.y = -outEnd.y; inStart.yaw = -outEnd.yaw;
            inStart.pitch = -outEnd.pitch; inStart.roll = -outEnd.roll;
            const LegPose inEnd{1.f,0.f,0.f,0.f,0.f,0.f,spec.focusStart >= 0.f ? spec.focusStart : -1.f};
            const bool hasRoll = spec.rollStart != 0.f || spec.rollEnd != 0.f;
            const bool hasFocus = spec.focusStart >= 0.f && spec.focusEnd >= 0.f;
            if (hasFocus && !(shot.focusFar > shot.focusNear))
                throw std::invalid_argument("applyRecipe: focusFar must exceed focusNear");
            const int in = shot.inFrame, cut = in + static_cast<int>(std::round(duration * spec.cutAt));
            const int endFrame = in + duration;
            const float cutTime = at(cut, fps), inTime = cutTime + dt;
            const Easing outEase = std::string(spec.easing) == "whip_in" ? Easing::easeIn() : Easing::easeInOut();
            const CameraPose outPose = poseFor(rest, shot.subject, ref, viewport.x, viewport.y, outEnd);
            const CameraPose inPose = poseFor(rest, shot.subject, ref, viewport.x, viewport.y, inStart);
            const float rollOut = degToRad(outEnd.roll), rollIn = degToRad(inStart.roll);
            const float focusOut = hasFocus ? focusFor(outEnd.focus, shot.focusNear, shot.focusFar) : 0.f;
            const float focusIn = hasFocus ? focusFor(inStart.focus, shot.focusNear, shot.focusFar) : 0.f;
            const float focusRest = hasFocus ? focusFor(inEnd.focus, shot.focusNear, shot.focusFar) : 0.f;
            keyLeg(rig, rest, degToRad(spec.rollStart * scale), hasRoll,
                   hasFocus ? focusFor(spec.focusStart, shot.focusNear, shot.focusFar) : 0.f,
                   hasFocus, at(in, fps), outEase);
            rig.positionTrack().add(cutTime, outPose.position, outEase);
            rig.targetTrack().add(cutTime, outPose.target, Easing::easeInOut());
            if (hasRoll) rig.rollTrack().add(cutTime, rollOut, outEase);
            if (hasFocus) rig.focusDistanceTrack().add(cutTime, focusOut, outEase);
            const Easing inEase = spec.snapIn ? Easing::expoOut() : Easing::easeInOut();
            keyLeg(rig, inPose, rollIn, hasRoll, focusIn, hasFocus, inTime, inEase);
            keyLeg(rig, rest, 0.f, hasRoll, focusRest, hasFocus, at(endFrame, fps), Easing::linear());
            rig.setTarget(rest.target).setPosition(rest.position).setFov(shot.fov);
        }
    }

    float referenceDistance(const RecipeSubject& subject, float fovDegrees, const Vector2& viewport, float frameFill) {
        if (!finiteSubject(subject) || subject.halfWidth <= 0.f || subject.halfHeight <= 0.f)
            throw std::invalid_argument("referenceDistance: subject must be finite with positive extents");
        if (!(fovDegrees > 0.f) || !(viewport.x > 0.f) || !(viewport.y > 0.f))
            throw std::invalid_argument("referenceDistance: fov and viewport must be positive");
        if (!(frameFill > 0.f) || frameFill > 1.f)
            throw std::invalid_argument("referenceDistance: frameFill must be in (0, 1]");
        const float margin = (1.f - frameFill) * 0.5f;
        return CameraFramingSolver::distanceForBounds(subject.halfWidth, subject.halfHeight,
            0.f, fovDegrees, viewport, CameraSafeArea{margin, margin, margin, margin});
    }

    Easing easingFor(const std::string& name) {
        if (name == "cinematic" || name == "smooth") return Easing::easeInOut();
        if (name == "gentle_out") return Easing::easeOut();
        if (name == "snappy_out" || name == "whip_out") return Easing::expoOut();
        if (name == "whip_in") return Easing::easeIn();
        throw std::invalid_argument("easingFor: unknown easing " + name);
    }

    float intensityFactor(RecipeIntensity intensity) noexcept {
        switch (intensity) {
            case RecipeIntensity::Subtle: return .5f;
            case RecipeIntensity::Normal: return 1.f;
            case RecipeIntensity::Strong: return 1.5f;
        }
        return 1.f;
    }

    void applyCameraTrack(CameraRig& rig, std::span<const CameraTrackWaypoint> waypoints,
                          float fps) {
        if (!(fps > 0.f) || !std::isfinite(fps))
            throw std::invalid_argument("applyCameraTrack: fps must be positive and finite");
        if (waypoints.size() < 2)
            throw std::invalid_argument("applyCameraTrack: at least two waypoints are required");
        std::vector<CameraTrackWaypoint> ordered(waypoints.begin(), waypoints.end());
        std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
            return a.frame < b.frame;
        });
        int previousFrame = -1;
        CameraPose last{};
        for (const auto& waypoint : ordered) {
            if (waypoint.frame < 0 || waypoint.frame <= previousFrame ||
                !finiteVector(waypoint.position) || !finiteVector(waypoint.target))
                throw std::invalid_argument("applyCameraTrack: invalid or unordered waypoint");
            const float time = at(waypoint.frame, fps);
            rig.positionTrack().add(time, waypoint.position, waypoint.easing);
            rig.targetTrack().add(time, waypoint.target, waypoint.easing);
            last.position = waypoint.position;
            last.target = waypoint.target;
            last.hasTarget = true;
            previousFrame = waypoint.frame;
        }
        rig.setPosition(last.position).setTarget(last.target);
    }

    void applyRecipe(CameraRig& rig, const CameraMotionRecipe& recipe,
                     const CameraRecipeShot& shot, float fps, const Vector2& viewport) {
        const int duration = shot.durationOverride > 0 ? shot.durationOverride : recipe.durationFrames;
        if (!(fps > 0.f) || !std::isfinite(fps) || !(viewport.x > 0.f) || !(viewport.y > 0.f))
            throw std::invalid_argument("applyRecipe: fps and viewport must be positive and finite");
        if (duration < 8) throw std::invalid_argument("applyRecipe: duration must be at least 8 frames");
        if (shot.inFrame < 0) throw std::invalid_argument("applyRecipe: inFrame must be non-negative");
        if (!finiteSubject(shot.subject) || shot.subject.halfWidth <= 0.f || shot.subject.halfHeight <= 0.f)
            throw std::invalid_argument("applyRecipe: subject must be finite with positive extents");
        if (!(shot.fov > 0.f) || !std::isfinite(shot.fov))
            throw std::invalid_argument("applyRecipe: fov must be positive and finite");
        if (!(recipe.travelStart >= 0.f) || !(recipe.travelEnd <= 1.f) || !(recipe.travelEnd > recipe.travelStart))
            throw std::invalid_argument("applyRecipe: corrupt travel window");
        if (recipe.twoShot && (!(recipe.cutAt > 0.f) || !(recipe.cutAt < 1.f)))
            throw std::invalid_argument("applyRecipe: cutAt must be inside (0, 1)");
        if (!recipe.waypoints.empty()) {
            if (recipe.waypoints.size() < 2 || recipe.waypoints.front().progress != 0.f ||
                recipe.waypoints.back().progress != 1.f)
                throw std::invalid_argument("applyRecipe: tracked recipe requires waypoint endpoints at 0 and 1");
            CameraPose last{};
            float previousProgress = -1.f;
            int previousFrame = -1;
            for (const auto& waypoint : recipe.waypoints) {
                if (!std::isfinite(waypoint.progress) || waypoint.progress <= previousProgress ||
                    waypoint.progress < 0.f || waypoint.progress > 1.f ||
                    !finiteVector(waypoint.positionOffset) || !finiteVector(waypoint.targetOffset) ||
                    !std::isfinite(waypoint.fov) || waypoint.fov <= 0.f || waypoint.fov >= 180.f ||
                    waypoint.easing == nullptr)
                    throw std::invalid_argument("applyRecipe: invalid tracked camera waypoint");
                const int frame = shot.inFrame + static_cast<int>(std::round(duration * waypoint.progress));
                if (frame <= previousFrame)
                    throw std::invalid_argument("applyRecipe: tracked camera waypoints collapse to the same frame");
                const float time = at(frame, fps);
                const Vector3 position = shot.subject.center.clone().add(waypoint.positionOffset);
                const Vector3 target = shot.subject.center.clone().add(waypoint.targetOffset);
                const Easing easing = easingFor(waypoint.easing);
                rig.positionTrack().add(time, position, easing);
                rig.targetTrack().add(time, target, easing);
                rig.fovTrack().add(time, waypoint.fov, easing);
                last.position = position;
                last.target = target;
                last.fov = waypoint.fov;
                last.hasTarget = true;
                previousProgress = waypoint.progress;
                previousFrame = frame;
            }
            rig.setPosition(last.position).setTarget(last.target).setFov(last.fov);
            return;
        }
        if (recipe.twoShot) applyTwoShot(rig, recipe, shot, duration, fps, viewport);
        else applySingleShot(rig, recipe, shot, duration, fps, viewport);
    }
}
