#include "motion_authoring_shared.hpp"

int main() {
    using namespace chrononmotion_test::authoring;
    testVerletAndRigidBodyBaking();
    testTwoSegmentIk();
    testThreeDimensionalIkChain();
    testPcmAudioAnalysis();
    testPresetTrackGolden();
    testDynamicFlowLayout();
    testVectorPathBaking();
#if defined(CHRONONMOTION_HAS_CHRONON3D_ADAPTER)
    testTypedChronon3dLowering();
#endif
    return chrononmotion_test::report();
}
