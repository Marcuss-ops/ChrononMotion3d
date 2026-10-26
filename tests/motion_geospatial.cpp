#include "motion_geospatial_shared.hpp"

int main() {
    using namespace chrononmotion_test::geospatial;
    std::cout << "=== ChrononMotion geospatial tests ===\n";
    testGeodesy();
    testWebMercator();
    testDynamicPyramidEngine();
    testGeoCameraDivePhysics();
    testGeoCameraTargetLock();
    testGeoCameraLodContinuity();
    testGeoCameraDeterminismAndBridge();
    testFlyoverScene();
    testEuropeFlyTo();
    testCalloutAndClamp();
    testDocumentaryFlyover();
    testDocumentaryPunctuation();
#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
    testPlanPlacement();
    testDocumentaryPlan();
    testCameraBackedPlatePlan();
#else
    std::cout << "\n=== plan placement skipped (no Chronon3D adapter in this build) ===\n";
#endif
    if (failures != 0) {
        std::cerr << "\n" << failures << " geospatial assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "\nall geospatial assertions passed\n";
    return EXIT_SUCCESS;
}
