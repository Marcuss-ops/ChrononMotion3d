#pragma once

#include "chrononmotion/geospatial/DynamicPyramidMap.hpp"
#include "chrononmotion/geospatial/GeoCameraRig.hpp"
#include "chrononmotion/geospatial/Geodetic.hpp"
#include "chrononmotion/geospatial/MapFlyover.hpp"
#include "chrononmotion/geospatial/WebMercator.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"
#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/math/camera_pose.hpp>
#endif

namespace chrononmotion_test::geospatial {
using namespace chrononmotion;
using namespace chrononmotion::geospatial;
inline int failures = 0;
inline const GeoCoord kColosseum{41.890210, 12.492231, 50.0};
inline void check(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; return; }
    std::cout << "PASS: " << message << '\n';
}
inline void checkNear(double actual, double expected, double epsilon, const std::string& message) {
    if (std::abs(actual - expected) > epsilon) {
        std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual
                  << ", diff " << std::abs(actual - expected) << ")\n"; ++failures; return;
    }
    std::cout << "PASS: " << message << '\n';
}
inline double floatTolerance(double expected) {

        return 4.0 * static_cast<double>(std::numeric_limits<float>::epsilon())
               * std::max(1.0, std::abs(expected));
    }
inline MapFlyoverSpec romeSpec(int frames, float fps) {

        MapFlyoverSpec spec;
        spec.origin = kColosseum;
        spec.poi = kColosseum;
        spec.poiTitle = "COLOSSEO";
        spec.poiSubtitle = "Roma, Italia";
        spec.durationSeconds = static_cast<float>(frames) / fps;
        spec.fps = fps;
        spec.canvasSize = Vector2{1920.f, 1080.f};
        spec.window = MapWindow::centeredOn(kColosseum, 14, spec.canvasSize);
        // A stand-in content identity: the plate is Chronon's, and this test never
        // loads one.
        spec.basemapAsset = "map/plate";
        spec.startMapScale = 1.0f;
        spec.endMapScale = 1.25f;
        spec.startMapPan = Vector2{0.f, 0.f};
        spec.endMapPan = Vector2{-40.f, 24.f};
        spec.route = {
            GeoCoord{41.8963, 12.4828, 45.0}, // Piazza Venezia
            GeoCoord{41.8925, 12.4853, 48.0}, // Roman Forum
            GeoCoord{41.8902, 12.4922, 50.0}, // Colosseum
        };
        return spec;
    }
inline DynamicPyramidSpec::MipmapBlendState spec_for_lod(float zoom) {
        DynamicPyramidSpec pyramid;
        pyramid.minZoom = 5.0f;
        pyramid.maxZoom = 17.5f;
        return pyramid.resolveMipmapBlend(zoom);
    }
inline float spec_lod_max(const GeoCameraRig& rig) { return rig.spec().pyramid.maxZoom; }
void testGeodesy();
void testWebMercator();
void testFlyoverScene();
void testEuropeFlyTo();
void testCalloutAndClamp();
void testDocumentaryFlyover();
void testDocumentaryPunctuation();
#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
void testPlanPlacement();
#endif
#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
void testDocumentaryPlan();
#endif
#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
void testCameraBackedPlatePlan();
#endif
void testDynamicPyramidEngine();
void testGeoCameraDivePhysics();
void testGeoCameraTargetLock();
void testGeoCameraLodContinuity();
void testGeoCameraDeterminismAndBridge();

} // namespace chrononmotion_test::geospatial




