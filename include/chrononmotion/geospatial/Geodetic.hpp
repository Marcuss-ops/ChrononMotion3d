// ChrononMotion — Geodesy & Cartographic Coordinate Systems (WGS84).
//
// Standalone, header-only geodetic math conforming to the WGS84 standard:
// - Lat/Lon/Alt <-> ECEF (Earth-Centered, Earth-Fixed)
// - ECEF <-> ENU (East-North-Up local tangent plane)
// - Conversion to Chronon3D graphics space (+X = East, +Y = Up, -Z = North)

#ifndef CHRONONMOTION_GEOSPATIAL_GEODETIC_HPP
#define CHRONONMOTION_GEOSPATIAL_GEODETIC_HPP

#include "chrononmotion/math/Vector3.hpp"

#include <cmath>
#include <numbers>

namespace chrononmotion::geospatial {

    /// Constants for the WGS84 Ellipsoid
    inline constexpr double kWgs84SemiMajorAxis = 6378137.0;          ///< a in meters
    inline constexpr double kWgs84InverseFlattening = 298.257223563;  ///< 1/f
    inline constexpr double kWgs84Flattening = 1.0 / kWgs84InverseFlattening;
    inline constexpr double kWgs84SemiMinorAxis = kWgs84SemiMajorAxis * (1.0 - kWgs84Flattening); ///< b = 6356752.314245
    inline constexpr double kWgs84EccentricitySquared = 2.0 * kWgs84Flattening - kWgs84Flattening * kWgs84Flattening; ///< e^2 = 0.00669437999014
    inline constexpr double kWgs84EPrimeSquared = (kWgs84SemiMajorAxis * kWgs84SemiMajorAxis - kWgs84SemiMinorAxis * kWgs84SemiMinorAxis) /
                                                  (kWgs84SemiMinorAxis * kWgs84SemiMinorAxis);

    /// Geographic coordinate (WGS84 ellipsoid).
    struct GeoCoord {
        double latitudeDeg{0.0};   ///< [-90.0, +90.0], North positive
        double longitudeDeg{0.0};  ///< [-180.0, +180.0], East positive
        double altitudeM{0.0};     ///< Height in meters above ellipsoid
    };

    /// Earth-Centered, Earth-Fixed Cartesian coordinate in double precision meters.
    struct EcefCoord {
        double x{0.0};
        double y{0.0};
        double z{0.0};
    };

    /// Local East-North-Up coordinate in meters relative to an anchor point.
    struct EnuCoord {
        double east{0.0};   ///< East in meters (+X)
        double north{0.0};  ///< North in meters (+Y)
        double up{0.0};     ///< Up in meters (+Z)

        /// Convert ENU to standard 3D Graphics space:
        /// +X = East, +Y = Up, -Z = North (camera forward)
        [[nodiscard]] Vector3 toSceneVector() const noexcept {
            return Vector3{static_cast<float>(east),
                           static_cast<float>(up),
                           static_cast<float>(-north)};
        }
    };

    /// Convert Geodetic (Lat, Lon, Alt) to ECEF (X, Y, Z).
    [[nodiscard]] inline EcefCoord toEcef(const GeoCoord& coord) noexcept {
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        const double latRad = coord.latitudeDeg * kDegToRad;
        const double lonRad = coord.longitudeDeg * kDegToRad;
        const double sinLat = std::sin(latRad);
        const double cosLat = std::cos(latRad);
        const double sinLon = std::sin(lonRad);
        const double cosLon = std::cos(lonRad);

        const double n = kWgs84SemiMajorAxis / std::sqrt(1.0 - kWgs84EccentricitySquared * sinLat * sinLat);
        const double x = (n + coord.altitudeM) * cosLat * cosLon;
        const double y = (n + coord.altitudeM) * cosLat * sinLon;
        const double z = (n * (1.0 - kWgs84EccentricitySquared) + coord.altitudeM) * sinLat;
        return EcefCoord{x, y, z};
    }

    /// Convert ECEF (X, Y, Z) to Geodetic (Lat, Lon, Alt) using Bowring's method.
    [[nodiscard]] inline GeoCoord fromEcef(const EcefCoord& ecef) noexcept {
        constexpr double kRadToDeg = 180.0 / std::numbers::pi;
        const double p = std::sqrt(ecef.x * ecef.x + ecef.y * ecef.y);
        if (p < 1e-6) {
            const double lat = (ecef.z >= 0.0) ? 90.0 : -90.0;
            const double alt = std::abs(ecef.z) - kWgs84SemiMinorAxis;
            return GeoCoord{lat, 0.0, alt};
        }

        const double theta = std::atan2(ecef.z * kWgs84SemiMajorAxis, p * kWgs84SemiMinorAxis);
        const double sinTheta = std::sin(theta);
        const double cosTheta = std::cos(theta);

        const double latRad = std::atan2(
            ecef.z + kWgs84EPrimeSquared * kWgs84SemiMinorAxis * sinTheta * sinTheta * sinTheta,
            p - kWgs84EccentricitySquared * kWgs84SemiMajorAxis * cosTheta * cosTheta * cosTheta);
        const double lonRad = std::atan2(ecef.y, ecef.x);

        const double sinLat = std::sin(latRad);
        const double n = kWgs84SemiMajorAxis / std::sqrt(1.0 - kWgs84EccentricitySquared * sinLat * sinLat);
        const double altM = p / std::cos(latRad) - n;

        return GeoCoord{latRad * kRadToDeg, lonRad * kRadToDeg, altM};
    }

    /// Transform an ECEF point into local ENU (East-North-Up) relative to a geodetic origin.
    [[nodiscard]] inline EnuCoord toEnu(const EcefCoord& point, const GeoCoord& origin) noexcept {
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        const EcefCoord originEcef = toEcef(origin);
        const double dx = point.x - originEcef.x;
        const double dy = point.y - originEcef.y;
        const double dz = point.z - originEcef.z;

        const double latRad = origin.latitudeDeg * kDegToRad;
        const double lonRad = origin.longitudeDeg * kDegToRad;
        const double sinLat = std::sin(latRad);
        const double cosLat = std::cos(latRad);
        const double sinLon = std::sin(lonRad);
        const double cosLon = std::cos(lonRad);

        const double east = -sinLon * dx + cosLon * dy;
        const double north = -sinLat * cosLon * dx - sinLat * sinLon * dy + cosLat * dz;
        const double up = cosLat * cosLon * dx + cosLat * sinLon * dy + sinLat * dz;

        return EnuCoord{east, north, up};
    }

    /// Transform a Geodetic point into local ENU relative to a geodetic origin.
    [[nodiscard]] inline EnuCoord toEnu(const GeoCoord& point, const GeoCoord& origin) noexcept {
        return toEnu(toEcef(point), origin);
    }

    /// Convert a Geodetic point directly into Chronon 3D scene space (+X = East, +Y = Up, -Z = North).
    [[nodiscard]] inline Vector3 toSceneSpace(const GeoCoord& point, const GeoCoord& origin) noexcept {
        return toEnu(point, origin).toSceneVector();
    }

    /// Great-circle distance between two geodetic coordinates using Haversine formula (in meters).
    [[nodiscard]] inline double distanceMeters(const GeoCoord& a, const GeoCoord& b) noexcept {
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        const double lat1 = a.latitudeDeg * kDegToRad;
        const double lat2 = b.latitudeDeg * kDegToRad;
        const double dLat = (b.latitudeDeg - a.latitudeDeg) * kDegToRad;
        const double dLon = (b.longitudeDeg - a.longitudeDeg) * kDegToRad;

        const double sinDLat2 = std::sin(dLat * 0.5);
        const double sinDLon2 = std::sin(dLon * 0.5);
        const double h = sinDLat2 * sinDLat2 + std::cos(lat1) * std::cos(lat2) * sinDLon2 * sinDLon2;
        const double c = 2.0 * std::atan2(std::sqrt(h), std::sqrt(1.0 - h));

        return kWgs84SemiMajorAxis * c;
    }

} // namespace chrononmotion::geospatial

#endif // CHRONONMOTION_GEOSPATIAL_GEODETIC_HPP
