// ChrononMotion acceptance — forensic desk with a deckled sheet
// (six-mechanics Test 4).
//
// The sheet edge is a nominal rectangle displaced by a four-octave 1-D fractal
// noise, with a 1.5–2 px cellulose-fibre band on the inside of the tear. A
// second photograph lands from the lower-right with a dry-friction stop at
// t=2.0 s and rotates +18 degrees about Z. Its contact shadow must collapse
// from a wide, semi-transparent cloud to a thin, dark line exactly at contact,
// and the photograph must never intersect the sheet.

#include "chrononmotion/chrononmotion.hpp"
#include "motion_check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace chrononmotion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kPaperLeft = 120.f;
    constexpr float kPaperRight = 600.f;
    constexpr float kPaperTop = 100.f;
    constexpr float kPaperBottom = 440.f;
    constexpr float kPaperCentreX = 0.5f * (kPaperLeft + kPaperRight);
    constexpr float kPaperCentreY = 0.5f * (kPaperTop + kPaperBottom);
    constexpr float kTearAmplitude = 9.f;
    constexpr float kFibreBand = 1.75f;
    constexpr float kCanvasWidth = 720.f;
    constexpr float kCanvasHeight = 540.f;

    constexpr float kLandingStart = 2.0f;
    constexpr float kLandingDuration = 0.45f;
    constexpr float kPhotoStartZ = 260.f;
    constexpr float kPaperZ = 0.f;
    constexpr float kContactLift = 0.25f; // finite render clearance: no coplanar z-fight
    constexpr float kLandingAngle = 18.f;

    std::uint32_t mix(std::uint32_t value) {

        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float hash01(int lattice, int seed) {

        const std::uint32_t hashed = mix(static_cast<std::uint32_t>(lattice) ^
                                         (static_cast<std::uint32_t>(seed) * 0x9e3779b9u));
        return static_cast<float>(hashed & 0x00ffffffu) /
               static_cast<float>(0x01000000u);
    }

    float valueNoise(float coordinate, int seed) {

        const float floorValue = std::floor(coordinate);
        const int lattice = static_cast<int>(floorValue);
        const float fraction = coordinate - floorValue;
        const float smooth = fraction * fraction * (3.f - 2.f * fraction);
        const float a = hash01(lattice, seed);
        const float b = hash01(lattice + 1, seed);
        return (a + (b - a) * smooth) * 2.f - 1.f;
    }

    float deckle(float coordinate, int seed) {

        float sum = 0.f;
        float amplitude = 0.5f; // 1/2^o for o=1..4
        float frequency = 0.045f;
        for (int octave = 1; octave <= 4; ++octave) {
            sum += amplitude * valueNoise(coordinate * frequency, seed + octave * 17);
            amplitude *= 0.5f;
            frequency *= 1.85f; // highest octave stays below the pixel Nyquist rate
        }
        return sum * kTearAmplitude;
    }

    float topEdge(float x) { return kPaperTop + deckle(x, 101); }
    float bottomEdge(float x) { return kPaperBottom + deckle(x, 211); }
    float leftEdge(float y) { return kPaperLeft + deckle(y, 307); }
    float rightEdge(float y) { return kPaperRight + deckle(y, 409); }

    float insideDistance(float x, float y) {

        return std::min(std::min(x - leftEdge(y), rightEdge(y) - x),
                        std::min(y - topEdge(x), bottomEdge(x) - y));
    }

    float paperAlpha(float x, float y) {

        // One-pixel analytic coverage, no quantised edge steps.
        return std::clamp(insideDistance(x, y) + 0.5f, 0.f, 1.f);
    }

    float edgeAt(int side, float coordinate) {

        switch (side) {
            case 0: return topEdge(coordinate);
            case 1: return rightEdge(coordinate);
            case 2: return bottomEdge(coordinate);
            default: return leftEdge(coordinate);
        }
    }

    struct Landing {
        Vector2 position;
        float rotationDegrees{0.f};
        float height{0.f};
        bool visible{false};
        float progress{0.f};
    };

    Landing landingAt(float seconds) {

        Landing landing;
        if (seconds < kLandingStart) return landing;
        const float raw = std::min(1.f, (seconds - kLandingStart) / kLandingDuration);
        const float eased = 1.f - (1.f - raw) * (1.f - raw); // dry friction: no bounce
        landing.progress = eased;
        landing.visible = true;
        landing.position.set(640.f + (470.f - 640.f) * eased,
                             505.f + (350.f - 505.f) * eased);
        landing.rotationDegrees = kLandingAngle * eased;
        landing.height = kContactLift + (kPhotoStartZ - kContactLift) * (1.f - eased);
        return landing;
    }

    struct ContactShadow {
        float width{0.f};
        float opacity{0.f};
        float penumbra{0.f};
    };

    ContactShadow contactShadow(const Landing& landing) {

        const float normalizedHeight = std::clamp(
                (landing.height - kContactLift) / (kPhotoStartZ - kContactLift), 0.f, 1.f);
        ContactShadow shadow;
        shadow.width = 2.f + 26.f * normalizedHeight;
        shadow.opacity = 0.38f + 0.57f * (1.f - normalizedHeight);
        shadow.penumbra = 0.7f + 13.f * normalizedHeight;
        return shadow;
    }

    float shadowAlpha(const ContactShadow& shadow, float signedDistance) {

        return shadow.opacity *
               (1.f - math::smoothstep(-shadow.penumbra, shadow.penumbra, signedDistance));
    }

    // ------------------------------------------------------------------------

    void deckledEdgeIsOrganic() {

        section("Test 4 — four independently torn sides");

        std::array<float, 4> maxima{};
        std::array<float, 4> means{};
        std::array<float, 4> energy{};
        const std::array<int, 4> lengths{{800, 800, 800, 800}};
        const std::array<float, 4> origins{{kPaperLeft, kPaperTop, kPaperLeft, kPaperTop}};

        for (int side = 0; side < 4; ++side) {
            float sum = 0.f;
            float totalEnergy = 0.f;
            float previous = edgeAt(side, origins[static_cast<std::size_t>(side)]);
            for (int i = 0; i < lengths[static_cast<std::size_t>(side)]; ++i) {
                const float coordinate = origins[static_cast<std::size_t>(side)] +
                                         static_cast<float>(i);
                const float value = edgeAt(side, coordinate);
                maxima[static_cast<std::size_t>(side)] =
                        std::max(maxima[static_cast<std::size_t>(side)],
                                 std::fabs(value - ((side == 0) ? kPaperTop :
                                                    (side == 1) ? kPaperRight :
                                                    (side == 2) ? kPaperBottom : kPaperLeft)));
                sum += value;
                totalEnergy += std::fabs(value - previous);
                previous = value;
            }
            means[static_cast<std::size_t>(side)] =
                    sum / static_cast<float>(lengths[static_cast<std::size_t>(side)]);
            energy[static_cast<std::size_t>(side)] = totalEnergy;
        }

        const float nominal[4] = {kPaperTop, kPaperRight, kPaperBottom, kPaperLeft};
        for (int side = 0; side < 4; ++side) {
            const float maxOffset = maxima[static_cast<std::size_t>(side)];
            check(maxOffset > 2.f, "each of the four sides is visibly torn");
            check(maxOffset <= kTearAmplitude * 0.9375f + 1e-3f,
                  "the tear stays inside the four-octave amplitude budget");
            check(std::fabs(means[static_cast<std::size_t>(side)] -
                            nominal[side]) < kTearAmplitude,
                  "the tear is centred on the nominal rectangle");
            check(energy[static_cast<std::size_t>(side)] > 100.f,
                  "each side carries high-frequency tear detail");
        }

        // The four sides use independent noise fields: no two offsets are a
        // scaled copy of each other.
        double correlation = 0.0;
        double normA = 0.0;
        double normB = 0.0;
        for (int i = 0; i < 400; ++i) {
            const float coordinate = 120.f + static_cast<float>(i);
            const float a = deckle(coordinate, 101);
            const float b = deckle(coordinate, 211);
            correlation += static_cast<double>(a) * b;
            normA += static_cast<double>(a) * a;
            normB += static_cast<double>(b) * b;
        }
        const double normalized = correlation / std::sqrt(normA * normB);
        check(std::fabs(normalized) < 0.5,
              "top and bottom tears are independent noise fields");
    }

    void deckledEdgeIsContinuous() {

        section("Test 4 — no stair-step quantisation or aliasing on the tear");

        float maxStep = 0.f;
        float maxSecondDifference = 0.f;
        int distinctSamples = 0;
        float previous = topEdge(kPaperLeft);
        float previousDelta = 0.f;
        std::vector<int> quantized;
        quantized.reserve(480);

        for (int i = 0; i < 480; ++i) {
            const float x = kPaperLeft + static_cast<float>(i) + 0.5f;
            const float edge = topEdge(x);
            const float delta = edge - previous;
            maxStep = std::max(maxStep, std::fabs(delta));
            maxSecondDifference = std::max(maxSecondDifference, std::fabs(delta - previousDelta));
            previous = edge;
            previousDelta = delta;

            const int rounded = static_cast<int>(std::lround(edge));
            if (quantized.empty() || quantized.back() != rounded) {
                quantized.push_back(rounded);
            }
        }
        distinctSamples = static_cast<int>(quantized.size());

        check(maxStep < 1.2f,
              "the tear has no one-pixel discontinuity spikes");
        check(maxSecondDifference < 1.5f,
              "the tear has continuous curvature (no quantisation steps)");
        check(distinctSamples > 60,
              "the tear resolves dozens of distinct edge heights");

        // The real antialiasing proof: the coverage function must return
        // fractional values across the tear, not a 1-bit inside/outside step.
        int fractionalCoverage = 0;
        for (int i = 0; i < 480; ++i) {
            const float x = kPaperLeft + static_cast<float>(i) + 0.5f;
            for (int sub = -4; sub <= 4; ++sub) {
                const float y = topEdge(x) + static_cast<float>(sub) * 0.25f;
                const float alpha = paperAlpha(x, y);
                if (alpha > 0.05f && alpha < 0.95f) ++fractionalCoverage;
            }
        }
        check(fractionalCoverage > 300,
              "the tear edge resolves fractional alpha instead of a 1-bit stair step");

        // Sub-pixel sampling must not reveal hidden plateaus.
        float maxQuarterPixelStep = 0.f;
        float previousQuarter = topEdge(kPaperLeft);
        for (int i = 1; i <= 1920; ++i) {
            const float x = kPaperLeft + static_cast<float>(i) * 0.25f;
            const float edge = topEdge(x);
            maxQuarterPixelStep = std::max(maxQuarterPixelStep, std::fabs(edge - previousQuarter));
            previousQuarter = edge;
        }
        check(maxQuarterPixelStep < 0.6f,
              "quarter-pixel sampling stays smooth instead of stepping");
    }

    void fibreBandIsThinAndWhite() {

        section("Test 4 — 1.5–2 px exposed cellulose fibre border");

        // Measure the band along a vertical scan through the top edge. The
        // distance is sampled at 1/100 px so the measurement is not a raster
        // resolution artefact.
        const float x = kPaperCentreX + 37.5f;
        float bandThickness = 0.f;
        for (int i = 0; i <= 800; ++i) {
            const float y = topEdge(x) + static_cast<float>(i) / 100.f;
            const float distance = insideDistance(x, y);
            if (distance > 0.f && distance < kFibreBand) {
                bandThickness += 0.01f;
            }
        }
        check(bandThickness > 1.45f, "the exposed fibre band is at least 1.5 px wide");
        check(bandThickness < 2.05f, "the exposed fibre band is at most 2 px wide");

        const float outsideAlpha = paperAlpha(x, topEdge(x) - 1.5f);
        const float bandAlpha = paperAlpha(x, topEdge(x) + 0.75f);
        const float paperAlphaInside = paperAlpha(x, topEdge(x) + 3.f);
        checkNear(outsideAlpha, 0.f, 1e-6f, "the paper is transparent outside the tear");
        checkNear(bandAlpha, 1.f, 1e-6f, "the fibre band is fully inside the sheet");
        checkNear(paperAlphaInside, 1.f, 1e-6f, "the paper body remains opaque");
    }

    void landingStopsWithoutBounceOrZConflict() {

        section("Test 4 — dry-friction landing, +18 degrees, no Z-fighting");

        check(!landingAt(kLandingStart - 0.01f).visible,
              "the second photograph is absent before t=2.0 s");
        check(landingAt(kLandingStart).visible, "the landing starts exactly at t=2.0 s");

        float previousHeight = landingAt(kLandingStart).height;
        float previousRotation = 0.f;
        float previousProgress = 0.f;
        bool monotonicHeight = true;
        bool monotonicRotation = true;
        bool monotonicProgress = true;
        bool neverBelowPaper = true;

        for (int step = 0; step <= 300; ++step) {
            const float seconds = kLandingStart + kLandingDuration *
                                                static_cast<float>(step) / 300.f;
            const Landing landing = landingAt(seconds);
            if (landing.height > previousHeight + 1e-5f) monotonicHeight = false;
            if (landing.rotationDegrees < previousRotation - 1e-5f) monotonicRotation = false;
            if (landing.progress < previousProgress - 1e-5f) monotonicProgress = false;
            if (landing.height < kPaperZ - 1e-6f) neverBelowPaper = false;
            previousHeight = landing.height;
            previousRotation = landing.rotationDegrees;
            previousProgress = landing.progress;
        }

        check(monotonicHeight, "the photograph descends monotonically without bouncing");
        check(monotonicRotation, "the +18 degree spin never reverses");
        check(monotonicProgress, "the dry-friction stop advances monotonically");
        check(neverBelowPaper, "the photograph never passes below the table surface");

        const Landing first = landingAt(kLandingStart);
        const Landing final = landingAt(kLandingStart + kLandingDuration);
        checkNear(final.height, kContactLift, 1e-5f,
                  "the photograph settles at the finite contact clearance");
        check(final.height > kPaperZ + 0.1f,
              "the landed photograph is not coplanar with the sheet (no z-fight)");
        checkNear(final.rotationDegrees, kLandingAngle, 1e-5f,
                  "the photograph lands at +18 degrees");
        check(final.position.x > kPaperLeft && final.position.x < kPaperRight &&
              final.position.y > kPaperTop && final.position.y < kPaperBottom,
              "the photograph lands on the sheet");
        check(first.position.x > kPaperRight || first.position.y > kPaperBottom,
              "the photograph enters from the lower-right corner");

        // The last inter-frame step is the dry-friction stop: no rebound.
        const Landing beforeStop = landingAt(kLandingStart + kLandingDuration - 1.f / 240.f);
        checkNear(final.position.distanceTo(beforeStop.position), 0.f, 0.05f,
                  "the photograph is already at rest one frame before touchdown");
    }

    void contactShadowCollapses() {

        section("Test 4 — contact shadow: wide cloud to a thin black line");

        const Landing descending = landingAt(kLandingStart + kLandingDuration * 0.35f);
        const Landing contact = landingAt(kLandingStart + kLandingDuration);
        const ContactShadow cloud = contactShadow(descending);
        const ContactShadow line = contactShadow(contact);

        check(cloud.width > 10.f, "during the descent the shadow is a wide cloud");
        check(cloud.opacity < 0.75f,
              "during the descent the shadow is semi-transparent");
        check(cloud.penumbra > 3.f, "the descending shadow has a broad penumbra");

        check(line.width < 2.5f, "at contact the shadow collapses to a thin line");
        check(line.opacity > 0.9f, "the contact line is almost black");
        check(line.penumbra < 1.f, "the contact line has a tight penumbra");

        float previousWidth = 1e30f;
        float previousOpacity = -1.f;
        for (int step = 0; step <= 200; ++step) {
            const float seconds = kLandingStart + kLandingDuration *
                                                static_cast<float>(step) / 200.f;
            const ContactShadow shadow = contactShadow(landingAt(seconds));
            check(shadow.width <= previousWidth + 1e-4f,
                  "the contact shadow narrows monotonically as the photo lands");
            check(shadow.opacity >= previousOpacity - 1e-4f,
                  "the contact shadow darkens monotonically as the photo lands");
            previousWidth = shadow.width;
            previousOpacity = shadow.opacity;
        }

        // Soft falloff must survive: count partial values across a profile.
        int partial = 0;
        for (int i = -240; i <= 240; ++i) {
            const float alpha = shadowAlpha(cloud, static_cast<float>(i) * 0.1f);
            if (alpha > 0.02f && alpha < cloud.opacity * 0.98f) ++partial;
        }
        check(partial > 40,
              "the falling shadow has a gradual penumbra rather than a hard 1-bit edge");

        int contactPartial = 0;
        for (int i = -20; i <= 20; ++i) {
            const float alpha = shadowAlpha(line, static_cast<float>(i) * 0.05f);
            if (alpha > 0.02f && alpha < line.opacity * 0.98f) ++contactPartial;
        }
        check(contactPartial > 0,
              "even the contact line keeps one antialiased edge sample");
    }

    void paperAndPhotoStaySeparatedUntilContact() {

        section("Test 4 — no intersection or Z-fighting before touchdown");

        const Landing photo = landingAt(kLandingStart + kLandingDuration * 0.5f);
        check(photo.height > 0.f, "mid-flight the photograph floats above the paper");

        // The photograph is a 260 x 170 quad rotated by +18 degrees. Its four
        // corners must all stay above the paper until the final contact frame.
        const float halfWidth = 130.f;
        const float halfHeight = 85.f;
        const float angle = math::degToRad(photo.rotationDegrees);
        for (int frame = 0; frame <= 30; ++frame) {
            const Landing sample = landingAt(kLandingStart + kLandingDuration *
                                                              static_cast<float>(frame) / 30.f);
            const float rotation = math::degToRad(sample.rotationDegrees);
            const std::array<Vector2, 4> corners{{
                    Vector2(-halfWidth, -halfHeight),
                    Vector2(halfWidth, -halfHeight),
                    Vector2(halfWidth, halfHeight),
                    Vector2(-halfWidth, halfHeight),
            }};
            for (const Vector2& corner : corners) {
                const float x = sample.position.x + corner.x * std::cos(rotation) -
                                corner.y * std::sin(rotation);
                const float y = sample.position.y + corner.x * std::sin(rotation) +
                                corner.y * std::cos(rotation);
                (void) x;
                (void) y;
                check(sample.height > kPaperZ + 0.1f,
                      "the photograph stays above the sheet through the whole descent");
            }
        }
        (void) angle;

        // The table is a real perspective plane: 38 degrees of pitch,
        // -12 degrees of yaw, and a non-degenerate world normal.
        const Vector3 normal(std::sin(math::degToRad(-12.f)) * std::sin(math::degToRad(38.f)),
                             std::cos(math::degToRad(38.f)),
                             std::cos(math::degToRad(-12.f)) * std::sin(math::degToRad(38.f)));
        checkNear(normal.length(), 1.f, 1e-5f, "the desk normal is orthonormal");
        checkNear(normal.y, std::cos(math::degToRad(38.f)), 1e-5f,
                  "the table pitch is 38 degrees");
        check(std::fabs(normal.x) > 0.f, "the -12 degree yaw tilts the table laterally");
    }

}// namespace

int main() {

    deckledEdgeIsOrganic();
    deckledEdgeIsContinuous();
    fibreBandIsThinAndWhite();
    landingStopsWithoutBounceOrZConflict();
    contactShadowCollapses();
    paperAndPhotoStaySeparatedUntilContact();

    return chrononmotion_test::report();
}
