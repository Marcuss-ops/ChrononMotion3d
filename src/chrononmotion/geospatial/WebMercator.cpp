#include "chrononmotion/geospatial/WebMercator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace chrononmotion::geospatial {

double clampMercatorLatitude(double latitudeDeg) noexcept {
    return std::clamp(latitudeDeg, -kMercatorLatitudeLimit, kMercatorLatitudeLimit);
}

Vector2 latLonToGlobalPixel(double latitudeDeg, double longitudeDeg, int zoom) noexcept {
    const double scale = kMercatorTileSize *
        static_cast<double>(std::int64_t{1} << std::max(0, zoom));
    const double lon = std::clamp(longitudeDeg, -180.0, 180.0);
    const double latRad = clampMercatorLatitude(latitudeDeg) * std::numbers::pi / 180.0;
    const double x = (lon + 180.0) / 360.0 * scale;
    const double y = (1.0 - std::log(std::tan(latRad) + 1.0 / std::cos(latRad)) /
                                 std::numbers::pi) * 0.5 * scale;
    return Vector2{static_cast<float>(x), static_cast<float>(y)};
}

GeoCoord globalPixelToLatLon(double x, double y, int zoom) noexcept {
    const double scale = kMercatorTileSize *
        static_cast<double>(std::int64_t{1} << std::max(0, zoom));
    const double lon = (x / scale) * 360.0 - 180.0;
    const double n = std::numbers::pi - 2.0 * std::numbers::pi * (y / scale);
    const double lat = std::atan(std::sinh(n)) * 180.0 / std::numbers::pi;
    return GeoCoord{lat, lon, 0.0};
}

MapWindow MapWindow::centeredOn(const GeoCoord& center, int zoom, Vector2 rasterSize) {
    const Vector2 centerPixel = latLonToGlobalPixel(center.latitudeDeg, center.longitudeDeg, zoom);
    return MapWindow{zoom,
                     Vector2{centerPixel.x - rasterSize.x * 0.5f,
                             centerPixel.y - rasterSize.y * 0.5f},
                     rasterSize};
}

Vector2 MapWindow::toRaster(const GeoCoord& point) const noexcept {
    const Vector2 pixel = latLonToGlobalPixel(point.latitudeDeg, point.longitudeDeg, zoom);
    return Vector2{pixel.x - topLeftPixel.x, pixel.y - topLeftPixel.y};
}

Vector2 MapWindow::toPlane(const GeoCoord& point) const noexcept {
    const Vector2 raster = toRaster(point);
    return Vector2{raster.x - rasterSize.x * 0.5f,
                   rasterSize.y * 0.5f - raster.y};
}

GeoCoord MapWindow::northWest() const noexcept {
    return globalPixelToLatLon(topLeftPixel.x, topLeftPixel.y, zoom);
}

GeoCoord MapWindow::southEast() const noexcept {
    return globalPixelToLatLon(topLeftPixel.x + rasterSize.x,
                               topLeftPixel.y + rasterSize.y, zoom);
}

} // namespace chrononmotion::geospatial
