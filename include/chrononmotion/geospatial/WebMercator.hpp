// ChrononMotion — Web Mercator (EPSG:3857) raster georeferencing.
//
// The companion of `Geodetic.hpp`. Geodetic answers "where is this in the real
// world" (WGS84 ellipsoid, ECEF, local ENU metres); this answers "where is this
// on the raster basemap we are about to draw", which is what a cinematic map
// flyover actually needs: a satellite or street raster is a Mercator image, and
// every route marker, pin and callout must land on the pixel that raster puts
// there or the map lies about geography.
//
// The math is the slippy-tile convention Chronon's Python cartography already
// uses (`Chronon3d/tools/cartography/map_cartography.py`, the content side of
// this tree): a global pixel grid of 256 * 2^zoom tiles, +x east, +y south,
// latitudes clamped to the Mercator limit. Keeping the two implementations on
// one formula is the whole point — the raster is produced on one side and
// georeferenced on the other, and each side asserts the same reference pixels
// (`map_cartography.py --self-test` and this tree's motion_geospatial tests).

#ifndef CHRONONMOTION_GEOSPATIAL_WEBMERCATOR_HPP
#define CHRONONMOTION_GEOSPATIAL_WEBMERCATOR_HPP

#include "chrononmotion/geospatial/Geodetic.hpp"
#include "chrononmotion/math/Vector2.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace chrononmotion::geospatial {

    /// The latitude beyond which Mercator has no finite image (the web map
    /// convention; 85.05112877980659° is atan(sinh(pi)) in degrees).
    inline constexpr double kMercatorLatitudeLimit = 85.05112877980659;

    /// One tile edge, in pixels, at every zoom level.
    inline constexpr double kMercatorTileSize = 256.0;

    [[nodiscard]] inline double clampMercatorLatitude(double latitudeDeg) noexcept {
        return std::clamp(latitudeDeg, -kMercatorLatitudeLimit, kMercatorLatitudeLimit);
    }

    /// Global pixel coordinate of a geographic point at `zoom`.
    [[nodiscard]] inline Vector2 latLonToGlobalPixel(double latitudeDeg, double longitudeDeg,
                                                     int zoom) noexcept {
        const double scale = kMercatorTileSize * static_cast<double>(std::int64_t{1} << std::max(0, zoom));
        const double lon = std::clamp(longitudeDeg, -180.0, 180.0);
        const double latRad = clampMercatorLatitude(latitudeDeg) * std::numbers::pi / 180.0;
        const double x = (lon + 180.0) / 360.0 * scale;
        const double y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) / std::numbers::pi)
                       * 0.5 * scale;
        return Vector2{static_cast<float>(x), static_cast<float>(y)};
    }

    /// Inverse of `latLonToGlobalPixel`.
    [[nodiscard]] inline GeoCoord globalPixelToLatLon(double x, double y, int zoom) noexcept {
        const double scale = kMercatorTileSize * static_cast<double>(std::int64_t{1} << std::max(0, zoom));
        const double lon = (x / scale) * 360.0 - 180.0;
        const double n = std::numbers::pi - 2.0 * std::numbers::pi * (y / scale);
        const double lat = std::atan(std::sinh(n)) * 180.0 / std::numbers::pi;
        return GeoCoord{lat, lon, 0.0};
    }

    /// The geography a raster basemap covers, and where that raster sits.
    ///
    /// `topLeftPixel` is the global pixel coordinate of the raster's own (0,0)
    /// corner, so a caller that composed tiles at `zoom` can state the window in
    /// the same units it downloaded them in. `rasterSize` is the raster's size in
    /// pixels — which is also its size in scene units, so a basemap drawn as a
    /// flat plane is measured in the same units as the camera's altitude.
    struct MapWindow {
        int zoom{8};
        Vector2 topLeftPixel{0.f, 0.f};
        Vector2 rasterSize{1920.f, 1080.f};

        /// A window centred on a geographic point.
        [[nodiscard]] static MapWindow centeredOn(const GeoCoord& center, int zoom, Vector2 rasterSize) {
            const Vector2 centerPixel = latLonToGlobalPixel(center.latitudeDeg, center.longitudeDeg, zoom);
            return MapWindow{zoom,
                             Vector2{centerPixel.x - rasterSize.x * 0.5f,
                                     centerPixel.y - rasterSize.y * 0.5f},
                             rasterSize};
        }

        /// Position of a geographic point inside the raster, in raster pixels
        /// (+x east, +y south, origin at the raster's top-left corner).
        [[nodiscard]] Vector2 toRaster(const GeoCoord& point) const noexcept {
            const Vector2 pixel = latLonToGlobalPixel(point.latitudeDeg, point.longitudeDeg, zoom);
            return Vector2{pixel.x - topLeftPixel.x, pixel.y - topLeftPixel.y};
        }

        /// Position of a geographic point in the plane the raster is drawn on,
        /// centred on the raster and with +y up (graphics space). This is the
        /// position a map-space layer is authored at, so a marker written here
        /// sits exactly on the geography the raster painted.
        [[nodiscard]] Vector2 toPlane(const GeoCoord& point) const noexcept {
            const Vector2 raster = toRaster(point);
            return Vector2{raster.x - rasterSize.x * 0.5f,
                           rasterSize.y * 0.5f - raster.y};
        }

        /// The geographic bounds of the raster: west/south/east/north.
        [[nodiscard]] GeoCoord northWest() const noexcept {
            return globalPixelToLatLon(topLeftPixel.x, topLeftPixel.y, zoom);
        }
        [[nodiscard]] GeoCoord southEast() const noexcept {
            return globalPixelToLatLon(topLeftPixel.x + rasterSize.x,
                                       topLeftPixel.y + rasterSize.y, zoom);
        }
    };

} // namespace chrononmotion::geospatial

#endif // CHRONONMOTION_GEOSPATIAL_WEBMERCATOR_HPP
