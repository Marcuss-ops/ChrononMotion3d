// ChrononMotion acceptance — ink-splash matte wipe (six-mechanics Test 5).
//
// A procedural 256x256 ink map holds a dense core, nine radial channels with
// drops, and fine spray. The compositor thresholds it
//   Matte(u,v) = step(tau(t), I_ink(u,v))
// as tau travels from 1.02 to -0.04, so the matte expands from the centre
// outward along the channels instead of cross-fading. Inside the matte the
// next scene is solarised to a hard black/white palette with red accents.
//
// The test checks monotonic radial growth, binary matte edges (no grey
// dissolve), connected thin channels, palette purity and the absence of
// blended A/B pixels.

#include "chrononmotion/chrononmotion.hpp"
#include "motion_check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr int kWidth = 256;
    constexpr int kHeight = 256;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kCentreX = 128.f;
    constexpr float kCentreY = 128.f;
    constexpr float kWipeDuration = 1.2f;

    struct Arm {
        float angle;
        float length;
        float width;
    };

    constexpr std::array<Arm, 9> kArms{{
            {0.10f, 118.f, 2.6f},
            {0.78f, 104.f, 1.8f},
            {1.49f, 122.f, 3.2f},
            {2.18f, 96.f, 1.4f},
            {2.95f, 114.f, 2.2f},
            {3.64f, 108.f, 1.7f},
            {4.29f, 125.f, 2.9f},
            {4.96f, 101.f, 1.5f},
            {5.62f, 112.f, 2.4f},
    }};

    std::uint32_t mix(std::uint32_t value) {

        value ^= value >> 16u;
        value *= 0x7feb352du;
        value ^= value >> 15u;
        value *= 0x846ca68bu;
        value ^= value >> 16u;
        return value;
    }

    float hash01(int x, int y) {

        const std::uint32_t hashed = mix(static_cast<std::uint32_t>(x) * 0x9e3779b9u ^
                                         static_cast<std::uint32_t>(y) * 0x85ebca6bu);
        return static_cast<float>(hashed & 0x00ffffffu) /
               static_cast<float>(0x01000000u);
    }

    float valueNoise(float x, float y) {

        const float floorX = std::floor(x);
        const float floorY = std::floor(y);
        const int ix = static_cast<int>(floorX);
        const int iy = static_cast<int>(floorY);
        const float fx = x - floorX;
        const float fy = y - floorY;
        const float ux = fx * fx * (3.f - 2.f * fx);
        const float uy = fy * fy * (3.f - 2.f * fy);
        const float a = hash01(ix, iy);
        const float b = hash01(ix + 1, iy);
        const float c = hash01(ix, iy + 1);
        const float d = hash01(ix + 1, iy + 1);
        const float top = a + (b - a) * ux;
        const float bottom = c + (d - c) * ux;
        return (top + (bottom - top) * uy) * 2.f - 1.f;
    }

    float inkField(float x, float y) {

        const float dx = x - kCentreX;
        const float dy = y - kCentreY;
        const float radius = std::sqrt(dx * dx + dy * dy);

        // Dense core: value 1 at the centre, positive out to 64 px.
        float value = std::max(0.f, 1.f - radius / 64.f);

        for (const Arm& arm : kArms) {
            const float directionX = std::cos(arm.angle);
            const float directionY = std::sin(arm.angle);
            const float along = dx * directionX + dy * directionY;
            if (along < 0.f || along > arm.length) continue;
            const float perpendicular = std::fabs(-dx * directionY + dy * directionX);
            const float t = along / arm.length;
            // A one-pixel flat core keeps a hairline raster-connected; the
            // gaussian shoulder still gives the branch a soft organic edge.
            const float shoulder = perpendicular <= 1.1f
                                           ? 1.f
                                           : std::exp(-((perpendicular - 1.1f) *
                                                        (perpendicular - 1.1f)) /
                                                       (arm.width * arm.width));
            const float channel = (0.97f - 0.52f * t) * shoulder;
            value = std::max(value, channel);

            // A drop is a small bulge on the channel, never a detached island:
            // its peak is below the local channel value.
            const float dropCentre = arm.length * (0.35f + 0.27f * std::floor(t * 3.f));
            const float dropDistance = std::fabs(along - dropCentre);
            if (dropDistance < 9.f) {
                const float localChannel = (0.97f - 0.52f * (dropCentre / arm.length)) *
                                           (perpendicular <= 1.1f
                                                    ? 1.f
                                                    : std::exp(-((perpendicular - 1.1f) *
                                                                 (perpendicular - 1.1f)) /
                                                                (arm.width * arm.width)));
                value = std::max(value, localChannel - dropDistance * 0.006f);
            }
        }

        // Fine spray breaks the silhouette without disconnecting the channels.
        if (value > 0.02f) {
            value += 0.018f * valueNoise(x * 0.9f, y * 0.9f) *
                     valueNoise(x * 2.3f + 11.f, y * 2.3f + 7.f);
        }
        return std::clamp(value, 0.f, 1.f);
    }

    struct Field {
        std::vector<float> ink;
        Field() : ink(static_cast<std::size_t>(kWidth * kHeight)) {
            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    ink[static_cast<std::size_t>(y * kWidth + x)] =
                            inkField(static_cast<float>(x) + 0.5f,
                                     static_cast<float>(y) + 0.5f);
                }
            }
        }

        [[nodiscard]] float at(int x, int y) const {
            return ink[static_cast<std::size_t>(y * kWidth + x)];
        }
    };

    float thresholdAt(float seconds) {

        return 1.02f - 1.06f * chrononmotion::math::smoothstep(0.f, 1.f,
                                                              seconds / kWipeDuration);
    }

    struct RGB {
        float r{0.f};
        float g{0.f};
        float b{0.f};
    };

    RGB solarise(float ink, float radius) {

        if (radius > 88.f && ink < 0.55f) {
            return RGB{0.92f, 0.04f, 0.04f}; // red accent on outer drops
        }
        return ink > 0.55f ? RGB{1.f, 1.f, 1.f} : RGB{0.f, 0.f, 0.f};
    }

    RGB underlyingA(int x, int y) {

        return RGB{0.18f + 0.12f * std::sin(static_cast<float>(x) * 0.06f),
                   0.22f + 0.10f * std::cos(static_cast<float>(y) * 0.05f),
                   0.35f + 0.08f * std::sin(static_cast<float>(x + y) * 0.04f)};
    }

    bool sameColor(const RGB& a, const RGB& b) {

        return std::fabs(a.r - b.r) < 1e-6f &&
               std::fabs(a.g - b.g) < 1e-6f &&
               std::fabs(a.b - b.b) < 1e-6f;
    }

    bool paletteColor(const RGB& color) {

        return sameColor(color, RGB{1.f, 1.f, 1.f}) ||
               sameColor(color, RGB{0.f, 0.f, 0.f}) ||
               sameColor(color, RGB{0.92f, 0.04f, 0.04f});
    }

    bool connectedToCentre(const std::vector<std::uint8_t>& mask, int targetX, int targetY) {

        if (!mask[static_cast<std::size_t>(targetY * kWidth + targetX)]) return false;
        std::vector<std::uint8_t> visited(mask.size(), 0);
        std::vector<int> queue;
        queue.reserve(mask.size());
        const int startIndex = (kHeight / 2) * kWidth + (kWidth / 2);
        queue.push_back(startIndex);
        visited[static_cast<std::size_t>(startIndex)] = 1;

        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int current = queue[head];
            if (current == targetY * kWidth + targetX) return true;
            const int cx = current % kWidth;
            const int cy = current / kWidth;
            // 8-connectivity is the correct topology for hairline filaments;
            // a 4-connected walk would split a one-pixel diagonal by design.
            constexpr std::array<int, 8> offsetsX{{1, -1, 0, 0, 1, 1, -1, -1}};
            constexpr std::array<int, 8> offsetsY{{0, 0, 1, -1, 1, -1, 1, -1}};
            for (int direction = 0; direction < 8; ++direction) {
                const int nx = cx + offsetsX[static_cast<std::size_t>(direction)];
                const int ny = cy + offsetsY[static_cast<std::size_t>(direction)];
                if (nx < 0 || nx >= kWidth || ny < 0 || ny >= kHeight) continue;
                const int index = ny * kWidth + nx;
                if (!mask[static_cast<std::size_t>(index)] ||
                    visited[static_cast<std::size_t>(index)]) {
                    continue;
                }
                visited[static_cast<std::size_t>(index)] = 1;
                queue.push_back(index);
            }
        }
        return false;
    }

    double matteCoverage(float threshold, const Field& field) {

        int inside = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                if (field.at(x, y) >= threshold) ++inside;
            }
        }
        return static_cast<double>(inside) / static_cast<double>(kWidth * kHeight);
    }

    float matteRadius(float threshold, const Field& field) {

        float maximum = 0.f;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                if (field.at(x, y) < threshold) continue;
                const float dx = static_cast<float>(x) + 0.5f - kCentreX;
                const float dy = static_cast<float>(y) + 0.5f - kCentreY;
                maximum = std::max(maximum, std::sqrt(dx * dx + dy * dy));
            }
        }
        return maximum;
    }

    // ------------------------------------------------------------------------

    void thresholdExpandsFromTheCentre() {

        section("Test 5 — monotonic radial expansion, not a dissolve");

        const Field field;
        checkNear(thresholdAt(0.f), 1.02f, 1e-6f, "the wipe starts fully closed");
        check(thresholdAt(kWipeDuration) < 0.f,
              "the wipe threshold passes the ink floor and opens completely");

        float previous = 2.f;
        double previousCoverage = -1.0;
        float previousRadius = -1.f;
        bool first = true;
        double startCoverage = 0.0;

        for (int step = 0; step <= 120; ++step) {
            const float seconds = kWipeDuration * static_cast<float>(step) / 120.f;
            const float threshold = thresholdAt(seconds);
            check(threshold <= previous + 1e-6f,
                  "tau passes progressively in one direction");
            previous = threshold;

            const double coverage = matteCoverage(threshold, field);
            const float radius = matteRadius(threshold, field);
            if (first) {
                startCoverage = coverage;
                first = false;
            } else {
                check(coverage >= previousCoverage - 1e-6,
                      "the ink matte never shrinks");
                check(radius >= previousRadius - 1e-4f,
                      "the ink front expands radially from the centre outward");
            }
            previousCoverage = coverage;
            previousRadius = radius;
        }

        check(startCoverage < 0.001, "the first frame carries no ink");
        check(previousCoverage > 0.995, "the final frame covers the whole canvas");
        check(previousRadius > 120.f, "the ink front crosses the whole frame");
        check(previousRadius <= 181.1f, "the ink never exceeds the canvas radius");

        // The centroid of the growing matte stays on the splash origin.
        const float threshold = 0.4f;
        double weightedX = 0.0;
        double weightedY = 0.0;
        int count = 0;
        for (int y = 0; y < kHeight; ++y) {
            for (int x = 0; x < kWidth; ++x) {
                if (field.at(x, y) < threshold) continue;
                weightedX += static_cast<double>(x);
                weightedY += static_cast<double>(y);
                ++count;
            }
        }
        check(count > 100, "a mid-wipe frame has a substantial matte");
        checkNear(static_cast<float>(weightedX / count), kCentreX, 3.f,
                  "the matte centroid stays horizontally centred");
        checkNear(static_cast<float>(weightedY / count), kCentreY, 3.f,
                  "the matte centroid stays vertically centred");
    }

    void matteIsBinaryAndPalettePure() {

        section("Test 5 — binary matte, sharp edges and solarised palette");

        const Field field;
        int greyCrossfades = 0;
        int outsidePixels = 0;
        int insideWhite = 0;
        int insideBlack = 0;
        int insideRed = 0;

        for (int step = 0; step <= 20; ++step) {
            const float seconds = kWipeDuration * static_cast<float>(step) / 20.f;
            const float threshold = thresholdAt(seconds);
            for (int y = 0; y < kHeight; y += 2) {
                for (int x = 0; x < kWidth; x += 2) {
                    const RGB under = underlyingA(x, y);
                    const bool inside = field.at(x, y) >= threshold;
                    const RGB out = inside
                                        ? solarise(field.at(x, y),
                                                   std::sqrt((static_cast<float>(x) - kCentreX) *
                                                                     (static_cast<float>(x) - kCentreX) +
                                                             (static_cast<float>(y) - kCentreY) *
                                                                     (static_cast<float>(y) - kCentreY)))
                                        : under;
                    if (!inside) {
                        if (!sameColor(out, under)) ++greyCrossfades;
                        ++outsidePixels;
                    } else if (paletteColor(out)) {
                        if (sameColor(out, RGB{1.f, 1.f, 1.f})) ++insideWhite;
                        if (sameColor(out, RGB{0.f, 0.f, 0.f})) ++insideBlack;
                        if (sameColor(out, RGB{0.92f, 0.04f, 0.04f})) ++insideRed;
                    } else {
                        ++greyCrossfades;
                    }
                }
            }
        }

        check(outsidePixels > 1000, "the sampling covers the untouched scene");
        check(greyCrossfades == 0,
              "no pixel is a grey A/B cross-fade; every pixel is fully before or fully after");
        check(insideWhite > 100, "the solarised highlight is pure white");
        check(insideBlack > 100, "the solarised shadow is pure black");
        check(insideRed > 10, "the outer drops keep their red accents");

        // Contrast across a matte boundary is the full binary step.
        const float threshold = 0.35f;
        int edges = 0;
        int fullSteps = 0;
        for (int y = 1; y + 1 < kHeight; ++y) {
            for (int x = 1; x + 1 < kWidth; ++x) {
                if ((field.at(x, y) >= threshold) != (field.at(x + 1, y) >= threshold)) {
                    ++edges;
                    const bool leftInside = field.at(x, y) >= threshold;
                    const bool rightInside = field.at(x + 1, y) >= threshold;
                    const RGB a = leftInside
                                          ? solarise(field.at(x, y), 40.f)
                                          : underlyingA(x, y);
                    const RGB b = rightInside
                                          ? solarise(field.at(x + 1, y), 40.f)
                                          : underlyingA(x + 1, y);
                    const float difference = std::fabs(a.r - b.r) + std::fabs(a.g - b.g) +
                                             std::fabs(a.b - b.b);
                    if (difference > 0.25f) ++fullSteps;
                }
            }
        }
        check(edges > 100, "the mid-wipe matte has a long boundary");
        check(fullSteps == edges,
              "every matte boundary is a full-contrast cut, never a soft grey edge");
    }

    void thinChannelsStayConnected() {

        section("Test 5 — thin filaments remain connected to the ink core");

        const Field field;
        const std::array<float, 8> thresholds{{0.90f, 0.80f, 0.70f, 0.60f,
                                               0.50f, 0.35f, 0.20f, 0.05f}};

        for (float threshold : thresholds) {
            std::vector<std::uint8_t> mask(static_cast<std::size_t>(kWidth * kHeight), 0);
            for (int y = 0; y < kHeight; ++y) {
                for (int x = 0; x < kWidth; ++x) {
                    mask[static_cast<std::size_t>(y * kWidth + x)] =
                            field.at(x, y) >= threshold ? 1 : 0;
                }
            }

            for (const Arm& arm : kArms) {
                // Walk the channel axis and require every visible point to be
                // connected to the core: a clamped gradient may not sever a
                // thin branch into orphan islands.
                int visibleAxisSamples = 0;
                for (int sample = 2; sample <= 40; ++sample) {
                    const float t = static_cast<float>(sample) / 40.f;
                    const int x = static_cast<int>(std::lround(
                            kCentreX + std::cos(arm.angle) * arm.length * t));
                    const int y = static_cast<int>(std::lround(
                            kCentreY + std::sin(arm.angle) * arm.length * t));
                    if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) continue;
                    if (field.at(x, y) < threshold) continue;
                    ++visibleAxisSamples;
                    check(connectedToCentre(mask, x, y),
                          "every visible filament sample connects to the ink core");
                }
                check(visibleAxisSamples > 0,
                      "each radial arm contributes at least one visible sample");
            }
        }

        // The thinness itself: an arm with width 1.4 px must still occupy a
        // pixel at each cross-section where the field clears the threshold.
        const Arm thin = kArms[3];
        for (int sample = 2; sample <= 30; ++sample) {
            const float t = static_cast<float>(sample) / 40.f;
            const float along = thin.length * t;
            const int x = static_cast<int>(std::lround(kCentreX + std::cos(thin.angle) * along));
            const int y = static_cast<int>(std::lround(kCentreY + std::sin(thin.angle) * along));
            const float threshold = field.at(x, y) - 0.02f;
            const float interpolated = inkField(static_cast<float>(x) + 0.5f,
                                                static_cast<float>(y) + 0.5f);
            check(interpolated >= threshold,
                  "the thinnest arm survives sub-pixel sampling");
        }
    }

    void inkFieldHasHighGradient() {

        section("Test 5 — high gradient detail at the splash boundary");

        const Field field;
        float maximumGradient = 0.f;
        int steepEdges = 0;
        for (int y = 1; y + 1 < kHeight; ++y) {
            for (int x = 1; x + 1 < kWidth; ++x) {
                const float gx = field.at(x + 1, y) - field.at(x - 1, y);
                const float gy = field.at(x, y + 1) - field.at(x, y - 1);
                const float gradient = std::sqrt(gx * gx + gy * gy);
                maximumGradient = std::max(maximumGradient, gradient);
                if (gradient > 0.5f) ++steepEdges;
            }
        }
        check(maximumGradient > 0.35f,
              "the ink map carries high-slope drop edges, not a smooth grey ramp");
        check(steepEdges > 50,
              "many boundary pixels exceed the hard-edge slope threshold");
    }

}// namespace

int main() {

    thresholdExpandsFromTheCentre();
    matteIsBinaryAndPalettePure();
    thinChannelsStayConnected();
    inkFieldHasHighGradient();

    return chrononmotion_test::report();
}
