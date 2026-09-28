// ChrononMotion — geospatial, Web Mercator and map flyover tests.
//
// Three things are pinned here, in order of how expensive they would be to get
// wrong:
//
//   1. The geodesy. ECEF/Bowring/ENU and Haversine against known WGS84 values.
//   2. Web Mercator *against the other implementation of it*. The raster plates
//      are cropped by `tools/map_cartography.py`, and this side georeferences
//      them; if the two formulas drift, every marker lands on the wrong pixel
//      and nothing else in the tree would notice. The expected numbers below are
//      that Python's output, so this file fails the moment the two disagree.
//   3. The authoring bridge. A map point becomes a layer position by exactly one
//      conversion, and the plan that comes out of the lowering must place a
//      marker on the canvas pixel its geography maps to.
//
// What is deliberately *not* here: whether the pixels look right. That needs a
// raster and a renderer and lives in the end-to-end run of
// `chrononmotion_map_flyover`; a unit test that invented a plate could only
// assert its own invention.

#include "chrononmotion/geospatial/Geodetic.hpp"
#include "chrononmotion/geospatial/MapFlyover.hpp"
#include "chrononmotion/geospatial/WebMercator.hpp"

#include "chrononmotion/motion/MotionScene.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
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
        // These are the exact values `tools/map_cartography.py`
        // `latlon_to_mercator_px` returns for the same inputs. They are the
        // contract between the side that crops the raster and the side that
        // georeferences it.
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
#endif

}// namespace

int main() {

    std::cout << "=== ChrononMotion geospatial tests ===\n";
    testGeodesy();
    testWebMercator();
    testFlyoverScene();
    testCalloutAndClamp();
#ifdef CHRONONMOTION_HAS_CHRONON3D_ADAPTER
    testPlanPlacement();
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
