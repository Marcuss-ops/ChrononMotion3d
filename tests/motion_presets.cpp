#include "motion_presets_shared.hpp"

int main() {
    using namespace chrononmotion_test::preset_tests;
    slideAndFadePresets();
    scalePresets();
    rotationPresets();
    motionPresets();
    settleAndWavePresets();
    groupPresets();
    pivotAndLifetimePresets();
    cameraPresets();
    namedPresetAliases();
    presetTimingUsesFrames();
    environmentAndBackgroundPresets();
    documentaryPresets();

    return chrononmotion_test::report();
}
