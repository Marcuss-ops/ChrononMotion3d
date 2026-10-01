// ChrononMotion — the map flyover plan generator.
//
// The one place in this tree that turns a geospatial Motion scene into a
// renderable file, and the counterpart of `plan_showreel.cpp` for maps:
//
//     MotionScene  ->  lowerToRenderPlan  ->  renderPlanToJson  ->  .plan.json
//
// A map is two halves. Chronon owns the content half — the raster basemap, the
// typeface, the fills, the pixels. ChrononMotion owns the other half: which
// geography the raster covers, where a coordinate lands on it, and how the whole
// plane moves. This tool is where the two meet, and it is deliberately one-sided:
// nothing here fetches a tile, draws a route or names a colour on a map. It is
// handed a plate that was rendered *for a known window* and it places markers on
// the pixel that window puts them on.
//
// The window is the truth about a plate. `MapWindow::centeredOn` reproduces the
// exact crop Chronon3d/tools/cartography/map_cartography.py
// (`build_georeferenced_basemap`) made from `centerLat`/`centerLon`/`zoom`,
// which is why a place here is a window plus a basemap and never just a basemap:
// naming a plate without its window would put every marker on a map that is not
// there.
//
// Usage: chrononmotion_map_flyover [place] [output.plan.json] [font-asset] [frames]
//        [--camera-backed] [--documentary]
//   place           one of the shipped plates (see `places()`); default louisiana
//   output          default map_flyover.plan.json
//   font-asset      default assets/fonts/Inter-Bold.ttf, passed to Chronon
//   frames          default 180

#include "chrononmotion/geospatial/MapFlyover.hpp"
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::geospatial;
using namespace chrononmotion::motion;

namespace {

    constexpr int kWidth = 1920;
    constexpr int kHeight = 1080;
    constexpr float kFps = 30.f;
    constexpr int kFrames = 180;

    /// A raster basemap plus the geography it was rendered for.
    struct MapPlace {
        std::string name;
        /// Chronon-owned plate. Resolved relative to the working directory, the
        /// same way every other asset path in this tree is.
        std::string basemap;
        /// Optional DEM-derived relief for *the same window* as `basemap`,
        /// produced by `Chronon3d/tools/cartography/map_cartography.py relief`
        /// alongside it. Empty means the place is drawn without relief.
        std::string relief;
        double centerLat{0.0};
        double centerLon{0.0};
        int zoom{8};
        GeoCoord poi{};
        std::string title;
        std::string subtitle;
        std::vector<GeoCoord> route;
    };

    /// Documentary content for a place, used only with `--documentary`. The
    /// assets are Chronon-owned; a place that ships none authors none, so the
    /// flag is inert for every place but the ones that have them.
    struct DocumentaryAssets {
        std::string area;
        std::string leader;
        std::string reticle;
        std::string route;
        std::string vignette;
        std::string dim;
        float dimOpacity{0.f};
        GeoCoord routeFrom{};
        GeoCoord routeTo{};
        bool pinPulse{false};
    };

    DocumentaryAssets documentaryFor(const std::string& place) {

        DocumentaryAssets assets;
        assets.vignette = "assets/maps/doc_vignette.png";
        if (place == "alps") {
            assets.area = "assets/maps/doc_area_matterhorn.png";
            assets.leader = "assets/maps/doc_leader.png";
            assets.reticle = "assets/maps/doc_reticle.png";
            // A single placed POI is a spotlight: the whole plate steps back a
            // step so the zone, the reticle and the pin are what the eye lands on.
            assets.dim = "assets/maps/doc_dim.png";
            assets.dimOpacity = 0.18f;
            assets.pinPulse = true;
        } else if (place == "italy_usa") {
            assets.area = "assets/maps/doc_atlantic_dual.png";
            // One soft glow sprite, drawn many times along the great circle: the
            // route is a chain of these rather than a single stretched strip.
            assets.route = "assets/maps/doc_glow_dot.png";
            assets.routeFrom = GeoCoord{41.9028, 12.4964, 0.0}; // Rome
            assets.routeTo = GeoCoord{40.7128, -74.0060, 0.0};  // New York
            assets.reticle = "assets/maps/doc_reticle.png";
            assets.pinPulse = true;
        }
        return assets;
    }

    /// The plate size a place is rendered for, when it is not the canvas size.
    /// A place whose camera pans across its plate needs a plate wider than the
    /// window, or the pan would expose the canvas edges.
    Vector2 windowSizeFor(const std::string& place) {

        if (place == "italy_usa") return Vector2{3200.f, 1080.f};
        return Vector2{static_cast<float>(kWidth), static_cast<float>(kHeight)};
    }

    /// The shipped plates. Each one is a real crop: the basemap assets were
    /// rendered with exactly these centres and zooms, so the georeferencing below
    /// is a fact about the file rather than a plausible guess.
    std::vector<MapPlace> places() {

        return {
            MapPlace{
                "louisiana",
                "assets/maps/test1_basemap.png",
                "",
                29.86, -92.70, 8,
                GeoCoord{29.9511, -90.0715, 0.0},
                "NEW ORLEANS",
                "Louisiana, USA \u00b7 29.9511\u00b0 N, 90.0715\u00b0 W",
                {
                    GeoCoord{29.7604, -95.3698, 0.0}, // Houston
                    GeoCoord{30.2266, -93.2174, 0.0}, // Lake Charles
                    GeoCoord{30.2241, -92.0198, 0.0}, // Lafayette
                    GeoCoord{30.4515, -91.1871, 0.0}, // Baton Rouge
                    GeoCoord{29.9511, -90.0715, 0.0}, // New Orleans
                },
            },
            MapPlace{
                "cleveland",
                "assets/maps/test2_basemap.png",
                "",
                41.4925, -81.6660, 14,
                GeoCoord{41.4993, -81.6944, 0.0},
                "CLEVELAND",
                "Ohio, USA \u00b7 41.4993\u00b0 N, 81.6944\u00b0 W",
                {
                    GeoCoord{41.5051, -81.6124, 0.0}, // University Circle
                    GeoCoord{41.4993, -81.6944, 0.0}, // Public Square
                    GeoCoord{41.4824, -81.6697, 0.0}, // Broadway
                },
            },
            MapPlace{
                "atlanta",
                "assets/maps/test3_micro_sat.png",
                "",
                33.9598, -83.3768, 16,
                GeoCoord{33.9598, -83.3768, 0.0},
                "ATHENS, GA",
                "Georgia, USA \u00b7 33.9598\u00b0 N, 83.3768\u00b0 W",
                {
                    GeoCoord{33.9598, -83.3768, 0.0},
                },
            },
            MapPlace{
                "chicago",
                "assets/maps/test4_basemap.png",
                "",
                41.8300, -87.6200, 12,
                GeoCoord{41.8826, -87.6233, 0.0},
                "CHICAGO",
                "Illinois, USA \u00b7 41.8826\u00b0 N, 87.6233\u00b0 W",
                {
                    GeoCoord{41.8781, -87.6298, 0.0}, // The Loop
                    GeoCoord{41.8917, -87.6077, 0.0}, // Navy Pier
                    GeoCoord{41.8826, -87.6233, 0.0}, // Millennium Park
                },
            },
            // A place whose relief is the point of the plate: 2.9 km of
            // elevation across one window, from the Zermatt valley floor to the
            // Matterhorn summit. The basemap and the relief were rendered for
            // this exact window by Chronon3d/tools/cartography/map_cartography.py,
            // which is what makes the ridge lines on the drape sit on the ridge
            // lines in the imagery.
            MapPlace{
                "alps",
                // The graded copy of the plate: the same DEM-baked terrain, with
                // its colour and highlights quietened so the overlays drawn on it
                // read. Produced by `make_documentary_assets.py`.
                "assets/maps/alps_basemap_doc.png",
                // No drape layer: this plate was rendered shaded (map_cartography
                // `basemap --relief`), so the terrain is already in its pixels,
                // with the plate's own contrast intact. `relief` is for a plate
                // that is left unshaded and draped at render time instead.
                "",
                45.9763, 7.6586, 13,
                GeoCoord{45.9763, 7.6586, 4478.0},
                "MATTERHORN",
                "Pennine Alps, Switzerland \u00b7 45.9763\u00b0 N, 7.6586\u00b0 E",
                {
                    GeoCoord{45.9360, 7.6300, 2006.0}, // Breuil-Cervinia
                    GeoCoord{45.9430, 7.7050, 3295.0}, // Theodulpass
                    GeoCoord{45.9763, 7.6586, 4478.0}, // Matterhorn
                    GeoCoord{45.9834, 7.7847, 3089.0}, // Gornergrat
                    GeoCoord{46.0207, 7.7491, 1620.0}, // Zermatt
                },
            },
            // Two continents in one shot: the plate is a wide North-Atlantic
            // window, the route runs Rome -> New York, and the flyover travels
            // from one to the other. Both ends are documented by the same
            // georeferencing as every other marker.
            MapPlace{
                "italy_usa",
                "assets/maps/italy_usa_basemap.png",
                "",
                41.0, -30.0, 4,
                GeoCoord{40.7128, -74.0060, 0.0},
                "NEW YORK",
                "USA \u00b7 40.7128\u00b0 N, 74.0060\u00b0 W",
                {
                    GeoCoord{41.9028, 12.4964, 0.0},  // Rome
                    GeoCoord{40.7128, -74.0060, 0.0}, // New York
                },
            },
        };
    }

    /// The content a raster layer draws: the asset the motion layer was keyed
    /// with, and nothing else. The lowering fills the box from the ContentRef the
    /// scene was authored with, so a size stated twice could only disagree.
    chronon3d::render_plan::LayerPlan imagePlan(const std::string& asset) {

        chronon3d::render_plan::LayerPlan plan;
        plan.type = chronon3d::render_plan::LayerType::Image;
        plan.asset = asset;
        return plan;
    }

    /// The content a text layer draws: the words, and the concrete paint Chronon
    /// needs. ChrononMotion supplied the box and the moment, never the look.
    chronon3d::render_plan::LayerPlan textPlan(const std::string& words, const std::string& font,
                                               float fontSize, const std::string& fill) {

        chronon3d::render_plan::LayerPlan plan;
        plan.type = chronon3d::render_plan::LayerType::Text;
        plan.text = words;

        chronon3d::render_plan::LayerStylePlan style;
        style.font = font;
        style.font_size = fontSize;
        style.fill = fill;
        plan.style = style;
        return plan;
    }

    bool fileExists(const std::string& path) {

        if (std::ifstream file(path, std::ios::binary); file.good()) return true;
        return false;
    }

    const MapPlace* findPlace(const std::vector<MapPlace>& all, const std::string& name) {

        for (const auto& place : all)
            if (place.name == name) return &place;
        return nullptr;
    }

    void printUsage(const std::vector<MapPlace>& all) {

        std::cerr << "usage: chrononmotion_map_flyover [place] [output.plan.json] [font] [frames]"
                 " [--camera-backed] [--documentary]\n";
        std::cerr << "places:";
        for (const auto& place : all) std::cerr << ' ' << place.name;
        std::cerr << '\n';
    }

}// namespace

int main(int argc, char** argv) {

    const std::vector<MapPlace> all = places();
    const std::string placeName = argc > 1 ? argv[1] : all.front().name;
    const MapPlace* place = findPlace(all, placeName);
    if (place == nullptr) {
        std::cerr << "map_flyover: unknown place '" << placeName << "'\n";
        printUsage(all);
        return 2;
    }

    const std::string outputPath = argc > 2 ? argv[2] : "map_flyover.plan.json";
    const std::string font = argc > 3 ? argv[3] : "assets/fonts/Inter-Bold.ttf";
    const int frames = argc > 4 ? std::atoi(argv[4]) : kFrames;
    bool documentary = false;
    bool cameraBacked = false;
    for (int i = 1; i < argc; ++i)
        if (std::string(argv[i]) == "--documentary") documentary = true;
        else if (std::string(argv[i]) == "--camera-backed") cameraBacked = true;
    if (frames <= 1) {
        std::cerr << "map_flyover: frames must be greater than one\n";
        return 2;
    }

    if (!fileExists(place->basemap)) {
        std::cerr << "map_flyover: the basemap plate is missing: " << place->basemap
                  << "\n  the plate is Chronon-owned content; render it with"
                     " Chronon3d/tools/cartography/"
                     "map_cartography.py for this window first\n";
        return 1;
    }

    try {
        MapFlyoverSpec spec;
        spec.origin = place->poi;
        spec.poi = place->poi;
        spec.poiTitle = place->title;
        spec.poiSubtitle = place->subtitle;
        spec.route = place->route;
        spec.durationSeconds = static_cast<float>(frames) / kFps;
        spec.fps = kFps;
        spec.canvasSize = Vector2{static_cast<float>(kWidth), static_cast<float>(kHeight)};
        spec.basemapAsset = place->basemap;
        spec.reliefAsset = place->relief;
        // The window is the *place's* crop, not the POI's: a plate was rendered
        // around one centre, and declaring any other centre would georeference
        // every marker against a window the pixels never came from. The POI is
        // where the callout points, and it only has to be inside the window.
        spec.window = MapWindow::centeredOn(
            GeoCoord{place->centerLat, place->centerLon, 0.0}, place->zoom,
            windowSizeFor(place->name));
        spec.cameraBackedPlate = cameraBacked;
        // These heights are metres for the mesh-terrain camera. The optional
        // flat-plate path below interprets its overridden depth in plate units.
        spec.startAltitudeM = 2600.f;
        spec.endAltitudeM = 900.f;
        spec.orbitRadiusM = 1800.f;
        if (cameraBacked) {
            // Let perspective own the push-in and keep all plate-unit layers
            // stationary. This pins the camera's target to the POI throughout.
            spec.startMapScale = 1.f;
            spec.endMapScale = 1.f;
            spec.startMapPan = Vector2{0.f, 0.f};
            spec.endMapPan = Vector2{0.f, 0.f};
            spec.startAltitudeM = 1800.f;
            spec.endAltitudeM = 1100.f;
            spec.startFovDeg = 55.f;
            spec.endFovDeg = 44.f;
        }

        // The documentary punctuation, only when the caller asks for it. A plain
        // run authors exactly the flyover it always did.
        if (documentary) {
            const DocumentaryAssets assets = documentaryFor(place->name);
            spec.areaOverlayAsset = assets.area;
            spec.leaderLineAsset = assets.leader;
            spec.crosshairAsset = assets.reticle;
            spec.vignetteAsset = assets.vignette;
            spec.dimAsset = assets.dim;
            spec.dimOpacity = assets.dimOpacity;
            spec.enablePinPulse = assets.pinPulse;

            // A route between two places is drawn from the first to the second,
            // and the camera travels with it: the flyover pans from centring the
            // start towards centring the end, both as plane points of this
            // window. The pan stops short of the destination on purpose. A
            // window is 1920px wide and these two places sit ~984px apart on the
            // plate, so a full centred pan would push the origin clean off the
            // frame; ending at 76% of the travel keeps *both* ends of the line —
            // both highlighted glow discs — inside the shot when the line lands.
            if (!assets.route.empty()) {
                const Vector2 planeFrom = spec.window.toPlane(assets.routeFrom);
                const Vector2 planeTo = spec.window.toPlane(assets.routeTo);
                spec.docRouteAsset = assets.route;
                spec.docRouteFrom = assets.routeFrom;
                spec.docRouteTo = assets.routeTo;
                spec.startMapScale = 1.0f;
                spec.endMapScale = 1.0f;
                // The shot opens on the origin, led in from the right by a
                // sliver of the route: the destination's glow disc is present
                // from the first frame (both ends are the subject), and starting
                // it just outside the west edge lets it *slide* in as the camera
                // travels rather than popping in clipped against the frame.
                const float routeSpanX = std::fabs(planeTo.x - planeFrom.x);
                const float leadInX = routeSpanX * 0.14f;
                spec.startMapPan = Vector2{-planeFrom.x - leadInX, -planeFrom.y};
                // Travel most of the way to the destination, then stop: the last
                // quarter is left unwound so the origin stays inside the frame
                // and both highlighted ends read in the same shot. A window is
                // 1920px wide and these two places sit ~984px apart on the plate,
                // so a full centred pan would push the origin clean off it.
                const Vector2 centredOnEnd{-planeTo.x, -planeTo.y};
                constexpr float kArrivalFraction = 0.76f;
                spec.endMapPan = Vector2{
                    spec.startMapPan.x + (centredOnEnd.x - spec.startMapPan.x) * kArrivalFraction,
                    spec.startMapPan.y + (centredOnEnd.y - spec.startMapPan.y) * kArrivalFraction};
                // Both ends are the subject of the shot, so the glow discs are
                // present from the first frame instead of fading up mid-flight;
                // the camera opening on Italy is what shows Italy highlighted,
                // and the westward pan is what brings the second disc in.
                spec.areaRevealStartFrame = 0;
                spec.areaRevealEndFrame = 0;
                // The line draws across most of the clip and then holds lit, so
                // the arrival is the shot's climax rather than a blip.
                spec.docRouteStartFrame = 6;
                spec.docRouteEndFrame = 120;
            }
        }

        MotionScene scene{"map_flyover_" + place->name, kFps};
        const MapFlyoverBuilder builder(spec);
        const MapSceneLayers ids = builder.buildScene(scene);
        if (cameraBacked)
            builder.configureCameraBackedPlate(scene, 0.f, 34.f);
        CameraRig cameraRig{1, spec.startFovDeg,
                            static_cast<float>(kWidth) / static_cast<float>(kHeight), 1.f, 10000.f};
        if (cameraBacked)
            builder.rigCameraForPlate(cameraRig, spec.startAltitudeM,
                                      spec.endAltitudeM);

        // Hand every layer its content. This is the only moment the two halves
        // are visible in one place: the scene decided *where and when*, and each
        // plan below says *what* is drawn there.
        std::unordered_map<std::string, chronon3d::render_plan::LayerPlan> content;
        auto key = [](Layer::Id id) { return std::to_string(id); };

        if (ids.hasBasemap())
            content.emplace(key(ids.basemap), imagePlan(place->basemap));
        if (ids.relief != Layer::kRoot) {
            // Composed with plain alpha on purpose. The relief plate is already a
            // warm/cool two-sided drape whose tints sit either side of the
            // plate's mid tone, so it needs no blend mode — and a soft-light
            // layer currently kills a Vulkan render outright ("DEVICE LOST
            // REPORT phase=wait_for_pending"), which is the backend this plan is
            // rendered on. The emitter can write a blend mode if a backend ever
            // renders one safely.
            content.emplace(key(ids.relief), imagePlan(place->relief));
        }
        if (ids.route_overlay != Layer::kRoot)
            content.emplace(key(ids.route_overlay), imagePlan(spec.routeOverlayAsset));
        if (ids.ground_grid != Layer::kRoot)
            content.emplace(key(ids.ground_grid), imagePlan(spec.groundGridAsset));
        for (const Layer::Id id : ids.markers)
            content.emplace(key(id), textPlan(spec.markerText, font, spec.markerFontSize, "#F59E0B"));
        if (ids.pin != Layer::kRoot)
            content.emplace(key(ids.pin), textPlan(spec.pinText, font, spec.pinFontSize, "#EF4444"));
        if (ids.area != Layer::kRoot)
            content.emplace(key(ids.area), imagePlan(spec.areaOverlayAsset));
        if (ids.vignette != Layer::kRoot)
            content.emplace(key(ids.vignette), imagePlan(spec.vignetteAsset));
        if (ids.dim != Layer::kRoot)
            content.emplace(key(ids.dim), imagePlan(spec.dimAsset));
        if (ids.leader_line != Layer::kRoot)
            content.emplace(key(ids.leader_line), imagePlan(spec.leaderLineAsset));
        if (ids.crosshair != Layer::kRoot)
            content.emplace(key(ids.crosshair), imagePlan(spec.crosshairAsset));
        // The route is one sprite drawn many times along the arc, plus the
        // brighter head that rides the reveal front. They share one asset; the
        // scene decided how many and where.
        for (const Layer::Id id : ids.doc_route_sprites)
            content.emplace(key(id), imagePlan(spec.docRouteAsset));
        if (ids.doc_route_head != Layer::kRoot)
            content.emplace(key(ids.doc_route_head), imagePlan(spec.docRouteAsset));
        if (ids.title != Layer::kRoot)
            content.emplace(key(ids.title), textPlan(spec.poiTitle, font, spec.titleFontSize, "#FFFFFF"));
        if (ids.subtitle != Layer::kRoot)
            content.emplace(key(ids.subtitle), textPlan(spec.poiSubtitle, font, spec.subtitleFontSize, "#38BDF8"));

        RenderPlanLoweringOptions options;
        options.width = kWidth;
        options.height = kHeight;
        options.jobId = "map_flyover_" + place->name;
        options.outputPath = outputPath;
        options.contentLayers = content;
        if (cameraBacked) options.cameraRig = &cameraRig;

        chronon3d::render_plan::RenderPlan plan = lowerToRenderPlan(scene, options);
        plan.output.format = chronon3d::render_plan::OutputFormat::Mp4;

        const std::string problem = renderPlanEmissionProblem(plan);
        if (!problem.empty()) {
            std::cerr << "map_flyover: the plan is not expressible: " << problem << "\n";
            return 1;
        }

        const std::string json = renderPlanToJson(plan);
        std::ofstream file(outputPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            std::cerr << "map_flyover: cannot write " << outputPath << "\n";
            return 1;
        }
        file << json << '\n';
        file.close();

        // The readout is the point of the tool: it states, independently of the
        // plan, the canvas pixel each authored coordinate must land on. A plan
        // that renders the basemap somewhere else has to disagree with these
        // numbers, which makes the georeferencing checkable instead of asserted.
        const std::size_t lastFrame = static_cast<std::size_t>(
            std::max<std::int64_t>(0, plan.canvas.duration.integral() - 1));
        std::cout << "map_flyover: " << outputPath << " \u2014 " << place->name << ", "
                  << plan.layers.size() << " layers, " << plan.canvas.duration.integral()
                  << " frames at " << plan.canvas.fps.numerator << "/" << plan.canvas.fps.denominator
                  << " fps, " << plan.canvas.width << "x" << plan.canvas.height << ", "
                  << json.size() << " bytes\n";
        std::cout << "  plate: " << place->basemap << " \u2014 window zoom " << spec.window.zoom
                  << ", crop origin (" << spec.window.topLeftPixel.x << ", "
                  << spec.window.topLeftPixel.y << ") global px\n";
        // The POI has to be inside the plate it points at, or the callout would
        // name a place the map is not showing.
        const Vector2 poiOnPlate = spec.window.toRaster(place->poi);
        if (poiOnPlate.x < 0.f || poiOnPlate.x > spec.canvasSize.x ||
            poiOnPlate.y < 0.f || poiOnPlate.y > spec.canvasSize.y) {
            std::cerr << "map_flyover: the POI is outside the plate's window at raster ("
                      << poiOnPlate.x << ", " << poiOnPlate.y << ")\n";
            return 1;
        }
        for (const std::size_t frame : {std::size_t{0}, lastFrame}) {
            const float progress = plan.canvas.duration.integral() > 0
                ? static_cast<float>(frame) / static_cast<float>(plan.canvas.duration.integral()) : 0.f;
            const Vector2 scalePan = builder.mapPanAt(progress);
            const float scale = builder.mapScaleAt(progress);
            const Vector2 poiPlane = spec.window.toPlane(place->poi);
            const Vector2 firstPlane = spec.window.toPlane(place->route.front());
            const Vector2 poi = builder.mapPointToCanvas(place->poi, progress);
            const Vector2 first = builder.mapPointToCanvas(place->route.front(), progress);
            std::cout << "  frame " << frame << ": scale " << scale << ", pan ("
                      << scalePan.x << ", " << scalePan.y << ")\n"
                      << "    POI      plane (" << poiPlane.x << ", " << poiPlane.y
                      << ") \u2192 canvas (" << poi.x << ", " << poi.y << ")\n"
                      << "    route[0] plane (" << firstPlane.x << ", " << firstPlane.y
                      << ") \u2192 canvas (" << first.x << ", " << first.y << ")\n";
        }
        std::cout << "  marker box " << spec.markerFontSize * 2.f << "x" << spec.markerFontSize * 1.6f
                  << " px, at unit scale: symbols do not zoom with the map\n";
        // Every waypoint, on the pixel the flyover puts it on. This is the
        // georeferencing in full: the ratios between these x gaps are the map
        // scale, so a render can be checked against them without knowing where
        // a glyph puts its ink inside its own box.
        const std::size_t probeFrame = lastFrame / 2;
        const float probeProgress = plan.canvas.duration.integral() > 0
            ? static_cast<float>(probeFrame) / static_cast<float>(plan.canvas.duration.integral()) : 0.f;
        std::cout << "  frame " << probeFrame << " waypoints:\n";
        for (std::size_t i = 0; i < spec.route.size(); ++i) {
            const Vector2 canvas = builder.mapPointToCanvas(spec.route[i], probeProgress);
            std::cout << "    route[" << i << "] " << spec.route[i].latitudeDeg << ", "
                      << spec.route[i].longitudeDeg
                      << " \u2192 canvas (" << canvas.x << ", " << canvas.y << ")\n";
        }
        std::cout << "  POI raster pixel (" << poiOnPlate.x << ", " << poiOnPlate.y << ")\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "map_flyover: " << error.what() << "\n";
        return 1;
    }
}
