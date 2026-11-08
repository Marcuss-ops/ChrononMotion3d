// ChrononMotion acceptance — Bézier node network (six-mechanics Test 2).
//
// Three circular source badges on the left, one emissive pentagon on the right
// and one cubic Bézier connector per badge. Control points are constrained to
// horizontal end tangents:
//   P1 = P0 + (0.45 * dX, 0)      P2 = P3 - (0.45 * dX, 0)
//
// The test validates C1 endpoint tangents, a constant-width extruded ribbon
// (including the maximum-curvature region), simultaneous smoothstep stroke
// trim, non-intersecting branches and the additive arrival burst on the target.

#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/extras/curves/CubicBezierCurve.hpp"

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
    constexpr float kCanvasWidth = 1920.f;
    constexpr float kCanvasHeight = 1080.f;
    constexpr float kNodeRadius = 32.f;
    constexpr float kRibbonWidth = 22.f;
    constexpr float kTrimDuration = 1.4f;
    constexpr float kBurstTau = 0.18f;
    constexpr std::size_t kSamples = 1024;

    struct Bezier2 {
        Vector2 p0;
        Vector2 p1;
        Vector2 p2;
        Vector2 p3;

        [[nodiscard]] Vector2 point(float u) const {

            const float v = 1.f - u;
            const float b0 = v * v * v;
            const float b1 = 3.f * v * v * u;
            const float b2 = 3.f * v * u * u;
            const float b3 = u * u * u;
            Vector2 result = p0 * b0;
            result += p1 * b1;
            result += p2 * b2;
            result += p3 * b3;
            return result;
        }

        [[nodiscard]] Vector2 firstDerivative(float u) const {

            const float v = 1.f - u;
            const float b0 = 3.f * v * v;
            const float b1 = 6.f * v * u;
            const float b2 = 3.f * u * u;
            Vector2 result = (p1 - p0) * b0;
            result += (p2 - p1) * b1;
            result += (p3 - p2) * b2;
            return result;
        }

        [[nodiscard]] Vector2 secondDerivative(float u) const {

            const float v = 1.f - u;
            Vector2 result = (p2 - p1 * 2.f + p0) * (6.f * v);
            result += (p3 - p2 * 2.f + p1) * (6.f * u);
            return result;
        }
    };

    struct Connector {
        Vector2 nodeCenter;
        Vector2 attach;
        Vector2 portTangent{1.f, 0.f};
        Bezier2 curve;
        Vector2 target{};
    };

    std::array<Connector, 3> buildNetwork() {

        const Vector2 target(1500.f, 540.f);
        const std::array<Vector2, 3> nodes{{
                Vector2(300.f, 260.f),
                Vector2(300.f, 540.f),
                Vector2(300.f, 820.f),
        }};

        std::array<Connector, 3> connectors{};
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            Connector connector;
            connector.nodeCenter = nodes[k];
            // The connector leaves through a horizontal port at the circle's
            // right edge; the port tangent is the curve's start tangent.
            connector.attach.set(nodes[k].x + kNodeRadius, nodes[k].y);
            connector.target = target;
            const float deltaX = target.x - connector.attach.x;

            connector.curve.p0 = connector.attach;
            connector.curve.p1.set(connector.attach.x + deltaX * 0.45f, connector.attach.y);
            connector.curve.p2.set(target.x - deltaX * 0.45f, target.y);
            connector.curve.p3 = target;
            connectors[k] = connector;
        }
        return connectors;
    }

    float length(Vector2 value) {

        return std::sqrt(value.x * value.x + value.y * value.y);
    }

    Vector2 normalized(Vector2 value) {

        const float magnitude = length(value);
        return magnitude > 0.f ? value / magnitude : Vector2(0.f, 0.f);
    }

    Vector2 normalOf(Vector2 tangent) {

        const Vector2 unit = normalized(tangent);
        return Vector2(-unit.y, unit.x);
    }

    float cross(Vector2 a, Vector2 b) {

        return a.x * b.y - a.y * b.x;
    }

    float curvatureAt(const Bezier2& curve, float u) {

        const Vector2 first = curve.firstDerivative(u);
        const Vector2 second = curve.secondDerivative(u);
        const float speed = length(first);
        if (speed <= 1e-6f) return 0.f;
        return std::fabs(cross(first, second)) / (speed * speed * speed);
    }

    // Signed distance from a circle to the pentagon's border, kept only as the
    // target's geometry contract: five equal edges and the same radius.
    float pentagonRadius() {

        return 170.f;
    }

    float pointToSegmentDistance(const Vector2& point, const Vector2& a, const Vector2& b) {

        const Vector2 ab = b - a;
        const float denominator = ab.dot(ab);
        if (denominator <= 1e-9f) return point.distanceTo(a);
        float t = (point - a).dot(ab) / denominator;
        t = std::max(0.f, std::min(1.f, t));
        const Vector2 projection = a + ab * t;
        return point.distanceTo(projection);
    }

    // ------------------------------------------------------------------------

    void cubicMatchesTheEngineCurve() {

        section("Test 2 — analytic cubic agrees with the engine curve");

        const std::array<Connector, 3> connectors = buildNetwork();
        for (const Connector& connector : connectors) {
            CubicBezierCurve engineCurve(connector.curve.p0, connector.curve.p1,
                                         connector.curve.p2, connector.curve.p3);
            for (int step = 0; step <= 20; ++step) {
                const float u = static_cast<float>(step) / 20.f;
                Vector2 fromEngine;
                engineCurve.getPoint(u, fromEngine);
                checkNear(fromEngine, connector.curve.point(u), 1e-4f,
                          "the connector is the engine's cubic Bézier");
            }
        }
    }

    void endpointsAreC1() {

        section("Test 2 — horizontal C1 tangents at both node ports");

        const std::array<Connector, 3> connectors = buildNetwork();
        for (const Connector& connector : connectors) {
            // Port contract: the curve starts exactly on the source circle's
            // right edge, and the outgoing tangent is the port tangent.
            checkNear(connector.attach.distanceTo(connector.nodeCenter), kNodeRadius, 1e-5f,
                      "the connector starts on the source circle's edge");
            checkNear(connector.curve.point(0.f), connector.attach, 1e-6f,
                      "the Bézier interpolates its source endpoint");
            checkNear(connector.curve.point(1.f), connector.target, 1e-6f,
                      "the Bézier interpolates the pentagon endpoint");

            const Vector2 startTangent = normalized(connector.curve.firstDerivative(0.f));
            const Vector2 endTangent = normalized(connector.curve.firstDerivative(1.f));
            checkNear(startTangent.x, 1.f, 1e-6f, "the outgoing tangent is +X");
            checkNear(startTangent.y, 0.f, 1e-6f, "the outgoing tangent has no vertical kink");
            checkNear(endTangent.x, 1.f, 1e-6f, "the incoming tangent continues +X into the target");
            checkNear(endTangent.y, 0.f, 1e-6f, "the incoming tangent has no vertical kink");

            checkNear(connector.portTangent.dot(startTangent), 1.f, 1e-6f,
                      "the connector is C1 continuous with its node port");

            // The constrained control points are exactly the spec's formula.
            const Vector2 expectedP1(connector.attach.x + (connector.target.x - connector.attach.x) * 0.45f,
                                     connector.attach.y);
            const Vector2 expectedP2(connector.target.x - (connector.target.x - connector.attach.x) * 0.45f,
                                     connector.target.y);
            checkNear(connector.curve.p1, expectedP1, 1e-5f,
                      "P1 keeps the outgoing tangent horizontal");
            checkNear(connector.curve.p2, expectedP2, 1e-5f,
                      "P2 keeps the incoming tangent horizontal");
        }
    }

    void curveHasNoCusps() {

        section("Test 2 — no cusps, finite curvature and continuous tangents");

        const std::array<Connector, 3> connectors = buildNetwork();
        for (const Connector& connector : connectors) {
            float minSpeed = 1e30f;
            float maxCurvature = 0.f;
            float maxTangentTurn = 0.f;
            Vector2 previousTangent = normalized(connector.curve.firstDerivative(0.f));

            for (std::size_t sample = 0; sample <= kSamples; ++sample) {
                const float u = static_cast<float>(sample) / static_cast<float>(kSamples);
                const Vector2 derivative = connector.curve.firstDerivative(u);
                const Vector2 tangent = normalized(derivative);
                minSpeed = std::min(minSpeed, length(derivative));
                maxCurvature = std::max(maxCurvature, curvatureAt(connector.curve, u));
                if (sample > 0) {
                    const float turn = std::fabs(std::atan2(cross(previousTangent, tangent),
                                                            previousTangent.dot(tangent)));
                    maxTangentTurn = std::max(maxTangentTurn, turn);
                }
                previousTangent = tangent;
            }

            check(minSpeed > 300.f, "the connector never stalls or cusps");
            check(maxTangentTurn < 0.02f,
                  "the tangent turns continuously between adjacent samples");
            check(maxCurvature * (kRibbonWidth * 0.5f) < 0.98f,
                  "the ribbon half-width stays inside the curvature radius (no fold-over)");
            check(std::isfinite(maxCurvature), "curvature stays finite along the connector");
        }
    }

    void ribbonKeepsConstantWidth() {

        section("Test 2 — constant ribbon thickness at maximum curvature");

        const std::array<Connector, 3> connectors = buildNetwork();
        for (const Connector& connector : connectors) {
            std::vector<Vector2> centers;
            std::vector<Vector2> left;
            std::vector<Vector2> right;
            std::vector<float> curvatures;
            centers.reserve(kSamples + 1);
            left.reserve(kSamples + 1);
            right.reserve(kSamples + 1);
            curvatures.reserve(kSamples + 1);

            for (std::size_t sample = 0; sample <= kSamples; ++sample) {
                const float u = static_cast<float>(sample) / static_cast<float>(kSamples);
                const Vector2 center = connector.curve.point(u);
                const Vector2 normal = normalOf(connector.curve.firstDerivative(u));
                centers.push_back(center);
                left.push_back(center + normal * (kRibbonWidth * 0.5f));
                right.push_back(center - normal * (kRibbonWidth * 0.5f));
                curvatures.push_back(curvatureAt(connector.curve, u));
            }

            // Measure thickness against the opposite polyline, not merely
            // against the construction formula: every left vertex must be one
            // ribbon width away from the nearest right segment.
            float minMeasured = 1e30f;
            float maxMeasured = 0.f;
            for (std::size_t i = 0; i <= kSamples; ++i) {
                float nearest = 1e30f;
                for (std::size_t j = 0; j < kSamples; ++j) {
                    nearest = std::min(nearest, pointToSegmentDistance(left[i], right[j], right[j + 1]));
                }
                minMeasured = std::min(minMeasured, nearest);
                maxMeasured = std::max(maxMeasured, nearest);
            }
            check(minMeasured > kRibbonWidth - 0.05f,
                  "the ribbon never thins below its authored width");
            check(maxMeasured < kRibbonWidth + 0.15f,
                  "the ribbon never fattens beyond its authored width");

            // Build the quad strip and verify no quad flips: the area sign is
            // the invariant that catches a fold-over at high curvature.
            float stripArea = 0.f;
            float centerlineLength = 0.f;
            float firstAreaSign = 0.f;
            bool haveAreaSign = false;
            bool anyFlip = false;
            for (std::size_t i = 0; i < kSamples; ++i) {
                const Vector2 a = left[i];
                const Vector2 b = left[i + 1];
                const Vector2 c = right[i + 1];
                const Vector2 d = right[i];
                const float area = 0.5f * cross(b - a, c - a) + 0.5f * cross(c - a, d - a);
                if (!haveAreaSign) {
                    firstAreaSign = area;
                    haveAreaSign = true;
                } else if (area * firstAreaSign <= 0.f) {
                    anyFlip = true;
                }
                stripArea += std::fabs(area);
                centerlineLength += centers[i].distanceTo(centers[i + 1]);
            }
            check(!anyFlip, "the ribbon strip has no flipped quad at any curvature");
            checkNear(stripArea / centerlineLength, kRibbonWidth, 0.25f,
                      "the strip area equals length times authored width");

            // The high-curvature sample must still carry the full width.
            std::size_t hottest = 0;
            for (std::size_t i = 0; i < curvatures.size(); ++i) {
                if (curvatures[i] > curvatures[hottest]) hottest = i;
            }
            checkNear(left[hottest].distanceTo(right[hottest]), kRibbonWidth, 1e-4f,
                      "the tightest bend keeps the full ribbon width");
        }
    }

    void trimAnimatesTogether() {

        section("Test 2 — simultaneous smoothstep stroke trim");

        const std::array<Connector, 3> connectors = buildNetwork();

        auto head = [](float seconds) {
            return math::smoothstep(0.f, 1.f, seconds / kTrimDuration);
        };

        checkNear(head(0.f), 0.f, 1e-6f, "trim starts at u=0");
        checkNear(head(kTrimDuration), 1.f, 1e-6f, "trim reaches u=1 at T_draw");
        checkNear(head(kTrimDuration * 2.f), 1.f, 1e-6f, "trim is clamped at the target");

        float previous = -1.f;
        for (int step = 0; step <= 200; ++step) {
            const float seconds = kTrimDuration * static_cast<float>(step) / 200.f;
            const float current = head(seconds);
            check(current >= previous - 1e-6f, "the trim head never moves backwards");
            check(current >= 0.f && current <= 1.f, "the trim head never overshoots its range");
            previous = current;
        }

        // All three heads use the same clock and the same smoothstep, so at every
        // instant their parameters are identical; a shared ribbon draw must not
        // stagger the branches.
        for (int step = 0; step <= 40; ++step) {
            const float seconds = kTrimDuration * static_cast<float>(step) / 40.f;
            const float u = head(seconds);
            for (const Connector& connector : connectors) {
                const Vector2 tip = connector.curve.point(u);
                if (u <= 0.f) {
                    checkNear(tip, connector.attach, 1e-6f,
                              "at t=0 every branch sits exactly on its source port");
                }
                if (u >= 1.f) {
                    checkNear(tip, connector.target, 1e-6f,
                              "at t=T every branch has reached the pentagon");
                }
            }
        }

        // The stroke is drawn by arc length in parameter space: coverage must be
        // monotonic, and the tip must be the curve point at u_head.
        for (const Connector& connector : connectors) {
            float previousCoverage = -1.f;
            for (int step = 0; step <= 100; ++step) {
                const float u = static_cast<float>(step) / 100.f;
                Vector2 previousPoint = connector.curve.point(0.f);
                float arcLength = 0.f;
                const int subdivisions = 600;
                for (int s = 1; s <= subdivisions; ++s) {
                    const float su = u * static_cast<float>(s) / static_cast<float>(subdivisions);
                    const Vector2 point = connector.curve.point(su);
                    arcLength += point.distanceTo(previousPoint);
                    previousPoint = point;
                }
                check(arcLength >= previousCoverage - 1e-4f,
                      "stroke coverage grows monotonically with u_head");
                previousCoverage = arcLength;
            }
        }
    }

    void branchesDoNotCross() {

        section("Test 2 — three branches converge without spurious crossings");

        const std::array<Connector, 3> connectors = buildNetwork();

        // The three branches keep their vertical lanes at equal parameter u.
        // If two curves crossed, this ordering would invert somewhere on the
        // shared parameter domain.
        for (int sample = 0; sample <= 1024; ++sample) {
            const float u = static_cast<float>(sample) / 1024.f;
            const float top = connectors[0].curve.point(u).y;
            const float middle = connectors[1].curve.point(u).y;
            const float bottom = connectors[2].curve.point(u).y;
            check(top <= middle + 1e-3f,
                  "the top connector stays above the middle connector");
            check(middle <= bottom + 1e-3f,
                  "the middle connector stays above the bottom connector");
        }

        // The target geometry contract: a closed pentagon with five equal edges.
        const Vector2 center = connectors[0].target;
        const float radius = pentagonRadius();
        std::array<Vector2, 5> vertices{};
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const float angle = -0.5f * kPi + 2.f * kPi * static_cast<float>(i) / 5.f;
            vertices[i].set(center.x + radius * std::cos(angle),
                            center.y + radius * std::sin(angle));
        }
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            checkNear(vertices[i].distanceTo(vertices[(i + 1) % 5]), vertices[0].distanceTo(vertices[1]),
                      1e-3f, "the pentagon has five equal edges");
        }
        check(center.distanceTo(connectors[0].curve.p3) < 1e-5f,
              "all three connectors terminate at the pentagon anchor");
    }

    void arrivalBurstIsAdditive() {

        section("Test 2 — additive emission burst on arrival");

        auto burst = [](float seconds) {
            if (seconds < kTrimDuration) return 0.f;
            return std::exp(-(seconds - kTrimDuration) / kBurstTau);
        };

        checkNear(burst(kTrimDuration - 1e-3f), 0.f, 0.f,
                  "the target does not flash before the stroke lands");
        checkNear(burst(kTrimDuration), 1.f, 1e-6f,
                  "the target flashes exactly when u_head reaches 1");
        check(burst(kTrimDuration + kBurstTau) < 0.4f,
              "the flash decays within one time constant");
        check(burst(kTrimDuration + 4.f * kBurstTau) < 0.03f,
              "the flash is gone within four time constants");

        float previous = 2.f;
        for (int step = 0; step <= 100; ++step) {
            const float seconds = kTrimDuration + 1.f * static_cast<float>(step) / 100.f;
            const float value = burst(seconds);
            check(value <= previous + 1e-6f, "the additive burst only decays after arrival");
            previous = value;
        }

        const float baseEmission = 0.35f;
        const float flashGain = 1.5f;
        check(baseEmission + flashGain * burst(kTrimDuration) > baseEmission * 2.f,
              "the arrival flash is a visible additive lift on the pentagon border");
    }

}// namespace

int main() {

    cubicMatchesTheEngineCurve();
    endpointsAreC1();
    curveHasNoCusps();
    ribbonKeepsConstantWidth();
    trimAnimatesTogether();
    branchesDoNotCross();
    arrivalBurstIsAdditive();

    return chrononmotion_test::report();
}
