// ChrononMotion — Cinematic Geospatial Map Flyover Engine.
//
// Converts geodetic waypoints (WGS84) into smooth local scene coordinates,
// constructs cinematic camera flight paths (orbit, descend, POI tracking),
// animates drop pins with spring physics, and lowers to Chronon3D RenderPlan.
//
// The stage this engine builds is a *raster* map: a basemap image with a known
// Web Mercator window, a route revealed over it, a POI pin and a callout. Where
// each of those sits is not a taste decision — `MapWindow::toPlane` puts every
// marker on the pixel the raster painted for that coordinate, so the geography
// cannot drift from the picture.
//
// Chronon owns content. This side owns space and time: it never fetches a tile,
// rasterises a route or invents a font. It is handed the basemap and overlay
// asset paths and it places them.

#ifndef CHRONONMOTION_GEOSPATIAL_MAPFLYOVER_HPP
#define CHRONONMOTION_GEOSPATIAL_MAPFLYOVER_HPP

#include "chrononmotion/geospatial/Geodetic.hpp"
#include "chrononmotion/geospatial/WebMercator.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/Presets.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace chrononmotion::geospatial {

    // The course motion shared by every glow chain (a route, a territory
    // contour): a brightness wave that travels the chain, so a shape keeps
    // moving after it has been drawn instead of dying on the map, plus the
    // breathing of the head that draws it. The wave rides the opacity the chain
    // already keys, so an animated course costs no extra keyframes.
    inline constexpr float kGlowChainTwoPi = 6.283185307179586f;
    inline constexpr float kGlowChainFlowFloor = 0.74f;
    inline constexpr float kGlowChainFlowSeconds = 1.6f;
    inline constexpr float kGlowChainFlowCycles = 2.f;
    inline constexpr float kGlowHeadPulseDepth = 0.12f;
    inline constexpr float kGlowHeadPulseSeconds = 0.9f;

    // The motion-side vocabulary this file authors in. Named rather than
    // reached through a namespace-wide `using namespace`, so what MapFlyover
    // borrows from the engine is readable in one place.
    using motion::ContentRef;
    using motion::Layer;
    using motion::MotionScene;

    /// A waypoint along a geographic path or flight trajectory.
    struct MapWaypoint {
        GeoCoord coord;
        float timeSeconds{0.0f};
        std::string label{};
    };

    /// Specification for generating a cinematic map flyover scene.
    struct MapFlyoverSpec {
        /// Geodetic origin / anchor for the local ENU tangent plane.
        GeoCoord origin{41.890210, 12.492231, 50.0}; ///< Default: Rome Colosseum

        /// Point of interest (POI) to frame and track with the camera.
        GeoCoord poi{41.890210, 12.492231, 50.0};
        std::string poiTitle{"COLOSSEO"};
        std::string poiSubtitle{"Roma, Italia • 41.8902° N, 12.4922° E"};

        /// Optional list of GPS route waypoints.
        std::vector<GeoCoord> route{};

        /// Duration of the cinematic flyover in seconds.
        float durationSeconds{4.0f};
        float fps{60.0f};

        // ------------------------------------------------------------------
        // The stage. A map is only renderable when a content owner supplies the
        // bytes; every empty asset below simply drops that layer rather than
        // faking one.
        // ------------------------------------------------------------------

        /// The raster basemap to draw. Required for a map layer at all.
        std::string basemapAsset{};

        /// The geography `basemapAsset` covers; this is what georeferences every
        /// marker. Compose it with `MapWindow::centeredOn`.
        MapWindow window{};

        /// The output canvas the map is placed on.
        Vector2 canvasSize{1920.f, 1080.f};

        /// Render the raster plate and its annotations as one perspective plane.
        /// Positions use the camera's y-up basis while this mode is enabled.
        bool cameraBackedPlate{false};

        /// Optional terrain relief to drape on the basemap: a plate derived from a
        /// DEM (SRTM / Copernicus) for *the same window* as `basemapAsset`, so it
        /// lands on the imagery pixel for pixel. It is not scaled, tinted or
        /// rotated here — a relief plate that disagreed with the window its
        /// basemap covers would misplace every ridge on the map, so the two are
        /// produced together by the same content tool and consumed as a pair.
        std::string reliefAsset{};

        /// Optional raster of the route line revealed over the basemap, and the
        /// frame window it reveals across. Without it the route is still legible
        /// as its waypoint markers.
        std::string routeOverlayAsset{};
        /// The route appears by *fading the overlay in* across these frames, one
        /// marker at a time. It is not a wipe: a wipe is a mask, and the plan
        /// emitter refuses masks rather than silently dropping one.
        int routeFadeStartFrame{10};
        int routeFadeEndFrame{70};

        /// Optional graticule / ground-grid raster. Only used when
        /// `enableGroundGrid` is set.
        std::string groundGridAsset{};

        /// The flyover itself: a push-in and drift of the whole map plane.
        /// Scale is applied about the canvas centre, pan is in scene units.
        /// Every map-space layer shares this one transform, which is what keeps
        /// a marker welded to its geography.
        float startMapScale{1.0f};
        float endMapScale{1.22f};
        Vector2 startMapPan{0.f, 0.f};
        Vector2 endMapPan{0.f, 0.f};

        /// Camera flight parameters (used by `rigCamera`, and only attached to a
        /// RenderPlan when the caller asks for a camera-backed flyover).
        float startAltitudeM{650.0f};   ///< Starting camera height in meters
        float endAltitudeM{220.0f};     ///< Ending camera height in meters
        float orbitRadiusM{450.0f};     ///< Distance from POI
        float startOrbitDeg{0.0f};      ///< Initial orbit angle
        float endOrbitDeg{110.0f};      ///< Final orbit angle
        float startFovDeg{55.0f};       ///< Start field of view
        float endFovDeg{42.0f};         ///< End field of view (slight telephoto zoom)

        // ------------------------------------------------------------------
        // The words, and the boxes they are drawn in.
        //
        // These are content *identity* plus geometry, in the same sense as the
        // words a text layer is keyed by elsewhere in this tree: the motion side
        // says which text sits where and how large its box is. What that text
        // looks like — font, fill, outline, glow — belongs to the content owner
        // and is deliberately absent here, because a motion engine that starts
        // naming typefaces has started to become a second content engine.
        // ------------------------------------------------------------------
        std::string markerText{"●"};      ///< waypoint marker
        std::string pinText{"●"};         ///< glyph present in the bundled fallback fonts
        float markerFontSize{46.f};
        float pinFontSize{104.f};
        float titleFontSize{58.f};
        float subtitleFontSize{26.f};
        float calloutTitleYFraction{0.778f};    ///< of canvas height
        float calloutSubtitleYFraction{0.847f}; ///< of canvas height

        /// Visual styling
        bool enablePinBounce{true};
        bool enableGroundGrid{false};
        bool enableCalloutCard{true};

        // ------------------------------------------------------------------
        // Documentary punctuation. Every one of these is off by default, so a
        // plain flyover authors exactly the layers it always did, and a place
        // opts in only to the ones whose content an owner actually supplies.
        // ------------------------------------------------------------------

        /// A radar ping on the landed POI pin: once the drop has settled the pin
        /// pulses where it landed, so a target reads as tracked rather than
        /// merely placed. It pulses the pin already authored — no new content.
        bool enablePinPulse{false};
        int pinPulseStartFrame{72};
        int pinPulseCount{3};
        float pinPulseScale{1.6f};

        /// A filled region drawn on the map plane and revealed by fading in, the
        /// same way the route overlay is uncovered. The region's shape (and its
        /// glow) is the asset's business; this side only decides when it appears.
        std::string areaOverlayAsset{};
        int areaRevealStartFrame{20};
        int areaRevealEndFrame{60};

        /// A screen-space vignette: transparent centre, darkening towards the
        /// edges, drawn over the imagery and under every symbol. A vignette is a
        /// property of the *frame*, not of the plate — baked into a plate that is
        /// panned it would slide across the screen with the map.
        std::string vignetteAsset{};

        /// A dim drawn over the map and under the documentary punctuation, so a
        /// territory spotlight has something to step back from. Authored as its
        /// own screen-space raster rather than as a mask: the plan contract has no
        /// masks, and a full-frame translucent plate is what a mask would have
        /// been used to build here anyway.
        std::string dimAsset{};
        float dimOpacity{0.34f};
        int dimStartFrame{8};
        int dimEndFrame{30};

        /// A territory highlight: a polygon whose contour is walked with the same
        /// glow chain a route uses (clockwise, by the order of its own points),
        /// filled at a fraction of its own alpha. The fill is the content owner's
        /// raster — a polygon in the plate's own window — so this side only decides
        /// *when* it lights and how bright it ends up.
        std::vector<GeoCoord> territory{};
        std::string territoryFillAsset{};
        std::string territoryContourAsset{};
        float territorySpriteSize{84.f};
        int territorySprites{64};
        int territoryStartFrame{16};
        int territoryEndFrame{86};
        float territoryFillOpacity{0.20f};

        /// How close to the frame edge a callout is allowed to be drawn. The
        /// callout is centred, so this is a hard guarantee rather than a nudge:
        /// a title can never be clipped by the frame however long it is.
        float calloutSafeMargin{72.f};

        /// A connecting route between two places, drawn as a chain of soft glow
        /// sprites walked along the great circle between them (`greatCirclePoint`)
        /// and revealed in sequence, so the line arrives curve-first and soft-edged
        /// rather than appearing as a straight, hard bar.
        ///
        /// The ends are *geographic*: the arc between two places is the great
        /// circle, which is not the straight line any projection makes of it. The
        /// chain is the price of the plan contract, which carries only images and
        /// symbols — a curve has to be paid for in sprites, and the glow of each
        /// one is what lets a chain read as a single soft line.
        GeoCoord docRouteFrom{};
        GeoCoord docRouteTo{};
        std::string docRouteAsset{};      ///< one soft glow sprite, drawn many times
        float docRouteSpriteSize{96.f};   ///< side of one glow sprite in scene units
        int docRouteSprites{36};          ///< sprites along the arc (min 2)
        float docRouteHeadScale{1.9f};    ///< how much brighter the leading glow is
        int docRouteStartFrame{6};
        int docRouteEndFrame{120};

        /// A leader line from the target to the callout card, drawn on along its
        /// own width rather than popped in.
        std::string leaderLineAsset{};
        Vector2 leaderLineSize{520.f, 6.f};
        float leaderLineYFraction{0.72f};
        int leaderLineFrame{10};

        /// A reticle that closes on the target as the callout lands. The mark is
        /// an asset: which ring a production draws is content, not a size.
        std::string crosshairAsset{};
        float crosshairSize{132.f};
        float crosshairStartScale{2.1f};
        int crosshairFrame{16};

        /// The camera. The slam is a dive curve on the descent; the drift keeps a
        /// slow orbit running after the target is framed.
        bool enableSlamZoom{false};
        bool enableCameraDrift{false};
        float cameraDriftDegPerSec{2.5f};

        /// At most this many route waypoints are drawn as markers, so a hostile
        /// or accidental 10k-point GPX file cannot turn into a 10k-layer plan.
        static constexpr std::size_t kMaximumRouteMarkers = 64;
    };

    /// Evaluates analytic spring-damper physics: f(t) = 1 - e^(-zeta * omega * t) * cos(omega_d * t)
    [[nodiscard]] inline float evaluateSpringBounce(float t, float duration = 1.2f, float frequency = 12.0f, float damping = 0.55f) noexcept {
        if (t <= 0.0f) return 0.0f;
        if (t >= duration) return 1.0f;
        const float omega = frequency;
        const float zeta = damping;
        const float omegaD = omega * std::sqrt(1.0f - zeta * zeta);
        const float envelope = std::exp(-zeta * omega * t);
        return 1.0f - envelope * (std::cos(omegaD * t) + (zeta * omega / omegaD) * std::sin(omegaD * t));
    }

    /// The layers a built map scene owns, by id. A caller needs these to supply
    /// the matching content-owning RenderPlan layers, and a test needs them to
    /// assert the scene is the one that was asked for.
    struct MapSceneLayers {
        Layer::Id basemap{Layer::kRoot};
        Layer::Id relief{Layer::kRoot};
        Layer::Id route_overlay{Layer::kRoot};
        Layer::Id ground_grid{Layer::kRoot};
        Layer::Id pin{Layer::kRoot};
        Layer::Id title{Layer::kRoot};
        Layer::Id subtitle{Layer::kRoot};
        Layer::Id area{Layer::kRoot};
        Layer::Id vignette{Layer::kRoot};
        Layer::Id dim{Layer::kRoot};
        Layer::Id territory_fill{Layer::kRoot};
        std::vector<Layer::Id> territory_contour{};
        std::vector<Layer::Id> doc_route_sprites{};
        Layer::Id doc_route_head{Layer::kRoot};
        Layer::Id leader_line{Layer::kRoot};
        Layer::Id crosshair{Layer::kRoot};
        std::vector<Layer::Id> markers{};

        [[nodiscard]] bool hasBasemap() const { return basemap != Layer::kRoot; }
        [[nodiscard]] std::size_t count() const {
            std::size_t total = markers.size() + doc_route_sprites.size() + territory_contour.size();
            for (const Layer::Id id : {basemap, relief, route_overlay, ground_grid, pin, title, subtitle,
                                       area, vignette, dim, territory_fill, doc_route_head,
                                       leader_line, crosshair})
                if (id != Layer::kRoot) ++total;
            return total;
        }
    };

    /// Builds a cinematic map flyover rig and layers in ChrononMotion.
    class MapFlyoverBuilder {
    public:
        explicit MapFlyoverBuilder(MapFlyoverSpec spec) : spec_(std::move(spec)) {}

        [[nodiscard]] const MapFlyoverSpec& spec() const noexcept { return spec_; }

        /// The clip length the scene is authored over.
        [[nodiscard]] int frameCount() const noexcept {
            const float fps = spec_.fps > 0.f ? spec_.fps : 30.f;
            const float frames = std::max(2.f, std::round(spec_.durationSeconds * fps));
            return static_cast<int>(frames);
        }

        /// Converts all route waypoints to local 3D scene coordinates.
        [[nodiscard]] std::vector<Vector3> routeScenePoints() const {
            std::vector<Vector3> points;
            points.reserve(spec_.route.size());
            for (const auto& wp : spec_.route) {
                points.push_back(toSceneSpace(wp, spec_.origin));
            }
            return points;
        }

        /// Gets the local 3D position of the POI.
        [[nodiscard]] Vector3 poiScenePosition() const noexcept {
            return toSceneSpace(spec_.poi, spec_.origin);
        }

        /// Where a point of the plane the raster is drawn on lands on the
        /// canvas at clip progress `p`, in canvas pixels (top-left origin, +y
        /// down). The plane's own origin is the raster's centre.
        ///
        /// This is the georeferencing authority: the raster's centre is painted
        /// at the canvas centre offset by the flyover pan, the plane is scaled
        /// about that centre, and every marker is placed through this one
        /// function. A marker and the pixel it belongs to cannot drift apart,
        /// because there is only one function that decides where either lands.
        [[nodiscard]] Vector2 planePointToCanvas(const Vector2& planePoint, float progress) const noexcept {
            const float scale = mapScaleAt(progress);
            const Vector2 pan = mapPanAt(progress);
            return Vector2{spec_.canvasSize.x * 0.5f + planePoint.x * scale + pan.x,
                           spec_.canvasSize.y * 0.5f - planePoint.y * scale + pan.y};
        }

        /// Where a geographic point lands on the canvas at clip progress `p`.
        [[nodiscard]] Vector2 mapPointToCanvas(const GeoCoord& point, float progress) const noexcept {
            return planePointToCanvas(spec_.window.toPlane(point), progress);
        }

        /// A canvas point, as the position ChrononMotion authors.
        ///
        /// ChrononMotion's layer space has its origin at the **canvas centre**,
        /// while the canvas itself is measured from its top-left corner. The two
        /// halves of the RenderPlan bridge cancel that difference for text
        /// (`planPosition` adds the centre, `apply_animation_tracks` subtracts it)
        /// and leave it standing for images, so this single subtraction is what
        /// makes an authored position mean the same thing for a raster and for a
        /// text layer: the canvas pixel the layer's own centre lands on.
        [[nodiscard]] Vector3 authoredPosition(const Vector2& canvasPoint, float z) const noexcept {
            return Vector3{canvasPoint.x - spec_.canvasSize.x * 0.5f,
                           spec_.cameraBackedPlate
                               ? spec_.canvasSize.y * 0.5f - canvasPoint.y
                               : canvasPoint.y - spec_.canvasSize.y * 0.5f,
                           z};
        }

        /// Scale of the map plane at normalized progress in [0,1].
        [[nodiscard]] float mapScaleAt(float progress) const noexcept {
            const float eased = motion::Easing::easeInOut()(std::clamp(progress, 0.f, 1.f));
            return spec_.startMapScale + (spec_.endMapScale - spec_.startMapScale) * eased;
        }

        /// Pan of the map plane at normalized progress in [0,1], in scene units.
        [[nodiscard]] Vector2 mapPanAt(float progress) const noexcept {
            const float eased = motion::Easing::easeInOut()(std::clamp(progress, 0.f, 1.f));
            return Vector2{spec_.startMapPan.x + (spec_.endMapPan.x - spec_.startMapPan.x) * eased,
                           spec_.startMapPan.y + (spec_.endMapPan.y - spec_.startMapPan.y) * eased};
        }

        /// Author the full map scene into `scene`.
        ///
        /// What is authored, and why each layer exists:
        ///
        ///   basemap       the raster, carrying the flyover push-in itself
        ///   route_overlay the revealed route line, if a content owner drew one
        ///   ground_grid   the optional graticule
        ///   markers       one ● per route waypoint, welded to its geography
        ///   pin           the POI drop pin, with the spring bounce landing it
        ///   title/subtitle the callout, in screen space so it does not drift
        ///
        /// Map-space layers are keyed densely rather than sparsely: each one is
        /// placed by `mapPointToCanvas`, which is not linear in time (the scale
        /// and the pan ease independently), so interpolating two authored
        /// endpoints would not equal the transform at the frames between them.
        /// Dense keys make the authored curve *exactly* the georeferencing on
        /// every frame, which is the only version of this that can be verified.
        ///
        /// The hierarchy is flat on purpose. Every layer is placed in canvas
        /// space by the same function, so parenting a marker under the basemap
        /// would apply the flyover transform twice: once by the parent it
        /// inherits and once by the position it was authored with.
        [[nodiscard]] MapSceneLayers buildScene(MotionScene& scene) const {
            MapSceneLayers ids;
            const float fps = spec_.fps > 0.f ? spec_.fps : 30.f;
            const int frames = frameCount();
            const float secondsPerFrame = 1.f / fps;
            auto timeOf = [secondsPerFrame](int frame) { return static_cast<float>(frame) * secondsPerFrame; };
            auto progressOf = [frames](int frame) {
                return frames > 0 ? static_cast<float>(frame) / static_cast<float>(frames) : 0.f;
            };

            Layer::Id nextId = 1;

            // ── The map plane: the raster, and the flyover it carries ───────
            if (!spec_.basemapAsset.empty()) {
                ids.basemap = nextId++;
                Layer& basemap = scene.addContent(
                    ids.basemap, "map_basemap",
                    ContentRef::image(spec_.basemapAsset, spec_.window.rasterSize));
                basemap.flat();
                const Vector2 planeOrigin{0.f, 0.f};
                for (int frame = 0; frame <= frames; ++frame) {
                    const float progress = progressOf(frame);
                    const float scale = mapScaleAt(progress);
                    basemap.tracks.position.add(
                        timeOf(frame),
                        authoredPosition(planePointToCanvas(planeOrigin, progress), 0.f),
                        motion::Easing::linear());
                    basemap.tracks.scale.add(timeOf(frame), Vector3{scale, scale, 1.f},
                                             motion::Easing::linear());
                }
            }

            // ── Optional rasters drawn in the map plane ─────────────────────
            //
            // A raster is map content, so it takes the whole flyover: the same
            // position curve and the same scale as the basemap it was drawn
            // against. The symbols below take the same position curve but keep
            // unit scale, so a marker stays the size it was authored at while
            // the geography around it zooms.
            auto addMapRaster = [&](const std::string& asset, const char* name) -> Layer::Id {
                if (asset.empty()) return Layer::kRoot;
                const Layer::Id id = nextId++;
                Layer& layer = scene.addContent(id, name,
                    ContentRef::image(asset, spec_.window.rasterSize));
                layer.flat();
                const Vector2 planeOrigin{0.f, 0.f};
                for (int frame = 0; frame <= frames; ++frame) {
                    const float progress = progressOf(frame);
                    const float scale = mapScaleAt(progress);
                    layer.tracks.position.add(
                        timeOf(frame),
                        authoredPosition(planePointToCanvas(planeOrigin, progress), 0.f),
                        motion::Easing::linear());
                    layer.tracks.scale.add(timeOf(frame), Vector3{scale, scale, 1.f},
                                           motion::Easing::linear());
                }
                return id;
            };

            // The relief drapes the plate it was derived from, so it is the first
            // thing above the basemap and below every symbol and overlay.
            ids.relief = addMapRaster(spec_.reliefAsset, "map_terrain_relief");

            ids.route_overlay = addMapRaster(spec_.routeOverlayAsset, "map_route_overlay");

            // The route reveal is an opacity ramp on the overlay, not a new
            // geometry: the line is drawn once and uncovered.
            if (ids.route_overlay != Layer::kRoot && frames > 1) {
                Layer* overlay = scene.findLayer(ids.route_overlay);
                if (overlay) {
                    const float revealStart = static_cast<float>(
                        std::clamp(spec_.routeFadeStartFrame, 0, frames));
                    const float revealEnd = static_cast<float>(
                        std::clamp(spec_.routeFadeEndFrame, spec_.routeFadeStartFrame + 1, frames));
                    overlay->tracks.opacity.add(0.f, 0.f, motion::Easing::linear());
                    overlay->tracks.opacity.add(revealStart * secondsPerFrame, 0.f, motion::Easing::linear());
                    overlay->tracks.opacity.add(revealEnd * secondsPerFrame, 1.f, motion::Easing::linear());
                    overlay->tracks.opacity.add(timeOf(frames), 1.f, motion::Easing::linear());
                }
            }

            if (spec_.enableGroundGrid) {
                ids.ground_grid = addMapRaster(spec_.groundGridAsset, "map_ground_grid");
            }

            // ── The dim, and the territory it leaves lit ─────────────────────
            // A territory highlight is a spotlight, and a spotlight needs a
            // surround to fall back into: the dim is a full-frame translucent
            // plate over the imagery that fades in as the contour starts to draw.
            // A plate rather than a mask, because the plan contract has no masks —
            // and a plate is what the mask would have been used to build anyway.
            //
            // It is authored *before* the area and the territory, deliberately:
            // the whole point of the dim is that the zone being documented stays
            // at full strength while everything around it steps back.
            if (!spec_.dimAsset.empty()) {
                ids.dim = nextId++;
                Layer& dim = scene.addContent(ids.dim, "map_dim",
                                              ContentRef::image(spec_.dimAsset, spec_.canvasSize));
                dim.flat();
                dim.transform.position = authoredPosition(
                    Vector2{spec_.canvasSize.x * 0.5f, spec_.canvasSize.y * 0.5f}, 0.f);
                const int dimStart = std::clamp(spec_.dimStartFrame, 0, frames);
                const int dimEnd = std::clamp(spec_.dimEndFrame, dimStart + 1, frames);
                for (int frame = 0; frame <= frames; ++frame) {
                    const float reveal = std::clamp(
                        static_cast<float>(frame - dimStart) /
                            static_cast<float>(dimEnd - dimStart),
                        0.f, 1.f);
                    dim.tracks.opacity.add(timeOf(frame), reveal * spec_.dimOpacity,
                                           motion::Easing::linear());
                }
            }

            // The area overlay is a map raster that fades in across its own
            // window: it takes the whole flyover (so its outline stays welded to
            // the geography), and its reveal is an opacity ramp rather than a
            // second transform, because the plane's scale channel is already the
            // flyover's to own.
            if (!spec_.areaOverlayAsset.empty()) {
                ids.area = nextId++;
                Layer& area = scene.addContent(
                    ids.area, "map_area_overlay",
                    ContentRef::image(spec_.areaOverlayAsset, spec_.window.rasterSize));
                area.flat();
                const Vector2 planeOrigin{0.f, 0.f};
                const int revealStart = std::clamp(spec_.areaRevealStartFrame, 0, frames);
                const int revealEnd = std::clamp(spec_.areaRevealEndFrame, revealStart, frames);
                for (int frame = 0; frame <= frames; ++frame) {
                    const float progress = progressOf(frame);
                    const float scale = mapScaleAt(progress);
                    area.tracks.position.add(
                        timeOf(frame),
                        authoredPosition(planePointToCanvas(planeOrigin, progress), 0.f),
                        motion::Easing::linear());
                    area.tracks.scale.add(timeOf(frame), Vector3{scale, scale, 1.f},
                                          motion::Easing::linear());
                    const float reveal = revealEnd > revealStart
                        ? std::clamp(static_cast<float>(frame - revealStart) /
                                         static_cast<float>(revealEnd - revealStart),
                                     0.f, 1.f)
                        : 1.f;
                    area.tracks.opacity.add(timeOf(frame), reveal, motion::Easing::linear());
                }
            }

            // ── The vignette, in screen space ────────────────────────────────
            // Authored after the map rasters, so it darkens the imagery, and
            // before every symbol, so the callout, the reticle and the route stay
            // crisp. It is placed once and never moves: a vignette is a property
            // of the frame, not of the plate it sits over.
            if (!spec_.vignetteAsset.empty()) {
                ids.vignette = nextId++;
                Layer& vignette = scene.addContent(
                    ids.vignette, "map_vignette",
                    ContentRef::image(spec_.vignetteAsset, spec_.canvasSize));
                vignette.flat();
                vignette.transform.position = authoredPosition(
                    Vector2{spec_.canvasSize.x * 0.5f, spec_.canvasSize.y * 0.5f}, 0.f);
                vignette.tracks.opacity.add(0.f, 1.f, motion::Easing::linear());
                vignette.tracks.opacity.add(timeOf(frames), 1.f, motion::Easing::linear());
            }

            // The territory itself: a fill on the map plane that rises to a
            // fraction of its own alpha, and a contour walked clockwise by the
            // same glow chain a route uses. The zone is lit, the surround has
            // already stepped back, and nothing here invents a shape: the polygon
            // is geographic and the fill is the content owner's raster.
            if (spec_.territory.size() >= 3 && !spec_.territoryContourAsset.empty()) {
                const int contourStart = std::clamp(spec_.territoryStartFrame, 0, frames);
                const int contourEnd = std::clamp(spec_.territoryEndFrame, contourStart + 1, frames);
                if (!spec_.territoryFillAsset.empty()) {
                    ids.territory_fill = nextId++;
                    Layer& fill = scene.addContent(
                        ids.territory_fill, "map_territory_fill",
                        ContentRef::image(spec_.territoryFillAsset, spec_.window.rasterSize));
                    fill.flat();
                    const Vector2 planeOrigin{0.f, 0.f};
                    for (int frame = 0; frame <= frames; ++frame) {
                        const float progress = progressOf(frame);
                        const float scale = mapScaleAt(progress);
                        fill.tracks.position.add(
                            timeOf(frame),
                            authoredPosition(planePointToCanvas(planeOrigin, progress), 0.f),
                            motion::Easing::linear());
                        fill.tracks.scale.add(timeOf(frame), Vector3{scale, scale, 1.f},
                                              motion::Easing::linear());
                        const float reveal = std::clamp(
                            static_cast<float>(frame - contourStart) /
                                static_cast<float>(contourEnd - contourStart),
                            0.f, 1.f);
                        fill.tracks.opacity.add(timeOf(frame), reveal * spec_.territoryFillOpacity,
                                                motion::Easing::linear());
                    }
                }
                std::vector<Vector2> ring;
                ring.reserve(spec_.territory.size());
                for (const GeoCoord& vertex : spec_.territory)
                    ring.push_back(spec_.window.toPlane(vertex));
                ids.territory_contour = authorGlowChain(
                    scene, nextId, ring, true, spec_.territoryContourAsset,
                    spec_.territorySpriteSize, spec_.territorySprites, 0.f, contourStart,
                    contourEnd, frames).sprites;
            }

            // The connecting route: the great circle between the two places,
            // sampled into a plane-space polyline and walked by the same glow
            // chain a contour uses. The arc draws itself from one end to the other
            // with a bright head at the front — and because a glow has no edge,
            // the line it makes is soft rather than a bar laid across the plate.
            if (!spec_.docRouteAsset.empty()) {
                constexpr int kArcSamples = 128;
                std::vector<Vector2> arc;
                arc.reserve(kArcSamples + 1);
                for (int i = 0; i <= kArcSamples; ++i) {
                    const double t = static_cast<double>(i) / static_cast<double>(kArcSamples);
                    arc.push_back(spec_.window.toPlane(
                        geospatial::greatCirclePoint(spec_.docRouteFrom, spec_.docRouteTo, t)));
                }
                const int revealStart = std::clamp(spec_.docRouteStartFrame, 0, frames);
                const int revealEnd = std::clamp(spec_.docRouteEndFrame, revealStart + 1, frames);
                GlowChainResult chain = authorGlowChain(
                    scene, nextId, arc, false, spec_.docRouteAsset, spec_.docRouteSpriteSize,
                    spec_.docRouteSprites, spec_.docRouteHeadScale, revealStart, revealEnd, frames);
                ids.doc_route_sprites = std::move(chain.sprites);
                ids.doc_route_head = chain.head;
            }

            // ── Waypoint markers, welded to their geography ─────────────────
            const std::size_t markerCount = std::min(spec_.route.size(), MapFlyoverSpec::kMaximumRouteMarkers);
            ids.markers.reserve(markerCount);
            for (std::size_t i = 0; i < markerCount; ++i) {
                const Layer::Id id = nextId++;
                ids.markers.push_back(id);
                Layer& marker = scene.addContent(
                    id, "map_route_marker_" + std::to_string(i),
                    ContentRef::text(spec_.markerText,
                                     Vector2{spec_.markerFontSize * 2.f, spec_.markerFontSize * 1.6f}));
                // Symbols live on the same canvas plane as the raster they
                // annotate, so they stay flat and are stacked by plan layer
                // order instead of by depth.
                marker.flat();
                // Markers arrive with the route: each one fades up just ahead of
                // the reveal front so the line lights up its own waypoints.
                const float startFrame = markerCount > 1
                    ? revealFraction(i, markerCount, frames) : 0.f;
                for (int frame = 0; frame <= frames; ++frame) {
                    const float time = timeOf(frame);
                    marker.tracks.position.add(
                        time,
                        authoredPosition(mapPointToCanvas(spec_.route[i], progressOf(frame)), 0.f),
                        motion::Easing::linear());
                    const float age = time - startFrame * secondsPerFrame;
                    const float appear = std::clamp(age / (6.f * secondsPerFrame), 0.f, 1.f);
                    marker.tracks.opacity.add(time, appear, motion::Easing::linear());
                }
            }

            // ── The POI drop pin ────────────────────────────────────────────
            if (spec_.enablePinBounce || !spec_.poiTitle.empty()) {
                ids.pin = nextId++;
                Layer& pin = scene.addContent(
                    ids.pin, "map_poi_pin",
                    ContentRef::text(spec_.pinText,
                                     Vector2{spec_.pinFontSize * 2.f, spec_.pinFontSize * 2.f}));
                pin.flat();
                constexpr float kDropHeight = 260.f;
                constexpr float kDropSeconds = 1.2f;
                for (int frame = 0; frame <= frames; ++frame) {
                    const float time = timeOf(frame);
                    // The bounce lands the pin *on* the POI: the spring only ever
                    // offsets it above the point, never beside it, so the pin's
                    // horizontal identity is the geography's and nothing else.
                    const float bounce = spec_.enablePinBounce
                        ? evaluateSpringBounce(time, kDropSeconds, 15.0f, 0.52f) : 1.f;
                    const Vector2 poiCanvas
                        = mapPointToCanvas(spec_.poi, progressOf(frame));
                    const Vector2 dropped{poiCanvas.x, poiCanvas.y - kDropHeight * (1.f - bounce)};
                    pin.tracks.position.add(time, authoredPosition(dropped, 0.f),
                                            motion::Easing::linear());
                    pin.tracks.opacity.add(time, time > 0.f ? 1.f : 0.f, motion::Easing::linear());
                }

                // The ping: once the drop has settled, the pin keeps pulsing in
                // place. It is keyed as one ring repeated by the channel's own
                // loop window (not a list of pulses), so the number of pulses
                // never changes the shape of a single ping. Only the scale is
                // keyed — the pin's opacity belongs to its drop.
                if (spec_.enablePinPulse && frames > 1) {
                    const int start = std::clamp(spec_.pinPulseStartFrame, 0, frames - 1);
                    const int span = std::max(1, frames - start);
                    const int count = std::max(spec_.pinPulseCount, 1);
                    const float ringSeconds = secondsPerFrame * static_cast<float>(span)
                                              / static_cast<float>(count);
                    auto& scale = pin.tracks.scale;
                    scale.add(timeOf(start), Vector3{1.f, 1.f, 1.f}, motion::Easing::circOut());
                    scale.add(timeOf(start) + ringSeconds,
                              Vector3{spec_.pinPulseScale, spec_.pinPulseScale, 1.f},
                              motion::Easing::linear());
                    scale.loop(timeOf(start), timeOf(start) + ringSeconds);
                }
            }

            // ── The reticle, welded to the POI ────────────────────────────────
            // Dense keys, like every other symbol: the POI travels across the
            // canvas as the plane zooms, so a two-key lock would drift off it.
            // Only the closing is eased, and it overshoots so the reticle snaps
            // onto the point rather than merely shrinking near it.
            if (!spec_.crosshairAsset.empty()) {
                ids.crosshair = nextId++;
                Layer& reticle = scene.addContent(
                    ids.crosshair, "map_poi_reticle",
                    ContentRef::image(spec_.crosshairAsset,
                                      Vector2{spec_.crosshairSize, spec_.crosshairSize}));
                reticle.flat();
                const float closingStart = static_cast<float>(std::clamp(spec_.crosshairFrame, 0, frames));
                const float closingSpan = std::max(1.f, 18.f);
                for (int frame = 0; frame <= frames; ++frame) {
                    const float time = timeOf(frame);
                    reticle.tracks.position.add(
                        time,
                        authoredPosition(mapPointToCanvas(spec_.poi, progressOf(frame)), 0.f),
                        motion::Easing::linear());
                    const float closing = std::clamp((static_cast<float>(frame) - closingStart) / closingSpan,
                                                     0.f, 1.f);
                    const float eased = motion::Easing::overshoot()(closing);
                    const float scale = spec_.crosshairStartScale +
                                        (1.f - spec_.crosshairStartScale) * eased;
                    reticle.tracks.scale.add(time, Vector3{scale, scale, 1.f}, motion::Easing::linear());
                    reticle.tracks.opacity.add(time, closing, motion::Easing::linear());
                }
            }

            // ── The callout, in screen space ────────────────────────────────
            if (spec_.enableCalloutCard) {
                const int titleFrame = std::min(12, std::max(1, frames - 2));
                const int subtitleFrame = std::min(22, std::max(2, frames - 2));
                // The callout is centred inside a safe margin, and its box is the
                // frame minus that margin: however long the words are, the frame
                // can never clip them, which is the one failure a lower third has
                // no excuse for.
                const float safeMargin = std::clamp(spec_.calloutSafeMargin, 0.f,
                                                    std::max(0.f, spec_.canvasSize.x * 0.5f - 8.f));
                const float calloutWidth = std::max(16.f, spec_.canvasSize.x - 2.f * safeMargin);
                const float calloutX = spec_.canvasSize.x * 0.5f;

                ids.title = nextId++;
                Layer& title = scene.addContent(
                    ids.title, "map_callout_title",
                    ContentRef::text(spec_.poiTitle,
                                     Vector2{calloutWidth, spec_.titleFontSize * 1.6f}));
                // The callout is placed once and then animated *around* that
                // placement: the slide both starts from and lands on the rest
                // pose, so the title never drifts off the line it was authored
                // on no matter how the preset shapes its entrance.
                title.flat();
                title.transform.position = authoredPosition(
                    Vector2{calloutX, spec_.canvasSize.y * spec_.calloutTitleYFraction}, 0.f);
                motion::presets::fadeIn(title, titleFrame, 18, fps);
                motion::presets::fadeSlide(title, motion::presets::Direction::Up,
                                           titleFrame, 18, 26.f, fps);

                ids.subtitle = nextId++;
                Layer& subtitle = scene.addContent(
                    ids.subtitle, "map_callout_subtitle",
                    ContentRef::text(spec_.poiSubtitle,
                                     Vector2{calloutWidth, spec_.subtitleFontSize * 1.6f}));
                subtitle.flat();
                subtitle.transform.position = authoredPosition(
                    Vector2{calloutX, spec_.canvasSize.y * spec_.calloutSubtitleYFraction}, 0.f);
                motion::presets::fadeIn(subtitle, subtitleFrame, 18, fps);
                motion::presets::fadeSlide(subtitle, motion::presets::Direction::Up,
                                           subtitleFrame, 18, 18.f, fps);
            }

            // ── The leader line, in screen space ──────────────────────────────
            // It carries the callout's placement and then draws itself along its
            // own width, so the card is what the line is pointing at rather than
            // a rule that switched on beside it.
            if (!spec_.leaderLineAsset.empty()) {
                ids.leader_line = nextId++;
                Layer& line = scene.addContent(
                    ids.leader_line, "map_leader_line",
                    ContentRef::image(spec_.leaderLineAsset, spec_.leaderLineSize));
                line.flat();
                line.transform.position = authoredPosition(
                    Vector2{spec_.canvasSize.x * 0.5f,
                            spec_.canvasSize.y * spec_.leaderLineYFraction}, 0.f);
                motion::presets::lineDraw(line, spec_.leaderLineFrame, 16, fps);
            }

            if (spec_.cameraBackedPlate) {
                for (Layer& layer : scene.layers()) layer.is3D = true;
            }
            return ids;
        }

        /// Configures the CameraRig with the cinematic descent and orbit.
        void rigCamera(motion::CameraRig& rig) const {
            constexpr double kDegToRad = std::numbers::pi / 180.0;
            const Vector3 target = poiScenePosition();
            rig.setTarget(target);

            const int totalFrames = frameCount();
            auto& posTrack = rig.positionTrack();
            auto& fovTrack = rig.fovTrack();
            posTrack.clear();
            fovTrack.clear();

            for (int f = 0; f <= totalFrames; ++f) {
                const float tSec = static_cast<float>(f) / (spec_.fps > 0.f ? spec_.fps : 30.f);
                const float progress = static_cast<float>(f) / static_cast<float>(totalFrames);

                // The dive curve when the place asks for a slam — little travel
                // at first, the bulk through the middle, a long soft landing —
                // and the symmetric ease otherwise.
                const float eased = spec_.enableSlamZoom
                    ? motion::Easing::cubicBezier(0.72f, 0.f, 0.2f, 1.f)(progress)
                    : motion::Easing::easeInOut()(progress);

                // Interpolate orbit angle and altitude. The drift keeps a slow
                // orbit running underneath the authored swing, so the camera is
                // never completely frozen once the target is framed.
                const float driftDeg = spec_.enableCameraDrift
                    ? spec_.cameraDriftDegPerSec * tSec : 0.f;
                const float currentAngleDeg = spec_.startOrbitDeg + (spec_.endOrbitDeg - spec_.startOrbitDeg) * eased
                                              + driftDeg;
                const float currentAlt = spec_.startAltitudeM + (spec_.endAltitudeM - spec_.startAltitudeM) * eased;
                const float currentRadius = spec_.orbitRadiusM * (1.15f - 0.15f * eased);

                const float angleRad = static_cast<float>(currentAngleDeg * kDegToRad);
                const float camX = target.x + currentRadius * std::sin(angleRad);
                const float camY = target.y + currentAlt;
                const float camZ = target.z + currentRadius * std::cos(angleRad);

                posTrack.add(tSec, Vector3{camX, camY, camZ});

                // Interpolate FOV for cinematic compression
                const float fov = spec_.startFovDeg + (spec_.endFovDeg - spec_.startFovDeg) * eased;
                fovTrack.add(tSec, fov);
            }
        }

        /// Rig a perspective dolly over a plate-normalized surface. The map
        /// controller places the requested POI at (0,0,0); the renderer's
        /// canonical front-facing camera looks along +Z from negative Z.
        void rigCameraForPlate(motion::CameraRig& rig, float startDepth,
                               float endDepth) const {
            if (!spec_.cameraBackedPlate)
                throw std::invalid_argument("rigCameraForPlate requires cameraBackedPlate=true");
            if (!(startDepth > 0.f) || !(endDepth > 0.f) ||
                !std::isfinite(startDepth) || !std::isfinite(endDepth))
                throw std::invalid_argument("rigCameraForPlate requires finite positive depths");

            rig.clearTarget().setPosition(Vector3{0.f, 0.f, -startDepth}).setFov(spec_.startFovDeg);
            auto& position = rig.positionTrack();
            auto& fov = rig.fovTrack();
            position.clear();
            fov.clear();

            const int frames = frameCount();
            const float fps = spec_.fps > 0.f ? spec_.fps : 30.f;
            for (int frame = 0; frame <= frames; ++frame) {
                const float progress = static_cast<float>(frame) / static_cast<float>(frames);
                const float eased = motion::Easing::easeInOut()(progress);
                const float depth = startDepth + (endDepth - startDepth) * eased;
                position.add(static_cast<float>(frame) / fps, Vector3{0.f, 0.f, -depth});
                fov.add(static_cast<float>(frame) / fps,
                    spec_.startFovDeg + (spec_.endFovDeg - spec_.startFovDeg) * eased);
            }
        }

        /// Move the complete georeferenced plate under a shared tilt controller
        /// and translate the requested POI to the surface origin. This preserves
        /// exact registration while the plate rotates as one object.
        void configureCameraBackedPlate(MotionScene& scene, float startTiltDeg,
                                        float endTiltDeg) const {
            if (!spec_.cameraBackedPlate)
                throw std::invalid_argument("configureCameraBackedPlate requires cameraBackedPlate=true");
            if (!std::isfinite(startTiltDeg) || !std::isfinite(endTiltDeg) ||
                std::abs(startTiltDeg) > 80.f || std::abs(endTiltDeg) > 80.f)
                throw std::invalid_argument("map plate tilt must be finite and within +/-80 degrees");

            const Vector3 poi = authoredPosition(mapPointToCanvas(spec_.poi, 0.f), 0.f);
            const Layer::Id controllerId = scene.allocateLayerId();
            Layer& controller = scene.addNull(controllerId, "georeferenced_map_surface");
            const float fps = spec_.fps > 0.f ? spec_.fps : 30.f;
            const int frames = frameCount();
            const float radiansPerDegree = std::numbers::pi_v<float> / 180.f;
            for (int frame = 0; frame <= frames; ++frame) {
                const float progress = static_cast<float>(frame) / static_cast<float>(frames);
                const float eased = motion::Easing::easeInOut()(progress);
                const float tilt = startTiltDeg + (endTiltDeg - startTiltDeg) * eased;
                controller.tracks.rotation.add(static_cast<float>(frame) / fps,
                    Vector3{tilt * radiansPerDegree, 0.f, 0.f}, motion::Easing::linear());
            }

            for (Layer& layer : scene.layers()) {
                if (layer.id == controllerId) continue;
                layer.parentId = controllerId;
                layer.transform.position.x -= poi.x;
                layer.transform.position.y -= poi.y;
                for (auto& key : layer.tracks.position.keys()) {
                    key.value.x -= poi.x;
                    key.value.y -= poi.y;
                }
            }
        }

    private:
        /// What a glow chain authored: the sprite ids, and the id of the head.
        struct GlowChainResult {
            std::vector<Layer::Id> sprites{};
            Layer::Id head{Layer::kRoot};
        };

        /// Walk `points` at even arc length and author one soft glow sprite per
        /// step, lit in sequence from the first point to the last.
        ///
        /// A route (an open great-circle arc) and a territory contour (a closed
        /// ring) are the same object walked differently, so both are built here.
        /// Two things make the chain read as one soft line rather than a row of
        /// dots: the sprites are placed by *arc length*, so they are evenly spaced
        /// on screen however the projection bends, and the sprite's own falloff is
        /// wider than the step, so neighbours composite. `headScale` above zero
        /// adds the brighter sprite that rides the reveal front.
        [[nodiscard]] GlowChainResult authorGlowChain(
            MotionScene& scene, Layer::Id& nextId, const std::vector<Vector2>& points, bool closed,
            const std::string& asset, float spriteSize, int spriteCount, float headScale,
            int revealStart, int revealEnd, int frames) const {

            GlowChainResult result;
            if (asset.empty() || points.size() < 2) return result;

            std::vector<Vector2> walk = points;
            if (closed) walk.push_back(points.front());
            std::vector<float> cumulative(walk.size(), 0.f);
            for (std::size_t i = 1; i < walk.size(); ++i) {
                const float dx = walk[i].x - walk[i - 1].x;
                const float dy = walk[i].y - walk[i - 1].y;
                cumulative[i] = cumulative[i - 1] + std::sqrt(dx * dx + dy * dy);
            }
            const float total = cumulative.back();
            if (total <= 1.f) return result;

            auto pointAt = [&](float target) {
                std::size_t i = 1;
                while (i < cumulative.size() && cumulative[i] < target) ++i;
                if (i >= cumulative.size()) return walk.back();
                const float segment = cumulative[i] - cumulative[i - 1];
                const float local = segment > 0.f ? (target - cumulative[i - 1]) / segment : 0.f;
                return Vector2{walk[i - 1].x + (walk[i].x - walk[i - 1].x) * local,
                               walk[i - 1].y + (walk[i].y - walk[i - 1].y) * local};
            };

            const float fps = spec_.fps > 0.f ? spec_.fps : 30.f;
            const float secondsPerFrame = 1.f / fps;
            auto timeOf = [secondsPerFrame](int frame) {
                return static_cast<float>(frame) * secondsPerFrame;
            };
            auto progressOf = [frames](int frame) {
                return frames > 0 ? static_cast<float>(frame) / static_cast<float>(frames) : 0.f;
            };

            const int count = std::max(2, spriteCount);
            const float span = static_cast<float>(std::max(1, revealEnd - revealStart));
            const Vector2 box{spriteSize, spriteSize};
            result.sprites.reserve(static_cast<std::size_t>(count));
            for (int k = 0; k < count; ++k) {
                const float along = static_cast<float>(k) / static_cast<float>(count - 1);
                const Vector2 point = pointAt(total * along);
                const Layer::Id id = nextId++;
                result.sprites.push_back(id);
                Layer& dot = scene.addContent(id, "map_glow_chain_" + std::to_string(k),
                                              ContentRef::image(asset, box));
                dot.flat();
                const float lightAt = static_cast<float>(revealStart) + span * along;
                for (int frame = 0; frame <= frames; ++frame) {
                    const float time = timeOf(frame);
                    dot.tracks.position.add(
                        time,
                        authoredPosition(planePointToCanvas(point, progressOf(frame)), 0.f),
                        motion::Easing::linear());
                    const float appear = std::clamp((static_cast<float>(frame) - lightAt) / 5.f,
                                                    0.f, 1.f);
                    const float flow = kGlowChainFlowFloor + (1.f - kGlowChainFlowFloor) *
                        (0.5f + 0.5f * std::cos(kGlowChainTwoPi *
                            (time / kGlowChainFlowSeconds - along * kGlowChainFlowCycles)));
                    dot.tracks.opacity.add(time, appear * flow, motion::Easing::linear());
                }
            }

            if (headScale > 0.f) {
                result.head = nextId++;
                Layer& head = scene.addContent(
                    result.head, "map_glow_chain_head",
                    ContentRef::image(asset, Vector2{box.x * headScale, box.y * headScale}));
                head.flat();
                for (int frame = 0; frame <= frames; ++frame) {
                    const float reveal = std::clamp(
                        (static_cast<float>(frame) - revealStart) / span, 0.f, 1.f);
                    const float eased = motion::Easing::easeInOut()(reveal);
                    const Vector2 point = pointAt(total * eased);
                    const float time = timeOf(frame);
                    head.tracks.position.add(
                        time,
                        authoredPosition(planePointToCanvas(point, progressOf(frame)), 0.f),
                        motion::Easing::linear());
                    // The head breathes while it travels: a perfect circle reads
                    // as a decal, a slowly pulsing one as a moving point of light.
                    // The pulse never reaches zero, so the scale stays invertible.
                    const float pulse = 1.f + kGlowHeadPulseDepth *
                        std::sin(kGlowChainTwoPi * time / kGlowHeadPulseSeconds);
                    head.tracks.scale.add(time, Vector3{pulse, pulse, 1.f},
                                          motion::Easing::linear());
                    const float fadeOut = std::clamp(
                        1.f - (static_cast<float>(frame) - revealEnd) / 8.f, 0.f, 1.f);
                    head.tracks.opacity.add(time, frame < revealStart ? 0.f : fadeOut,
                                            motion::Easing::linear());
                }
            }
            return result;
        }

        /// The fraction of the clip at which waypoint `index` lights up: the
        /// route arrives front-to-back across its own fade window.
        [[nodiscard]] float revealFraction(std::size_t index, std::size_t count, int frames) const {
            if (count <= 1) return 0.f;
            const float share = static_cast<float>(index) / static_cast<float>(count - 1);
            const float span = static_cast<float>(std::max(1, spec_.routeFadeEndFrame - spec_.routeFadeStartFrame));
            return static_cast<float>(spec_.routeFadeStartFrame) +
                   share * std::min(span, static_cast<float>(frames));
        }

        MapFlyoverSpec spec_;
    };

} // namespace chrononmotion::geospatial

#endif // CHRONONMOTION_GEOSPATIAL_MAPFLYOVER_HPP
