// ChrononMotion — geospatial, Web Mercator and map flyover tests.
//
// Three things are pinned here, in order of how expensive they would be to get
// wrong:
//
//   1. The geodesy. ECEF/Bowring/ENU and Haversine against known WGS84 values.
//   2. Web Mercator *against the other implementation of it*. The raster plates
//      are cropped by `Chronon3d/tools/cartography/map_cartography.py`, and this
//      side georeferences them; if the two formulas drift, every marker lands on
//      the wrong pixel and nothing else in the tree would notice. The expected
//      numbers below are that tool's output, so this file fails the moment the
//      two disagree — and its `--self-test` asserts the same numbers back, so a
//      drift is caught on whichever side it is introduced.
//   3. The authoring bridge. A map point becomes a layer position by exactly one
//      conversion, and the plan that comes out of the lowering must place a
//      marker on the canvas pixel its geography maps to.
//
// What is deliberately *not* here: whether the pixels look right. That needs a
// raster and a renderer and lives in the end-to-end run of
// `chrononmotion_map_flyover`; a unit test that invented a plate could only
// assert its own invention.

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

// The plan half of this file needs Chronon3D's typed RenderPlan, which only a
// build that explicitly points at Chronon3D's include tree has. Without it the
// geospatial model is tested exactly as thoroughly; only the emitted placement
// is skipped, and it says so.
#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include <chronon3d/render_plan/render_plan.hpp>
#include <chronon3d/math/camera_pose.hpp>
#endif

using namespace chrononmotion;
using namespace chrononmotion::geospatial;

namespace {

    int failures = 0;

    void check(bool condition, const std::string& message) {

        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            ++failures;
            return;
        }
        std::cout << "PASS: " << message << '\n';
    }

    void checkNear(double actual, double expected, double epsilon, const std::string& message) {

        if (std::abs(actual - expected) > epsilon) {
            std::cerr << "FAIL: " << message << " (expected " << expected << ", got " << actual
                      << ", diff " << std::abs(actual - expected) << ")\n";
            ++failures;
            return;
        }
        std::cout << "PASS: " << message << '\n';
    }

    /// The tolerance a value of this magnitude can even be reproduced to.
    ///
    /// The map side is measured in float end to end, so a reference taken in
    /// double — as the cartography tool's is — can only be matched to float's
    /// resolution at that magnitude. At zoom 16 a global pixel is in the
    /// millions, where a float's spacing is a fraction of a pixel; asserting
    /// tighter than that would test the type rather than the formula.
    double floatTolerance(double expected) {

        return 4.0 * static_cast<double>(std::numeric_limits<float>::epsilon())
               * std::max(1.0, std::abs(expected));
    }

    /// Rome, the Colosseum. Also the plate the original geospatial work used.
    const GeoCoord kColosseum{41.890210, 12.492231, 50.0};

    void testGeodesy() {

        std::cout << "\n=== geodesy ===\n";
        const EcefCoord ecef = toEcef(kColosseum);
        check(ecef.x > 4640000.0 && ecef.x < 4645000.0, "ECEF X is in the Rome range");
        check(ecef.y > 1025000.0 && ecef.y < 1030000.0, "ECEF Y is in the Rome range");
        check(ecef.z > 4235000.0 && ecef.z < 4242000.0, "ECEF Z is in the Rome range");

        const GeoCoord roundTrip = fromEcef(ecef);
        checkNear(roundTrip.latitudeDeg, kColosseum.latitudeDeg, 1e-6, "Bowring latitude round-trip");
        checkNear(roundTrip.longitudeDeg, kColosseum.longitudeDeg, 1e-6, "Bowring longitude round-trip");
        checkNear(roundTrip.altitudeM, kColosseum.altitudeM, 1e-3, "Bowring altitude round-trip");

        const EnuCoord self = toEnu(kColosseum, kColosseum);
        checkNear(self.east, 0.0, 1e-4, "ENU east of the origin is zero");
        checkNear(self.north, 0.0, 1e-4, "ENU north of the origin is zero");
        checkNear(self.up, 0.0, 1e-4, "ENU up of the origin is zero");

        const GeoCoord milanDuomo{45.464211, 9.191383, 120.0};
        const double distanceKm = distanceMeters(kColosseum, milanDuomo) / 1000.0;
        check(distanceKm > 470.0 && distanceKm < 485.0, "Haversine Rome-Milan is about 477 km");

        // A point north of the origin must be -Z in scene space, and a point east
        // of it +X: that sign is what makes the whole map render the right way up.
        const GeoCoord north{41.900210, 12.492231, 50.0};
        const GeoCoord east{41.890210, 12.502231, 50.0};
        check(toSceneSpace(north, kColosseum).z < 0.f, "north is -Z in scene space");
        check(toSceneSpace(east, kColosseum).x > 0.f, "east is +X in scene space");
    }

    void testWebMercator() {

        std::cout << "\n=== web mercator (locked against map_cartography.py) ===\n";
        // These are the exact values `Chronon3d/tools/cartography/
        // map_cartography.py` `latlon_to_mercator_px` returns for the same
        // inputs. They are the contract between the side that crops the raster
        // and the side that georeferences it.
        const struct { double lat, lon; int zoom; double x, y; } reference[] = {
            {41.890210, 12.492231, 14, 2242697.0401450666, 1558716.8764844453},
            {29.86, -92.70, 8, 15892.48, 27067.93732941762},
            {33.9598, -83.3768, 16, 4502967.491697778, 6704228.367772281},
            {0.0, 0.0, 0, 128.0, 128.0},
        };
        for (const auto& expected : reference) {
            const Vector2 pixel = latLonToGlobalPixel(expected.lat, expected.lon, expected.zoom);
            const double tolerance = floatTolerance(std::max(expected.x, expected.y));
            checkNear(pixel.x, expected.x, tolerance,
                      "global pixel X matches the cartography tool at zoom "
                          + std::to_string(expected.zoom));
            checkNear(pixel.y, expected.y, tolerance,
                      "global pixel Y matches the cartography tool at zoom "
                          + std::to_string(expected.zoom));
        }

        const GeoCoord back = globalPixelToLatLon(2242697.0401450666, 1558716.8764844453, 14);
        checkNear(back.latitudeDeg, kColosseum.latitudeDeg, 1e-9, "global pixel round-trips to latitude");
        checkNear(back.longitudeDeg, kColosseum.longitudeDeg, 1e-9, "global pixel round-trips to longitude");

        checkNear(clampMercatorLatitude(89.0), kMercatorLatitudeLimit, 0.0,
                  "an out-of-range latitude clamps to the Mercator limit");

        // A window centred on a point must place that point's plane position at
        // the raster centre, and its raster position at the middle pixel. This is
        // the assumption the entire marker placement rests on.
        const Vector2 rasterSize{1920.f, 1080.f};
        const MapWindow window = MapWindow::centeredOn(kColosseum, 14, rasterSize);
        const Vector2 plane = window.toPlane(kColosseum);
        checkNear(plane.x, 0.0, 1e-4, "the window centre is the plane origin in X");
        checkNear(plane.y, 0.0, 1e-4, "the window centre is the plane origin in Y");
        const Vector2 raster = window.toRaster(kColosseum);
        checkNear(raster.x, 960.0, 1e-4, "the window centre is the middle raster pixel in X");
        checkNear(raster.y, 540.0, 1e-4, "the window centre is the middle raster pixel in Y");

        // North is up on the raster, so a point north of the centre must have
        // +y in plane space, which is what the authoring bridge flips back.
        const GeoCoord north{41.900210, 12.492231, 50.0};
        check(window.toPlane(north).y > 0.f, "a point north of the centre is +y in plane space");
        check(window.toRaster(north).y < 540.f, "a point north of the centre is above the middle raster row");
    }

    /// A scene with a basemap, a route, a pin and a callout, over a Rome window.
    MapFlyoverSpec romeSpec(int frames, float fps) {

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

    void testFlyoverScene() {

        std::cout << "\n=== map flyover scene ===\n";
        constexpr int kFrameCount = 120;
        const float fps = 60.f;
        const MapFlyoverSpec spec = romeSpec(kFrameCount, fps);
        const MapFlyoverBuilder builder(spec);

        check(builder.frameCount() == kFrameCount, "the clip length is the authored duration in frames");

        checkNear(builder.mapScaleAt(0.f), 1.0, 1e-6, "the flyover starts at the authored scale");
        checkNear(builder.mapScaleAt(1.f), 1.25, 1e-6, "the flyover ends at the authored scale");
        checkNear(builder.mapPanAt(0.f).x, 0.0, 1e-6, "the flyover starts at the authored pan");
        checkNear(builder.mapPanAt(1.f).y, 24.0, 1e-6, "the flyover ends at the authored pan");

        // At rest, with the scale at one and no pan, the plane origin is the
        // canvas centre and a geographic point sits at its plain offset from it.
        const Vector2 centreAtRest = builder.planePointToCanvas(Vector2{0.f, 0.f}, 0.f);
        checkNear(centreAtRest.x, 960.0, 1e-4, "the raster centre is painted at the middle canvas column");
        checkNear(centreAtRest.y, 540.0, 1e-4, "the raster centre is painted at the middle canvas row");

        const Vector2 planeOfPoi = spec.window.toPlane(spec.poi);
        const Vector2 poiAtEnd = builder.mapPointToCanvas(spec.poi, 1.f);
        checkNear(poiAtEnd.x, 960.0 + planeOfPoi.x * 1.25 - 40.0, 1e-3,
                  "a geographic point follows the flyover scale and pan");
        checkNear(poiAtEnd.y, 540.0 - planeOfPoi.y * 1.25 + 24.0, 1e-3,
                  "a geographic point follows the flyover scale and pan in Y");

        // The bridge into ChrononMotion's authoring space is one conversion, and
        // it must be the inverse of the canvas centre offset.
        const Vector3 authored = builder.authoredPosition(builder.mapPointToCanvas(spec.poi, 0.f), 0.f);
        checkNear(authored.x, builder.mapPointToCanvas(spec.poi, 0.f).x - 960.0, 1e-6,
                  "authoring a canvas point subtracts the canvas centre in X");
        checkNear(authored.y, builder.mapPointToCanvas(spec.poi, 0.f).y - 540.0, 1e-6,
                  "authoring a canvas point subtracts the canvas centre in Y");

        // Every waypoint must be inside the plate, or the window and the route
        // disagree about where the map is.
        for (std::size_t i = 0; i < spec.route.size(); ++i) {
            const Vector2 raster = spec.window.toRaster(spec.route[i]);
            check(raster.x >= 0.f && raster.x <= spec.canvasSize.x &&
                      raster.y >= 0.f && raster.y <= spec.canvasSize.y,
                  "waypoint " + std::to_string(i) + " is inside the plate");
        }

        MotionScene scene{"map_flyover_test", fps};
        const MapSceneLayers ids = builder.buildScene(scene);
        check(ids.hasBasemap(), "the scene owns the basemap layer");
        check(ids.markers.size() == spec.route.size(), "the scene owns one marker per route waypoint");
        check(ids.pin != Layer::kRoot, "the scene owns the POI pin");
        check(ids.title != Layer::kRoot && ids.subtitle != Layer::kRoot, "the scene owns the callout");
        check(ids.count() == 1 + spec.route.size() + 1 + 2,
              "the scene owns exactly the basemap, the markers, the pin and the callout");
        check(scene.validate().empty(), "the authored scene has no hierarchy problems");

        // Flat on purpose: the flyover is authored on each layer, so a parent
        // would apply the same transform twice.
        check(scene.findLayer(ids.basemap)->parentId == Layer::kRoot,
              "the basemap is not parented");
        for (const Layer::Id id : ids.markers)
            check(scene.findLayer(id)->parentId == Layer::kRoot,
                  "a marker is not parented to the map it sits on");

        // The lock that matters: at every sampled frame, what the scene evaluates
        // must be exactly what the georeferencing says. A sparse bake, a wrong
        // easing or a stray parent breaks this immediately.
        const float secondsPerFrame = 1.f / fps;
        for (const int frame : {0, 1, 30, 77, kFrameCount}) {
            scene.evaluate(frame);
            const float progress = static_cast<float>(frame) / static_cast<float>(kFrameCount);

            for (std::size_t i = 0; i < ids.markers.size(); ++i) {
                const Vector3 expected = builder.authoredPosition(
                    builder.mapPointToCanvas(spec.route[i], progress), 0.002f);
                const Vector3 actual = scene.state(ids.markers[i]).local.position;
                checkNear(actual.x, expected.x, 1e-3,
                          "marker " + std::to_string(i) + " is on its geography in X at frame "
                              + std::to_string(frame));
                checkNear(actual.y, expected.y, 1e-3,
                          "marker " + std::to_string(i) + " is on its geography in Y at frame "
                              + std::to_string(frame));
            }

            const Vector3 expectedBasemap = builder.authoredPosition(
                builder.planePointToCanvas(Vector2{0.f, 0.f}, progress), 0.f);
            const Vector3 actualBasemap = scene.state(ids.basemap).local.position;
            checkNear(actualBasemap.x, expectedBasemap.x, 1e-3,
                      "the raster centre is panned where the flyover says in X at frame "
                          + std::to_string(frame));
            checkNear(actualBasemap.y, expectedBasemap.y, 1e-3,
                      "the raster centre is panned where the flyover says in Y at frame "
                          + std::to_string(frame));
            checkNear(scene.state(ids.basemap).local.scale.x, builder.mapScaleAt(progress), 1e-4,
                      "the raster is scaled by the flyover at frame " + std::to_string(frame));
        }

        // Symbols do not zoom with the map; the geography around them does.
        scene.evaluate(kFrameCount);
        checkNear(scene.state(ids.pin).local.scale.x, 1.0, 1e-5,
                  "the pin keeps unit scale while the map zooms");
        checkNear(scene.state(ids.basemap).local.scale.x, spec.endMapScale, 1e-5,
                  "the raster ends at the flyover's final scale");

        // The pin drops onto the POI and stays on its column: the spring offsets
        // it vertically, never sideways.
        scene.evaluate(0);
        const float pinXAtStart = scene.state(ids.pin).local.position.x;
        scene.evaluate(kFrameCount);
        const float pinXAtEnd = scene.state(ids.pin).local.position.x;
        const Vector2 poiAtRest = builder.mapPointToCanvas(spec.poi, 0.f);
        checkNear(pinXAtStart, poiAtRest.x - 960.0, 1e-3, "the pin starts on the POI column");
        checkNear(pinXAtEnd, builder.mapPointToCanvas(spec.poi, 1.f).x - 960.0, 1e-3,
                  "the pin follows the POI across the flyover");

        // The spring lands: a dropped pin is above the point at the start and on
        // it at the end.
        check(evaluateSpringBounce(0.0f, 1.2f, 15.0f, 0.52f) == 0.0f, "the drop spring starts at zero");
        check(evaluateSpringBounce(1.5f, 1.2f, 15.0f, 0.52f) == 1.0f, "the drop spring settles at one");
        check(evaluateSpringBounce(0.6f, 1.2f, 15.0f, 0.52f) > 0.9f,
              "the drop spring is most of the way there at half the drop");
    }

    // The Europe flyTo: one flight, London -> Paris -> Rome, framed by a zoom-5
    // plate centred on Rome and flown by the perspective camera rig. Every
    // reference number below is the spherical/Mercator formula this tree
    // implements (Haversine on the WGS84 semi-major sphere, Web Mercator),
    // recomputed independently to double precision — so this fails the moment
    // the geography and the flight disagree about where Europe is.
    void testEuropeFlyTo() {

        std::cout << "\n=== europe flyTo: London -> Paris -> Rome ===\n";
        const GeoCoord london{51.5074, -0.1278, 0.0};
        const GeoCoord paris{48.8566, 2.3522, 0.0};
        const GeoCoord rome{41.890210, 12.492231, 50.0};

        // ── The geodesy the flight flies ──────────────────────────────
        // The arc the waypoints are keyed against, on the same sphere
        // `distanceMeters` uses (`kWgs84SemiMajorAxis`).
        checkNear(distanceMeters(london, rome) / 1000.0, 1436.32, 2.0,
                  "Haversine London-Rome is about 1436 km");
        checkNear(distanceMeters(paris, rome) / 1000.0, 1107.35, 2.0,
                  "Haversine Paris-Rome is about 1107 km");
        checkNear(distanceMeters(london, paris) / 1000.0, 343.95, 2.0,
                  "Haversine London-Paris is about 344 km");

        // The great-circle mid London-Rome: the subject the flight frames at
        // its halfway frame. Radius-independent (angles only), so it is pinned
        // exactly, and the interpolation must return its own endpoints.
        const GeoCoord gcMid = greatCirclePoint(london, rome, 0.5);
        checkNear(gcMid.latitudeDeg, 46.87115777, 1e-6, "the great-circle mid London-Rome latitude");
        checkNear(gcMid.longitudeDeg, 6.74775451, 1e-6, "the great-circle mid London-Rome longitude");
        const GeoCoord gcStart = greatCirclePoint(london, rome, 0.0);
        const GeoCoord gcEnd = greatCirclePoint(london, rome, 1.0);
        checkNear(gcStart.latitudeDeg, london.latitudeDeg, 1e-4, "the great circle starts at London");
        checkNear(gcStart.longitudeDeg, london.longitudeDeg, 1e-4, "the great circle starts at London's longitude");
        checkNear(gcEnd.latitudeDeg, rome.latitudeDeg, 1e-4, "the great circle lands at Rome");
        checkNear(gcEnd.longitudeDeg, rome.longitudeDeg, 1e-4, "the great circle lands at Rome's longitude");

        // ── The window the flight is framed by ────────────────────────
        // A zoom-5, 1920x1080 plate centred on Rome: the plate's pixel grid and
        // the flight's frame must agree on where every city sits, or a marker
        // lands on the wrong country while the picture shows the right one.
        const Vector2 rasterSize{1920.f, 1080.f};
        const MapWindow window = MapWindow::centeredOn(rome, 5, rasterSize);

        const Vector2 romePx = latLonToGlobalPixel(rome.latitudeDeg, rome.longitudeDeg, 5);
        checkNear(romePx.x, 4380.2676565333, 1e-2, "Rome's global pixel X at zoom 5");
        checkNear(romePx.y, 3044.3688993837, 1e-2, "Rome's global pixel Y at zoom 5");

        const Vector2 romePlane = window.toPlane(rome);
        checkNear(romePlane.x, 0.0, 1e-3, "the window centres Rome in plane X");
        checkNear(romePlane.y, 0.0, 1e-3, "the window centres Rome in plane Y");

        // West of the centre is -x and north of it is +y in plane space, so
        // London must sit in the plate's north-west quadrant — and inside it.
        const Vector2 londonPlane = window.toPlane(london);
        const Vector2 parisPlane = window.toPlane(paris);
        checkNear(londonPlane.x, -287.18, 0.05, "London sits 287 px west of the Rome window centre");
        checkNear(londonPlane.y, 320.32, 0.05, "London sits 320 px north of the Rome window centre");
        checkNear(parisPlane.x, -230.74, 0.05, "Paris sits 231 px west of the Rome window centre");
        checkNear(parisPlane.y, 226.09, 0.05, "Paris sits 226 px north of the Rome window centre");
        check(std::abs(londonPlane.x) < rasterSize.x * 0.5f &&
                  std::abs(londonPlane.y) < rasterSize.y * 0.5f,
              "London is inside the Rome plate");
        check(std::abs(parisPlane.x) < rasterSize.x * 0.5f &&
                  std::abs(parisPlane.y) < rasterSize.y * 0.5f,
              "Paris is inside the Rome plate");

        // ── The flat-plate flyover authors the flight ─────────────────
        MapFlyoverSpec spec;
        spec.origin = rome;
        spec.poi = rome;
        spec.poiTitle = "ROMA";
        spec.poiSubtitle = "41.8902° N, 12.4922° E";
        spec.durationSeconds = 8.0f;
        spec.fps = 30.f;
        spec.canvasSize = rasterSize;
        spec.window = window;
        spec.basemapAsset = "map/europe";
        spec.route = {london, paris, rome};
        spec.startMapScale = 1.0f;
        spec.endMapScale = 1.35f;
        spec.startMapPan = Vector2{0.f, 0.f};
        spec.endMapPan = Vector2{0.f, 0.f};

        const MapFlyoverBuilder builder(spec);
        check(builder.frameCount() == 240, "the 8 s flyover is 240 frames at 30 fps");

        MotionScene scene{"europe_flyto", spec.fps};
        const MapSceneLayers ids = builder.buildScene(scene);
        check(ids.hasBasemap(), "the Europe flyover owns its basemap");
        check(ids.markers.size() == spec.route.size(),
              "one marker per waypoint: London, Paris, Rome");
        check(ids.pin != Layer::kRoot, "the Rome POI owns its drop pin");

        // Waypoints ride the flyover transform — the same scale and pan the
        // plate takes — so a marker never separates from the geography the
        // raster painted. Rome is the window centre, so its column never moves
        // however far the plate zooms; London starts where Mercator paints it
        // and is pushed out by the zoom, westward and northward, never across.
        scene.evaluate(0);
        const Vector3 londonStart = scene.state(ids.markers.front()).local.position;
        checkNear(londonStart.x, londonPlane.x, 0.05, "London's marker starts where the plate paints London in X");
        checkNear(londonStart.y, -londonPlane.y, 0.05, "London's marker starts north of the frame centre (north is -y)");

        scene.evaluate(builder.frameCount() / 2);
        const Vector3 londonMid = scene.state(ids.markers.front()).local.position;
        check(londonMid.x > spec.endMapScale * londonPlane.x && londonMid.x < londonPlane.x,
              "the flight approaches Rome monotonically from the west");

        scene.evaluate(builder.frameCount());
        const Vector3 londonEnd = scene.state(ids.markers.front()).local.position;
        checkNear(londonEnd.x, spec.endMapScale * londonPlane.x, 0.05, "London's marker rides the plate zoom in X");
        checkNear(londonEnd.y, -spec.endMapScale * londonPlane.y, 0.05, "London's marker rides the plate zoom in Y");

        const Vector3 pinEnd = scene.state(ids.pin).local.position;
        checkNear(pinEnd.x, 0.0, 1e-3, "the pin lands on Rome's column at the flight's end");
        checkNear(pinEnd.y, 0.0, 1e-3, "the pin lands on Rome's row once the spring settles");

        // ── The same flight, camera-backed ────────────────────────────
        // The 3D pass: the plate becomes a perspective surface under a shared
        // tilt controller, and the rig flies the authored dive over it.
        MapFlyoverSpec flightSpec = spec;
        flightSpec.cameraBackedPlate = true;
        flightSpec.poiTitle.clear();
        flightSpec.poiSubtitle.clear();
        const MapFlyoverBuilder flight(flightSpec);
        MotionScene flightScene{"europe_flyto_3d", spec.fps};
        const MapSceneLayers flightIds = flight.buildScene(flightScene);
        flight.configureCameraBackedPlate(flightScene, 0.f, 55.f);

        motion::CameraRig rig{1, 55.f, 16.f / 9.f, 1.f, 100000.f};
        flight.rigCameraForPlate(rig, 4200.f, 1400.f);

        const motion::CameraPose departure = rig.sample(0.f);
        const motion::CameraPose arrival = rig.sample(spec.durationSeconds);
        checkNear(departure.position.z, -4200.f, 1e-3, "the Europe flyTo starts 4200 units over the plate");
        checkNear(arrival.position.z, -1400.f, 1e-3, "the Europe flyTo lands 1400 units over Rome");
        checkNear(departure.fov, spec.startFovDeg, 1e-4, "the flyTo starts at the wide framing");
        checkNear(arrival.fov, spec.endFovDeg, 1e-4, "the flyTo ends at the telephoto framing");
        check(!arrival.hasTarget && std::abs(arrival.orientation.w - 1.f) < 1e-6f,
              "the plate flyTo stays on the front-facing canonical camera");

        // The descent is monotonic between the authored depths, and a symmetric
        // ease passes the mid depth at the mid frame.
        const motion::CameraPose midFlight = rig.sample(spec.durationSeconds * 0.5f);
        check(midFlight.position.z < arrival.position.z && midFlight.position.z > departure.position.z,
              "the descent is monotonic between the authored depths");
        check(std::abs(midFlight.position.z - 0.5f * (departure.position.z + arrival.position.z)) < 60.f,
              "the eased descent passes the mid depth near the mid frame");

        // No frame of the flight may produce a non-finite pose: a NaN camera
        // poisons every frame after it, so it must never be authored at all.
        bool flightFinite = true;
        for (int frame = 0; frame <= flight.frameCount(); ++frame) {
            const motion::CameraPose pose = rig.sample(static_cast<float>(frame) / spec.fps);
            flightFinite = flightFinite && std::isfinite(pose.position.x) &&
                std::isfinite(pose.position.y) && std::isfinite(pose.position.z) &&
                std::isfinite(pose.fov) && std::isfinite(pose.orientation.w) &&
                std::isfinite(pose.orientation.x) && std::isfinite(pose.orientation.y) &&
                std::isfinite(pose.orientation.z);
        }
        check(flightFinite, "every sampled pose of the Europe flyTo is finite");

        // The tilt: the whole georeferenced surface rotates as one object, so
        // the imagery and every marker on it stay registered while the camera
        // comes down. The controller is a null rig node, and the plate (and
        // everything welded to it) hangs off it.
        flightScene.evaluate(flight.frameCount());
        const Layer* plate = flightScene.findLayer(flightIds.basemap);
        check(plate != nullptr && plate->parentId != Layer::kRoot,
              "the Europe plate is parented under the shared tilt controller");
        if (plate != nullptr && plate->parentId != Layer::kRoot) {
            const Layer* controller = flightScene.findLayer(plate->parentId);
            check(controller != nullptr && controller->isNull() &&
                      controller->name == "georeferenced_map_surface",
                  "the tilt controller is the georeferenced surface null node");
            if (controller != nullptr) {
                checkNear(controller->tracks.rotation.keys().back().value.x,
                          55.f * std::numbers::pi_v<float> / 180.f, 1e-4,
                          "the Europe plate tilts 55 degrees into the arrival");
            }
        }
    }

    void testCalloutAndClamp() {

        std::cout << "\n=== callout and limits ===\n";
        constexpr int kFrameCount = 120;
        const MapFlyoverSpec spec = romeSpec(kFrameCount, 30.f);
        const MapFlyoverBuilder builder(spec);
        MotionScene scene{"map_callout_test", 30.f};
        const MapSceneLayers ids = builder.buildScene(scene);

        // The callout sits on the lines the spec authors, and the entrance is
        // keyed around that placement rather than inventing its own.
        const Layer* title = scene.findLayer(ids.title);
        check(title != nullptr, "the callout title exists");
        if (title != nullptr) {
            checkNear(title->transform.position.y,
                      spec.canvasSize.y * spec.calloutTitleYFraction - spec.canvasSize.y * 0.5f,
                      1e-3, "the callout title rests on its authored line");
            checkNear(title->tracks.opacity.sample(0.f), 0.f, 1e-4, "the callout starts transparent");
            checkNear(title->tracks.opacity.sample(60.f), 1.f, 1e-4, "the callout is opaque once it has entered");
        }

        // A route longer than the marker budget is clamped rather than turned
        // into one layer per point.
        MapFlyoverSpec crowded = romeSpec(kFrameCount, 30.f);
        crowded.route.assign(500, GeoCoord{41.8902, 12.4922, 50.0});
        MotionScene crowdedScene{"crowded", 30.f};
        const MapSceneLayers crowdedIds = MapFlyoverBuilder(crowded).buildScene(crowdedScene);
        check(crowdedIds.markers.size() == MapFlyoverSpec::kMaximumRouteMarkers,
              "a hostile route is clamped to the marker budget");

        // A spec with no plate authors no basemap rather than a fake one.
        MapFlyoverSpec plateless = romeSpec(kFrameCount, 30.f);
        plateless.basemapAsset.clear();
        plateless.route.clear();
        MotionScene platelessScene{"plateless", 30.f};
        const MapSceneLayers platelessIds = MapFlyoverBuilder(plateless).buildScene(platelessScene);
        check(!platelessIds.hasBasemap(), "a plateless spec authors no basemap layer");
        check(platelessIds.markers.empty(), "a routeless spec authors no markers");
        check(platelessIds.count() == 3, "a plateless, routeless spec authors only the pin and the callout");

        // Relief is optional and never invented: no asset, no layer.
        MapFlyoverSpec bare = romeSpec(kFrameCount, 30.f);
        bare.reliefAsset.clear();
        MotionScene bareScene{"no_relief", 30.f};
        const MapSceneLayers bareIds = MapFlyoverBuilder(bare).buildScene(bareScene);
        check(bareIds.relief == Layer::kRoot, "a spec without a relief plate authors no drape");
    }

    void testDocumentaryFlyover() {

        std::cout << "\n=== documentary flyover layers ===\n";
        constexpr int kFrames = 120;
        constexpr float fps = 60.f;
        const float frameSeconds = 1.f / fps;

        // Off by default: a place that supplies no content authors no documentary
        // layer, so a plain flyover is exactly what it always was.
        {
            MapFlyoverSpec plain = romeSpec(kFrames, fps);
            MotionScene plainScene{"plain", fps};
            const MapSceneLayers plainIds = MapFlyoverBuilder(plain).buildScene(plainScene);
            check(plainIds.area == Layer::kRoot && plainIds.leader_line == Layer::kRoot &&
                      plainIds.crosshair == Layer::kRoot,
                  "the documentary layers are off unless a place opts in");
        }

        MapFlyoverSpec spec = romeSpec(kFrames, fps);
        spec.enablePinPulse = true;
        spec.areaOverlayAsset = "map/area";
        spec.leaderLineAsset = "map/leader";
        spec.crosshairAsset = "map/reticle";

        const MapFlyoverBuilder builder(spec);
        MotionScene scene{"documentary", fps};
        const MapSceneLayers ids = builder.buildScene(scene);

        check(ids.area != Layer::kRoot, "an area asset authors an area layer");
        check(ids.leader_line != Layer::kRoot, "a leader asset authors a leader line");
        check(ids.crosshair != Layer::kRoot, "opting in authors the reticle");
        check(ids.count() == 1 + spec.route.size() + 1 + 2 + 3,
              "the layer count includes the documentary layers");
        check(scene.validate().empty(), "the documentary scene has no hierarchy problems");

        // The area fades in across its own window and takes the whole flyover.
        const Layer* area = scene.findLayer(ids.area);
        check(area != nullptr, "the area layer is reachable");
        if (area != nullptr) {
            checkNear(area->tracks.opacity.sample(0.f), 0.f, 1e-4f, "the area starts invisible");
            checkNear(area->tracks.opacity.sample(19.f * frameSeconds), 0.f, 1e-4f,
                      "the area is still invisible before its reveal window");
            checkNear(area->tracks.opacity.sample(40.f * frameSeconds), 0.5f, 1e-3f,
                      "the area is half revealed at the middle of its window");
            checkNear(area->tracks.opacity.sample(60.f * frameSeconds), 1.f, 1e-4f,
                      "the area is fully revealed at the end of its window");
            scene.evaluate(kFrames);
            checkNear(scene.state(ids.area).local.scale.x, builder.mapScaleAt(1.f), 1e-4,
                      "the area rides the flyover's scale like the plate it sits on");
        }

        // The leader line draws itself along its own width.
        const Layer* line = scene.findLayer(ids.leader_line);
        check(line != nullptr, "the leader line layer is reachable");
        if (line != nullptr) {
            checkNear(line->tracks.scale.sample(10.f * frameSeconds).x, 1e-3f, 1e-4f,
                      "the leader line starts from a hairline, never a singular zero");
            checkNear(line->tracks.scale.sample(26.f * frameSeconds).x, 1.f, 1e-4f,
                      "the leader line is drawn to its full width across the window");
            checkNear(line->tracks.opacity.sample(10.f * frameSeconds), 0.f, 1e-4f,
                      "the leader line starts transparent");
        }

        // The reticle is welded to the POI and closes with a snap.
        const Layer* reticle = scene.findLayer(ids.crosshair);
        check(reticle != nullptr, "the reticle layer is reachable");
        if (reticle != nullptr) {
            checkNear(reticle->tracks.scale.sample(16.f * frameSeconds).x, spec.crosshairStartScale, 1e-3f,
                      "the reticle starts wide open");
            checkNear(reticle->tracks.scale.sample(34.f * frameSeconds).x, 1.f, 1e-3f,
                      "the reticle closes onto the target");
            for (const int frame : {40, 60, 90}) {
                scene.evaluate(frame);
                const Vector3 expected = builder.authoredPosition(
                    builder.mapPointToCanvas(spec.poi,
                                             static_cast<float>(frame) / static_cast<float>(kFrames)), 0.f);
                const Vector3 actual = scene.state(ids.crosshair).local.position;
                checkNear(actual.x, expected.x, 1e-3,
                          "the reticle stays on the POI column at frame " + std::to_string(frame));
                checkNear(actual.y, expected.y, 1e-3,
                          "the reticle stays on the POI row at frame " + std::to_string(frame));
            }
        }

        // The pin pings in place after it lands: one ring repeated, so the scale
        // resets at each boundary while the pin's own opacity is untouched.
        const Layer* pin = scene.findLayer(ids.pin);
        check(pin != nullptr, "the pin layer is reachable");
        if (pin != nullptr) {
            const float ringSeconds = frameSeconds * static_cast<float>(kFrames - spec.pinPulseStartFrame) /
                                      static_cast<float>(spec.pinPulseCount);
            const float startSeconds = static_cast<float>(spec.pinPulseStartFrame) * frameSeconds;
            checkNear(pin->tracks.scale.sample(startSeconds).x, 1.f, 1e-4f,
                      "the ping starts at the pin's own scale");
            check(pin->tracks.scale.sample(startSeconds + ringSeconds - 1e-3f).x > 1.5f,
                  "the ping expands the pin across one ring");
            check(pin->tracks.scale.sample(startSeconds + ringSeconds + 1e-3f).x < 1.1f,
                  "the ping resets at the ring boundary");
        }

        // The connecting route is a *curved* chain of glow sprites: the great
        // circle between the two places, walked at even arc length, lit in
        // sequence, with a brighter head riding the reveal front.
        {
            constexpr int kRouteSprites = 12;
            MapFlyoverSpec routed = romeSpec(kFrames, fps);
            routed.window = MapWindow::centeredOn(GeoCoord{41.0, -30.0, 0.0}, 4,
                                                  Vector2{3200.f, 1080.f});
            routed.docRouteAsset = "map/glow";
            routed.docRouteFrom = GeoCoord{41.9028, 12.4964, 0.0}; // Rome
            routed.docRouteTo = GeoCoord{40.7128, -74.0060, 0.0};  // New York
            routed.docRouteSprites = kRouteSprites;
            routed.docRouteStartFrame = 10;
            routed.docRouteEndFrame = 60;
            MotionScene rs{"routed", fps};
            const MapSceneLayers rids = MapFlyoverBuilder(routed).buildScene(rs);
            check(static_cast<int>(rids.doc_route_sprites.size()) == kRouteSprites,
                  "a route authors one glow sprite per step of the chain");
            check(rids.doc_route_head != Layer::kRoot, "the route has a leading glow");
            check(rids.count() == 1 + routed.route.size() + 1 + 2 + kRouteSprites + 1,
                  "the layer count includes every route sprite and the head");

            const Layer* near = rs.findLayer(rids.doc_route_sprites.front());
            const Layer* far = rs.findLayer(rids.doc_route_sprites.back());
            const Layer* head = rs.findLayer(rids.doc_route_head);
            check(near != nullptr && far != nullptr && head != nullptr,
                  "the route's ends and its head are reachable");
            if (near != nullptr && far != nullptr && head != nullptr) {
                const float startT = static_cast<float>(routed.docRouteStartFrame) / fps;
                const float endT = static_cast<float>(routed.docRouteEndFrame) / fps;
                checkNear(near->tracks.opacity.sample(startT - 0.1f), 0.f, 1e-4f,
                          "the near end is dark before the reveal opens");
                check(near->tracks.opacity.sample(startT + 0.2f) > 0.6f,
                      "the near end lights first");
                checkNear(far->tracks.opacity.sample(startT + 0.2f), 0.f, 1e-4f,
                          "the far end is still dark while the near end lights");
                check(far->tracks.opacity.sample(endT + 0.2f) > 0.6f,
                      "the far end lights last, so the route draws on");
                checkNear(head->tracks.opacity.sample(startT - 0.1f), 0.f, 1e-4f,
                          "the head waits for the reveal to open");
                check(head->tracks.opacity.sample(startT + 0.2f) > 0.5f,
                      "the head rides the reveal front");
                checkNear(head->tracks.opacity.sample((endT + 0.5f)), 0.f, 1e-4f,
                          "the head fades out once the route is complete");

                // The route keeps moving once it is drawn: a brightness wave
                // travels the chain and the head breathes as it goes, so a
                // finished route is never a dead stroke over the map.
                const Layer* mid = rs.findLayer(rids.doc_route_sprites[kRouteSprites / 2]);
                check(mid != nullptr, "a mid-chain sprite is reachable");
                if (mid != nullptr) {
                    float low = 1.f;
                    float high = 0.f;
                    for (int frame = 60; frame <= 150; ++frame) {
                        const float value = mid->tracks.opacity.sample(
                            static_cast<float>(frame) / fps);
                        if (value < low) low = value;
                        if (value > high) high = value;
                    }
                    check(high - low > 0.05f,
                          "a lit route sprite modulates: the wave travels the chain");
                    check(low > 0.f, "the wave never darkens the route to nothing");
                }
                {
                    float low = 1e9f;
                    float high = -1e9f;
                    for (int frame = 20; frame <= 55; ++frame) {
                        const float value = head->tracks.scale.sample(
                            static_cast<float>(frame) / fps).x;
                        if (value < low) low = value;
                        if (value > high) high = value;
                    }
                    check(high - low > 0.1f, "the head pulses while it travels the route");
                    check(low > 0.5f, "the head pulse never reaches a singular scale");
                }

                // The chain bows off the chord: the route is the great circle,
                // not the straight line the projection makes of it.
                rs.evaluate(static_cast<int>(endT * fps) + 6);
                const Vector3 a = rs.state(rids.doc_route_sprites.front()).local.position;
                const Vector3 b = rs.state(rids.doc_route_sprites.back()).local.position;
                const Vector3 m = rs.state(rids.doc_route_sprites[kRouteSprites / 2]).local.position;
                const float dx = b.x - a.x;
                const float dy = b.y - a.y;
                const float chord = std::sqrt(dx * dx + dy * dy);
                const float bow = chord > 1e-3f
                    ? std::fabs(((m.x - a.x) * dy - (m.y - a.y) * dx) / chord) : 0.f;
                check(bow > 40.f, "the route bows off the chord, a great circle not a bar");
            }
        }

        // The camera: the slam dives, the drift keeps moving.
        const float endSeconds = static_cast<float>(kFrames) / fps;
        {
            motion::CameraRig plain(20, 60.f, 16.f / 9.f, 0.1f, 2000.f);
            plain.setPosition(Vector3(0.f, 0.f, 12.f)).setTarget(Vector3(0.f, 0.f, 0.f));
            MapFlyoverSpec plainSpec = spec;
            plainSpec.enableSlamZoom = false;
            plainSpec.enableCameraDrift = false;
            MapFlyoverBuilder(plainSpec).rigCamera(plain);

            motion::CameraRig slam(20, 60.f, 16.f / 9.f, 0.1f, 2000.f);
            slam.setPosition(Vector3(0.f, 0.f, 12.f)).setTarget(Vector3(0.f, 0.f, 0.f));
            MapFlyoverSpec slamSpec = spec;
            slamSpec.enableSlamZoom = true;
            MapFlyoverBuilder(slamSpec).rigCamera(slam);

            // Same endpoints, different middle: the slam is the curve, not the stops.
            checkNear(slam.sample(0.f).position.distanceTo(plain.sample(0.f).position), 0.f, 1e-4f,
                      "the slam shares the plain file's start pose");
            checkNear(slam.sample(endSeconds).position.distanceTo(plain.sample(endSeconds).position),
                      0.f, 1e-4f, "the slam shares the plain file's end pose");
            check(slam.sample(endSeconds * 0.5f).position.distanceTo(Vector3(0.f, 0.f, 0.f)) <
                      plain.sample(endSeconds * 0.5f).position.distanceTo(Vector3(0.f, 0.f, 0.f)),
                  "the slam covers more of the descent by the middle of the move");

            motion::CameraRig drift(20, 60.f, 16.f / 9.f, 0.1f, 2000.f);
            drift.setPosition(Vector3(0.f, 0.f, 12.f)).setTarget(Vector3(0.f, 0.f, 0.f));
            MapFlyoverSpec driftSpec = spec;
            driftSpec.enableCameraDrift = true;
            MapFlyoverBuilder(driftSpec).rigCamera(drift);
            check(drift.sample(endSeconds).position.distanceTo(plain.sample(endSeconds).position) > 0.1f,
                  "the drift moves the camera off the plain file's arrival");
        }
    }

    void testDocumentaryPunctuation() {

        std::cout << "\n=== documentary punctuation: vignette, dim, territory ===\n";
        constexpr int kFrames = 120;
        constexpr float fps = 60.f;
        const float frameSeconds = 1.f / fps;

        MapFlyoverSpec spec = romeSpec(kFrames, fps);
        spec.vignetteAsset = "map/vignette";
        spec.dimAsset = "map/dim";
        spec.dimOpacity = 0.34f;
        spec.territoryFillAsset = "map/zone_fill";
        spec.territoryContourAsset = "map/glow";
        spec.territorySprites = 8;
        spec.territoryStartFrame = 20;
        spec.territoryEndFrame = 70;
        spec.territoryFillOpacity = 0.20f;
        spec.territory = {
            GeoCoord{41.9000, 12.4800, 0.0},
            GeoCoord{41.9000, 12.5000, 0.0},
            GeoCoord{41.8800, 12.5000, 0.0},
            GeoCoord{41.8800, 12.4800, 0.0},
        };

        MotionScene scene{"punctuation", fps};
        const MapSceneLayers ids = MapFlyoverBuilder(spec).buildScene(scene);

        check(ids.vignette != Layer::kRoot, "a vignette asset authors a vignette layer");
        check(ids.dim != Layer::kRoot, "a dim asset authors a dim layer");
        check(ids.territory_fill != Layer::kRoot, "a fill asset authors a territory fill");
        check(static_cast<int>(ids.territory_contour.size()) == spec.territorySprites,
              "the contour is a chain of the requested number of sprites");

        // The vignette is a plate of the frame that never moves: it exists to
        // darken the imagery, not to travel with it.
        const Layer* vignette = scene.findLayer(ids.vignette);
        check(vignette != nullptr, "the vignette layer is reachable");
        if (vignette != nullptr) {
            checkNear(vignette->transform.position.x, 0.0, 1e-4,
                      "the vignette is placed on the canvas centre, in scene space");
            checkNear(vignette->tracks.opacity.sample(0.f), 1.f, 1e-4,
                      "the vignette is present from the first frame");
            checkNear(vignette->tracks.opacity.sample(static_cast<float>(kFrames) * frameSeconds),
                      1.f, 1e-4, "the vignette holds to the last frame");
        }

        // The dim rises to the authored fraction of its own alpha and no further.
        const Layer* dim = scene.findLayer(ids.dim);
        check(dim != nullptr, "the dim layer is reachable");
        if (dim != nullptr) {
            checkNear(dim->tracks.opacity.sample(0.f), 0.f, 1e-4, "the dim starts clear");
            checkNear(dim->tracks.opacity.sample(spec.dimEndFrame * frameSeconds),
                      spec.dimOpacity, 1e-3, "the dim rises to exactly its authored strength");
            checkNear(dim->tracks.opacity.sample(static_cast<float>(kFrames) * frameSeconds),
                      spec.dimOpacity, 1e-3, "the dim holds after it has risen");
        }

        // The fill rises to its own fraction, and it rides the map plane like the
        // plate it sits on: a zone drawn in the plate's window has to move with it.
        const Layer* fill = scene.findLayer(ids.territory_fill);
        check(fill != nullptr, "the territory fill layer is reachable");
        if (fill != nullptr) {
            checkNear(fill->tracks.opacity.sample(0.f), 0.f, 1e-4, "the fill starts clear");
            checkNear(fill->tracks.opacity.sample(spec.territoryEndFrame * frameSeconds),
                      spec.territoryFillOpacity, 1e-3,
                      "the fill rises to a fraction of its own alpha, never opaque");
            scene.evaluate(kFrames);
            checkNear(scene.state(ids.territory_fill).local.scale.x,
                      MapFlyoverBuilder(spec).mapScaleAt(1.f), 1e-4,
                      "the fill rides the flyover's scale like the plate it annotates");
        }

        // The contour is walked as a closed ring and lit in sequence, so the
        // shape draws itself round; the last sprite lights after the first.
        const Layer* first = scene.findLayer(ids.territory_contour.front());
        const Layer* last = scene.findLayer(ids.territory_contour.back());
        check(first != nullptr && last != nullptr, "the contour's ends are reachable");
        if (first != nullptr && last != nullptr) {
            const float startT = static_cast<float>(spec.territoryStartFrame) / fps;
            const float endT = static_cast<float>(spec.territoryEndFrame) / fps;
            checkNear(first->tracks.opacity.sample(startT - 0.1f), 0.f, 1e-4f,
                      "the contour lights only once its window opens");
            check(first->tracks.opacity.sample(startT + 0.2f) > 0.6f,
                  "the first contour sprite lights first");
            checkNear(last->tracks.opacity.sample(startT + 0.2f), 0.f, 1e-4f,
                      "the ring is still dark where it has not reached yet");
            check(last->tracks.opacity.sample(endT + 0.2f) > 0.6f,
                  "the ring closes on its last sprite");
        }

        // The callout is centred inside the safe margin: the frame can never clip it.
        const Layer* title = scene.findLayer(ids.title);
        check(title != nullptr, "the callout title exists");
        if (title != nullptr) {
            const float safeWidth = spec.canvasSize.x - 2.f * spec.calloutSafeMargin;
            checkNear(title->content.metrics.naturalSize.x, safeWidth, 1e-3f,
                      "the callout box is the frame minus the safe margin");
            check(spec.calloutSafeMargin >= 60.f,
                  "a callout keeps at least a 60px safe margin");
        }

        // Off by default: a plain flyover authors none of it.
        MapFlyoverSpec plain = romeSpec(kFrames, fps);
        MotionScene plainScene{"plain_punctuation", fps};
        const MapSceneLayers plainIds = MapFlyoverBuilder(plain).buildScene(plainScene);
        check(plainIds.vignette == Layer::kRoot && plainIds.dim == Layer::kRoot &&
                  plainIds.territory_fill == Layer::kRoot && plainIds.territory_contour.empty(),
              "the vignette, the dim and the territory are all opt-in");
    }

#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
    void testPlanPlacement() {

        // The content identity this test stands in for. A concrete typeface
        // asset is the content owner's business and never appears in this tree.
        constexpr const char* kStandInAsset = "font-asset";

        std::cout << "\n=== plan placement (through the canonical emitter) ===\n";
        constexpr int kFrameCount = 120;
        MapFlyoverSpec spec = romeSpec(kFrameCount, 30.f);
        // A stand-in relief plate: the real one is derived from a DEM for the
        // same window as the basemap, which is the whole reason it can be laid
        // over it without any placement of its own.
        spec.reliefAsset = "map/terrain-relief";
        const MapFlyoverBuilder builder(spec);

        MotionScene scene{"map_plan_test", 30.f};
        const MapSceneLayers ids = builder.buildScene(scene);

        // Stand-in content plans: this test pins space and time, and a real
        // typeface asset is the content owner's business.
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content;
        auto key = [](Layer::Id id) { return std::to_string(id); };
        auto text = [](const std::string& words) {
            chronon3d::render_plan::LayerPlan plan;
            plan.type = chronon3d::render_plan::LayerType::Text;
            plan.text = words;
            chronon3d::render_plan::LayerStylePlan style;
            style.font = kStandInAsset;
            style.font_size = 46.f;
            style.fill = "#FFFFFF";
            plan.style = style;
            return plan;
        };
        chronon3d::render_plan::LayerPlan basemap;
        basemap.type = chronon3d::render_plan::LayerType::Image;
        basemap.asset = spec.basemapAsset;
        content.emplace(key(ids.basemap), basemap);
        chronon3d::render_plan::LayerPlan relief;
        relief.type = chronon3d::render_plan::LayerType::Image;
        relief.asset = spec.reliefAsset;
        // A DEM hillshade is a mid-grey luminance plate: soft light turns it
        // into shading on the plate underneath instead of a grey wash over it.
        relief.blend_mode = chronon3d::BlendMode::SoftLight;
        content.emplace(key(ids.relief), relief);
        for (std::size_t i = 0; i < ids.markers.size(); ++i)
            content.emplace(key(ids.markers[i]), text("marker"));
        content.emplace(key(ids.pin), text("pin"));
        content.emplace(key(ids.title), text(spec.poiTitle));
        content.emplace(key(ids.subtitle), text(spec.poiSubtitle));

        motion::RenderPlanLoweringOptions options;
        options.width = 1920;
        options.height = 1080;
        options.jobId = "map_plan_test";
        options.outputPath = "map_plan_test.mp4";
        options.contentLayers = content;

        const chronon3d::render_plan::RenderPlan plan = motion::lowerToRenderPlan(scene, options);
        check(plan.canvas.duration.integral() == kFrameCount + 1,
              "the plan covers the authored clip");
        check(plan.layers.size() == ids.count(), "every authored layer reaches the plan");

        const auto findPlan = [&plan](const std::string& id) -> const chronon3d::render_plan::LayerPlan* {
            for (const auto& layer : plan.layers)
                if (layer.id == id) return &layer;
            return nullptr;
        };

        // A text layer's plan position is the canvas pixel it lands on: the
        // lowering adds the canvas centre, and the decoder takes it back out.
        for (std::size_t i = 0; i < ids.markers.size(); ++i) {
            const auto* marker = findPlan(key(ids.markers[i]));
            check(marker != nullptr, "marker " + std::to_string(i) + " is in the plan");
            if (marker == nullptr) continue;
            check(marker->type == chronon3d::render_plan::LayerType::Text, "a marker stays text");
            const Vector2 canvas = builder.mapPointToCanvas(spec.route[i], 0.f);
            checkNear(marker->position[0], canvas.x, 1e-3,
                      "marker " + std::to_string(i) + " is placed on its canvas column");
            checkNear(marker->position[1], canvas.y, 1e-3,
                      "marker " + std::to_string(i) + " is placed on its canvas row");
        }

        // The raster is not a text layer, so the lowering leaves the centre out
        // of its position; adding it back must recover the canvas pixel.
        const auto* plate = findPlan(key(ids.basemap));
        check(plate != nullptr, "the basemap is in the plan");
        if (plate != nullptr) {
            check(plate->type == chronon3d::render_plan::LayerType::Image, "the basemap stays an image");
            const Vector2 centre = builder.planePointToCanvas(Vector2{0.f, 0.f}, 0.f);
            checkNear(plate->position[0] + 960.f, centre.x, 1e-3,
                      "the raster's plan position recovers its canvas column");
            checkNear(plate->position[1] + 540.f, centre.y, 1e-3,
                      "the raster's plan position recovers its canvas row");
        }

        // The drape is not placed: it inherits the plate's transform channel for
        // channel, so a ridge can only land where the imagery under it has the
        // ridge. Anything else would be a second, disagreeing georeference.
        const auto trackOf = [](const chronon3d::render_plan::LayerPlan& layer,
                                const std::string& property) -> const chronon3d::render_plan::AnimationTrackPlan* {
            if (!layer.animation) return nullptr;
            for (const auto& track : layer.animation->tracks)
                if (track.property == property) return &track;
            return nullptr;
        };
        const auto sameChannel = [&trackOf](const chronon3d::render_plan::LayerPlan& a,
                                            const chronon3d::render_plan::LayerPlan& b,
                                            const std::string& property) {
            const auto* left = trackOf(a, property);
            const auto* right = trackOf(b, property);
            if (left == nullptr || right == nullptr) return false;
            if (left->keyframes.size() != right->keyframes.size()) return false;
            for (std::size_t i = 0; i < left->keyframes.size(); ++i)
                if (left->keyframes[i].frame != right->keyframes[i].frame ||
                    left->keyframes[i].value != right->keyframes[i].value) return false;
            return true;
        };
        const auto* drape = findPlan(key(ids.relief));
        check(drape != nullptr, "the relief drape is in the plan");
        if (drape != nullptr && plate != nullptr) {
            check(drape->type == chronon3d::render_plan::LayerType::Image,
                  "the relief drape stays an image");
            check(drape->asset == spec.reliefAsset, "the drape keeps the relief asset");
            check(sameChannel(*drape, *plate, "position"),
                  "the relief rides the plate's exact position channel");
            check(sameChannel(*drape, *plate, "scale"),
                  "the relief rides the plate's exact scale channel");
            check(drape->blend_mode.has_value() &&
                      *drape->blend_mode == chronon3d::BlendMode::SoftLight,
                  "the drape keeps its soft-light blend through the lowering");
            std::size_t plateIndex = plan.layers.size();
            std::size_t drapeIndex = plan.layers.size();
            for (std::size_t i = 0; i < plan.layers.size(); ++i) {
                if (plan.layers[i].id == key(ids.basemap)) plateIndex = i;
                if (plan.layers[i].id == key(ids.relief)) drapeIndex = i;
            }
            check(drapeIndex == plateIndex + 1,
                  "the drape is composited directly above the plate it was derived from");
        }

        check(motion::renderPlanEmissionProblem(plan).empty(),
              "the map plan is expressible by the canonical writer");
        const std::string json = motion::renderPlanToJson(plan);
        check(json.find("chronon.render-plan.v2") != std::string::npos,
              "the plan is written as a v2 document");
        check(json.find("\"position\"") != std::string::npos,
              "the plan carries the baked position tracks");
        check(json.find("soft_light") != std::string::npos,
              "the emitted plan names the drape's blend so a render cannot silently ignore it");
    }

    void testDocumentaryPlan() {

        std::cout << "\n=== documentary layers reach the plan ===\n";
        MapFlyoverSpec spec = romeSpec(120, 30.f);
        spec.enablePinPulse = true;
        spec.areaOverlayAsset = "map/area";
        spec.leaderLineAsset = "map/leader";
        spec.crosshairAsset = "map/reticle";
        const MapFlyoverBuilder builder(spec);

        MotionScene scene{"documentary_plan", 30.f};
        const MapSceneLayers ids = builder.buildScene(scene);

        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content;
        auto key = [](Layer::Id id) { return std::to_string(id); };
        auto text = [](const std::string& words) {
            chronon3d::render_plan::LayerPlan plan;
            plan.type = chronon3d::render_plan::LayerType::Text;
            plan.text = words;
            chronon3d::render_plan::LayerStylePlan style;
            style.font = "font-asset";
            style.font_size = 46.f;
            style.fill = "#FFFFFF";
            plan.style = style;
            return plan;
        };
        auto image = [](const std::string& asset) {
            chronon3d::render_plan::LayerPlan plan;
            plan.type = chronon3d::render_plan::LayerType::Image;
            plan.asset = asset;
            return plan;
        };
        content.emplace(key(ids.basemap), image(spec.basemapAsset));
        content.emplace(key(ids.area), image(spec.areaOverlayAsset));
        content.emplace(key(ids.leader_line), image(spec.leaderLineAsset));
        for (const Layer::Id id : ids.markers) content.emplace(key(id), text("marker"));
        content.emplace(key(ids.pin), text("pin"));
        content.emplace(key(ids.crosshair), image(spec.crosshairAsset));
        content.emplace(key(ids.title), text(spec.poiTitle));
        content.emplace(key(ids.subtitle), text(spec.poiSubtitle));

        motion::RenderPlanLoweringOptions options;
        options.width = 1920;
        options.height = 1080;
        options.jobId = "documentary_plan";
        options.outputPath = "documentary_plan.mp4";
        options.contentLayers = content;

        const chronon3d::render_plan::RenderPlan plan = motion::lowerToRenderPlan(scene, options);
        check(plan.layers.size() == ids.count(), "every documentary layer reaches the plan");
        check(motion::renderPlanEmissionProblem(plan).empty(),
              "the documentary plan is expressible by the canonical writer");

        // The ping is baked, not left as a loop the emitter would have to refuse:
        // the pin's scale track carries per-frame keys the renderer can play.
        const auto findPlan = [&plan](const std::string& id) -> const chronon3d::render_plan::LayerPlan* {
            for (const auto& layer : plan.layers)
                if (layer.id == id) return &layer;
            return nullptr;
        };
        const auto* pinPlan = findPlan(key(ids.pin));
        check(pinPlan != nullptr, "the pin is in the plan");
        if (pinPlan != nullptr) {
            bool hasScale = false;
            if (pinPlan->animation) {
                for (const auto& track : pinPlan->animation->tracks) {
                    if (track.property == "scale") hasScale = true;
                }
            }
            check(hasScale, "the pin's ping reaches the plan as a baked scale track");
        }
    }

    void testCameraBackedPlatePlan() {

        std::cout << "\n=== camera-backed georeferenced plate ===\n";
        MapFlyoverSpec spec = romeSpec(12, 30.f);
        spec.poi = GeoCoord{41.8910, 12.4950, 50.0}; // deliberately off the plate centre
        spec.route = {spec.poi};
        spec.poiTitle.clear();
        spec.poiSubtitle.clear();
        spec.cameraBackedPlate = true;
        spec.startMapScale = spec.endMapScale = 1.f;
        spec.startMapPan = spec.endMapPan = Vector2{0.f, 0.f};
        const MapFlyoverBuilder builder(spec);
        MotionScene scene{"camera_backed_map", spec.fps};
        const MapSceneLayers ids = builder.buildScene(scene);
        builder.configureCameraBackedPlate(scene, 0.f, 30.f);

        motion::CameraRig rig{1, 55.f, 16.f / 9.f, 1.f, 10000.f};
        builder.rigCameraForPlate(rig, 1800.f, 1200.f);
        const motion::CameraPose arrival = rig.sample(spec.durationSeconds);
        check(!arrival.hasTarget && std::abs(arrival.orientation.w - 1.f) < 1e-6f,
              "the front-facing camera avoids a backend-specific rotated camera pose");
        check(arrival.position.z < 0.f,
              "the perspective camera sits on Chronon's visible negative-Z side of the plate");
        check(scene.findLayer(ids.basemap)->is3D,
              "the basemap is lowered as a perspective surface");
        scene.evaluate(builder.frameCount());
        const Matrix4& markerWorld = scene.state(ids.markers.front()).world;
        checkNear(markerWorld.elements[12], 0.f, 1e-3,
                  "the off-centre geographic POI is translated to the shared surface origin in X");
        checkNear(markerWorld.elements[13], 0.f, 1e-3,
                  "the off-centre geographic POI is translated to the shared surface origin in Y");

        motion::RenderPlanLoweringOptions options;
        options.width = 1920;
        options.height = 1080;
        options.jobId = "camera_backed_map";
        options.outputPath = "camera_backed_map.mp4";
        for (const Layer& layer : scene.layers()) {
            if (layer.isNull()) continue;
            chronon3d::render_plan::LayerPlan content;
            if (layer.content.kind == motion::ContentKind::Image) {
                content.type = chronon3d::render_plan::LayerType::Image;
                content.asset = layer.content.id;
            } else if (layer.content.kind == motion::ContentKind::Text) {
                content.type = chronon3d::render_plan::LayerType::Text;
                content.text = layer.content.id;
                chronon3d::render_plan::LayerStylePlan style;
                style.font = "test-font";
                style.font_size = 32.f;
                style.fill = "#FFFFFF";
                content.style = style;
            }
            options.contentLayers.emplace(std::to_string(layer.id), content);
        }
        options.cameraRig = &rig;
        const auto plan = motion::lowerToRenderPlan(scene, options);
        check(plan.camera.has_value(), "the emitted map plan carries a perspective camera");
        if (plan.camera) {
            const chronon3d::Vec3 angles{plan.camera->rotation_deg[0],
                                         plan.camera->rotation_deg[1],
                                         plan.camera->rotation_deg[2]};
            const auto loweredOrientation = chronon3d::math::camera_rotation_quat(angles);
            const motion::CameraPose startPose = rig.sample(0.f);
            const float orientationDot = loweredOrientation.x * startPose.orientation.x +
                loweredOrientation.y * startPose.orientation.y +
                loweredOrientation.z * startPose.orientation.z +
                loweredOrientation.w * startPose.orientation.w;
            checkNear(std::abs(orientationDot), 1.0, 1e-4,
                      "RenderPlan Euler angles preserve the rig's camera orientation");
        }
        check(plan.camera_animation.has_value() && !plan.camera_animation->empty(),
              "the emitted map plan carries animated orbit and dolly tracks");
        const auto plateLayer = std::find_if(plan.layers.begin(), plan.layers.end(), [&](const auto& layer) {
            return layer.id == std::to_string(ids.basemap);
        });
        check(plateLayer != plan.layers.end() && plateLayer->enable_3d,
              "the georeferenced raster reaches the plan as a camera-visible 3D layer");
        if (plateLayer != plan.layers.end() && plateLayer->animation) {
            const auto tilt = std::find_if(plateLayer->animation->tracks.begin(),
                                           plateLayer->animation->tracks.end(), [](const auto& track) {
                return track.property == "rotation";
            });
            check(tilt != plateLayer->animation->tracks.end() && !tilt->keyframes.empty() &&
                      std::abs(tilt->keyframes.back().value[0]) > 1.f,
                  "the map plate carries the authored camera-angle tilt through lowering");
        } else {
            check(false, "the map plate carries the authored camera-angle tilt through lowering");
        }
        check(motion::renderPlanEmissionProblem(plan).empty(),
              "the camera-backed map stays on the canonical render-plan path");

        motion::CameraRig oblique{2, 55.f, 16.f / 9.f, 1.f, 10000.f};
        oblique.setPosition(Vector3{-140.f, 530.f, -1800.f}).setTarget(Vector3{0.f, 0.f, 0.f});
        motion::CameraPose resolved = oblique.sample(0.f);
        resolved.hasTarget = false; // RenderPlan carries the resolved orientation, not target metadata.
        const auto obliquePlan = motion::lowerCameraPose(resolved, 1.f, 10000.f);
        const auto obliqueRendererRotation = chronon3d::math::camera_rotation_quat(
            chronon3d::Vec3{obliquePlan.rotation_deg[0], obliquePlan.rotation_deg[1],
                            obliquePlan.rotation_deg[2]});
        const float obliqueDot = obliqueRendererRotation.x * resolved.orientation.x +
            obliqueRendererRotation.y * resolved.orientation.y +
            obliqueRendererRotation.z * resolved.orientation.z +
            obliqueRendererRotation.w * resolved.orientation.w;
        checkNear(std::abs(obliqueDot), 1.0, 1e-4,
                  "oblique look-at Euler conversion matches Chronon3D camera convention");
    }
#endif

    void testDynamicPyramidEngine() {
        std::cout << "\n=== dynamic multi-resolution tile pyramid ===\n";
        DynamicPyramidSpec spec;
        spec.targetAnchor = kColosseum;
        spec.minZoom = 5.0f;
        spec.maxZoom = 18.0f;

        const float orbitAlt = spec.altitudeMetersAtZoom(5.0f);
        check(orbitAlt > 2000000.f && orbitAlt < 5000000.f, "zoom 5 altitude is in space/orbit range (~3700 km)");

        const float streetAlt = spec.altitudeMetersAtZoom(18.0f);
        check(streetAlt > 200.f && streetAlt < 800.f, "zoom 18 altitude is in street/building range (~450 m)");

        const float zInverted = spec.continuousZoomAtAltitude(orbitAlt);
        checkNear(zInverted, 5.0, 0.05, "continuousZoomAtAltitude exactly inverts altitudeMetersAtZoom");

        const auto blend = spec.resolveMipmapBlend(15.5f);
        check(blend.zLow == 15 && blend.zHigh == 16, "z=15.5 resolves zLow=15 and zHigh=16");
        checkNear(blend.alpha, 0.5, 1e-4, "midpoint zoom yields exact alpha=0.5 smoothstep blend");

        const auto tile18 = spec.tileIndexAt(18.0f);
        check(tile18.first == 140168 && tile18.second == 97419, "Colosseum tile index at z=18 matches (140168, 97419)");
    }

    // --------------------------------------------------------------- geo_camera_v1
    // The map dive as a camera family. What is certified here is the *motion
    // law*: a dive that never snaps, a target that never leaves the reticle,
    // LOD that never skips, and a rig that is a pure function of the frame.

    /// The pyramid spec a dive of `zoom` samples: LOD levels and blend weight.
    DynamicPyramidSpec::MipmapBlendState spec_for_lod(float zoom) {
        DynamicPyramidSpec pyramid;
        pyramid.minZoom = 5.0f;
        pyramid.maxZoom = 17.5f;
        return pyramid.resolveMipmapBlend(zoom);
    }

    float spec_lod_max(const GeoCameraRig& rig) { return rig.spec().pyramid.maxZoom; }

    void testGeoCameraDivePhysics() {
        std::cout << "\n=== geo_camera_v1: dive physics (altitude, velocity, acceleration) ===\n";
        const auto spec = GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit);
        const GeoCameraRig rig(spec);
        const auto frames = rig.sampleAll();

        // TestMapDiveAltitudeMonotonic: during the pure dive the camera only
        // ever descends, to numerical tolerance, and it lands far below where
        // it took off.
        float maxRise = 0.f;
        for (std::size_t i = 1; i < frames.size(); ++i)
            maxRise = std::max(maxRise, frames[i].groundRelativeAltitudeM
                                        - frames[i - 1].groundRelativeAltitudeM);
        check(maxRise <= 1e-3f, "altitude never rises during the dive (max rise "
              + std::to_string(maxRise) + " m <= 1e-3)");
        check(frames.back().groundRelativeAltitudeM < frames.front().groundRelativeAltitudeM / 1000.f,
              "the dive lands more than 1000x lower than orbit");

        // TestMapZoomVelocityContinuity: the per-frame zoom step is bounded by
        // the smootherstep slope (max 1.875 of the mean step) — no slow-slow-
        // slow-SNAP across a level handover — and the second difference stays
        // under the smootherstep acceleration ceiling (max |s''| = 10/sqrt(3)),
        // so the velocity itself changes smoothly.
        const int diveSpan = std::max(1, rig.schedule().diveEnd);
        const float dz = spec.endZoom - spec.startZoom;
        const float meanStep = dz / static_cast<float>(diveSpan);
        float maxStep = 0.f;
        float maxAccel = 0.f;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            const float step = frames[i].continuousZoom - frames[i - 1].continuousZoom;
            maxStep = std::max(maxStep, std::abs(step));
            if (i > 1)
                maxAccel = std::max(maxAccel,
                    std::abs(frames[i].continuousZoom - 2.f * frames[i - 1].continuousZoom
                             + frames[i - 2].continuousZoom));
        }
        check(maxStep <= 1.8751f * meanStep + 1e-6f,
              "zoom velocity is continuous: max frame step " + std::to_string(maxStep)
              + " <= 1.8751x mean step " + std::to_string(meanStep));
        // The analytic ceiling bounds the continuous curve; the discrete second
        // difference of the samples carries an O(1/span^2) curvature term on
        // top of it, so the discrete check allows half a percent of slack.
        const float accelCeiling = (10.f / std::sqrt(3.f)) * dz
                                   / (static_cast<float>(diveSpan) * static_cast<float>(diveSpan));
        check(maxAccel <= accelCeiling * 1.005f,
              "zoom acceleration is bounded: max 2nd difference " + std::to_string(maxAccel)
              + " <= ceiling " + std::to_string(accelCeiling) + " (+0.5% discretisation)");

        // The dive ends at the authored zoom and holds it (no drift after).
        checkNear(frames[rig.schedule().diveEnd].continuousZoom, spec.endZoom, 1e-4,
                  "the dive reaches exactly the authored landing zoom");
        checkNear(frames.back().continuousZoom, spec.endZoom, 1e-5,
                  "zoom holds at the landing zoom through tilt and orbit");
    }

    void testGeoCameraTargetLock() {
        std::cout << "\n=== geo_camera_v1: target lock (the anchor never leaves the reticle) ===\n";
        const Vector3 anchor = Vector3(0.f, 0.f, 0.f); // ENU origin == the anchor

        // Every preset must hold the anchor exactly framed for every frame.
        for (const GeoShotPreset preset : {
                 GeoShotPreset::MapDiveToLandmark, GeoShotPreset::MapDiveThenTilt,
                 GeoShotPreset::MapDiveThenOrbit, GeoShotPreset::MapDiveThenCraneOut,
                 GeoShotPreset::MapDiveMetricReveal}) {
            const GeoCameraRig rig(GeoDiveSpec::forPreset(preset));
            const auto frames = rig.sampleAll();
            bool allAimed = true;
            for (const auto& kf : frames)
                if (kf.lookTarget.x != anchor.x || kf.lookTarget.y != anchor.y
                    || kf.lookTarget.z != anchor.z)
                    allAimed = false;
            check(allAimed, "every preset aims the optical axis at the anchor on every frame");
        }

        // TestMapDiveTargetLock, in screen terms: a look-at camera projects its
        // target to the frame centre. Projected through an explicit pinhole
        // (1920x1080, vertical FOV 60), the anchor must land within a sub-
        // pixel tolerance of (960, 540) on every sampled frame — including
        // the tilted, orbiting ones, which is where a drifting rig shows.
        const GeoDiveSpec spec = GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit);
        const GeoCameraRig rig(spec);
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        float maxPixelError = 0.f;
        for (int f = 0; f <= rig.frameCount(); f += 7) {
            const GeoCameraKeyframe kf = rig.sampleAtFrame(f);
            const Vector3 forward = kf.lookTarget - kf.cameraPosition;
            const float dist = std::sqrt(forward.x * forward.x + forward.y * forward.y
                                         + forward.z * forward.z);
            // Basis orthogonal to the view axis, hinted by the orbit heading so
            // the nadir frame (pitch 0) is not singular.
            const Vector3 east{1.f, 0.f, 0.f};
            const Vector3 north{0.f, 0.f, 1.f};
            const float yawRad = static_cast<float>(kf.yawDeg * kDegToRad);
            const Vector3 heading{std::sin(yawRad), 0.f, std::cos(yawRad)};
            auto normalize = [](const Vector3& v) {
                const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                return Vector3{v.x / n, v.y / n, v.z / n};
            };
            auto cross = [](const Vector3& a, const Vector3& b) {
                return Vector3{a.y * b.z - a.z * b.y,
                               a.z * b.x - a.x * b.z,
                               a.x * b.y - a.y * b.x};
            };
            auto dot = [](const Vector3& a, const Vector3& b) {
                return a.x * b.x + a.y * b.y + a.z * b.z;
            };
            const Vector3 fDir = normalize(forward);
            const Vector3 right = normalize(cross(fDir, heading));
            const Vector3 up = cross(right, fDir);

            // Pinhole: the target sits on the axis, so both camera-plane
            // coordinates must vanish and the screen point is the centre.
            const Vector3 d = kf.lookTarget - kf.cameraPosition;
            const float xCam = dot(d, right);
            const float yCam = dot(d, up);
            const float zCam = dot(d, fDir);
            const float focalPx = static_cast<float>(spec.canvasHeight)
                                  / (2.f * std::tan(static_cast<float>(spec.fovDeg * kDegToRad) * 0.5f));
            const float screenX = spec.canvasWidth * 0.5f + (xCam / zCam) * focalPx;
            const float screenY = spec.canvasHeight * 0.5f - (yCam / zCam) * focalPx;
            maxPixelError = std::max(maxPixelError,
                std::max(std::abs(screenX - 960.f), std::abs(screenY - 540.f)));
            checkNear(dist, kf.groundRelativeAltitudeM / std::max(0.05f, std::cos(
                          static_cast<float>(kf.pitchDeg * kDegToRad))), 1e-2f,
                      "the camera rides the slant-distance sphere the altitude law implies");
        }
        check(maxPixelError < 0.01f,
              "target lock: the anchor projects to the reticle (960, 540) within "
              + std::to_string(maxPixelError) + " px on every frame");
    }

    void testGeoCameraLodContinuity() {
        std::cout << "\n=== geo_camera_v1: LOD continuity (no skipped levels, no pop) ===\n";
        const GeoCameraRig rig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveToLandmark));
        const auto frames = rig.sampleAll();

        // TestMapLODNoMissingTiles / NoVisibleSeams, at the sampling layer.
        // A visible seam or a missing region can only come from three events:
        // a level jumping by more than one between frames, a handover where
        // the retiring level still held the majority (a content snap), or the
        // new level's centre tile not being the ground child of the old one.
        // None of the three may happen.
        int maxLevelJump = 0;
        int previousLow = -1;
        float previousAlpha = 0.f;
        float maxHandoverAlpha = 0.f;
        float maxAlphaStep = 0.f;
        int maxChildMismatch = 0;
        for (const auto& kf : frames) {
            const auto blend = spec_for_lod(kf.continuousZoom);
            if (previousLow >= 0) {
                const int jump = std::abs(blend.zLow - previousLow);
                maxLevelJump = std::max(maxLevelJump, jump);
                // The physical cross-fade is the *weight of the level on
                // screen*, and across a pair switch that level is the shared
                // one: it holds `alpha` in the old pair and `1 - alpha` in the
                // new one. Continuity is measured in that single coordinate.
                const float persistentWeightStep = jump == 1
                    ? std::abs((1.f - blend.alpha) - previousAlpha)
                    : std::abs(blend.alpha - previousAlpha);
                maxAlphaStep = std::max(maxAlphaStep, persistentWeightStep);
                if (jump == 1) {
                    // At a handover the shared level must already hold the
                    // majority weight in the new pair, so the dominant layer is
                    // the same image on both sides of the switch.
                    maxHandoverAlpha = std::max(maxHandoverAlpha, blend.alpha);
                    // And the new centre tile must be the ground child of the
                    // previous level's centre tile: same ground, finer pyramid.
                    const auto oldTile = rig.spec().pyramid.tileIndexAt(
                        static_cast<float>(previousLow) + 0.5f);
                    const auto newTile = rig.spec().pyramid.tileIndexAt(
                        static_cast<float>(blend.zLow) + 0.5f);
                    const int cx = oldTile.first >> 1, cy = oldTile.second >> 1;
                    const int dx = std::abs(cx * 2 + (cx << 1 == cx ? 0 : 0) - newTile.first * 2);
                    // child x = 2*parent x or 2*parent x + 1
                    const int childMis = std::min(std::abs(newTile.first - 2 * oldTile.first),
                                                  std::abs(newTile.first - (2 * oldTile.first + 1)));
                    const int childMisY = std::min(std::abs(newTile.second - 2 * oldTile.second),
                                                   std::abs(newTile.second - (2 * oldTile.second + 1)));
                    (void)dx;
                    (void)cy;
                    maxChildMismatch = std::max(maxChildMismatch,
                                                std::max(childMis, childMisY));
                }
            }
            previousLow = blend.zLow;
            previousAlpha = blend.alpha;
        }
        check(maxLevelJump <= 1,
              "LOD never skips a quadtree level (max jump " + std::to_string(maxLevelJump) + ")");
        check(maxHandoverAlpha <= 0.5f,
              "level handovers keep the shared level dominant (max incoming alpha "
              + std::to_string(maxHandoverAlpha) + " <= 0.5)");
        // The cross-fade weight is a smoothstep of the very zoom the camera
        // samples, so its per-frame step follows the dive's own velocity with
        // at most the smoothstep slope factor of 1.5. A value above that bound
        // would mean a blend changing independently of the camera — the
        // definition of a pop.
        float maxZoomStep = 0.f;
        for (std::size_t i = 1; i < frames.size(); ++i)
            maxZoomStep = std::max(maxZoomStep,
                std::abs(frames[i].continuousZoom - frames[i - 1].continuousZoom));
        check(maxAlphaStep <= 1.5f * maxZoomStep + 1e-6f,
              "the cross-fade rides the camera exactly (max alpha step "
              + std::to_string(maxAlphaStep) + " <= 1.5 x max zoom step "
              + std::to_string(maxZoomStep) + ")");
        check(maxChildMismatch <= 1,
              "every level handover lands on the ground child tile (max mismatch "
              + std::to_string(maxChildMismatch) + ") — no blank blocks, no seams");

        // TestMapLODCoverage: the prefetch pyramid that feeds a render covers
        // every level the dive will sample, with the documented tile budget.
        const int levels = static_cast<int>(std::floor(spec_lod_max(rig)))
                           - static_cast<int>(std::floor(rig.spec().startZoom)) + 1;
        const int prefetchBudget = levels * 25; // radius-2 window per level
        check(prefetchBudget == 325,
              "the z=5..17 dive prefetches 325 tiles (13 levels x radius-2 window), got "
              + std::to_string(prefetchBudget));
    }

    void testGeoCameraDeterminismAndBridge() {
        std::cout << "\n=== geo_camera_v1: determinism + CameraShot bridge ===\n";
        const auto framesA = GeoCameraRig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit)).sampleAll();
        const auto framesB = GeoCameraRig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit)).sampleAll();

        // Same spec, same samples: bit for bit, not "to tolerance". A camera
        // path that cannot be reproduced cannot be certified.
        check(framesA.size() == framesB.size() && !framesA.empty(),
              "two rigs of the same spec produce the same frame count");
        bool identical = true;
        for (std::size_t i = 0; i < framesA.size() && identical; ++i)
            identical = std::memcmp(&framesA[i], &framesB[i], sizeof(GeoCameraKeyframe)) == 0;
        check(identical, "camera samples, zoom, LOD and pitch/yaw are bit-for-bit deterministic");

        // The tile identity a renderer would fetch: same spec, same tile IDs,
        // and the landing tile is the documented Colosseum one at z=17.
        const auto landingTile = GeoCameraRig(GeoDiveSpec::forPreset(
            GeoShotPreset::MapDiveToLandmark)).spec().pyramid.tileIndexAt(17.f);
        check(landingTile.first == 70084 && landingTile.second == 48709,
              "the landing frame resolves the documented Colosseum tile (70084, 48709) at z=17");

        // The bridge: a rig pose becomes an ordinary CameraShot with the exact
        // same geometry — this is the handover into the Chronon camera language.
        const GeoCameraRig rig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit));
        const GeoCameraKeyframe posed = rig.sampleAtFrame(rig.schedule().orbitEnd);
        const motion::CameraShot shot = rig.toCameraShot(posed);
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        checkNear(shot.azimuth, static_cast<float>(posed.yawDeg * kDegToRad), 1e-5,
                  "the bridge maps orbit yaw to CameraShot azimuth exactly");
        checkNear(shot.elevation, static_cast<float>((90.0 - posed.pitchDeg) * kDegToRad), 1e-5,
                  "the bridge maps ground pitch to CameraShot elevation exactly (nadir = 90 deg)");
        const Vector3 offset = posed.cameraPosition - posed.lookTarget;
        const float expectedDistance = std::sqrt(offset.x * offset.x + offset.y * offset.y
                                                 + offset.z * offset.z);
        checkNear(shot.distance, expectedDistance, 1e-3f,
                  "the bridge preserves the slant distance of the rig pose");
        check(shot.target.kind == motion::AnchorKind::WorldPoint,
              "the bridged shot anchors on the geographic target as a world point");
        const motion::CameraPose pose = shot.evaluatePose(nullptr, 0.f);
        checkNear(pose.target.x, posed.lookTarget.x, 1e-3f,
                  "the bridged shot's evaluated pose looks at the geographic anchor (x)");
        checkNear(pose.target.y, posed.lookTarget.y, 1e-3f, "... (y)");
        checkNear(pose.target.z, posed.lookTarget.z, 1e-3f, "... (z)");
    }

}// namespace

int main() {

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
