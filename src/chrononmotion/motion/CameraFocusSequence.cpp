#include "chrononmotion/motion/CameraFocusSequence.hpp"

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
            !(sequence.legacyDefocusStrength > 0.f && sequence.legacyDefocusStrength <= 1.f) ||
            !(sequence.fStop > 0.f) || !(sequence.maxBlur >= 0.f) ||
            !(sequence.highlightGain >= 0.f) || sequence.focusTravelFrames < 2) {
            throw std::invalid_argument("applyFocusSequence: invalid timing or lens values");
        }
        int priorFocus = -1;
        for (const auto& subject : sequence.subjects) {
            if (!std::isfinite(subject.anchor.x) || !std::isfinite(subject.anchor.y) ||
                !std::isfinite(subject.anchor.z) || !std::isfinite(subject.focusDepth) ||
                subject.focusDepth < 0.f || subject.focusFrame < 0 || subject.focusFrame <= priorFocus) {
                throw std::invalid_argument("applyFocusSequence: subject anchors and focus frames must be finite and ordered");
            }
            priorFocus = subject.focusFrame;
        }

        const auto& first = sequence.subjects.front();
        rig.setPosition(positionFor(first, sequence))
            .setTarget(targetFor(first, sequence))
            .setFov(sequence.baseFov)
            .setLegacyDefocusStrength(sequence.legacyDefocusStrength)
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
        rig.legacyDefocusStrengthTrack().add(at(firstKey.focusFrame, fps), sequence.legacyDefocusStrength, ease);

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
            rig.legacyDefocusStrengthTrack().add(t0, sequence.legacyDefocusStrength * 0.78f, ease)
                .add(t1, sequence.legacyDefocusStrength, ease);
        }
    }
}
