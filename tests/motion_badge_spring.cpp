// ChrononMotion acceptance — spring badge over a defocused field
// (six-mechanics Test 3).
//
// The badge pop-in is the closed-form underdamped step response
//   s(t) = 1 - e^(-z w_n t) [cos(w_d t) + (z w_n / w_d) sin(w_d t)]
// at w_n = 18 rad/s. The setup also names z=0.65 and a 15% overshoot, which
// are mutually inconsistent: z=0.65 produces 6.8% overshoot. This test keeps
// both facts honest by pinning the physical response of the named z and the
// z that actually yields the acceptance target 1.15x.
//
// The raster half validates the compositing contract: a defocused background
// loses high-frequency energy at sigma=24 px while the midground stays sharp,
// and the badge casts a soft penumbra onto both the silhouette and the
// background, offset down-right for a top-left key light.

#include "chrononmotion/chrononmotion.hpp"
#include "motion_check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace chrononmotion;
using chrononmotion_test::check;
using chrononmotion_test::checkNear;
using chrononmotion_test::section;

namespace {

    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kOmegaN = 18.f;
    constexpr float kZetaSpecified = 0.65f;
    constexpr float kBadgeZ = 40.f;
    constexpr float kMidgroundZ = 0.f;
    constexpr float kBackgroundZ = -200.f;
    constexpr float kBackgroundBlurSigma = 24.f;
    constexpr float kCornerRadius = 16.f;
    constexpr float kCocAtBackground = 24.f;

    constexpr int kCanvasWidth = 480;
    constexpr int kCanvasHeight = 270;
    const Vector2 kBadgeCenter(240.f, 135.f);
    const Vector2 kIconCenter(-80.f, 0.f);
    constexpr float kIconRadius = 36.f;
    const Vector2 kPillCenter(35.f, 0.f);
    const Vector2 kPillHalf(105.f, 32.f);

    float zetaForOvershoot(float overshoot) {

        const float logMagnitude = std::log(overshoot);
        return -logMagnitude / std::sqrt(kPi * kPi + logMagnitude * logMagnitude);
    }

    float springScale(float seconds, float zeta) {

        if (seconds <= 0.f) return 0.f;
        const float omegaD = kOmegaN * std::sqrt(1.f - zeta * zeta);
        const float envelope = std::exp(-zeta * kOmegaN * seconds);
        return 1.f - envelope * (std::cos(omegaD * seconds) +
                                 (zeta * kOmegaN / omegaD) * std::sin(omegaD * seconds));
    }

    struct SpringAudit {
        float peak{0.f};
        float peakTime{0.f};
        float lastOutsideBand{0.f};
        int extrema{0};
        float secondExtremumTime{0.f};
    };

    SpringAudit auditSpring(float zeta, float band) {

        SpringAudit audit;
        constexpr float step = 0.0005f;
        constexpr float horizon = 2.f;

        float previous = springScale(0.f, zeta);
        float current = springScale(step, zeta);
        if (current > audit.peak) {
            audit.peak = current;
            audit.peakTime = step;
        }
        for (float seconds = 2.f * step; seconds <= horizon; seconds += step) {
            const float next = springScale(seconds, zeta);
            if (next > audit.peak) {
                audit.peak = next;
                audit.peakTime = seconds;
            }
            if ((current > previous && current > next) ||
                (current < previous && current < next)) {
                ++audit.extrema;
                audit.secondExtremumTime = seconds;
            }
            if (std::fabs(next - 1.f) > band) audit.lastOutsideBand = seconds;
            previous = current;
            current = next;
        }
        return audit;
    }

    float sdCircle(const Vector2& point, const Vector2& center, float radius) {

        return point.distanceTo(center) - radius;
    }

    float sdRoundedBox(const Vector2& point, const Vector2& half, float radius) {

        const float qx = std::fabs(point.x) - half.x + radius;
        const float qy = std::fabs(point.y) - half.y + radius;
        const float outsideX = std::max(qx, 0.f);
        const float outsideY = std::max(qy, 0.f);
        return std::min(std::max(qx, qy), 0.f) +
               std::sqrt(outsideX * outsideX + outsideY * outsideY) - radius;
    }

    float badgeDistance(const Vector2& point) {

        return std::min(sdCircle(point, kIconCenter, kIconRadius),
                        sdRoundedBox(point - kPillCenter, kPillHalf, kCornerRadius));
    }

    float sdMidground(const Vector2& point) {

        return sdRoundedBox(point - Vector2(240.f, 140.f), Vector2(110.f, 80.f), 26.f);
    }

    std::vector<float> makeBackgroundTexture() {

        std::vector<float> texture(static_cast<std::size_t>(kCanvasWidth) *
                                   static_cast<std::size_t>(kCanvasHeight));
        for (int y = 0; y < kCanvasHeight; ++y) {
            for (int x = 0; x < kCanvasWidth; ++x) {
                const float bars = 0.5f + 0.25f * std::sin(static_cast<float>(x) * 0.42f) +
                                   0.18f * std::sin(static_cast<float>(y) * 0.77f);
                const float checker = ((x / 9 + y / 7) % 2 == 0) ? 0.16f : -0.16f;
                const float noise = 0.10f * std::sin(static_cast<float>(x * 13 + y * 7) * 0.11f);
                texture[static_cast<std::size_t>(y * kCanvasWidth + x)] =
                        std::clamp(bars + checker + noise, 0.f, 1.f);
            }
        }
        return texture;
    }

    void gaussianBlur(const std::vector<float>& source, std::vector<float>& destination,
                      int width, int height, float sigma) {

        const int radius = std::max(1, static_cast<int>(std::ceil(sigma * 3.f)));
        std::vector<float> kernel(static_cast<std::size_t>(2 * radius + 1));
        float sum = 0.f;
        for (int i = -radius; i <= radius; ++i) {
            const float value = std::exp(-static_cast<float>(i * i) /
                                         (2.f * sigma * sigma));
            kernel[static_cast<std::size_t>(i + radius)] = value;
            sum += value;
        }
        for (float& value : kernel) value /= sum;

        std::vector<float> temporary(source.size());
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float accumulated = 0.f;
                for (int i = -radius; i <= radius; ++i) {
                    const int sx = std::clamp(x + i, 0, width - 1);
                    accumulated += source[static_cast<std::size_t>(y * width + sx)] *
                                   kernel[static_cast<std::size_t>(i + radius)];
                }
                temporary[static_cast<std::size_t>(y * width + x)] = accumulated;
            }
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                float accumulated = 0.f;
                for (int i = -radius; i <= radius; ++i) {
                    const int sy = std::clamp(y + i, 0, height - 1);
                    accumulated += temporary[static_cast<std::size_t>(sy * width + x)] *
                                   kernel[static_cast<std::size_t>(i + radius)];
                }
                destination[static_cast<std::size_t>(y * width + x)] = accumulated;
            }
        }
    }

    float gradientEnergy(const std::vector<float>& values, int width, int height) {

        double energy = 0.0;
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x + 1 < width; ++x) {
                energy += std::fabs(values[static_cast<std::size_t>(y * width + x + 1)] -
                                    values[static_cast<std::size_t>(y * width + x)]);
            }
        }
        for (int y = 0; y + 1 < height; ++y) {
            for (int x = 0; x < width; ++x) {
                energy += std::fabs(values[static_cast<std::size_t>((y + 1) * width + x)] -
                                    values[static_cast<std::size_t>(y * width + x)]);
            }
        }
        return static_cast<float>(energy /
                                  static_cast<double>(width * height * 2));
    }

    float cocRadius(float depth, float focusDepth) {

        if (depth == focusDepth) return 0.f;
        return kCocAtBackground * std::fabs(depth - focusDepth) /
               std::max(std::fabs(kBackgroundZ - focusDepth), 1.f);
    }

    Vector2 shadowOffset(float receiverDepth) {

        // Key light from the upper-left: the shadow falls down-right. The
        // separation grows with the distance between badge and receiver.
        const float separation = kBadgeZ - receiverDepth;
        return Vector2(0.6f, 0.8f) * (separation * 0.35f);
    }

    float shadowCore(const Vector2& pixel, float receiverDepth) {

        const float separation = kBadgeZ - receiverDepth;
        const float penumbra = 1.5f + 0.06f * separation;
        const Vector2 local = pixel - kBadgeCenter - shadowOffset(receiverDepth);
        const float distance = badgeDistance(local);
        return 1.f - math::smoothstep(-penumbra * 0.5f, penumbra * 0.5f, distance);
    }

    float midgroundCoverage(const Vector2& pixel) {

        return 1.f - math::smoothstep(-0.5f, 0.5f, sdMidground(pixel));
    }

    float firstTransitionWidth(float receiverDepth, int row, bool midground) {

        bool entered = false;
        int start = 0;
        for (int x = 0; x < kCanvasWidth; ++x) {
            const Vector2 pixel(static_cast<float>(x), static_cast<float>(row));
            const float receiver = midground ? midgroundCoverage(pixel) : 1.f;
            const float alpha = shadowCore(pixel, receiverDepth) * receiver;
            if (!entered && alpha > 0.05f) {
                entered = true;
                start = x;
            } else if (entered && alpha > 0.45f) {
                return static_cast<float>(x - start);
            }
        }
        return 0.f;
    }

    void springResponseMatchesTheClosedForm() {

        section("Test 3 — damped harmonic oscillator response");

        checkNear(springScale(0.f, kZetaSpecified), 0.f, 0.f,
                  "the badge starts from scale zero");
        check(springScale(0.01f, kZetaSpecified) > 0.f,
              "the badge immediately begins to grow");

        const SpringAudit specified = auditSpring(kZetaSpecified, 0.02f);
        checkNear(specified.peak, 1.0682f, 0.002f,
                  "z=0.65 has the physical 6.8% overshoot of the closed form");
        check(specified.peak < 1.15f,
              "z=0.65 cannot reach 1.15x (the setup's 15% annotation is inconsistent)");
        check(specified.peak > 1.05f,
              "the specified spring still overshoots visibly");
        check(specified.lastOutsideBand <= 0.4f + 0.005f,
              "the specified spring settles inside 2% by 0.4 s");
        check(specified.extrema >= 2,
              "the spring produces at least two organic micro-oscillations");

        const float zetaFifteen = zetaForOvershoot(0.15f);
        checkNear(zetaFifteen, 0.5176f, 0.002f,
                  "the damping ratio for a real 15% overshoot is ~0.5176");
        const SpringAudit accepted = auditSpring(zetaFifteen, 0.05f);
        checkNear(accepted.peak, 1.15f, 0.003f,
                  "the acceptance badge overshoots to 1.15x");
        check(accepted.lastOutsideBand <= 0.4f + 0.005f,
              "the 1.15x badge settles inside 5% by 0.4 s");
        check(accepted.extrema >= 2 && accepted.secondExtremumTime <= 0.45f,
              "the 1.15x badge completes two micro-oscillations by 0.45 s");

        // No discontinuity, no negative scale and no runaway on the way.
        for (int step = 0; step <= 2000; ++step) {
            const float seconds = 2.f * static_cast<float>(step) / 2000.f;
            const float scale = springScale(seconds, zetaFifteen);
            check(scale >= 0.f && scale <= 1.16f,
                  "badge scale stays finite and inside its overshoot envelope");
        }
    }

    void badgeGeometryIsRounded() {

        section("Test 3 — circular icon and 16 px pill geometry");

        checkNear(sdCircle(kIconCenter, kIconCenter, kIconRadius), -kIconRadius, 1e-5f,
                  "the icon centre is inside its circular silhouette");
        checkNear(sdCircle(kIconCenter + Vector2(kIconRadius, 0.f),
                           kIconCenter, kIconRadius), 0.f, 1e-5f,
                  "the circular icon has the authored radius");

        // The rounded corner's 45-degree point lies on the border.
        const Vector2 innerCorner = kPillCenter +
                                    Vector2(kPillHalf.x - kCornerRadius, kPillHalf.y - kCornerRadius);
        const Vector2 diagonal = Vector2(0.70710678f, 0.70710678f) * kCornerRadius;
        checkNear(sdRoundedBox(innerCorner + diagonal - kPillCenter,
                               kPillHalf, kCornerRadius), 0.f, 0.02f,
                  "the pill's 16 px rounded corner is tangent to its edges");
        checkNear(sdRoundedBox(kPillCenter - kPillCenter, kPillHalf, kCornerRadius),
                  -kPillHalf.y, 1e-4f,
                  "the pill centre sits a full half-height from its nearest edge");

        // The two shapes form one connected badge, not two separate pills.
        const float gap = badgeDistance(Vector2(0.f, 0.f));
        check(gap < -10.f, "icon and pill overlap into one connected badge");
        check(badgeDistance(Vector2(kPillCenter.x + kPillHalf.x, 0.f)) > -0.1f,
              "the badge reaches the authored pill width");
    }

    void backgroundBokehAndMidgroundFocus() {

        section("Test 3 — sigma=24 background defocus and sharp midground");

        checkNear(cocRadius(kMidgroundZ, 0.f), 0.f, 0.f,
                  "the midground sits exactly on the focal plane");
        checkNear(cocRadius(kBackgroundZ, 0.f), kBackgroundBlurSigma, 0.01f,
                  "the background carries the authored 24 px circle of confusion");
        check(cocRadius(-120.f, 0.f) < cocRadius(kBackgroundZ, 0.f),
              "defocus grows with distance from the focal plane");

        const std::vector<float> sharp = makeBackgroundTexture();
        std::vector<float> blurred(sharp.size());
        gaussianBlur(sharp, blurred, kCanvasWidth, kCanvasHeight, kBackgroundBlurSigma);

        const float sharpEnergy = gradientEnergy(sharp, kCanvasWidth, kCanvasHeight);
        const float blurredEnergy = gradientEnergy(blurred, kCanvasWidth, kCanvasHeight);
        check(blurredEnergy < sharpEnergy * 0.2f,
              "the 24 px bokeh removes most of the background's high-frequency detail");

        double sharpMean = 0.0;
        double blurredMean = 0.0;
        for (std::size_t i = 0; i < sharp.size(); ++i) {
            sharpMean += sharp[i];
            blurredMean += blurred[i];
        }
        sharpMean /= static_cast<double>(sharp.size());
        blurredMean /= static_cast<double>(blurred.size());
        checkNear(static_cast<float>(blurredMean), static_cast<float>(sharpMean), 0.02f,
                  "defocus preserves the background's average luminance");
    }

    void badgeShadowSpansBothReceivers() {

        section("Test 3 — soft shadow on the silhouette and the blurred background");

        int midAffected = 0;
        int backgroundAffected = 0;
        int midPartial = 0;
        int backgroundPartial = 0;
        double weightedX = 0.0;
        double weightedY = 0.0;
        double weight = 0.0;

        for (int y = 0; y < kCanvasHeight; ++y) {
            for (int x = 0; x < kCanvasWidth; ++x) {
                const Vector2 pixel(static_cast<float>(x), static_cast<float>(y));
                const float coverage = midgroundCoverage(pixel);
                const float midAlpha = shadowCore(pixel, kMidgroundZ) * coverage;
                const float backgroundAlpha = shadowCore(pixel, kBackgroundZ) * (1.f - coverage);

                if (midAlpha > 0.01f) {
                    ++midAffected;
                    if (midAlpha < 0.54f) ++midPartial;
                }
                if (backgroundAlpha > 0.01f) {
                    ++backgroundAffected;
                    if (backgroundAlpha < 0.54f) ++backgroundPartial;
                    weightedX += static_cast<double>(backgroundAlpha * static_cast<float>(x));
                    weightedY += static_cast<double>(backgroundAlpha * static_cast<float>(y));
                    weight += backgroundAlpha;
                }
            }
        }

        check(midAffected > 200,
              "the badge casts a visible shadow onto the sharp midground silhouette");
        check(backgroundAffected > 200,
              "the badge casts a visible shadow onto the defocused background");
        check(midPartial > 150,
              "the silhouette shadow is a penumbra, not a 1-bit cut-out");
        check(backgroundPartial > 500,
              "the background shadow has an even wider soft falloff");
        check(weight > 0.0, "the background shadow has energy");

        const double meanX = weightedX / weight;
        const double meanY = weightedY / weight;
        check(meanX > kBadgeCenter.x + 10.0,
              "the top-left key light throws the shadow to the right");
        check(meanY > kBadgeCenter.y + 10.0,
              "the top-left key light throws the shadow downward");

        const float midWidth = firstTransitionWidth(kMidgroundZ, 146, true);
        const float backgroundWidth = firstTransitionWidth(kBackgroundZ, 202, false);
        check(midWidth > 1.f, "the silhouette shadow has a measurable soft edge");
        check(backgroundWidth > midWidth * 2.f,
              "the farther receiver receives the wider penumbra");
        check(backgroundWidth > 4.5f,
              "the background penumbra decays over several pixels");
    }

}// namespace

int main() {

    springResponseMatchesTheClosedForm();
    badgeGeometryIsRounded();
    backgroundBokehAndMidgroundFocus();
    badgeShadowSpansBothReceivers();

    return chrononmotion_test::report();
}
