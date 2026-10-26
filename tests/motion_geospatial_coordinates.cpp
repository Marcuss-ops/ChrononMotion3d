#include "motion_geospatial_shared.hpp"

namespace chrononmotion_test::geospatial {
void testGeodesy() {

        std::cout << "\n=== geodesy ===\n";
        const EcefCoord ecef = toEcef(kColosseum);
        check(ecef.x > 4640000.0 && ecef.x < 4645000.0, "ECEF X is in the Rome range");
        check(ecef.y > 1025000.0 && ecef.y < 1030000.0, "ECEF Y is in the Rome range");
        check(ecef.z > 4235000.0 && ecef.z < 4242000.0, "ECEF Z is in the Rome range");

        const GeoCoord roundTrip = fromEcef(ecef);
        checkNear(roundTrip.latitudeDeg, kColosseum.latitudeDeg, 1e-6, "Bowring latitude round-trip");
        checkNear(roundTrip.longitudeDeg, kColosseum.longitudeDeg, 1e-6, "Bowring longitude round-trip");
        checkNear(roundTrip.altitudeM, kColosseum.altitudeM, 1e-3, "Bowring altitude round-trip");

        const EnuCoord self = toEnu(kColosseum, kColosseum);
        checkNear(self.east, 0.0, 1e-4, "ENU east of the origin is zero");
        checkNear(self.north, 0.0, 1e-4, "ENU north of the origin is zero");
        checkNear(self.up, 0.0, 1e-4, "ENU up of the origin is zero");

        const GeoCoord milanDuomo{45.464211, 9.191383, 120.0};
        const double distanceKm = distanceMeters(kColosseum, milanDuomo) / 1000.0;
        check(distanceKm > 470.0 && distanceKm < 485.0, "Haversine Rome-Milan is about 477 km");

        // A point north of the origin must be -Z in scene space, and a point east
        // of it +X: that sign is what makes the whole map render the right way up.
        const GeoCoord north{41.900210, 12.492231, 50.0};
        const GeoCoord east{41.890210, 12.502231, 50.0};
        check(toSceneSpace(north, kColosseum).z < 0.f, "north is -Z in scene space");
        check(toSceneSpace(east, kColosseum).x > 0.f, "east is +X in scene space");
    }

void testWebMercator() {

        std::cout << "\n=== web mercator (locked against map_cartography.py) ===\n";
        // These are the exact values `Chronon3d/tools/cartography/
        // map_cartography.py` `latlon_to_mercator_px` returns for the same
        // inputs. They are the contract between the side that crops the raster
        // and the side that georeferences it.
        const struct { double lat, lon; int zoom; double x, y; } reference[] = {
            {41.890210, 12.492231, 14, 2242697.0401450666, 1558716.8764844453},
            {29.86, -92.70, 8, 15892.48, 27067.93732941762},
            {33.9598, -83.3768, 16, 4502967.491697778, 6704228.367772281},
            {0.0, 0.0, 0, 128.0, 128.0},
        };
        for (const auto& expected : reference) {
            const Vector2 pixel = latLonToGlobalPixel(expected.lat, expected.lon, expected.zoom);
            const double tolerance = floatTolerance(std::max(expected.x, expected.y));
            checkNear(pixel.x, expected.x, tolerance,
                      "global pixel X matches the cartography tool at zoom "
                          + std::to_string(expected.zoom));
            checkNear(pixel.y, expected.y, tolerance,
                      "global pixel Y matches the cartography tool at zoom "
                          + std::to_string(expected.zoom));
        }

        const GeoCoord back = globalPixelToLatLon(2242697.0401450666, 1558716.8764844453, 14);
        checkNear(back.latitudeDeg, kColosseum.latitudeDeg, 1e-9, "global pixel round-trips to latitude");
        checkNear(back.longitudeDeg, kColosseum.longitudeDeg, 1e-9, "global pixel round-trips to longitude");

        checkNear(clampMercatorLatitude(89.0), kMercatorLatitudeLimit, 0.0,
                  "an out-of-range latitude clamps to the Mercator limit");

        // A window centred on a point must place that point's plane position at
        // the raster centre, and its raster position at the middle pixel. This is
        // the assumption the entire marker placement rests on.
        const Vector2 rasterSize{1920.f, 1080.f};
        const MapWindow window = MapWindow::centeredOn(kColosseum, 14, rasterSize);
        const Vector2 plane = window.toPlane(kColosseum);
        checkNear(plane.x, 0.0, 1e-4, "the window centre is the plane origin in X");
        checkNear(plane.y, 0.0, 1e-4, "the window centre is the plane origin in Y");
        const Vector2 raster = window.toRaster(kColosseum);
        checkNear(raster.x, 960.0, 1e-4, "the window centre is the middle raster pixel in X");
        checkNear(raster.y, 540.0, 1e-4, "the window centre is the middle raster pixel in Y");

        // North is up on the raster, so a point north of the centre must have
        // +y in plane space, which is what the authoring bridge flips back.
        const GeoCoord north{41.900210, 12.492231, 50.0};
        check(window.toPlane(north).y > 0.f, "a point north of the centre is +y in plane space");
        check(window.toRaster(north).y < 540.f, "a point north of the centre is above the middle raster row");
    }
}
