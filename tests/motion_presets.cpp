#include "motion_presets_internal.hpp"
int main() {
    motion_presets_test::slideAndFadePresets();
    motion_presets_test::scalePresets();
    motion_presets_test::rotationPresets();
    motion_presets_test::motionPresets();
    motion_presets_test::groupPresets();
    motion_presets_test::pivotAndLifetimePresets();
    motion_presets_test::cameraPresets();
    motion_presets_test::namedPresetAliases();
    motion_presets_test::presetTimingUsesFrames();
    motion_presets_test::environmentAndBackgroundPresets();
    motion_presets_test::settleAndWavePresets();
    motion_presets_test::documentaryPresets();
    return chrononmotion_test::report();
}
