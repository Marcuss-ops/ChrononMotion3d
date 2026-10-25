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
    [[nodiscard]] float evaluateSpringBounce(float t, float duration = 1.2f,
                                             float frequency = 12.0f,
                                             float damping = 0.55f) noexcept;

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

        [[nodiscard]] bool hasBasemap() const noexcept;
        [[nodiscard]] std::size_t count() const noexcept;
    };

    /// Builds a cinematic map flyover rig and layers in ChrononMotion.
    class MapFlyoverBuilder {
    public:
        explicit MapFlyoverBuilder(MapFlyoverSpec spec);

        [[nodiscard]] const MapFlyoverSpec& spec() const noexcept { return spec_; }

        /// The clip length the scene is authored over.
        [[nodiscard]] int frameCount() const noexcept;

        /// Converts all route waypoints to local 3D scene coordinates.
        [[nodiscard]] std::vector<Vector3> routeScenePoints() const;

        /// Gets the local 3D position of the POI.
        [[nodiscard]] Vector3 poiScenePosition() const noexcept;

        /// Where a point of the plane the raster is drawn on lands on the
        /// canvas at clip progress `p`, in canvas pixels (top-left origin, +y
        /// down). The plane's own origin is the raster's centre.
        ///
        /// This is the georeferencing authority: the raster's centre is painted
        /// at the canvas centre offset by the flyover pan, the plane is scaled
        /// about that centre, and every marker is placed through this one
        /// function. A marker and the pixel it belongs to cannot drift apart,
        /// because there is only one function that decides where either lands.
        [[nodiscard]] Vector2 planePointToCanvas(const Vector2& planePoint, float progress) const noexcept;

        /// Where a geographic point lands on the canvas at clip progress `p`.
        [[nodiscard]] Vector2 mapPointToCanvas(const GeoCoord& point, float progress) const noexcept;

        /// A canvas point, as the position ChrononMotion authors.
        ///
        /// ChrononMotion's layer space has its origin at the **canvas centre**,
        /// while the canvas itself is measured from its top-left corner. The two
        /// halves of the RenderPlan bridge cancel that difference for text
        /// (`planPosition` adds the centre, `apply_animation_tracks` subtracts it)
        /// and leave it standing for images, so this single subtraction is what
        /// makes an authored position mean the same thing for a raster and for a
        /// text layer: the canvas pixel the layer's own centre lands on.
        [[nodiscard]] Vector3 authoredPosition(const Vector2& canvasPoint, float z) const noexcept;

        /// Scale of the map plane at normalized progress in [0,1].
        [[nodiscard]] float mapScaleAt(float progress) const noexcept;

        /// Pan of the map plane at normalized progress in [0,1], in scene units.
        [[nodiscard]] Vector2 mapPanAt(float progress) const noexcept;

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
        [[nodiscard]] MapSceneLayers buildScene(MotionScene& scene) const;

        /// Configures the CameraRig with the cinematic descent and orbit.
        void rigCamera(motion::CameraRig& rig) const;

        /// Rig a perspective dolly over a plate-normalized surface. The map
        /// controller places the requested POI at (0,0,0); the renderer's
        /// canonical front-facing camera looks along +Z from negative Z.
        void rigCameraForPlate(motion::CameraRig& rig, float startDepth,
                               float endDepth) const;

        /// Move the complete georeferenced plate under a shared tilt controller
        /// and translate the requested POI to the surface origin. This preserves
        /// exact registration while the plate rotates as one object.
        void configureCameraBackedPlate(MotionScene& scene, float startTiltDeg,
                                        float endTiltDeg) const;

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
            int revealStart, int revealEnd, int frames) const;

        /// The fraction of the clip at which waypoint `index` lights up: the
        /// route arrives front-to-back across its own fade window.
        [[nodiscard]] float revealFraction(std::size_t index, std::size_t count, int frames) const;

        MapFlyoverSpec spec_;
    };

} // namespace chrononmotion::geospatial

#endif // CHRONONMOTION_GEOSPATIAL_MAPFLYOVER_HPP
