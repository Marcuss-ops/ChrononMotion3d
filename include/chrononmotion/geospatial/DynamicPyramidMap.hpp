// ChrononMotion — Authoritative Dynamic Multi-Resolution Tile Pyramid Geospatial Engine.
//
// Represents the continuous multi-resolution ground plane model.
// Resolves exact Level-of-Detail (LOD), Texel-to-Screen Ratio (TSR),
// and sub-pixel Mipmap cross-fade blend factors as the camera flies between
// space/orbit (z=5, 650 km) and ground/street level (z=18, 150 m).

#ifndef CHRONONMOTION_GEOSPATIAL_DYNAMICPYRAMIDMAP_HPP
#define CHRONONMOTION_GEOSPATIAL_DYNAMICPYRAMIDMAP_HPP

#include "chrononmotion/geospatial/Geodetic.hpp"
#include "chrononmotion/geospatial/WebMercator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

namespace chrononmotion::geospatial {

    /// Metres per pixel at the equator at zoom 0.
    inline constexpr double kWebMercatorEquatorMetresPerPixel = 156543.03392;

    /// Authoritative specification for dynamic multi-scale tile pyramids.
    struct DynamicPyramidSpec {
        GeoCoord targetAnchor{41.890210, 12.492231, 50.0}; ///< Default: Rome Colosseum
        std::string provider{"esri_sat"};
        float minZoom{5.0f};
        float maxZoom{18.0f};
        float canvasWidth{1920.0f};
        float canvasHeight{1080.0f};
        float fovDegrees{60.0f};

        /// Calculates exact optical eye altitude in meters corresponding to continuous Web Mercator zoom.
        [[nodiscard]] float altitudeMetersAtZoom(float zoom) const {
            const double latRad = targetAnchor.latitudeDeg * std::numbers::pi / 180.0;
            const double mPerPx = (kWebMercatorEquatorMetresPerPixel * std::cos(latRad)) / std::pow(2.0, static_cast<double>(zoom));
            return static_cast<float>(mPerPx * canvasHeight * 0.95);
        }

        /// Inverts altitude to determine the continuous floating-point zoom required to maintain 1:1 TSR.
        [[nodiscard]] float continuousZoomAtAltitude(float altitudeMeters) const {
            const double latRad = targetAnchor.latitudeDeg * std::numbers::pi / 180.0;
            const double mPerPxTarget = std::max(0.01, static_cast<double>(altitudeMeters) / (canvasHeight * 0.95));
            const double z = std::log2((kWebMercatorEquatorMetresPerPixel * std::cos(latRad)) / mPerPxTarget);
            return static_cast<float>(std::clamp(z, static_cast<double>(minZoom), static_cast<double>(maxZoom)));
        }

        /// Resolves the integer lower/upper LOD levels and the smoothstep blend factor alpha for cross-fading.
        struct MipmapBlendState {
            int zLow{5};
            int zHigh{6};
            float alpha{0.0f};      ///< Continuous smoothstep blend weight [0.0, 1.0]
            float scaleLow{1.0f};   ///< Scale multiplier for low-res layer
            float scaleHigh{0.5f};  ///< Scale multiplier for high-res layer
        };

        [[nodiscard]] MipmapBlendState resolveMipmapBlend(float continuousZoom) const {
            MipmapBlendState state;
            const float zClamped = std::clamp(continuousZoom, minZoom, maxZoom);
            state.zLow = static_cast<int>(std::floor(zClamped));
            state.zHigh = std::min(static_cast<int>(maxZoom), state.zLow + 1);

            const float t = zClamped - static_cast<float>(state.zLow);
            // Smoothstep polynomial: 3t^2 - 2t^3
            state.alpha = t * t * (3.0f - 2.0f * t);
            state.scaleLow = std::pow(2.0f, t);
            state.scaleHigh = std::pow(2.0f, t - 1.0f);
            return state;
        }

        /// Computes the tile coordinates (tx, ty) covering the target anchor at integer zoom.
        [[nodiscard]] std::pair<int, int> tileIndexAt(float zoom) const {
            const int z = static_cast<int>(std::floor(zoom));
            const double scale = 256.0 * std::pow(2.0, z);
            const double x = (targetAnchor.longitudeDeg + 180.0) / 360.0 * scale;
            const double latRad = targetAnchor.latitudeDeg * std::numbers::pi / 180.0;
            const double y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / std::numbers::pi) * 0.5 * scale;
            return {static_cast<int>(x / 256.0), static_cast<int>(y / 256.0)};
        }
    };

} // namespace chrononmotion::geospatial

#endif // CHRONONMOTION_GEOSPATIAL_DYNAMICPYRAMIDMAP_HPP
