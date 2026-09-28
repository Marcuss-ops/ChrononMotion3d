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
        std::string pinText{"📍"};        ///< drop pin
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
        Layer::Id route_overlay{Layer::kRoot};
        Layer::Id ground_grid{Layer::kRoot};
        Layer::Id pin{Layer::kRoot};
        Layer::Id title{Layer::kRoot};
        Layer::Id subtitle{Layer::kRoot};
        std::vector<Layer::Id> markers{};

        [[nodiscard]] bool hasBasemap() const { return basemap != Layer::kRoot; }
        [[nodiscard]] std::size_t count() const {
            std::size_t total = markers.size();
            for (const Layer::Id id : {basemap, route_overlay, ground_grid, pin, title, subtitle})
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
                           canvasPoint.y - spec_.canvasSize.y * 0.5f, z};
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
            }

            // ── The callout, in screen space ────────────────────────────────
            if (spec_.enableCalloutCard) {
                const int titleFrame = std::min(12, std::max(1, frames - 2));
                const int subtitleFrame = std::min(22, std::max(2, frames - 2));

                ids.title = nextId++;
                Layer& title = scene.addContent(
                    ids.title, "map_callout_title",
                    ContentRef::text(spec_.poiTitle,
                                     Vector2{spec_.canvasSize.x * 0.85f, spec_.titleFontSize * 1.6f}));
                // The callout is placed once and then animated *around* that
                // placement: the slide both starts from and lands on the rest
                // pose, so the title never drifts off the line it was authored
                // on no matter how the preset shapes its entrance.
                title.flat();
                title.transform.position = authoredPosition(
                    Vector2{spec_.canvasSize.x * 0.5f,
                            spec_.canvasSize.y * spec_.calloutTitleYFraction}, 0.f);
                motion::presets::fadeIn(title, titleFrame, 18, fps);
                motion::presets::fadeSlide(title, motion::presets::Direction::Up,
                                           titleFrame, 18, 26.f, fps);

                ids.subtitle = nextId++;
                Layer& subtitle = scene.addContent(
                    ids.subtitle, "map_callout_subtitle",
                    ContentRef::text(spec_.poiSubtitle,
                                     Vector2{spec_.canvasSize.x * 0.85f, spec_.subtitleFontSize * 1.6f}));
                subtitle.flat();
                subtitle.transform.position = authoredPosition(
                    Vector2{spec_.canvasSize.x * 0.5f,
                            spec_.canvasSize.y * spec_.calloutSubtitleYFraction}, 0.f);
                motion::presets::fadeIn(subtitle, subtitleFrame, 18, fps);
                motion::presets::fadeSlide(subtitle, motion::presets::Direction::Up,
                                           subtitleFrame, 18, 18.f, fps);
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

                // Smooth cubic ease-in-out curve for the camera motion
                const float eased = motion::Easing::easeInOut()(progress);

                // Interpolate orbit angle and altitude
                const float currentAngleDeg = spec_.startOrbitDeg + (spec_.endOrbitDeg - spec_.startOrbitDeg) * eased;
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

    private:
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
