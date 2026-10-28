#include "chrononmotion/motion/CameraFocusSequence.hpp"
#include "chrononmotion/math/Finite.hpp"

#include "chrononmotion/motion/CameraMotion.hpp"
#include "chrononmotion/motion/Easing.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace chrononmotion::motion::camera {
    namespace {
        float at(int frame, float fps) { return static_cast<float>(frame) / fps; }

        Vector3 positionFor(const FocusSubjectKey& subject, const FocusSequence& sequence) {
            return {subject.anchor.x + sequence.cameraAimOffset.x,
                    subject.anchor.y + sequence.cameraAimOffset.y,
                    subject.anchor.z + sequence.cameraDistance};
        }

        Vector3 targetFor(const FocusSubjectKey& subject, const FocusSequence& sequence) {
            return {subject.anchor.x + sequence.cameraAimOffset.x,
                    subject.anchor.y + sequence.cameraAimOffset.y,
                    subject.anchor.z - subject.focusDepth};
        }

        float axialFocusDistance(const FocusSubjectKey& subject, const FocusSequence& sequence) {
            return sequence.cameraDistance + subject.focusDepth;
        }
    }

    void applyFocusSequence(CameraRig& rig, const FocusSequence& sequence, float fps) {
        if (sequence.subjects.empty()) throw std::invalid_argument("applyFocusSequence: subjects are required");
        if (!(fps > 0.f) || !std::isfinite(fps) || !(sequence.cameraDistance > 0.f) ||
            !(sequence.baseFov > 0.f && sequence.baseFov < 180.f) ||
            !(sequence.focusFov > 0.f && sequence.focusFov <= sequence.baseFov) ||
            !(sequence.fStop > 0.f) || !(sequence.maxBlur >= 0.f) ||
            !(sequence.highlightGain >= 0.f) || sequence.focusTravelFrames < 2) {
            throw std::invalid_argument("applyFocusSequence: invalid timing or lens values");
        }
        int priorFocus = -1;
        for (const auto& subject : sequence.subjects) {
            if (!math::isFinite(subject.anchor) || !std::isfinite(subject.focusDepth) ||
                subject.focusDepth < 0.f || subject.focusFrame < 0 || subject.focusFrame <= priorFocus) {
                throw std::invalid_argument("applyFocusSequence: subject anchors and focus frames must be finite and ordered");
            }
            priorFocus = subject.focusFrame;
        }

        const auto& first = sequence.subjects.front();
        rig.setPosition(positionFor(first, sequence))
            .setTarget(targetFor(first, sequence))
            .setFov(sequence.baseFov)
            .setFocusDistance(axialFocusDistance(first, sequence))
            .setFStop(sequence.fStop)
            .setIrisBlades(sequence.irisBlades)
            .setMaxBlur(sequence.maxBlur)
            .setHighlightGain(sequence.highlightGain);

        const Easing ease = Easing::easeInOut();
        const auto& firstKey = sequence.subjects.front();
        rig.positionTrack().add(at(firstKey.focusFrame, fps), positionFor(firstKey, sequence), ease);
        rig.targetTrack().add(at(firstKey.focusFrame, fps), targetFor(firstKey, sequence), ease);
        rig.fovTrack().add(at(firstKey.focusFrame, fps), sequence.focusFov, ease);
        rig.focusDistanceTrack().add(at(firstKey.focusFrame, fps), axialFocusDistance(firstKey, sequence), ease);
        rig.fStopTrack().add(at(firstKey.focusFrame, fps), sequence.fStop, ease);

        for (std::size_t index = 1; index < sequence.subjects.size(); ++index) {
            const auto& previous = sequence.subjects[index - 1];
            const auto& current = sequence.subjects[index];
            const int halfTravel = std::max(1, sequence.focusTravelFrames / 2);
            const int travelStart = std::max(previous.focusFrame, current.focusFrame - halfTravel);
            const int travelEnd = current.focusFrame + halfTravel;
            const float t0 = at(travelStart, fps), t1 = at(travelEnd, fps);
            rig.positionTrack().add(t0, positionFor(previous, sequence), ease)
                .add(t1, positionFor(current, sequence), ease);
            rig.targetTrack().add(t0, targetFor(previous, sequence), ease)
                .add(t1, targetFor(current, sequence), ease);
            rig.fovTrack().add(t0, sequence.baseFov, ease).add(t1, sequence.focusFov, ease);
            rig.focusDistanceTrack().add(t0, axialFocusDistance(previous, sequence), ease)
                .add(t1, axialFocusDistance(current, sequence), ease);
            // Blur recedes while the focus plane travels (legacy 0.78 gain
            // ramp) and settles back. No gains are authored: through the
            // bridge, gain·0.78 is exactly an f/stop ramp of f/0.78 under the
            // contract ceiling of 32 — written directly instead of round-
            // tripping through blur gains.
            const float rampFStop = sequence.fStop > 0.f
                                        ? std::min(sequence.fStop / 0.78f, 32.f)
                                        : 0.f;
            rig.fStopTrack().add(t0, rampFStop, ease).add(t1, sequence.fStop, ease);
        }
    }

    void applyTargetFocusTrack(CameraRig& rig, int firstFrame, int lastFrame, float fps) {
        if (firstFrame < 0 || lastFrame < firstFrame || !(fps > 0.f) || !std::isfinite(fps))
            throw std::invalid_argument("applyTargetFocusTrack: invalid frame range or fps");
        for (int frame = firstFrame; frame <= lastFrame; ++frame) {
            const CameraPose pose = rig.sample(at(frame, fps));
            const float distance = CameraFocusResolver::resolveFocus(
                CameraFocusMode::TargetDistance, pose, pose.target);
            if (!std::isfinite(distance) || distance <= 0.f)
                throw std::invalid_argument("applyTargetFocusTrack: target must stay in front of camera");
            rig.focusDistanceTrack().add(at(frame, fps), distance, Easing::linear());
        }
    }

    void applyZPlaneFocusTrack(CameraRig& rig, int firstFrame, int lastFrame,
                               int transitionFrame, float startPlaneZ, float endPlaneZ,
                               float fps) {
        if (firstFrame < 0 || lastFrame < firstFrame || transitionFrame < firstFrame ||
            transitionFrame > lastFrame || !(fps > 0.f) || !std::isfinite(fps) ||
            !std::isfinite(startPlaneZ) || !std::isfinite(endPlaneZ))
            throw std::invalid_argument("applyZPlaneFocusTrack: invalid range or plane");
        const int travelFrames = std::max(1, lastFrame - transitionFrame);
        for (int frame = firstFrame; frame <= lastFrame; ++frame) {
            const float progress = frame <= transitionFrame ? 0.f :
                std::clamp(static_cast<float>(frame - transitionFrame) /
                           static_cast<float>(travelFrames), 0.f, 1.f);
            const float eased = progress * progress * (3.f - 2.f * progress);
            const float planeZ = startPlaneZ + (endPlaneZ - startPlaneZ) * eased;
            const CameraPose pose = rig.sample(at(frame, fps));
            Vector3 forward(0.f, 0.f, -1.f);
            forward.applyQuaternion(pose.orientation);
            forward.normalize();
            if (std::fabs(forward.z) <= 1e-4f)
                throw std::invalid_argument("applyZPlaneFocusTrack: optical axis cannot reach focus plane");
            const float distance = (planeZ - pose.position.z) / forward.z;
            if (!std::isfinite(distance) || distance <= 0.f)
                throw std::invalid_argument("applyZPlaneFocusTrack: focus plane must stay in front of camera");
            rig.focusDistanceTrack().add(at(frame, fps), distance, Easing::linear());
        }
    }
}
