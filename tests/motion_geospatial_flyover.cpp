#include "motion_geospatial_shared.hpp"

namespace chrononmotion_test::geospatial {
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
}
