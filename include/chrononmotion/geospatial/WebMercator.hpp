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

namespace chrononmotion::geospatial {

    /// The latitude beyond which Mercator has no finite image (the web map
    /// convention; 85.05112877980659° is atan(sinh(pi)) in degrees).
    inline constexpr double kMercatorLatitudeLimit = 85.05112877980659;

    /// One tile edge, in pixels, at every zoom level.
    inline constexpr double kMercatorTileSize = 256.0;

    [[nodiscard]] double clampMercatorLatitude(double latitudeDeg) noexcept;

    /// Global pixel coordinate of a geographic point at `zoom`.
    [[nodiscard]] Vector2 latLonToGlobalPixel(double latitudeDeg, double longitudeDeg,
                                              int zoom) noexcept;

    /// Inverse of `latLonToGlobalPixel`.
    [[nodiscard]] GeoCoord globalPixelToLatLon(double x, double y, int zoom) noexcept;

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
        [[nodiscard]] static MapWindow centeredOn(const GeoCoord& center, int zoom, Vector2 rasterSize);

        /// Position of a geographic point inside the raster, in raster pixels
        /// (+x east, +y south, origin at the raster's top-left corner).
        [[nodiscard]] Vector2 toRaster(const GeoCoord& point) const noexcept;

        /// Position of a geographic point in the plane the raster is drawn on,
        /// centred on the raster and with +y up (graphics space). This is the
        /// position a map-space layer is authored at, so a marker written here
        /// sits exactly on the geography the raster painted.
        [[nodiscard]] Vector2 toPlane(const GeoCoord& point) const noexcept;

        /// The geographic bounds of the raster: west/south/east/north.
        [[nodiscard]] GeoCoord northWest() const noexcept;
        [[nodiscard]] GeoCoord southEast() const noexcept;
    };

} // namespace chrononmotion::geospatial

#endif // CHRONONMOTION_GEOSPATIAL_WEBMERCATOR_HPP
