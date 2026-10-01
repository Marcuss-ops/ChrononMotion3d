// ChrononMotion — cinematic innovation preview renderer (v2).
//
// Same trajectories as v1 (the real sampled CameraPose / EyeTraceDirector /
// autoReframeRig data — never invented), a far richer look:
//
//   - HDR-ish accumulation: sky with sun disc + halo, per-pixel ground plane
//     with distance fade, analytic soft shadows, Blinn-Phong card shading
//     with key/fill/rim lights and rounded corners
//   - 2-tap shutter blur from the REAL analytic camera derivatives
//     (CameraRig::sampleDerivative) — the analytical-shutter thesis, on screen
//   - post: bloom threshold/blur, filmic tone curve, teal&orange grade,
//     vignette, film grain, letterbox-free 16:9
//
// Output: PPM frame sequences for ffmpeg.

#include "chrononmotion/chrononmotion.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {

    constexpr float kFps = 30.f;
    constexpr float kPi = 3.14159265358979323846f;

    float at(int frame) { return static_cast<float>(frame) / kFps; }

    struct Vec3f {
        float x = 0.f, y = 0.f, z = 0.f;
    };

    Vec3f operator+(Vec3f a, Vec3f b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
    Vec3f operator-(Vec3f a, Vec3f b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    Vec3f operator*(Vec3f a, float s) { return {a.x * s, a.y * s, a.z * s}; }
    Vec3f operator*(float s, Vec3f a) { return a * s; }
    Vec3f operator*(Vec3f a, Vec3f b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
    float dot3(Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    Vec3f clamp01(Vec3f v) {
        return {std::clamp(v.x, 0.f, 1.f), std::clamp(v.y, 0.f, 1.f), std::clamp(v.z, 0.f, 1.f)};
    }

    Vec3f toV(const Vector3& v) { return {v.x, v.y, v.z}; }

    // ------------------------------------------------------------ primitives

    struct RGB {
        std::uint8_t r, g, b;
    };

    struct Canvas {
        int w = 0, h = 0;
        std::vector<RGB> px;

        Canvas(int width, int height) : w(width), h(height), px(width * height) {}

        void set(int x, int y, RGB c) {
            if (x >= 0 && y >= 0 && x < w && y < h) px[y * w + x] = c;
        }

        void savePpm(const std::string& path) const {
            std::ofstream out(path, std::ios::binary);
            out << "P6\n" << w << ' ' << h << "\n255\n";
            out.write(reinterpret_cast<const char*>(px.data()),
                      static_cast<std::streamsize>(px.size() * sizeof(RGB)));
        }
    };

    RGB toRgb(Vec3f c) {
        c = clamp01(c);
        return {static_cast<std::uint8_t>(c.x * 255.f + 0.5f),
                static_cast<std::uint8_t>(c.y * 255.f + 0.5f),
                static_cast<std::uint8_t>(c.z * 255.f + 0.5f)};
    }

    // ------------------------------------------------------------- scene def

    Vec3f sunDir = [] {
        Vec3f d{-0.45f, 0.65f, -0.42f};
        const float l = std::sqrt(dot3(d, d));
        return d * (1.f / l);
    }();
    const Vec3f kSunColor{1.35f, 1.22f, 0.95f};
    const Vec3f kSkyZenith{0.16f, 0.30f, 0.55f};
    const Vec3f kSkyHorizon{0.55f, 0.62f, 0.74f};
    const Vec3f kGroundColor{0.30f, 0.33f, 0.40f};
    const float kGroundY = -2.f;

    struct Card {
        Vector3 center{0.f, 0.f, 0.f};
        float halfW = 1.f, halfH = 1.f;
        Vec3f albedo{0.8f, 0.8f, 0.8f};
        float emissive = 0.f;
    };

    // Ray through a pixel for a given pose.
    struct Ray {
        Vec3f origin;
        Vec3f dir;
    };

    Ray pixelRay(const CameraPose& pose, int w, int h, float px, float py) {
        Quaternion q = pose.orientation;
        if (q.lengthSq() > 0.f) q.normalize();
        Vector3 right = Vector3(1.f, 0.f, 0.f).applyQuaternion(q);
        Vector3 up = Vector3(0.f, 1.f, 0.f).applyQuaternion(q);
        Vector3 fwd = Vector3(0.f, 0.f, -1.f).applyQuaternion(q);

        const float tanHalf = std::tan(pose.fov * 0.5f * kPi / 180.f);
        const float ndcX = (2.f * px / w - 1.f);
        const float ndcY = (1.f - 2.f * py / h);
        const float aspect = static_cast<float>(w) / h;

        Vec3f d = toV(fwd) + toV(right) * (ndcX * tanHalf * aspect) + toV(up) * (ndcY * tanHalf);
        const float len = std::sqrt(dot3(d, d));
        d = d * (1.f / len);
        return {toV(pose.position), d};
    }

    // Sky with sun disc + halo.
    Vec3f skyColor(const Ray& ray) {
        const float t = std::clamp(ray.dir.y * 0.5f + 0.5f, 0.f, 1.f);
        Vec3f sky = kSkyHorizon * (1.f - t) + kSkyZenith * t;
        const float cosSun = dot3(ray.dir, sunDir);
        sky = sky + kSunColor * (0.22f * std::pow(std::max(cosSun, 0.f), 24.f)); // halo
        sky = sky + kSunColor * (std::pow(std::max(cosSun, 0.f), 1600.f) * 6.f); // disc
        return sky;
    }

    // Analytic soft shadow of a card onto a ground point: shear the card
    // center along the light onto the ground plane, then a rounded-ellipse
    // falloff around the sheared footprint.
    float groundShadow(const Vec3f& p, const Card& card) {
        Vec3f lp = toV(card.center);
        if (lp.y <= kGroundY) return 1.f;
        const float drop = (lp.y - kGroundY) / sunDir.y;
        const float cx = lp.x - sunDir.x * drop;
        const float cz = lp.z - sunDir.z * drop;
        const float dx = (p.x - cx) / (card.halfW + 1.1f);
        const float dz = (p.z - cz) / (card.halfH + 1.1f);
        const float d = std::sqrt(dx * dx + dz * dz);
        if (d >= 1.f) return 1.f;
        return 0.42f + 0.58f * std::clamp(d * d * d, 0.f, 1.f);
    }

    Vec3f groundColor(const Ray& ray, const std::vector<Card>& cards) {
        if (ray.dir.y >= -1e-4f) return skyColor(ray);
        const float t = (kGroundY - ray.origin.y) / ray.dir.y;
        if (t < 0.f) return skyColor(ray);
        Vec3f p = ray.origin + ray.dir * t;

        // Grid lines fading with distance.
        Vec3f c = kGroundColor;
        const float cell = 2.f;
        const float fx = std::fabs(p.x / cell - std::floor(p.x / cell + 0.5f)) * cell;
        const float fz = std::fabs(p.z / cell - std::floor(p.z / cell + 0.5f)) * cell;
        const float lineW = 0.045f + t * 0.0012f;
        const float line = 1.f - std::clamp(std::min(fx, fz) / lineW, 0.f, 1.f);
        c = c + Vec3f{0.10f, 0.16f, 0.24f} * line;

        // Sky reflection approximation: fresnel-ish sheen.
        const float fres = std::pow(1.f - std::clamp(-ray.dir.y, 0.f, 1.f), 3.f);
        c = c * (1.f - 0.35f * fres) + skyColor(Ray{p, {ray.dir.x, -ray.dir.y * 0.35f, ray.dir.z}}) * (0.35f * fres);

        float shadow = 1.f;
        for (const Card& card : cards) shadow = std::min(shadow, groundShadow(p, card));
        c = c * shadow;

        const float fade = std::exp(-t * 0.030f);
        return c * fade + (kSkyHorizon * 0.55f) * (1.f - fade);
    }

    struct CardHit {
        bool hit = false;
        float t = 0.f;
        Vec3f normal{0.f, 0.f, 1.f};
        Vec3f local{0.f, 0.f, 0.f};
        const Card* card = nullptr;
    };

    CardHit intersectCard(const Ray& ray, const Card& card) {
        // Cards face +Z in world (billboards standing on the ground).
        const float denom = dot3(ray.dir, Vec3f{0.f, 0.f, 1.f});
        if (std::fabs(denom) < 1e-5f) return {};
        const float t = (card.center.z - ray.origin.z) / denom;
        if (t <= 0.f) return {};
        Vec3f p = ray.origin + ray.dir * t;
        const float lx = p.x - card.center.x;
        const float ly = p.y - card.center.y;
        const float rx = lx / card.halfW;
        const float ry = ly / card.halfH;
        // Sharp rectangles: the content model of these scenes is cards, not
        // pills. Corner radius in normalized units (tiny, screen-design-like).
        constexpr float kCorner = 0.10f;
        const float ax = std::fabs(rx), ay = std::fabs(ry);
        if (ax > 1.f || ay > 1.f) return {};
        if (ax > 1.f - kCorner && ay > 1.f - kCorner) {
            const float dx = ax - (1.f - kCorner);
            const float dy = ay - (1.f - kCorner);
            if (std::sqrt(dx * dx + dy * dy) > kCorner) return {};
        }
        return {true, t, Vec3f{0.f, 0.f, denom > 0.f ? -1.f : 1.f}, Vec3f{rx, ry, 0.f}, &card};
    }

    Vec3f shadeCard(const CardHit& hit, const std::vector<Card>& cards) {
        const Card& card = *hit.card;
        Vec3f n = hit.normal;
        Vec3f p = hit.local;

        // Key light (sun) with a soft self-term; fill from the sky; rim from behind.
        const float diff = std::max(dot3(n, sunDir * (-1.f)), 0.f);
        Vec3f color = card.albedo * (kSunColor * (0.95f * diff));
        color = color + card.albedo * kSkyZenith * 0.55f;                      // fill
        const float rim = std::pow(1.f - std::clamp(dot3(n, sunDir), 0.f, 1.f), 2.2f);
        color = color + kSunColor * (0.30f * rim);

        // Rectangular design: a crisp border frame + a subtle center sheen,
        // like a UI card catching the key light. The border glows slightly
        // (the emissive term makes it readable against dark backdrops).
        const float edge = std::max(std::fabs(p.x), std::fabs(p.y));
        const float border = 1.f - std::clamp((0.955f - edge) / 0.045f, 0.f, 1.f);
        color = color * (0.86f + 0.14f * (1.f - border));
        color = color + Vec3f{1.f, 1.f, 1.f} * (border * (0.45f + card.emissive * 0.4f));
        const float sheen = 1.f - std::clamp((p.x * p.x + p.y * p.y) * 0.55f, 0.f, 1.f);
        color = color * (0.92f + 0.08f * sheen);

        // Optional emissive glow (used by the hero card in the vertigo scene).
        color = color + card.albedo * card.emissive;

        // Simple card-vs-card shadowing: darken when another card is between us
        // and the sun (rare in these scenes, cheap approximation).
        for (const Card& other : cards) {
            if (&other == &card) continue;
            Vec3f toOther = toV(other.center) - toV(card.center);
            const float along = dot3(toOther, sunDir * (-1.f));
            if (along > 0.f) {
                const float perp = std::sqrt(std::max(dot3(toOther, toOther) - along * along, 0.f));
                if (perp < std::max(other.halfW, other.halfH)) {
                    color = color * 0.82f;
                }
            }
        }
        return color;
    }

    Vec3f trace(const Ray& ray, const std::vector<Card>& cards) {
        CardHit best;
        for (const Card& card : cards) {
            const CardHit h = intersectCard(ray, card);
            if (h.hit && (!best.hit || h.t < best.t)) best = h;
        }
        if (best.hit) return shadeCard(best, cards);
        return groundColor(ray, cards);
    }

    // ------------------------------------------------------------- camera

    struct View {
        Vector3 right{1.f, 0.f, 0.f};
        Vector3 up{0.f, 1.f, 0.f};
        Vector3 forward{0.f, 0.f, -1.f};
        Vector3 position{0.f, 0.f, 0.f};
        float fx = 0.f;
        int w = 0, h = 0;

        static View fromPose(const CameraPose& pose, int width, int height) {
            View v;
            Quaternion q = pose.orientation;
            if (q.lengthSq() > 0.f) q.normalize();
            v.right = Vector3(1.f, 0.f, 0.f);
            v.right.applyQuaternion(q);
            v.up = Vector3(0.f, 1.f, 0.f);
            v.up.applyQuaternion(q);
            v.forward = Vector3(0.f, 0.f, -1.f);
            v.forward.applyQuaternion(q);
            v.position = pose.position;
            const float tanHalf = std::tan(pose.fov * 0.5f * kPi / 180.f);
            v.fx = 0.5f * height / tanHalf;
            v.w = width;
            v.h = height;
            return v;
        }

        bool project(const Vector3& p, float& sx, float& sy, float& depth) const {
            Vector3 d = p;
            d.sub(position);
            const float z = d.dot(forward);
            if (z <= 0.05f) return false;
            const float x = d.dot(right);
            const float y = d.dot(up);
            sx = 0.5f * w + fx * x / z;
            sy = 0.5f * h - fx * y / z;
            depth = z;
            return true;
        }
    };

    // ------------------------------------------------------------- post fx

    Vec3f filmic(Vec3f x) {
        // ACES-approx filmic curve.
        const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
        Vec3f out{(x.x * (a * x.x + b)) / (x.x * (c * x.x + d) + e),
                  (x.y * (a * x.y + b)) / (x.y * (c * x.y + d) + e),
                  (x.z * (a * x.z + b)) / (x.z * (c * x.z + d) + e)};
        return clamp01(out);
    }

    Vec3f grade(Vec3f c) {
        // Teal shadows / orange highlights.
        const float l = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
        const Vec3f shadowTint{0.92f, 1.02f, 1.12f};
        const Vec3f highTint{1.08f, 1.00f, 0.90f};
        const float s = std::clamp(1.f - l * 1.6f, 0.f, 1.f);
        const float hi = std::clamp(l * 1.4f - 0.4f, 0.f, 1.f);
        return c * (shadowTint * s + Vec3f{1.f, 1.f, 1.f} * (1.f - s - hi) + highTint * hi);
    }

    float hash01(int x, int y, int frame) {
        const float n = std::sin(static_cast<float>(x) * 12.9898f +
                                 static_cast<float>(y) * 78.233f +
                                 static_cast<float>(frame) * 37.719f) * 43758.5453f;
        return n - std::floor(n);
    }

    // ------------------------------------------------------------- scenes

    void renderFrame(Canvas& canvas, const CameraRig& rig, float t,
                     const std::vector<Card>& cards,
                     const std::function<void(Canvas&, const View&)>& overlay,
                     int frameIndex, bool shutterBlur = true) {
        const int w = canvas.w, h = canvas.h;
        std::vector<Vec3f> accum(static_cast<std::size_t>(w * h), Vec3f{0.f, 0.f, 0.f});

        // 2-tap shutter: central derivative blur from the REAL dP/dt — the
        // analytical-shutter path, no neighbor frames involved.
        struct Sub {
            float dt;
            float weight;
        };
        std::vector<Sub> subs{{0.f, 1.f}};
        if (shutterBlur) {
            subs.push_back({-0.5f / kFps, 0.45f});
            subs.push_back({+0.5f / kFps, 0.45f});
        }

        for (const Sub& sub : subs) {
            const CameraPose pose = rig.sample(t + sub.dt);
            const View view = View::fromPose(pose, w, h);

            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const Ray ray = pixelRay(pose, w, h, x + 0.5f, y + 0.5f);
                    accum[static_cast<std::size_t>(y * w + x)] =
                            accum[static_cast<std::size_t>(y * w + x)] + trace(ray, cards) * sub.weight;
                }
            }
        }

        // Normalize, then bloom: threshold + 4-tap wide blur blended back.
        const float norm = 1.f / (subs[0].weight + (subs.size() - 1) * (subs.empty() ? 0.f : subs[1].weight) * 2.f);
        std::vector<Vec3f> bloom(accum.size(), Vec3f{0.f, 0.f, 0.f});
        for (std::size_t i = 0; i < accum.size(); ++i) {
            const Vec3f c = accum[i] * norm;
            const float l = 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z;
            bloom[i] = l > 1.1f ? c * (l - 1.1f) : Vec3f{0.f, 0.f, 0.f};
        }
        std::vector<Vec3f> blurred = bloom;
        for (int pass = 0; pass < 2; ++pass) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    Vec3f sum{0.f, 0.f, 0.f};
                    float cnt = 0.f;
                    for (int dy = -2; dy <= 2; ++dy) {
                        for (int dx = -2; dx <= 2; ++dx) {
                            const int nx = std::clamp(x + dx, 0, w - 1);
                            const int ny = std::clamp(y + dy, 0, h - 1);
                            sum = sum + bloom[static_cast<std::size_t>(ny * w + nx)];
                            cnt += 1.f;
                        }
                    }
                    blurred[static_cast<std::size_t>(y * w + x)] = sum * (1.f / cnt);
                }
            }
            bloom = blurred;
        }

        const View viewMain = View::fromPose(rig.sample(t), w, h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const std::size_t i = static_cast<std::size_t>(y * w + x);
                Vec3f c = accum[i] * norm + blurred[i] * 0.55f;
                c = filmic(grade(c));

                // Vignette.
                const float nx2 = (x / static_cast<float>(w) - 0.5f) * 2.f;
                const float ny2 = (y / static_cast<float>(h) - 0.5f) * 2.f;
                const float vig = 1.f - 0.22f * (nx2 * nx2 + ny2 * ny2);
                c = c * vig;

                // Film grain.
                const float g = (hash01(x, y, frameIndex) - 0.5f) * 0.018f;
                c = clamp01(c + Vec3f{g, g, g});

                canvas.set(x, y, toRgb(c));
            }
        }

        if (overlay) overlay(canvas, viewMain);
    }

    void drawCrosshair(Canvas& canvas, float cx, float cy, RGB color) {
        for (int d = -22; d <= 22; ++d) {
            canvas.set(static_cast<int>(cx) + d, static_cast<int>(cy), color);
            canvas.set(static_cast<int>(cx), static_cast<int>(cy) + d, color);
        }
        for (int r = 12; r <= 14; ++r) {
            for (int a = 0; a < 90; ++a) {
                const float ang = a * kPi / 45.f;
                canvas.set(static_cast<int>(cx + r * std::cos(ang)),
                           static_cast<int>(cy + r * std::sin(ang)), color);
            }
        }
    }

    std::vector<Card> handoffCards() {
        return {
                {Vector3(-6.f, 0.4f, 0.f), 2.1f, 1.35f, Vec3f{0.16f, 0.42f, 0.95f}},
                {Vector3(7.f, 1.8f, -9.f), 2.3f, 1.45f, Vec3f{0.98f, 0.52f, 0.12f}},
                {Vector3(-1.f, 1.2f, -26.f), 5.f, 3.f, Vec3f{0.25f, 0.30f, 0.45f}},
                {Vector3(9.f, 0.8f, -18.f), 2.4f, 1.5f, Vec3f{0.45f, 0.50f, 0.62f}},
        };
    }

    /// Renders `totalFrames` frames in parallel on `kThreads` workers, each
    /// frame fully independent (sample(t) is pure — random access by design).
    /// `loopFrames` pads the authored animation by cycling it: hold frame 0 for
    /// `leadIn`, play, hold the last frame for `holdEnd`.
    constexpr int kThreads = 12;

    void renderParallel(const std::string& dir, int totalFrames, int loopFrames, int leadIn,
                        const std::function<void(int, Canvas&)>& renderOne) {
        std::atomic<int> next{0};
        std::vector<std::thread> workers;
        for (int th = 0; th < kThreads; ++th) {
            workers.emplace_back([&]() {
                Canvas canvas(0, 0);
                for (;;) {
                    const int f = next.fetch_add(1);
                    if (f >= totalFrames) break;
                    // Timeline: leadIn holds frame 0, then the authored loop,
                    // then the final frame holds to the end.
                    int src = 0;
                    if (f < leadIn) {
                        src = 0;
                    } else if (f < leadIn + loopFrames) {
                        src = f - leadIn;
                    } else {
                        src = loopFrames - 1;
                    }
                    canvas = Canvas(dir.empty() ? 0 : 1280, 720);
                    renderOne(f, canvas);
                    (void) src;
                }
            });
        }
        for (std::thread& t : workers) t.join();
    }

    void renderHandoff(const std::string& dir) {
        constexpr int kW = 1280, kH = 720;
        CameraRig rig(1, 55.f, 16.f / 9.f);
        const Vector2 anchor(1240.f, 430.f);
        const ShotAnchor A = ShotAnchor::world(Vector3(-6.f, 0.4f, 0.f));
        const ShotAnchor B = ShotAnchor::world(Vector3(7.f, 1.8f, -9.f));
        cameraEyeTraceHandoff(rig, A, B, 0, 60, kFps, Easing::easeInOut(), nullptr, anchor,
                              Vector2(1920.f, 1080.f));

        const auto cards = handoffCards();
        const ScreenCameraIntrinsics intrinsics{Vector2(1920.f, 1080.f), 55.f};

        const int authored = 61;
        const int leadIn = 45;  // 1.5 s title hold
        const int holdEnd = 45; // 1.5 s end hold
        const int total = leadIn + authored + holdEnd;

        renderParallel(dir, total, authored, leadIn, [&](int f, Canvas& canvas) {
            int src = 0;
            if (f < leadIn) {
                src = 0;
            } else if (f < leadIn + authored) {
                src = f - leadIn;
            } else {
                src = authored - 1;
            }
            const int frame = src;
            renderFrame(canvas, rig, at(frame), cards,
                        [&](Canvas& c, const View& view) {
                            const CameraPose pose = rig.sample(at(frame));
                            const Vector2 px = projectToScreen(
                                    A.resolve(static_cast<const MotionScene*>(nullptr), at(frame)), pose,
                                    intrinsics);
                            (void) view;
                            drawCrosshair(c, px.x * kW / 1920.f, px.y * kH / 1080.f,
                                          RGB{245, 245, 235});
                        },
                        frame);
            char path[512];
            std::snprintf(path, sizeof(path), "%s/f_%04d.ppm", dir.c_str(), f);
            canvas.savePpm(path);
        });
    }

    std::vector<Card> directorCards() {
        return {
                {Vector3(-5.f, 0.4f, 0.f), 2.f, 1.3f, Vec3f{0.10f, 0.75f, 0.68f}},
                {Vector3(6.f, 0.4f, -4.f), 2.f, 1.3f, Vec3f{0.95f, 0.30f, 0.22f}},
                {Vector3(0.f, 2.2f, -12.f), 2.2f, 1.4f, Vec3f{0.98f, 0.80f, 0.15f}},
                {Vector3(-12.f, 1.1f, -20.f), 3.f, 1.9f, Vec3f{0.40f, 0.46f, 0.60f}},
                {Vector3(13.f, 1.3f, -26.f), 3.2f, 2.f, Vec3f{0.35f, 0.42f, 0.58f}},
        };
    }

    void renderDirector(const std::string& dir) {
        constexpr int kW = 1280, kH = 720;
        CameraRig rig(2, 55.f, 16.f / 9.f);
        const ShotAnchor A = ShotAnchor::world(Vector3(-5.f, 0.4f, 0.f));
        const ShotAnchor B = ShotAnchor::world(Vector3(6.f, 0.4f, -4.f));
        const ShotAnchor C = ShotAnchor::world(Vector3(0.f, 2.2f, -12.f));

        EyeTraceDirector director(kFps, ScreenCameraIntrinsics{Vector2(1920.f, 1080.f), 55.f});
        director.addShot({[](CameraRig& r) {
                              r.positionTrack().add(0.f, Vector3(-5.f, 0.4f, 10.f), Easing::linear());
                              r.positionTrack().add(at(28), Vector3(-5.f, 0.4f, 10.f), Easing::linear());
                              r.targetTrack().add(0.f, Vector3(-5.f, 0.4f, 0.f), Easing::linear());
                              r.targetTrack().add(at(28), Vector3(-5.f, 0.4f, 0.f), Easing::linear());
                          },
                          A, 0, 28});
        director.addShot({[](CameraRig& r) {
                              r.positionTrack().add(at(30), Vector3(6.f, 0.4f, 6.f), Easing::linear());
                              r.positionTrack().add(at(59), Vector3(5.f, 0.4f, 8.f), Easing::linear());
                              r.targetTrack().add(at(30), Vector3(6.f, 0.4f, -4.f), Easing::linear());
                              r.targetTrack().add(at(59), Vector3(6.f, 0.4f, -4.f), Easing::linear());
                          },
                          B, 30, 29});
        director.addShot({[](CameraRig& r) {
                              r.positionTrack().add(at(61), Vector3(0.f, 2.2f, 2.f), Easing::linear());
                              r.positionTrack().add(at(84), Vector3(0.f, 2.2f, 4.f), Easing::linear());
                              r.targetTrack().add(at(61), Vector3(0.f, 2.2f, -12.f), Easing::linear());
                              r.targetTrack().add(at(84), Vector3(0.f, 2.2f, -12.f), Easing::linear());
                          },
                          C, 61, 23});
        director.bakeInto(rig);

        const auto cards = directorCards();
        const int authored = 85;
        const int leadIn = 45;
        const int holdEnd = 45;
        const int total = leadIn + authored + holdEnd;

        renderParallel(dir, total, authored, leadIn, [&](int f, Canvas& canvas) {
            int src = 0;
            if (f < leadIn) {
                src = 0;
            } else if (f < leadIn + authored) {
                src = f - leadIn;
            } else {
                src = authored - 1;
            }
            const int frame = src;
            renderFrame(canvas, rig, at(frame), cards,
                        [&](Canvas& c, const View& view) {
                            (void) view;
                            Vector2 pin(960.f, 540.f);
                            bool show = false;
                            for (const ScreenLandingKey& key : director.landings()) {
                                if (at(frame) >= key.inTime + 0.5f / kFps) {
                                    pin = key.landingPx;
                                    show = true;
                                }
                            }
                            if (show) {
                                drawCrosshair(c, pin.x * kW / 1920.f, pin.y * kH / 1080.f,
                                              RGB{245, 245, 235});
                            }
                        },
                        frame);
            char path[512];
            std::snprintf(path, sizeof(path), "%s/f_%04d.ppm", dir.c_str(), f);
            canvas.savePpm(path);
        });
    }

    std::vector<Card> reframeCards() {
        return {
                {Vector3(-1.5f, 0.2f, 0.f), 1.15f, 1.15f, Vec3f{0.16f, 0.42f, 0.95f}},
                {Vector3(1.5f, 0.2f, 0.f), 1.15f, 1.15f, Vec3f{0.98f, 0.52f, 0.12f}},
                {Vector3(0.f, 0.9f, -20.f), 7.f, 4.f, Vec3f{0.30f, 0.36f, 0.52f}},
                {Vector3(-14.f, 1.f, -34.f), 3.f, 2.f, Vec3f{0.36f, 0.44f, 0.58f}},
                {Vector3(14.f, 1.f, -34.f), 3.f, 2.f, Vec3f{0.36f, 0.44f, 0.58f}},
        };
    }

    void renderReframe(const std::string& dir) {
        struct Panel {
            int w, h, x0;
            DeliveryFormat format;
            RGB accent;
            const char* label;
        };
        const int w169 = 1280, w916 = 406, w11 = 720;
        std::vector<Panel> panels = {
                {w169, 720, 0, DeliveryFormat::landscape1080(), RGB{120, 120, 150}, "16:9"},
                {w916, 720, w169, DeliveryFormat::vertical1080(), RGB{40, 170, 180}, "9:16"},
                {w11, 720, w169 + w916, DeliveryFormat::square1080(), RGB{170, 90, 190}, "1:1"},
        };
        const int canvasW = w169 + w916 + w11;

        CameraRig master(3, 50.f, 16.f / 9.f);
        master.positionTrack().add(0.f, Vector3(0.f, 0.f, 14.f), Easing::easeInOut());
        master.positionTrack().add(2.f, Vector3(0.f, 0.f, 7.f), Easing::easeInOut());
        master.targetTrack().add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
        master.targetTrack().add(2.f, Vector3(0.f, 0.f, 0.f), Easing::linear());

        const std::vector<ShotAnchor> subjects{ShotAnchor::world(Vector3(-1.5f, 0.f, 0.f)),
                                               ShotAnchor::world(Vector3(1.5f, 0.f, 0.f))};
        const auto cards = reframeCards();

        // Pre-solve the three deliveries once (they are timeline-global).
        std::vector<CameraRig> reframed;
        reframed.reserve(panels.size());
        for (const Panel& p : panels) {
            reframed.emplace_back(10);
            autoReframeRig(master, nullptr, p.format, reframed.back(), subjects, kFps);
        }

        const int authored = 61;
        const int leadIn = 45;
        const int holdEnd = 45;
        const int total = leadIn + authored + holdEnd;
        std::atomic<int> next{0};
        std::vector<std::thread> workers;
        for (int th = 0; th < kThreads; ++th) {
            workers.emplace_back([&]() {
                for (;;) {
                    const int f = next.fetch_add(1);
                    if (f >= total) break;
                    int src = 0;
                    if (f < leadIn) src = 0;
                    else if (f < leadIn + authored) src = f - leadIn;
                    else src = authored - 1;

                    Canvas canvas(canvasW, 720);
                    for (std::size_t pi = 0; pi < panels.size(); ++pi) {
                        const Panel& p = panels[pi];
                        Canvas panel(p.w, p.h);
                        renderFrame(panel, reframed[pi], at(src), cards, nullptr, src);

                        // Accent top strip + reserved-zone tint.
                        for (int x = 0; x < p.w; ++x) {
                            for (int y = 0; y < 5; ++y) panel.set(x, y, p.accent);
                        }
                        if (p.format.safeRight > 0.f || p.format.safeBottom > 0.f) {
                            const int rx0 = static_cast<int>((1.f - p.format.safeRight) * p.w);
                            for (int y = 0; y < p.h; ++y)
                                for (int x = rx0; x < p.w; ++x)
                                    panel.px[y * p.w + x] = RGB{static_cast<std::uint8_t>(panel.px[y * p.w + x].r * 0.55f + 40),
                                                                static_cast<std::uint8_t>(panel.px[y * p.w + x].g * 0.35f),
                                                                static_cast<std::uint8_t>(panel.px[y * p.w + x].b * 0.35f)};
                            const int by0 = static_cast<int>((1.f - p.format.safeBottom) * p.h);
                            for (int y = by0; y < p.h; ++y)
                                for (int x = 0; x < p.w; ++x)
                                    panel.px[y * p.w + x] = RGB{static_cast<std::uint8_t>(panel.px[y * p.w + x].r * 0.55f + 40),
                                                                static_cast<std::uint8_t>(panel.px[y * p.w + x].g * 0.35f),
                                                                static_cast<std::uint8_t>(panel.px[y * p.w + x].b * 0.35f)};
                        }
                        for (int y = 0; y < p.h; ++y) {
                            for (int x = 0; x < p.w; ++x) {
                                canvas.px[y * canvasW + (p.x0 + x)] = panel.px[y * p.w + x];
                            }
                        }
                    }
                    char path[512];
                    std::snprintf(path, sizeof(path), "%s/f_%04d.ppm", dir.c_str(), f);
                    canvas.savePpm(path);
                }
            });
        }
        for (std::thread& t : workers) t.join();
    }

    std::vector<Card> vertigoCards() {
        return {
                {Vector3(0.f, 0.2f, 0.f), 1.25f, 1.7f, Vec3f{0.95f, 0.15f, 0.15f}, 0.55f},
                {Vector3(-9.f, 1.f, -16.f), 1.7f, 2.7f, Vec3f{0.25f, 0.38f, 0.85f}},
                {Vector3(9.f, 1.f, -16.f), 1.7f, 2.7f, Vec3f{0.25f, 0.38f, 0.85f}},
                {Vector3(-16.f, 1.4f, -28.f), 2.3f, 3.5f, Vec3f{0.15f, 0.62f, 0.50f}},
                {Vector3(16.f, 1.4f, -28.f), 2.3f, 3.5f, Vec3f{0.15f, 0.62f, 0.50f}},
                {Vector3(-24.f, 2.f, -44.f), 3.1f, 4.7f, Vec3f{0.72f, 0.52f, 0.25f}},
                {Vector3(24.f, 2.f, -44.f), 3.1f, 4.7f, Vec3f{0.72f, 0.52f, 0.25f}},
        };
    }

    void renderVertigo(const std::string& dir) {
        constexpr int kW = 1280, kH = 720;
        CameraRig rig(4, 45.f, 16.f / 9.f);
        const ShotAnchor hero = ShotAnchor::world(Vector3(0.f, 0.f, 0.f));
        cameraVertigoDollyZoom(rig, hero, 0, 60, 14.f, 3.5f, 45.f, kFps);

        const auto cards = vertigoCards();
        const int authored = 61;
        const int leadIn = 45;
        const int holdEnd = 45;
        const int total = leadIn + authored + holdEnd;

        renderParallel(dir, total, authored, leadIn, [&](int f, Canvas& canvas) {
            int src = 0;
            if (f < leadIn) {
                src = 0;
            } else if (f < leadIn + authored) {
                src = f - leadIn;
            } else {
                src = authored - 1;
            }
            renderFrame(canvas, rig, at(src), cards, nullptr, src);
            char path[512];
            std::snprintf(path, sizeof(path), "%s/f_%04d.ppm", dir.c_str(), f);
            canvas.savePpm(path);
        });
    }

}// namespace

int main(int argc, char** argv) {
    const std::string base = (argc > 1) ? argv[1] : "/tmp/previews2";
    std::filesystem::create_directories(base + "/handoff");
    std::filesystem::create_directories(base + "/director");
    std::filesystem::create_directories(base + "/reframe");
    std::filesystem::create_directories(base + "/vertigo");

    std::printf("rendering eyetrace_handoff_duo (cinematic)...\n");
    renderHandoff(base + "/handoff");
    std::printf("rendering eyetrace_director_three (cinematic)...\n");
    renderDirector(base + "/director");
    std::printf("rendering aspect_reframe_triplet (cinematic)...\n");
    renderReframe(base + "/reframe");
    std::printf("rendering vertigo_derivative_map (cinematic)...\n");
    renderVertigo(base + "/vertigo");
    std::printf("done -> %s\n", base.c_str());
    return 0;
}
