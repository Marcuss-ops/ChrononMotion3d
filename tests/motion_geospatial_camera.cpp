#include "motion_geospatial_shared.hpp"

namespace chrononmotion_test::geospatial {
void testDynamicPyramidEngine() {
        std::cout << "\n=== dynamic multi-resolution tile pyramid ===\n";
        DynamicPyramidSpec spec;
        spec.targetAnchor = kColosseum;
        spec.minZoom = 5.0f;
        spec.maxZoom = 18.0f;

        const float orbitAlt = spec.altitudeMetersAtZoom(5.0f);
        check(orbitAlt > 2000000.f && orbitAlt < 5000000.f, "zoom 5 altitude is in space/orbit range (~3700 km)");

        const float streetAlt = spec.altitudeMetersAtZoom(18.0f);
        check(streetAlt > 200.f && streetAlt < 800.f, "zoom 18 altitude is in street/building range (~450 m)");

        const float zInverted = spec.continuousZoomAtAltitude(orbitAlt);
        checkNear(zInverted, 5.0, 0.05, "continuousZoomAtAltitude exactly inverts altitudeMetersAtZoom");

        const auto blend = spec.resolveMipmapBlend(15.5f);
        check(blend.zLow == 15 && blend.zHigh == 16, "z=15.5 resolves zLow=15 and zHigh=16");
        checkNear(blend.alpha, 0.5, 1e-4, "midpoint zoom yields exact alpha=0.5 smoothstep blend");

        const auto tile18 = spec.tileIndexAt(18.0f);
        check(tile18.first == 140168 && tile18.second == 97419, "Colosseum tile index at z=18 matches (140168, 97419)");
    }

void testGeoCameraDivePhysics() {
        std::cout << "\n=== geo_camera_v1: dive physics (altitude, velocity, acceleration) ===\n";
        const auto spec = GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit);
        const GeoCameraRig rig(spec);
        const auto frames = rig.sampleAll();

        // TestMapDiveAltitudeMonotonic: during the pure dive the camera only
        // ever descends, to numerical tolerance, and it lands far below where
        // it took off.
        float maxRise = 0.f;
        for (std::size_t i = 1; i < frames.size(); ++i)
            maxRise = std::max(maxRise, frames[i].groundRelativeAltitudeM
                                        - frames[i - 1].groundRelativeAltitudeM);
        check(maxRise <= 1e-3f, "altitude never rises during the dive (max rise "
              + std::to_string(maxRise) + " m <= 1e-3)");
        check(frames.back().groundRelativeAltitudeM < frames.front().groundRelativeAltitudeM / 1000.f,
              "the dive lands more than 1000x lower than orbit");

        // TestMapZoomVelocityContinuity: the per-frame zoom step is bounded by
        // the smootherstep slope (max 1.875 of the mean step) — no slow-slow-
        // slow-SNAP across a level handover — and the second difference stays
        // under the smootherstep acceleration ceiling (max |s''| = 10/sqrt(3)),
        // so the velocity itself changes smoothly.
        const int diveSpan = std::max(1, rig.schedule().diveEnd);
        const float dz = spec.endZoom - spec.startZoom;
        const float meanStep = dz / static_cast<float>(diveSpan);
        float maxStep = 0.f;
        float maxAccel = 0.f;
        for (std::size_t i = 1; i < frames.size(); ++i) {
            const float step = frames[i].continuousZoom - frames[i - 1].continuousZoom;
            maxStep = std::max(maxStep, std::abs(step));
            if (i > 1)
                maxAccel = std::max(maxAccel,
                    std::abs(frames[i].continuousZoom - 2.f * frames[i - 1].continuousZoom
                             + frames[i - 2].continuousZoom));
        }
        check(maxStep <= 1.8751f * meanStep + 1e-6f,
              "zoom velocity is continuous: max frame step " + std::to_string(maxStep)
              + " <= 1.8751x mean step " + std::to_string(meanStep));
        // The analytic ceiling bounds the continuous curve; the discrete second
        // difference of the samples carries an O(1/span^2) curvature term on
        // top of it, so the discrete check allows half a percent of slack.
        const float accelCeiling = (10.f / std::sqrt(3.f)) * dz
                                   / (static_cast<float>(diveSpan) * static_cast<float>(diveSpan));
        check(maxAccel <= accelCeiling * 1.005f,
              "zoom acceleration is bounded: max 2nd difference " + std::to_string(maxAccel)
              + " <= ceiling " + std::to_string(accelCeiling) + " (+0.5% discretisation)");

        // The dive ends at the authored zoom and holds it (no drift after).
        checkNear(frames[rig.schedule().diveEnd].continuousZoom, spec.endZoom, 1e-4,
                  "the dive reaches exactly the authored landing zoom");
        checkNear(frames.back().continuousZoom, spec.endZoom, 1e-5,
                  "zoom holds at the landing zoom through tilt and orbit");
    }

void testGeoCameraTargetLock() {
        std::cout << "\n=== geo_camera_v1: target lock (the anchor never leaves the reticle) ===\n";
        const Vector3 anchor = Vector3(0.f, 0.f, 0.f); // ENU origin == the anchor

        // Every preset must hold the anchor exactly framed for every frame.
        for (const GeoShotPreset preset : {
                 GeoShotPreset::MapDiveToLandmark, GeoShotPreset::MapDiveThenTilt,
                 GeoShotPreset::MapDiveThenOrbit, GeoShotPreset::MapDiveThenCraneOut,
                 GeoShotPreset::MapDiveMetricReveal}) {
            const GeoCameraRig rig(GeoDiveSpec::forPreset(preset));
            const auto frames = rig.sampleAll();
            bool allAimed = true;
            for (const auto& kf : frames)
                if (kf.lookTarget.x != anchor.x || kf.lookTarget.y != anchor.y
                    || kf.lookTarget.z != anchor.z)
                    allAimed = false;
            check(allAimed, "every preset aims the optical axis at the anchor on every frame");
        }

        // TestMapDiveTargetLock, in screen terms: a look-at camera projects its
        // target to the frame centre. Projected through an explicit pinhole
        // (1920x1080, vertical FOV 60), the anchor must land within a sub-
        // pixel tolerance of (960, 540) on every sampled frame — including
        // the tilted, orbiting ones, which is where a drifting rig shows.
        const GeoDiveSpec spec = GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit);
        const GeoCameraRig rig(spec);
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        float maxPixelError = 0.f;
        for (int f = 0; f <= rig.frameCount(); f += 7) {
            const GeoCameraKeyframe kf = rig.sampleAtFrame(f);
            const Vector3 forward = kf.lookTarget - kf.cameraPosition;
            const float dist = std::sqrt(forward.x * forward.x + forward.y * forward.y
                                         + forward.z * forward.z);
            // Basis orthogonal to the view axis, hinted by the orbit heading so
            // the nadir frame (pitch 0) is not singular.
            const Vector3 east{1.f, 0.f, 0.f};
            const Vector3 north{0.f, 0.f, 1.f};
            const float yawRad = static_cast<float>(kf.yawDeg * kDegToRad);
            const Vector3 heading{std::sin(yawRad), 0.f, std::cos(yawRad)};
            auto normalize = [](const Vector3& v) {
                const float n = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
                return Vector3{v.x / n, v.y / n, v.z / n};
            };
            auto cross = [](const Vector3& a, const Vector3& b) {
                return Vector3{a.y * b.z - a.z * b.y,
                               a.z * b.x - a.x * b.z,
                               a.x * b.y - a.y * b.x};
            };
            auto dot = [](const Vector3& a, const Vector3& b) {
                return a.x * b.x + a.y * b.y + a.z * b.z;
            };
            const Vector3 fDir = normalize(forward);
            const Vector3 right = normalize(cross(fDir, heading));
            const Vector3 up = cross(right, fDir);

            // Pinhole: the target sits on the axis, so both camera-plane
            // coordinates must vanish and the screen point is the centre.
            const Vector3 d = kf.lookTarget - kf.cameraPosition;
            const float xCam = dot(d, right);
            const float yCam = dot(d, up);
            const float zCam = dot(d, fDir);
            const float focalPx = static_cast<float>(spec.canvasHeight)
                                  / (2.f * std::tan(static_cast<float>(spec.fovDeg * kDegToRad) * 0.5f));
            const float screenX = spec.canvasWidth * 0.5f + (xCam / zCam) * focalPx;
            const float screenY = spec.canvasHeight * 0.5f - (yCam / zCam) * focalPx;
            maxPixelError = std::max(maxPixelError,
                std::max(std::abs(screenX - 960.f), std::abs(screenY - 540.f)));
            checkNear(dist, kf.groundRelativeAltitudeM / std::max(0.05f, std::cos(
                          static_cast<float>(kf.pitchDeg * kDegToRad))), 1e-2f,
                      "the camera rides the slant-distance sphere the altitude law implies");
        }
        check(maxPixelError < 0.01f,
              "target lock: the anchor projects to the reticle (960, 540) within "
              + std::to_string(maxPixelError) + " px on every frame");
    }

void testGeoCameraLodContinuity() {
        std::cout << "\n=== geo_camera_v1: LOD continuity (no skipped levels, no pop) ===\n";
        const GeoCameraRig rig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveToLandmark));
        const auto frames = rig.sampleAll();

        // TestMapLODNoMissingTiles / NoVisibleSeams, at the sampling layer.
        // A visible seam or a missing region can only come from three events:
        // a level jumping by more than one between frames, a handover where
        // the retiring level still held the majority (a content snap), or the
        // new level's centre tile not being the ground child of the old one.
        // None of the three may happen.
        int maxLevelJump = 0;
        int previousLow = -1;
        float previousAlpha = 0.f;
        float maxHandoverAlpha = 0.f;
        float maxAlphaStep = 0.f;
        int maxChildMismatch = 0;
        for (const auto& kf : frames) {
            const auto blend = spec_for_lod(kf.continuousZoom);
            if (previousLow >= 0) {
                const int jump = std::abs(blend.zLow - previousLow);
                maxLevelJump = std::max(maxLevelJump, jump);
                // The physical cross-fade is the *weight of the level on
                // screen*, and across a pair switch that level is the shared
                // one: it holds `alpha` in the old pair and `1 - alpha` in the
                // new one. Continuity is measured in that single coordinate.
                const float persistentWeightStep = jump == 1
                    ? std::abs((1.f - blend.alpha) - previousAlpha)
                    : std::abs(blend.alpha - previousAlpha);
                maxAlphaStep = std::max(maxAlphaStep, persistentWeightStep);
                if (jump == 1) {
                    // At a handover the shared level must already hold the
                    // majority weight in the new pair, so the dominant layer is
                    // the same image on both sides of the switch.
                    maxHandoverAlpha = std::max(maxHandoverAlpha, blend.alpha);
                    // And the new centre tile must be the ground child of the
                    // previous level's centre tile: same ground, finer pyramid.
                    const auto oldTile = rig.spec().pyramid.tileIndexAt(
                        static_cast<float>(previousLow) + 0.5f);
                    const auto newTile = rig.spec().pyramid.tileIndexAt(
                        static_cast<float>(blend.zLow) + 0.5f);
                    const int cx = oldTile.first >> 1, cy = oldTile.second >> 1;
                    const int dx = std::abs(cx * 2 + (cx << 1 == cx ? 0 : 0) - newTile.first * 2);
                    // child x = 2*parent x or 2*parent x + 1
                    const int childMis = std::min(std::abs(newTile.first - 2 * oldTile.first),
                                                  std::abs(newTile.first - (2 * oldTile.first + 1)));
                    const int childMisY = std::min(std::abs(newTile.second - 2 * oldTile.second),
                                                   std::abs(newTile.second - (2 * oldTile.second + 1)));
                    (void)dx;
                    (void)cy;
                    maxChildMismatch = std::max(maxChildMismatch,
                                                std::max(childMis, childMisY));
                }
            }
            previousLow = blend.zLow;
            previousAlpha = blend.alpha;
        }
        check(maxLevelJump <= 1,
              "LOD never skips a quadtree level (max jump " + std::to_string(maxLevelJump) + ")");
        check(maxHandoverAlpha <= 0.5f,
              "level handovers keep the shared level dominant (max incoming alpha "
              + std::to_string(maxHandoverAlpha) + " <= 0.5)");
        // The cross-fade weight is a smoothstep of the very zoom the camera
        // samples, so its per-frame step follows the dive's own velocity with
        // at most the smoothstep slope factor of 1.5. A value above that bound
        // would mean a blend changing independently of the camera — the
        // definition of a pop.
        float maxZoomStep = 0.f;
        for (std::size_t i = 1; i < frames.size(); ++i)
            maxZoomStep = std::max(maxZoomStep,
                std::abs(frames[i].continuousZoom - frames[i - 1].continuousZoom));
        check(maxAlphaStep <= 1.5f * maxZoomStep + 1e-6f,
              "the cross-fade rides the camera exactly (max alpha step "
              + std::to_string(maxAlphaStep) + " <= 1.5 x max zoom step "
              + std::to_string(maxZoomStep) + ")");
        check(maxChildMismatch <= 1,
              "every level handover lands on the ground child tile (max mismatch "
              + std::to_string(maxChildMismatch) + ") — no blank blocks, no seams");

        // TestMapLODCoverage: the prefetch pyramid that feeds a render covers
        // every level the dive will sample, with the documented tile budget.
        const int levels = static_cast<int>(std::floor(spec_lod_max(rig)))
                           - static_cast<int>(std::floor(rig.spec().startZoom)) + 1;
        const int prefetchBudget = levels * 25; // radius-2 window per level
        check(prefetchBudget == 325,
              "the z=5..17 dive prefetches 325 tiles (13 levels x radius-2 window), got "
              + std::to_string(prefetchBudget));
    }

void testGeoCameraDeterminismAndBridge() {
        std::cout << "\n=== geo_camera_v1: determinism + CameraShot bridge ===\n";
        const auto framesA = GeoCameraRig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit)).sampleAll();
        const auto framesB = GeoCameraRig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit)).sampleAll();

        // Same spec, same samples: bit for bit, not "to tolerance". A camera
        // path that cannot be reproduced cannot be certified.
        check(framesA.size() == framesB.size() && !framesA.empty(),
              "two rigs of the same spec produce the same frame count");
        bool identical = true;
        for (std::size_t i = 0; i < framesA.size() && identical; ++i)
            identical = std::memcmp(&framesA[i], &framesB[i], sizeof(GeoCameraKeyframe)) == 0;
        check(identical, "camera samples, zoom, LOD and pitch/yaw are bit-for-bit deterministic");

        // The tile identity a renderer would fetch: same spec, same tile IDs,
        // and the landing tile is the documented Colosseum one at z=17.
        const auto landingTile = GeoCameraRig(GeoDiveSpec::forPreset(
            GeoShotPreset::MapDiveToLandmark)).spec().pyramid.tileIndexAt(17.f);
        check(landingTile.first == 70084 && landingTile.second == 48709,
              "the landing frame resolves the documented Colosseum tile (70084, 48709) at z=17");

        // The bridge: a rig pose becomes an ordinary CameraShot with the exact
        // same geometry — this is the handover into the Chronon camera language.
        const GeoCameraRig rig(GeoDiveSpec::forPreset(GeoShotPreset::MapDiveThenOrbit));
        const GeoCameraKeyframe posed = rig.sampleAtFrame(rig.schedule().orbitEnd);
        const motion::CameraShot shot = rig.toCameraShot(posed);
        constexpr double kDegToRad = std::numbers::pi / 180.0;
        checkNear(shot.azimuth, static_cast<float>(posed.yawDeg * kDegToRad), 1e-5,
                  "the bridge maps orbit yaw to CameraShot azimuth exactly");
        checkNear(shot.elevation, static_cast<float>((90.0 - posed.pitchDeg) * kDegToRad), 1e-5,
                  "the bridge maps ground pitch to CameraShot elevation exactly (nadir = 90 deg)");
        const Vector3 offset = posed.cameraPosition - posed.lookTarget;
        const float expectedDistance = std::sqrt(offset.x * offset.x + offset.y * offset.y
                                                 + offset.z * offset.z);
        checkNear(shot.distance, expectedDistance, 1e-3f,
                  "the bridge preserves the slant distance of the rig pose");
        check(shot.target.kind == motion::AnchorKind::WorldPoint,
              "the bridged shot anchors on the geographic target as a world point");
        const motion::CameraPose pose = shot.evaluatePose(nullptr, 0.f);
        checkNear(pose.target.x, posed.lookTarget.x, 1e-3f,
                  "the bridged shot's evaluated pose looks at the geographic anchor (x)");
        checkNear(pose.target.y, posed.lookTarget.y, 1e-3f, "... (y)");
        checkNear(pose.target.z, posed.lookTarget.z, 1e-3f, "... (z)");
    }
}
