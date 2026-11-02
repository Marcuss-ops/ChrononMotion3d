#include "chrononmotion/motion/CameraFocusSequence.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
    void check(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    bool near(float a, float b, float epsilon = 1e-4f) {
        return std::fabs(a - b) <= epsilon;
    }
}

int main() try {
    using namespace chrononmotion;
    using namespace chrononmotion::motion;
    using namespace chrononmotion::motion::camera;

    CameraRig rig(1, 48.f, 16.f / 9.f, 0.1f, 5000.f);
    FocusSequence sequence;
    check(near(sequence.highlightGain, 0.f) && sequence.irisBlades == 0,
          "default focus sequence keeps supported highlight weighting off and iris circular");
    sequence.subjects = {
            {{620.f, 390.f, 0.f}, 12.f, 6},
            {{1310.f, 645.f, 0.f}, 80.f, 48},
    };
    sequence.cameraAimOffset = {-110.f, -120.f};
    sequence.cameraDistance = 1080.f;
    sequence.baseFov = 48.f;
    sequence.focusFov = 38.f;
    sequence.fStop = 5.6f;
    sequence.maxBlur = 10.f;
    sequence.highlightGain = 1.25f;
    sequence.focusTravelFrames = 24;

    applyFocusSequence(rig, sequence, 30.f);
    const CameraPose nearWord = rig.sample(6.f / 30.f);
    const CameraPose farWord = rig.sample(60.f / 30.f);

    check(near(nearWord.position.x, 510.f) && near(nearWord.position.y, 270.f),
          "camera position follows the first off-center subject and aim offset");
    check(near(nearWord.target.x, 510.f) && near(nearWord.target.y, 270.f) && near(nearWord.target.z, -12.f),
          "camera target follows the first subject focus plane");
    check(near(nearWord.focusDistance, 1092.f),
          "focus distance is measured axially, independent of off-center aim offset");
    check(near(farWord.position.x, 1200.f) && near(farWord.position.y, 525.f),
          "camera reaches the second off-center subject");
    check(near(farWord.focusDistance, 1160.f), "rack focus reaches the far subject's axial plane");
    check(near(farWord.fov, 38.f), "the focus FOV is authored at the destination");
    check(near(farWord.fStop, 5.6f) && farWord.irisBlades == 0 &&
                  near(farWord.maxBlur, 10.f) && near(farWord.highlightGain, 1.25f),
          "focus sequence preserves supported highlight weighting and circular bokeh");

    FocusSequence unsupportedShape = sequence;
    unsupportedShape.irisBlades = 8;
    bool rejectedShape = false;
    try {
        CameraRig rejectedRig;
        applyFocusSequence(rejectedRig, unsupportedShape, 30.f);
    } catch (const std::invalid_argument&) {
        rejectedShape = true;
    }
    check(rejectedShape, "focus sequence rejects unsupported polygonal iris before authoring rig keys");
    unsupportedShape.irisBlades = 0;
    unsupportedShape.highlightGain = 8.1f;
    rejectedShape = false;
    try {
        CameraRig rejectedRig;
        applyFocusSequence(rejectedRig, unsupportedShape, 30.f);
    } catch (const std::invalid_argument&) {
        rejectedShape = true;
    }
    check(rejectedShape, "focus sequence rejects highlight gain outside the CameraRig contract");

    CameraRig focusTrackRig;
    focusTrackRig.setPosition({0.f, 0.f, 10.f}).setTarget({0.f, 0.f, 0.f});
    applyTargetFocusTrack(focusTrackRig, 0, 10, 30.f);
    check(near(focusTrackRig.sample(5.f / 30.f).focusDistance, 10.f),
          "CM3D resolves target focus for every evaluated camera sample");
    CameraRig planeFocusRig;
    planeFocusRig.setPosition({0.f, 0.f, 10.f}).setTarget({0.f, 0.f, 0.f});
    applyZPlaneFocusTrack(planeFocusRig, 0, 10, 0, 0.f, -5.f, 30.f);
    check(near(planeFocusRig.sample(0.f).focusDistance, 10.f) &&
                  near(planeFocusRig.sample(10.f / 30.f).focusDistance, 15.f),
          "CM3D resolves a world-plane rack focus along the optical axis");

    // Boundary contract (CM3D <-> Chronon3d DOF unification): highlight gain
    // is supported for circular bokeh; polygonal iris shaping remains rejected.
    // The default sequence leaves gain off and iris blades unset.
    check(near(FocusSequence{}.highlightGain, 0.f) && FocusSequence{}.irisBlades == 0,
          "default FocusSequence leaves optional highlight weighting off and iris unset");
    CameraRig defaultSeqRig(1, 48.f, 16.f / 9.f, 0.1f, 5000.f);
    FocusSequence defaultSequence;
    defaultSequence.subjects = {{{620.f, 390.f, 0.f}, 12.f, 6}}; // the only required authoring
    applyFocusSequence(defaultSeqRig, defaultSequence, 30.f);
    const CameraPose defaultPose = defaultSeqRig.sample(6.f / 30.f);
    check(near(defaultPose.highlightGain, 0.f) && defaultPose.irisBlades == 0,
          "applyFocusSequence preserves the default circular-bokeh settings");

    std::cout << "Camera focus sequence checks passed\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
