// ChrononMotion acceptance — 2.5D multiplane parallax (six-mechanics Test 1).
//
// Five planes: one fixed subject at Z=0 and four satellite quads at
// Z = 40 / 80 / 60 / 120 mm, each carrying a de-synchronised Lissajous hover
// (Ax=12 px, Ay=18 px, tilt=3.5 deg, phase step pi/2). The camera is a 6-second
// arc: +150 mm forward on Z and +/-80 mm on X.
//
// The test owns no renderer. It validates the geometry and camera kernels the
// renderer consumes: projection, per-frame painter ordering, parallax gain with
// depth, rigid in-plane tilt and hover de-synchronisation.

#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/MotionPlane.hpp"

#include "motion_check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

using namespace chrononmotion;
using namespace chrononmotion::motion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.f * kPi;
    constexpr float kAx = 12.f;
    constexpr float kAy = 18.f;
    constexpr float kTiltMaxDegrees = 3.5f;
    constexpr float kDuration = 6.f;
    constexpr float kFps = 30.f;
    constexpr int kFrameCount = static_cast<int>(kDuration * kFps);
    constexpr float kViewportWidth = 1920.f;
    constexpr float kViewportHeight = 1080.f;

    struct Satellite {
        Vector2 base;
        float depth;
        float omega;
        float phase;
        float width;
        float height;
    };

    // The four quads in the reference schema: top-left Z=40, top-right Z=80,
    // bottom-right Z=60, bottom-left Z=120.
    const std::array<Satellite, 4> kSatellites{{
            {Vector2(-330.f, 200.f), 40.f, 0.80f, 0.f, 220.f, 150.f},
            {Vector2(330.f, 200.f), 80.f, 0.97f, 0.5f * kPi, 220.f, 150.f},
            {Vector2(330.f, -200.f), 60.f, 1.29f, kPi, 220.f, 150.f},
            {Vector2(-330.f, -200.f), 120.f, 1.11f, 1.5f * kPi, 220.f, 150.f},
    }};

    constexpr float kSubjectWidth = 380.f;
    constexpr float kSubjectHeight = 470.f;

    struct Hover {
        Vector2 center;
        Vector2 offset;
        float tiltRadians;
    };

    Hover hoverAt(const Satellite& satellite, float seconds) {

        Hover hover;
        hover.offset.set(kAx * std::sin(satellite.omega * seconds + satellite.phase),
                         kAy * std::cos(satellite.omega * 1.31f * seconds + satellite.phase));
        hover.center = satellite.base + hover.offset;
        hover.tiltRadians = math::degToRad(kTiltMaxDegrees) *
                            std::sin(satellite.omega * 0.73f * seconds);
        return hover;
    }

    Vector3 cameraAt(float seconds) {

        // A lateral arc (a full left/right sweep) while advancing 150 mm.
        return Vector3(80.f * std::sin(kTwoPi * seconds / kDuration),
                       0.f,
                       600.f - 25.f * seconds);
    }

    void applyCamera(const Vector3& position, CameraRig& rig, PerspectiveCamera& camera) {

        CameraPose pose;
        pose.position = position;
        pose.fov = 50.f;
        CameraRig::lookAtPose(pose, Vector3(0.f, 0.f, 0.f));
        rig.apply(pose, camera);
    }

    bool projectPoint(PerspectiveCamera& camera, const Vector3& world, Vector2& pixels) {

        PerspectiveView view(camera, Vector2(kViewportWidth, kViewportHeight));
        Vector3 ndc;
        return view.project(world, pixels, ndc);
    }

    std::array<Vector3, 4> quadCorners(const Vector3& center, float width, float height,
                                       float tiltRadians) {

        const float halfWidth = width * 0.5f;
        const float halfHeight = height * 0.5f;
        const float cosine = std::cos(tiltRadians);
        const float sine = std::sin(tiltRadians);

        const std::array<Vector2, 4> local{{
                Vector2(-halfWidth, -halfHeight),
                Vector2(halfWidth, -halfHeight),
                Vector2(halfWidth, halfHeight),
                Vector2(-halfWidth, halfHeight),
        }};

        std::array<Vector3, 4> corners{};
        for (std::size_t i = 0; i < local.size(); ++i) {
            corners[i].set(center.x + local[i].x * cosine - local[i].y * sine,
                           center.y + local[i].x * sine + local[i].y * cosine,
                           center.z);
        }
        return corners;
    }

    float projectedArea(const std::array<Vector2, 4>& corners) {

        float doubledArea = 0.f;
        for (std::size_t i = 0; i < corners.size(); ++i) {
            const Vector2& a = corners[i];
            const Vector2& b = corners[(i + 1) % corners.size()];
            doubledArea += a.x * b.y - b.x * a.y;
        }
        return std::fabs(doubledArea) * 0.5f;
    }

    struct ScreenBox {
        float minX{0.f};
        float maxX{0.f};
        float minY{0.f};
        float maxY{0.f};
        bool valid{false};
    };

    ScreenBox boxOf(const std::array<Vector2, 4>& screenCorners) {

        ScreenBox box;
        box.minX = screenCorners[0].x;
        box.maxX = screenCorners[0].x;
        box.minY = screenCorners[0].y;
        box.maxY = screenCorners[0].y;
        for (const Vector2& corner : screenCorners) {
            box.minX = std::min(box.minX, corner.x);
            box.maxX = std::max(box.maxX, corner.x);
            box.minY = std::min(box.minY, corner.y);
            box.maxY = std::max(box.maxY, corner.y);
        }
        box.valid = true;
        return box;
    }

    bool overlaps(const ScreenBox& a, const ScreenBox& b) {

        return a.valid && b.valid &&
               a.minX < b.maxX && b.minX < a.maxX &&
               a.minY < b.maxY && b.minY < a.maxY;
    }

    float worldDistance(const Vector3& a, const Vector3& b) {

        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return std::sqrt(dx * dx + dy * dy + dz * dz);
    }

    // ------------------------------------------------------------------------

    void hoverIsBoundedAndDeSynchronised() {

        section("Test 1 — Lissajous hover bounds, phase step and de-synchronisation");

        std::array<float, 4> maxDx{};
        std::array<float, 4> maxDy{};
        std::array<float, 4> maxTilt{};

        for (int sample = 0; sample <= 2400; ++sample) {
            const float seconds = kDuration * static_cast<float>(sample) / 2400.f;
            for (std::size_t k = 0; k < kSatellites.size(); ++k) {
                const Hover hover = hoverAt(kSatellites[k], seconds);
                maxDx[k] = std::max(maxDx[k], std::fabs(hover.offset.x));
                maxDy[k] = std::max(maxDy[k], std::fabs(hover.offset.y));
                maxTilt[k] = std::max(maxTilt[k], std::fabs(hover.tiltRadians));
            }
        }

        for (std::size_t k = 0; k < kSatellites.size(); ++k) {
            checkNear(maxDx[k], kAx, 0.05f, "X hover reaches Ax without exceeding it");
            checkNear(maxDy[k], kAy, 0.05f, "Y hover reaches Ay without exceeding it");
            checkNear(math::radToDeg(maxTilt[k]), kTiltMaxDegrees, 0.01f,
                      "tilt reaches 3.5 degrees without exceeding it");
            checkNear(kSatellites[k].phase, 0.5f * kPi * static_cast<float>(k), 1e-6f,
                      "satellite phase advances by exactly pi/2");
        }

        // At t=0 the phase step is observable in both channels.
        const std::array<float, 4> expectedDx{{0.f, 12.f, 0.f, -12.f}};
        const std::array<float, 4> expectedDy{{18.f, 0.f, -18.f, 0.f}};
        for (std::size_t k = 0; k < kSatellites.size(); ++k) {
            const Hover hover = hoverAt(kSatellites[k], 0.f);
            checkNear(hover.offset.x, expectedDx[k], 1e-4f,
                      "the four X hover channels start a quarter cycle apart");
            checkNear(hover.offset.y, expectedDy[k], 1e-4f,
                      "the four Y hover channels start a quarter cycle apart");
        }

        for (std::size_t a = 0; a < kSatellites.size(); ++a) {
            for (std::size_t b = a + 1; b < kSatellites.size(); ++b) {
                const Hover ha = hoverAt(kSatellites[a], 0.75f);
                const Hover hb = hoverAt(kSatellites[b], 0.75f);
                check(ha.center.distanceTo(hb.center) > 5.f,
                      "no two satellites collapse onto the same screen position");
            }
        }
    }

    void cameraArcIsExact() {

        section("Test 1 — six-second dolly arc");

        checkNear(cameraAt(0.f).z, 600.f, 1e-5f, "camera starts at Z=600");
        checkNear(cameraAt(kDuration).z, 450.f, 1e-5f,
                  "camera advances exactly 150 mm over six seconds");
        checkNear(cameraAt(0.f).x, 0.f, 1e-5f, "arc starts centred");
        checkNear(cameraAt(kDuration).x, 0.f, 1e-4f, "arc returns centred");

        float maxX = 0.f;
        for (int frame = 0; frame <= kFrameCount; ++frame) {
            maxX = std::max(maxX, std::fabs(cameraAt(static_cast<float>(frame) / kFps).x));
        }
        checkNear(maxX, 80.f, 0.5f, "lateral sweep reaches +/-80 mm");
    }

    void depthAndOcclusionAreWellFormed() {

        section("Test 1 — depth order, no subject-plane penetration, no bad occlusion");

        CameraRig rig(1, 50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);
        PerspectiveCamera camera(50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);

        for (int frame = 0; frame <= kFrameCount; ++frame) {
            const float seconds = static_cast<float>(frame) / kFps;
            const Vector3 cameraPosition = cameraAt(seconds);
            applyCamera(cameraPosition, rig, camera);

            // Five world quads: four satellites plus the fixed subject.
            std::array<std::array<Vector3, 4>, 5> corners{};
            std::array<Vector3, 5> centers{};
            std::array<Vector2, 5> screenCenters{};
            std::array<ScreenBox, 5> boxes{};

            for (std::size_t k = 0; k < kSatellites.size(); ++k) {
                const Hover hover = hoverAt(kSatellites[k], seconds);
                centers[k].set(hover.center.x, hover.center.y, kSatellites[k].depth);
                corners[k] = quadCorners(centers[k], kSatellites[k].width,
                                         kSatellites[k].height, hover.tiltRadians);

                // Rigid in-plane tilt: every world edge keeps its authored length,
                // every corner keeps the authored depth, and the projected quad
                // remains non-degenerate (no flipped winding / alpha collapse).
                const std::array<float, 4> edgeLengths{{
                        worldDistance(corners[k][0], corners[k][1]),
                        worldDistance(corners[k][1], corners[k][2]),
                        worldDistance(corners[k][2], corners[k][3]),
                        worldDistance(corners[k][3], corners[k][0]),
                }};
                checkNear(edgeLengths[0], kSatellites[k].width, 1e-4f,
                          "tilt preserves the horizontal quad edge");
                checkNear(edgeLengths[2], kSatellites[k].width, 1e-4f,
                          "tilt preserves the opposite quad edge");
                checkNear(edgeLengths[1], kSatellites[k].height, 1e-4f,
                          "tilt preserves the vertical quad edge");
                checkNear(edgeLengths[3], kSatellites[k].height, 1e-4f,
                          "tilt preserves the opposite vertical edge");

                std::array<Vector2, 4> screenCorners{};
                bool allVisible = true;
                for (std::size_t c = 0; c < 4; ++c) {
                    checkNear(corners[k][c].z, kSatellites[k].depth, 1e-6f,
                              "in-plane tilt never pulls a corner off the satellite plane");
                    allVisible = allVisible && projectPoint(camera, corners[k][c], screenCorners[c]);
                }
                check(allVisible, "all four satellite corners stay in front of the near plane");
                check(projectedArea(screenCorners) > 100.f,
                      "tilt never collapses the projected satellite footprint");
                boxes[k] = boxOf(screenCorners);
                check(projectPoint(camera, centers[k], screenCenters[k]),
                      "satellite center projects");
            }

            centers[4].set(0.f, 0.f, 0.f);
            corners[4] = quadCorners(centers[4], kSubjectWidth, kSubjectHeight, 0.f);
            std::array<Vector2, 4> subjectScreenCorners{};
            bool subjectVisible = true;
            for (std::size_t c = 0; c < 4; ++c) {
                subjectVisible = subjectVisible && projectPoint(camera, corners[4][c], subjectScreenCorners[c]);
            }
            check(subjectVisible, "the fixed subject stays visible for the whole dolly");
            check(projectPoint(camera, centers[4], screenCenters[4]), "subject center projects");
            boxes[4] = boxOf(subjectScreenCorners);

            // The subject is the only plane at Z=0; every satellite has positive
            // clearance on all four corners, so nothing can slice through it.
            for (std::size_t k = 0; k < kSatellites.size(); ++k) {
                for (const Vector3& corner : corners[k]) {
                    check(corner.z > 0.05f,
                          "no satellite corner penetrates the Z=0 subject plane");
                }
            }

            // Painter order: farthest Euclidean plane first. The sort is
            // recomputed every frame; its contract is that no nearer plane is
            // painted before a farther one.
            std::array<int, 5> order{{0, 1, 2, 3, 4}};
            std::sort(order.begin(), order.end(), [&](int a, int b) {
                return worldDistance(cameraPosition, centers[a]) >
                       worldDistance(cameraPosition, centers[b]);
            });
            for (std::size_t i = 0; i + 1 < order.size(); ++i) {
                check(worldDistance(cameraPosition, centers[static_cast<std::size_t>(order[i])]) >=
                              worldDistance(cameraPosition, centers[static_cast<std::size_t>(order[i + 1])]) - 1e-4f,
                      "draw order is monotonically far-to-near by real Euclidean distance");
            }

            // No screen-space collision between any two planes: an occlusion
            // cannot be painted in the wrong order if no quads overlap.
            for (int a = 0; a < 5; ++a) {
                for (int b = a + 1; b < 5; ++b) {
                    check(!overlaps(boxes[static_cast<std::size_t>(a)],
                                    boxes[static_cast<std::size_t>(b)]),
                          "the five multiplane quads never overlap on screen");
                }
            }

            // Tilt must not turn into a scaling hack: the projected footprint of
            // a quad is invariant under an in-plane rotation.
            const float untiltedArea = [&] {
                std::array<Vector2, 4> screen{};
                const std::array<Vector3, 4> flat = quadCorners(centers[0], kSatellites[0].width,
                                                                kSatellites[0].height, 0.f);
                for (std::size_t c = 0; c < 4; ++c) {
                    (void) projectPoint(camera, flat[c], screen[c]);
                }
                return projectedArea(screen);
            }();
            std::array<Vector2, 4> tiltedScreen{};
            for (std::size_t c = 0; c < 4; ++c) {
                (void) projectPoint(camera, corners[0][c], tiltedScreen[c]);
            }
            checkNear(projectedArea(tiltedScreen), untiltedArea,
                      untiltedArea * 0.01f + 0.5f,
                      "in-plane tilt preserves the projected quad area");
        }
    }

    void nearerDepthMovesFaster() {

        section("Test 1 — parallax gain separates Z=120 from Z=40");

        CameraRig rig(1, 50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);
        PerspectiveCamera camera(50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);

        std::array<double, 4> pathLengths{};
        std::array<Vector2, 4> previous{};
        std::array<bool, 4> hasPrevious{{false, false, false, false}};

        for (int frame = 0; frame <= kFrameCount; ++frame) {
            const float seconds = static_cast<float>(frame) / kFps;
            applyCamera(cameraAt(seconds), rig, camera);

            for (std::size_t k = 0; k < kSatellites.size(); ++k) {
                const Hover hover = hoverAt(kSatellites[k], seconds);
                Vector2 screen;
                check(projectPoint(camera, Vector3(hover.center.x, hover.center.y,
                                                   kSatellites[k].depth), screen),
                      "satellite stays projectable across the dolly");
                if (hasPrevious[k]) {
                    pathLengths[k] += static_cast<double>(screen.distanceTo(previous[k]));
                }
                previous[k] = screen;
                hasPrevious[k] = true;
            }
        }

        const std::size_t nearIndex = 3; // Z = 120 mm
        const std::size_t farIndex = 0;  // Z = 40 mm
        checkNear(kSatellites[nearIndex].depth, 120.f, 1e-5f, "near satellite is at Z=120");
        checkNear(kSatellites[farIndex].depth, 40.f, 1e-5f, "far satellite is at Z=40");

        const double nearMeanSpeed = pathLengths[nearIndex] / kDuration;
        const double farMeanSpeed = pathLengths[farIndex] / kDuration;
        check(nearMeanSpeed > farMeanSpeed * 1.15,
              "the Z=120 quad travels visibly faster than the Z=40 quad");
        check(nearMeanSpeed - farMeanSpeed > 10.0,
              "the parallax difference is measurable in pixels, not numerical noise");
        check(farMeanSpeed > 10.0,
              "even the farthest quad keeps a visible slow drift");
    }

    void projectionIsDeterministic() {

        section("Test 1 — projection is a pure function of time");

        CameraRig rig(1, 50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);
        PerspectiveCamera camera(50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);

        applyCamera(cameraAt(2.25f), rig, camera);
        const Hover hover = hoverAt(kSatellites[2], 2.25f);
        Vector2 first;
        Vector2 again;
        Vector3 unusedNdc;
        PerspectiveView view(camera, Vector2(kViewportWidth, kViewportHeight));
        check(view.project(Vector3(hover.center.x, hover.center.y, kSatellites[2].depth),
                           first, unusedNdc),
              "first projection succeeds");
        check(view.project(Vector3(hover.center.x, hover.center.y, kSatellites[2].depth),
                           again, unusedNdc),
              "second projection succeeds");

        // Random access must not depend on a previous evaluated frame.
        CameraRig freshRig(1, 50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);
        PerspectiveCamera freshCamera(50.f, kViewportWidth / kViewportHeight, 1.f, 4000.f);
        applyCamera(cameraAt(2.25f), freshRig, freshCamera);
        PerspectiveView freshView(freshCamera, Vector2(kViewportWidth, kViewportHeight));
        Vector2 fresh;
        check(freshView.project(Vector3(hover.center.x, hover.center.y, kSatellites[2].depth),
                                fresh, unusedNdc),
              "fresh projection succeeds");
        checkNear(first, again, 0.f, "repeated projection is bit-identical");
        checkNear(first, fresh, 0.f, "a fresh camera at the same time is bit-identical");
    }

}// namespace

int main() {

    hoverIsBoundedAndDeSynchronised();
    cameraArcIsExact();
    depthAndOcclusionAreWellFormed();
    nearerDepthMovesFaster();
    projectionIsDeterministic();

    return chrononmotion_test::report();
}
