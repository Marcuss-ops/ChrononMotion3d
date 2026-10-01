// ChrononMotion — innovation animations exporter.
//
// Generates ready-to-inspect camera animations built on the new primitives,
// with embedded per-frame verification so the JSON is self-certifying:
//
//   1. eyetrace_handoff_duo        — the exact pixel-landing handoff (A -> B)
//   2. eyetrace_director_three     — a 3-shot cut sequence with a pinned gaze
//   3. aspect_reframe_triplet      — one master push, 16:9 / 9:16 / 1:1 deliveries
//   4. vertigo_derivative_map      — dolly zoom with closed-form dP/dt, omega, dFOV/dt
//
// Output: camera_innovation_animations.json (next to the executable's working
// directory), same sampling spirit as export_camera_trajectories.

#include "chrononmotion/chrononmotion.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

using namespace chrononmotion;
using namespace chrononmotion::motion;

namespace {

    constexpr float kFps = 30.f;
    const Vector2 kViewport(1920.f, 1080.f);
    constexpr float kPi = 3.14159265358979323846f;

    float at(int frame) {
        return static_cast<float>(frame) / kFps;
    }

    std::string vec2Json(const Vector2& v) {
        char buf[96];
        std::snprintf(buf, sizeof(buf), "[%.6f, %.6f]", v.x, v.y);
        return buf;
    }

    std::string vec3Json(const Vector3& v) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "[%.6f, %.6f, %.6f]", v.x, v.y, v.z);
        return buf;
    }

    struct FrameSample {
        float time{0.f};
        Vector3 position{0.f, 0.f, 0.f};
        Vector3 target{0.f, 0.f, 0.f};
        float fov{60.f};
        float focusDistance{0.f};
        Vector2 focusPx{0.f, 0.f};
        Vector3 velocity{0.f, 0.f, 0.f};
        Vector3 angularVelocity{0.f, 0.f, 0.f};
        float fovRate{0.f};
    };

    std::string framesJson(const std::vector<FrameSample>& frames) {
        std::string out = "    \"frames\": [\n";
        for (std::size_t i = 0; i < frames.size(); ++i) {
            const FrameSample& f = frames[i];
            char buf[1024];
            std::snprintf(buf, sizeof(buf),
                          "      {\"t\": %.6f, \"position\": %s, \"target\": %s, \"fov\": %.4f, "
                          "\"focusDistance\": %.4f, \"focusPx\": %s, \"velocity\": %s, "
                          "\"angularVelocity\": %s, \"fovRate\": %.6f}%s\n",
                          f.time, vec3Json(f.position).c_str(), vec3Json(f.target).c_str(),
                          f.fov, f.focusDistance, vec2Json(f.focusPx).c_str(),
                          vec3Json(f.velocity).c_str(), vec3Json(f.angularVelocity).c_str(),
                          f.fovRate, (i + 1 < frames.size()) ? "," : "");
            out += buf;
        }
        out += "    ]";
        return out;
    }

    std::vector<FrameSample> sampleRig(const CameraRig& rig, int lastFrame,
                                       const ShotAnchor& focusAnchor,
                                       const ScreenCameraIntrinsics& intrinsics) {
        std::vector<FrameSample> frames;
        frames.reserve(static_cast<std::size_t>(lastFrame + 1));
        for (int f = 0; f <= lastFrame; ++f) {
            const float t = at(f);
            const CameraPose pose = rig.sample(t);
            const CameraPoseDerivative d = rig.sampleDerivative(t);

            FrameSample s;
            s.time = t;
            s.position = pose.position;
            s.target = pose.target;
            s.fov = pose.fov;
            s.focusDistance = pose.focusDistance;
            s.focusPx = projectToScreen(focusAnchor.resolve(static_cast<const MotionScene*>(nullptr), t), pose, intrinsics);
            s.velocity = d.linearVelocity;
            s.angularVelocity = d.angularVelocity;
            s.fovRate = d.fovRate;
            frames.push_back(s);
        }
        return frames;
    }

    // ------------------------------------------------------------- 1. handoff duo

    void writeHandoffDuo(std::ofstream& out, bool& first) {
        CameraRig rig(1, 55.f, 16.f / 9.f);
        const Vector2 anchor(1240.f, 430.f);
        const ShotAnchor A = ShotAnchor::world(Vector3(-6.f, 0.f, 0.f));
        const ShotAnchor B = ShotAnchor::world(Vector3(7.f, 1.5f, -9.f));

        cameraEyeTraceHandoff(rig, A, B, 0, 60, kFps, Easing::easeInOut(), nullptr,
                              anchor, kViewport);

        const ScreenCameraIntrinsics intrinsics{kViewport, 55.f};
        const std::vector<FrameSample> frames = sampleRig(rig, 60, B, intrinsics);

        const Vector2 endPx = projectToScreen(B.resolve(static_cast<const MotionScene*>(nullptr), at(60)), rig.sample(at(60)), intrinsics);
        const float landingError = endPx.distanceTo(anchor);
        const Vector2 startPx = projectToScreen(A.resolve(static_cast<const MotionScene*>(nullptr), 0.f), rig.sample(0.f), intrinsics);
        const float pinError = startPx.distanceTo(anchor);

        if (!first) out << ",\n";
        first = false;
        out << "  {\n";
        out << "    \"name\": \"eyetrace_handoff_duo\",\n";
        out << "    \"description\": \"Exact pixel-landing handoff: subject A is pinned on (1240, 430), subject B lands on the very same pixels at frame 60. The correction is aim-preserving (position+target translate together), so orientation, FOV and DOF authoring survive untouched.\",\n";
        out << "    \"innovation\": \"eye-trace continuity director\",\n";
        out << "    \"fps\": 30, \"viewport\": " << vec2Json(kViewport) << ",\n";
        out << "    \"verification\": {\"landingErrorPx\": " << landingError
            << ", \"phase1PinErrorPx\": " << pinError << "},\n";
        out << framesJson(frames) << "\n  }";
    }

    // ---------------------------------------------------------- 2. director three

    void writeDirectorThree(std::ofstream& out, bool& first) {
        CameraRig rig(2, 55.f, 16.f / 9.f);
        const ShotAnchor A = ShotAnchor::world(Vector3(-5.f, 0.f, 0.f));
        const ShotAnchor B = ShotAnchor::world(Vector3(6.f, 0.f, -4.f));
        const ShotAnchor C = ShotAnchor::world(Vector3(0.f, 2.f, -12.f));

        EyeTraceDirector director(kFps, ScreenCameraIntrinsics{kViewport, 55.f});
        director.addShot({[](CameraRig& r) {
                              r.positionTrack().add(0.f, Vector3(-5.f, 0.f, 10.f), Easing::linear());
                              r.positionTrack().add(at(28), Vector3(-5.f, 0.f, 10.f), Easing::linear());
                              r.targetTrack().add(0.f, Vector3(-5.f, 0.f, 0.f), Easing::linear());
                              r.targetTrack().add(at(28), Vector3(-5.f, 0.f, 0.f), Easing::linear());
                          },
                          A, 0, 28});
        director.addShot({[](CameraRig& r) {
                              r.positionTrack().add(at(30), Vector3(6.f, 0.f, 6.f), Easing::linear());
                              r.positionTrack().add(at(59), Vector3(5.f, 0.f, 8.f), Easing::linear());
                              r.targetTrack().add(at(30), Vector3(6.f, 0.f, -4.f), Easing::linear());
                              r.targetTrack().add(at(59), Vector3(6.f, 0.f, -4.f), Easing::linear());
                          },
                          B, 30, 29});
        director.addShot({[](CameraRig& r) {
                              r.positionTrack().add(at(61), Vector3(0.f, 2.f, 2.f), Easing::linear());
                              r.positionTrack().add(at(84), Vector3(0.f, 2.f, 4.f), Easing::linear());
                              r.targetTrack().add(at(61), Vector3(0.f, 2.f, -12.f), Easing::linear());
                              r.targetTrack().add(at(84), Vector3(0.f, 2.f, -12.f), Easing::linear());
                          },
                          C, 61, 23});
        director.bakeInto(rig);

        // Verification: within each shot the active anchor is pinned — its
        // screen position never moves more than a tiny fraction of a pixel
        // from the landing it inherited, however much the camera travels.
        // (The separation frames between shots are the authored blend, not a
        // cut, so they are excluded from the pin metric.)
        const ScreenCameraIntrinsics intrinsics{kViewport, 55.f};
        float maxPinDriftPx = 0.f;
        auto measurePin = [&](int fromFrame, int toFrame, const ShotAnchor& anchor) {
            const Vector2 landing = projectToScreen(
                    anchor.resolve(static_cast<const MotionScene*>(nullptr), at(fromFrame)),
                    rig.sample(at(fromFrame)), intrinsics);
            for (int f = fromFrame; f <= toFrame; ++f) {
                const Vector2 px = projectToScreen(
                        anchor.resolve(static_cast<const MotionScene*>(nullptr), at(f)),
                        rig.sample(at(f)), intrinsics);
                maxPinDriftPx = std::max(maxPinDriftPx, px.distanceTo(landing));
            }
        };
        measurePin(30, 58, B);
        measurePin(61, 83, C);

        // Sample with the ACTIVE anchor per shot for a meaningful focusPx.
        std::vector<FrameSample> frames;
        frames.reserve(85);
        for (int f = 0; f <= 84; ++f) {
            const float t = at(f);
            const CameraPose pose = rig.sample(t);
            const CameraPoseDerivative d = rig.sampleDerivative(t);
            const ShotAnchor& active = (f <= 28) ? A : (f <= 59 ? B : C);

            FrameSample s;
            s.time = t;
            s.position = pose.position;
            s.target = pose.target;
            s.fov = pose.fov;
            s.focusDistance = pose.focusDistance;
            s.focusPx = projectToScreen(active.resolve(static_cast<const MotionScene*>(nullptr), t), pose, intrinsics);
            s.velocity = d.linearVelocity;
            s.angularVelocity = d.angularVelocity;
            s.fovRate = d.fovRate;
            frames.push_back(s);
        }

        if (!first) out << ",\n";
        first = false;
        out << "  {\n";
        out << "    \"name\": \"eyetrace_director_three\",\n";
        out << "    \"description\": \"Three-shot cut sequence baked by the EyeTraceDirector: at every cut the incoming subject's anchor is pinned to the pixels the outgoing subject occupied, and stays pinned while the shot's camera keeps moving. The viewer's gaze never jumps.\",\n";
        out << "    \"innovation\": \"eye-trace continuity director (cuts)\",\n";
        out << "    \"fps\": 30, \"viewport\": " << vec2Json(kViewport) << ",\n";
        out << "    \"verification\": {\"maxWithinShotPinDriftPx\": " << maxPinDriftPx
            << ", \"landings\": " << director.landings().size() << "},\n";
        out << framesJson(frames) << "\n  }";
    }

    // ------------------------------------------------------- 3. reframe triplet

    void writeReframeTriplet(std::ofstream& out, bool& first) {
        // Master: a 60-frame push onto a 3-unit-wide subject at the origin.
        CameraRig master(3, 50.f, 16.f / 9.f);
        master.positionTrack().add(0.f, Vector3(0.f, 0.f, 14.f), Easing::easeInOut());
        master.positionTrack().add(2.f, Vector3(0.f, 0.f, 7.f), Easing::easeInOut());
        master.targetTrack().add(0.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
        master.targetTrack().add(2.f, Vector3(0.f, 0.f, 0.f), Easing::linear());
        master.focusDistanceTrack().add(0.f, 14.f, Easing::linear());
        master.focusDistanceTrack().add(2.f, 7.f, Easing::linear());

        const std::vector<ShotAnchor> subjects{ShotAnchor::world(Vector3(-1.5f, 0.f, 0.f)),
                                               ShotAnchor::world(Vector3(1.5f, 0.f, 0.f))};

        struct Delivery {
            const char* name;
            DeliveryFormat format;
        };
        const Delivery deliveries[] = {
                {"landscape_16_9", DeliveryFormat::landscape1080()},
                {"vertical_9_16_tiktok", DeliveryFormat::vertical1080()},
                {"square_1_1", DeliveryFormat::square1080()},
        };

        if (!first) out << ",\n";
        first = false;
        out << "  {\n";
        out << "    \"name\": \"aspect_reframe_triplet\",\n";
        out << "    \"description\": \"One authored master push (60 frames), three deliveries. autoReframeRig backs the camera off along the view axis exactly enough that the subject pair fits the narrower axis (plus TikTok safe zones), then re-centers the subject on the usable band with an aim-preserving pan. Vertical size is preserved: the subject keeps its on-screen scale.\",\n";
        out << "    \"innovation\": \"one-source multi-aspect auto-reframing\",\n";
        out << "    \"fps\": 30,\n";
        out << "    \"deliveries\": [\n";

        for (std::size_t di = 0; di < 3; ++di) {
            const Delivery& d = deliveries[di];
            CameraRig reframed(10 + static_cast<int>(di));
            autoReframeRig(master, nullptr, d.format, reframed, subjects, kFps);

            // Verification: the subject pair stays inside the safe band.
            const float fov = master.fov();
            const ScreenCameraIntrinsics intrinsics{d.format.viewport, fov};
            int inside = 0;
            int total = 0;
            for (int f = 0; f <= 60; ++f) {
                const CameraPose pose = reframed.sample(at(f));
                for (const ShotAnchor& s : subjects) {
                    const Vector2 px = projectToScreen(s.resolve(static_cast<const MotionScene*>(nullptr), at(f)), pose, intrinsics);
                    const float xMin = d.format.safeLeft * d.format.viewport.x;
                    const float xMax = (1.f - d.format.safeRight) * d.format.viewport.x;
                    const float yMin = d.format.safeTop * d.format.viewport.y;
                    const float yMax = (1.f - d.format.safeBottom) * d.format.viewport.y;
                    ++total;
                    if (px.x >= xMin - 1e-3f && px.x <= xMax + 1e-3f && px.y >= yMin - 1e-3f &&
                        px.y <= yMax + 1e-3f) {
                        ++inside;
                    }
                }
            }

            char buf[512];
            std::snprintf(buf, sizeof(buf),
                          "      {\"delivery\": \"%s\", \"viewport\": %s, "
                          "\"safeBandContainment\": %.4f,\n       \"frames\": [\n",
                          d.name, vec2Json(d.format.viewport).c_str(),
                          static_cast<float>(inside) / static_cast<float>(total));
            out << buf;

            for (int f = 0; f <= 60; ++f) {
                const CameraPose pose = reframed.sample(at(f));
                const Vector2 heroPx =
                        projectToScreen(subjects[0].resolve(static_cast<const MotionScene*>(nullptr), at(f)), pose, intrinsics);
                char row[512];
                std::snprintf(row, sizeof(row),
                              "        {\"t\": %.6f, \"position\": %s, \"target\": %s, "
                              "\"focusDistance\": %.4f, \"heroPx\": %s}%s\n",
                              at(f), vec3Json(pose.position).c_str(), vec3Json(pose.target).c_str(),
                              pose.focusDistance, vec2Json(heroPx).c_str(), (f < 60) ? "," : "");
                out << row;
            }
            out << "       ]}";
            if (di + 1 < 3) out << ",";
            out << "\n";
        }
        out << "    ]\n  }";
    }

    // ------------------------------------------------------ 4. vertigo derivatives

    void writeVertigoDerivatives(std::ofstream& out, bool& first) {
        CameraRig rig(4, 45.f, 16.f / 9.f);
        const ShotAnchor hero = ShotAnchor::world(Vector3(0.f, 0.f, 0.f));
        cameraVertigoDollyZoom(rig, hero, 0, 60, 14.f, 3.5f, 45.f, kFps);

        const ScreenCameraIntrinsics intrinsics{kViewport, 45.f};
        const std::vector<FrameSample> frames = sampleRig(rig, 60, hero, intrinsics);

        // Screen-size invariance: the hero's projected half-height in pixels
        // stays ~constant while the world collapses — the Vertigo contract.
        float maxSizePx = 0.f;
        float minSizePx = 1e9f;
        for (const FrameSample& f : frames) {
            const float halfHeightPx = 0.5f * kViewport.y *
                                       (1.f / (f.focusDistance *
                                               std::tan(f.fov * 0.5f * kPi / 180.f))) *
                                       3.f; // 3 world units of hero height
            maxSizePx = std::max(maxSizePx, halfHeightPx);
            minSizePx = std::min(minSizePx, halfHeightPx);
        }
        const float sizeDrift = (maxSizePx - minSizePx) / maxSizePx;

        if (!first) out << ",\n";
        first = false;
        out << "  {\n";
        out << "    \"name\": \"vertigo_derivative_map\",\n";
        out << "    \"description\": \"Dolly zoom from 14 to 3.5 units with the FOV re-solved per frame, exported together with the closed-form derivatives dP/dt, omega and dFOV/dt. Frame N needs no neighbor frames: this is the data an analytical shutter motion blur consumes, and what makes any frame independently computable on a render farm.\",\n";
        out << "    \"innovation\": \"analytic pose derivatives (frame-independent shutter)\",\n";
        out << "    \"fps\": 30, \"viewport\": " << vec2Json(kViewport) << ",\n";
        out << "    \"verification\": {\"heroScreenSizeDrift\": " << sizeDrift << "},\n";
        out << framesJson(frames) << "\n  }";
    }

}// namespace

int main() {
    std::ofstream out("camera_innovation_animations.json");
    if (!out) {
        std::fprintf(stderr, "cannot open camera_innovation_animations.json for writing\n");
        return 1;
    }
    out << std::setprecision(9);

    bool first = true;
    out << "{\n";
    out << "  \"generator\": \"chrononmotion_export_camera_innovations\",\n";
    out << "  \"scenes\": [\n";
    writeHandoffDuo(out, first);
    writeDirectorThree(out, first);
    writeReframeTriplet(out, first);
    writeVertigoDerivatives(out, first);
    out << "\n  ]\n}\n";
    out.close();

    std::printf("Successfully exported 4 innovation camera animations to camera_innovation_animations.json\n");
    return 0;
}
