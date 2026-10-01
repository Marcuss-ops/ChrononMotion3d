// ChrononMotion — geo_camera_v1: the map dive as an ordinary camera language.
//
// The dynamic tile pyramid dive (see DynamicPyramidMap.hpp) certified a
// primitive: a top-down multi-LOD descent from orbit to a landmark. This file
// turns that primitive into a *family* of shots. A GeoDiveSpec authors the
// geographic approach (Europe -> Italy -> Rome -> Colosseum), a GeoCameraRig
// samples the pose deterministically frame by frame, and the final pose
// converts into an ordinary motion::CameraShot so the rest of the Chronon
// camera language (transitions, focus, rigs) can take over seamlessly:
//
//     geo target -> MapCameraRig -> arrive at target -> CameraShot
//
// The rig is a pure function of the frame index. Same spec, same samples —
// which is what makes a render reproducible bit for bit and a camera path
// verifiable without rendering it.

#ifndef CHRONONMOTION_GEOSPATIAL_GEOCAMERARIG_HPP
#define CHRONONMOTION_GEOSPATIAL_GEOCAMERARIG_HPP

#include "chrononmotion/geospatial/DynamicPyramidMap.hpp"
#include "chrononmotion/geospatial/Geodetic.hpp"
#include "chrononmotion/motion/CameraMotion.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

namespace chrononmotion::geospatial {

    /// The shot family. Every preset is the same dive with a different landing,
    /// the way a lens family is one optical formula at different focal lengths.
    enum class GeoShotPreset {
        MapDiveToLandmark,   ///< pure dive, ends framed on the landmark (nadir)
        MapDiveToCity,       ///< dive that lands at city scale
        MapDiveToCountry,    ///< dive that lands at country scale
        MapDiveThenTilt,     ///< dive, then pitch up to a ground-plane horizon
        MapDiveThenOrbit,    ///< dive, tilt, then a yaw sweep around the anchor
        MapDivePinReveal,    ///< dive, then a drop pin fires at the anchor
        MapDiveLabelReveal,  ///< dive, then a label card fires at the anchor
        MapDiveMetricReveal, ///< dive, then a metric figure rises over the place
        MapDiveRouteContinue,///< dive, then hand off to a route walking onward
        MapDiveThenCraneOut, ///< dive, then crane back up for a closing wide
    };

    /// One sample of the rig: everything a renderer and an acceptance test need.
    struct GeoCameraKeyframe {
        Vector3 cameraPosition{0.f, 0.f, 0.f}; ///< ENU scene metres (x east, y up, z north)
        Vector3 lookTarget{0.f, 0.f, 0.f};     ///< the rig always aims here
        float fovDeg{60.f};
        float rollDeg{0.f};
        float groundRelativeAltitudeM{0.f};    ///< camera height above the anchor ground
        float continuousZoom{5.f};
        float pitchDeg{0.f};                   ///< 0 = nadir (straight down)
        float yawDeg{0.f};                     ///< orbit heading after tilt
        int frame{0};
    };

    /// Authoring description of a geo camera shot.
    struct GeoDiveSpec {
        GeoShotPreset preset{GeoShotPreset::MapDiveToLandmark};

        /// Where the dive lands. Default: the Colosseum, the certified canary.
        GeoCoord targetAnchor{41.890210, 12.492231, 50.0};
        /// The ENU tangent origin. Defaults to the anchor itself, so the target
        /// sits at the scene origin and every pose is anchor-relative.
        GeoCoord enuOrigin{41.890210, 12.492231, 50.0};

        float startZoom{5.2f};   ///< orbit: the Mediterranean basin
        float endZoom{17.5f};    ///< the arena fills the frame
        float fovDeg{60.f};
        float canvasWidth{1920.f};
        float canvasHeight{1080.f};
        int frameCount{150};
        float fps{30.f};

        /// Post-dive shaping, consumed per preset.
        float tiltDeg{35.f};        ///< MapDiveThenTilt / ThenOrbit landing pitch
        float orbitDeg{15.f};       ///< MapDiveThenOrbit yaw sweep
        float craneOutMeters{0.f};  ///< MapDiveThenCraneOut pull-back altitude
        float cranePitchDeg{25.f};  ///< its ground-plane pitch

        /// The pyramid the altitude law reads. Altitude is never authored by
        /// hand: it follows from the continuous zoom through the same
        /// texel-to-screen law the tile renderer samples with, so the camera
        /// and the ground cannot disagree about scale.
        DynamicPyramidSpec pyramid{};

        /// The frame fraction at which the reveal overlays fire (pin, label,
        /// metric, route hand-off). The rig does not draw them; it publishes
        /// when they belong so a renderer cannot guess differently.
        float revealAtFraction{0.86f};

        /// Applies the preset's landing shape. Every field it sets is one the
        /// doc comment above it promises; a pure dive leaves them untouched.
        void applyPreset() {
            pyramid.targetAnchor = targetAnchor;
            pyramid.canvasWidth = canvasWidth;
            pyramid.canvasHeight = canvasHeight;
            pyramid.fovDegrees = fovDeg;
            pyramid.minZoom = std::min(startZoom, endZoom);
            pyramid.maxZoom = std::max(startZoom, endZoom);

            switch (preset) {
                case GeoShotPreset::MapDiveToCity:
                    endZoom = 14.5f;
                    break;
                case GeoShotPreset::MapDiveToCountry:
                    endZoom = 9.5f;
                    break;
                case GeoShotPreset::MapDiveThenTilt:
                    tiltDeg = 35.f;
                    break;
                case GeoShotPreset::MapDiveThenOrbit:
                    tiltDeg = 35.f;
                    orbitDeg = 15.f;
                    break;
                case GeoShotPreset::MapDiveThenCraneOut:
                    craneOutMeters = 900.f;
                    cranePitchDeg = 25.f;
                    break;
                case GeoShotPreset::MapDivePinReveal:
                case GeoShotPreset::MapDiveLabelReveal:
                case GeoShotPreset::MapDiveMetricReveal:
                case GeoShotPreset::MapDiveRouteContinue:
                    revealAtFraction = 0.86f;
                    break;
                case GeoShotPreset::MapDiveToLandmark:
                default:
                    break;
            }
            pyramid.minZoom = std::min(startZoom, endZoom);
            pyramid.maxZoom = std::max(startZoom, endZoom);
        }

        /// The authored shot, ready to sample.
        [[nodiscard]] static GeoDiveSpec forPreset(GeoShotPreset preset) {
            GeoDiveSpec spec;
            spec.preset = preset;
            spec.applyPreset();
            return spec;
        }
    };

    /// C2-continuous smootherstep: zero velocity AND zero acceleration at both
    /// ends, bounded acceleration in between. This is the anti-"snap" law: a
    /// zoom level change can never be felt as an event because its first and
    /// second derivative vanish exactly where levels hand over.
    [[nodiscard]] inline float geoSmootherStep(float p) noexcept {
        const float t = std::clamp(p, 0.f, 1.f);
        return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
    }

    /// Phase boundaries of a shot, in frames. A phase ends where the next one
    /// begins; the last phase always runs to the final frame.
    struct GeoPhaseSchedule {
        int diveEnd{0};   ///< dive completes (target zoom reached)
        int tiltEnd{0};   ///< post-dive pitch completes (== diveEnd when untilted)
        int orbitEnd{0};  ///< yaw sweep completes (== tiltEnd when no orbit)
    };

    /// The rig. Pure, deterministic, and total: every frame of every preset
    /// samples from the same closed-form law, with no hidden state.
    class GeoCameraRig {
    public:
        explicit GeoCameraRig(GeoDiveSpec spec) : spec_(std::move(spec)) {
            spec_.applyPreset();
            schedule_ = computeSchedule();
            targetScene_ = toSceneSpace(spec_.targetAnchor, spec_.enuOrigin);
        }

        [[nodiscard]] const GeoDiveSpec& spec() const noexcept { return spec_; }
        [[nodiscard]] const GeoPhaseSchedule& schedule() const noexcept { return schedule_; }
        [[nodiscard]] Vector3 targetScenePoint() const noexcept { return targetScene_; }
        [[nodiscard]] int frameCount() const noexcept { return std::max(1, spec_.frameCount); }

        /// The frame the reveal overlays fire on (pin / label / metric / route).
        [[nodiscard]] int revealFrame() const noexcept {
            return static_cast<int>(std::round(
                static_cast<float>(frameCount()) * spec_.revealAtFraction));
        }

        /// Continuous zoom at a frame. The dive carries it; every later phase
        /// holds it (the crane is the one phase allowed to give zoom back).
        [[nodiscard]] float continuousZoomAt(int frame) const noexcept {
            const float f = static_cast<float>(frame);
            const float diveSpan = std::max(1.f, static_cast<float>(schedule_.diveEnd));
            if (f <= schedule_.diveEnd) {
                const float e = geoSmootherStep(f / diveSpan);
                return spec_.startZoom + (spec_.endZoom - spec_.startZoom) * e;
            }
            const bool crane = spec_.preset == GeoShotPreset::MapDiveThenCraneOut
                               && schedule_.tiltEnd > schedule_.diveEnd;
            if (crane) {
                const float span = std::max(1.f, static_cast<float>(
                    schedule_.tiltEnd - schedule_.diveEnd));
                const float e = geoSmootherStep((f - schedule_.diveEnd) / span);
                // Give back about a third of the dive's zoom: a crane reads as
                // a pull-back, not as a second dive in reverse.
                const float giveBack = (spec_.endZoom - spec_.startZoom) / 3.f;
                return spec_.endZoom - giveBack * e;
            }
            return spec_.endZoom;
        }

        /// Ground pitch at a frame: nadir through the dive, then the preset's
        /// landing tilt, held through the orbit.
        [[nodiscard]] float pitchDegAt(int frame) const noexcept {
            const float f = static_cast<float>(frame);
            const bool hasTilt = schedule_.tiltEnd > schedule_.diveEnd;
            if (!hasTilt || f <= schedule_.diveEnd) return 0.f;
            const float span = std::max(1.f, static_cast<float>(
                schedule_.tiltEnd - schedule_.diveEnd));
            const float e = geoSmootherStep((f - schedule_.diveEnd) / span);
            const float landingPitch = spec_.preset == GeoShotPreset::MapDiveThenCraneOut
                ? spec_.cranePitchDeg : spec_.tiltDeg;
            return landingPitch * e;
        }

        /// Orbit yaw at a frame: zero until the tilt lands, then the sweep.
        [[nodiscard]] float yawDegAt(int frame) const noexcept {
            const float f = static_cast<float>(frame);
            if (schedule_.orbitEnd <= schedule_.tiltEnd || f <= schedule_.tiltEnd) return 0.f;
            const float span = std::max(1.f, static_cast<float>(
                schedule_.orbitEnd - schedule_.tiltEnd));
            const float e = geoSmootherStep((f - schedule_.tiltEnd) / span);
            return spec_.orbitDeg * e;
        }

        /// Ground-relative altitude at a frame, straight from the pyramid's
        /// texel-to-screen law. This is the "ground-relative altitude" clause:
        /// the camera flies at the height the ground's own resolution implies.
        [[nodiscard]] float groundRelativeAltitudeAt(int frame) const noexcept {
            const float zoom = continuousZoomAt(frame);
            float alt = spec_.pyramid.altitudeMetersAtZoom(zoom);
            const float f = static_cast<float>(frame);
            if (spec_.preset == GeoShotPreset::MapDiveThenCraneOut
                && schedule_.tiltEnd > schedule_.diveEnd && f > schedule_.diveEnd) {
                const float span = std::max(1.f, static_cast<float>(
                    schedule_.tiltEnd - schedule_.diveEnd));
                const float e = geoSmootherStep((f - schedule_.diveEnd) / span);
                alt += spec_.craneOutMeters * e;
            }
            return alt;
        }

        /// The full pose at a frame. The camera sits on the sphere that keeps
        /// the anchor exactly framed: at slant distance R = alt / cos(pitch),
        /// so the height above the ground stays the altitude law's word while
        /// the pitch and yaw move around it.
        [[nodiscard]] GeoCameraKeyframe sampleAtFrame(int frame) const noexcept {
            constexpr double kDegToRad = std::numbers::pi / 180.0;
            GeoCameraKeyframe kf;
            kf.frame = frame;
            kf.continuousZoom = continuousZoomAt(frame);
            kf.pitchDeg = pitchDegAt(frame);
            kf.yawDeg = yawDegAt(frame);
            kf.fovDeg = spec_.fovDeg;
            kf.groundRelativeAltitudeM = groundRelativeAltitudeAt(frame);

            const float pitch = static_cast<float>(kf.pitchDeg * kDegToRad);
            const float yaw = static_cast<float>(kf.yawDeg * kDegToRad);
            const float cosP = std::cos(pitch);
            const float radius = kf.groundRelativeAltitudeM / std::max(0.05f, cosP);

            // pitch 0 is straight above the anchor looking down; pitch grows
            // towards the horizon along the heading `yaw`.
            const Vector3 offset{radius * std::sin(pitch) * std::sin(yaw),
                                 radius * cosP,
                                 radius * std::sin(pitch) * std::cos(yaw)};
            kf.cameraPosition = targetScene_ + offset;
            kf.lookTarget = targetScene_;
            return kf;
        }

        /// Every frame, in order. The render is this vector, nothing else.
        [[nodiscard]] std::vector<GeoCameraKeyframe> sampleAll() const {
            std::vector<GeoCameraKeyframe> frames;
            frames.reserve(static_cast<std::size_t>(frameCount()) + 1);
            for (int f = 0; f <= frameCount(); ++f) frames.push_back(sampleAtFrame(f));
            return frames;
        }

        /// The bridge the whole family exists for: a rig pose becomes an
        /// ordinary CameraShot, so "Europe -> Rome -> Colosseum -> tilt -> orbit"
        /// hands over to the Chronon camera language without a seam. The
        /// mapping is exact: this engine's pitch-0 nadir is CameraShot's
        /// 90-degree elevation, and the yaw is the same angle about the target.
        [[nodiscard]] motion::CameraShot toCameraShot(const GeoCameraKeyframe& kf) const {
            constexpr double kDegToRad = std::numbers::pi / 180.0;
            const Vector3 offset = kf.cameraPosition - kf.lookTarget;
            motion::CameraShot shot;
            shot.target = motion::ShotAnchor(kf.lookTarget);
            shot.distance = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
            shot.fov = kf.fovDeg;
            shot.azimuth = static_cast<float>(kf.yawDeg * kDegToRad);
            shot.elevation = static_cast<float>((90.0 - kf.pitchDeg) * kDegToRad);
            shot.roll = static_cast<float>(kf.rollDeg * kDegToRad);
            return shot;
        }

    private:
        [[nodiscard]] GeoPhaseSchedule computeSchedule() const noexcept {
            const float n = static_cast<float>(frameCount());
            GeoPhaseSchedule s;
            const auto at = [n](float fraction) {
                return std::clamp(static_cast<int>(std::round(n * fraction)), 1,
                                  static_cast<int>(n));
            };
            switch (spec_.preset) {
                case GeoShotPreset::MapDiveThenTilt:
                    s.diveEnd = at(0.70f);
                    s.tiltEnd = static_cast<int>(n);
                    break;
                case GeoShotPreset::MapDiveThenOrbit:
                    s.diveEnd = at(0.62f);
                    s.tiltEnd = at(0.80f);
                    s.orbitEnd = static_cast<int>(n);
                    break;
                case GeoShotPreset::MapDiveThenCraneOut:
                    s.diveEnd = at(0.70f);
                    s.tiltEnd = static_cast<int>(n);
                    break;
                default:
                    s.diveEnd = static_cast<int>(n);
                    s.tiltEnd = static_cast<int>(n);
                    break;
            }
            s.orbitEnd = std::max(s.orbitEnd, s.tiltEnd);
            return s;
        }

        GeoDiveSpec spec_;
        GeoPhaseSchedule schedule_{};
        Vector3 targetScene_{0.f, 0.f, 0.f};
    };

} // namespace chrononmotion::geospatial

#endif // CHRONONMOTION_GEOSPATIAL_GEOCAMERARIG_HPP
