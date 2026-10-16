// Documentary animation presets for the Character V2.1 multilayer rig.
//
// Each preset is a short, light-movement clip built from the same corrected
// layers (assets/layers_v2_1). Presets are plain data: head roll keys, torso
// breathing amount, and right-arm IK hand targets.
//
// Usage: chrononmotion_render_documentary_presets [outDir] [--only <id>]

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

#include "threepp/threepp.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/motion/MotionBridge.hpp"

#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/RigGraph.hpp"
#include "chrononmotion/motion/RigBaking.hpp"
#include "chrononmotion/motion/RigDeformers.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"

namespace {

struct HeadKey { float t; float roll; };
struct HandKey { float t; chrononmotion::Vector3 target; };

struct Preset {
    std::string id;
    std::string title;
    float durationSec;
    std::vector<HeadKey> head;   // head roll (rad) keys; empty = head still
    float breathAmount;          // torso scale amplitude; 0 = no breathing
    std::vector<HandKey> hand;   // right-hand IK targets (scene units)
};

const chrononmotion::Vector3 kRestHand(-0.526f, 0.08f, 0.f);

std::vector<Preset> makePresets() {
    const float d = 6.0f;
    return {
        {"01_idle_breath", "Idle breathing", d, {}, 0.020f,
         {{0.f, kRestHand}, {d, kRestHand}}},
        {"02_head_turn_soft", "Soft head turn", d,
         {{0.f, 0.f}, {1.5f, 0.12f}, {3.5f, -0.10f}, {d, 0.f}}, 0.015f,
         {{0.f, kRestHand}, {d, kRestHand}}},
        {"03_head_tilt_listen", "Listening head tilt", d,
         {{0.f, 0.f}, {2.0f, 0.08f}, {4.0f, 0.08f}, {d, 0.f}}, 0.012f,
         {{0.f, kRestHand}, {d, kRestHand}}},
        {"04_explain_open_hand", "Open-hand explanation", d, {}, 0.015f,
         {{0.f, kRestHand}, {1.5f, {-0.40f, 0.20f, 0.05f}}, {4.5f, {-0.40f, 0.20f, 0.05f}}, {d, kRestHand}}},
        {"05_point_to_side", "Pointing to the side", d,
         {{0.f, 0.f}, {2.0f, -0.04f}, {d, 0.f}}, 0.012f,
         {{0.f, kRestHand}, {2.0f, {-0.21f, 0.36f, 0.05f}}, {4.0f, {-0.21f, 0.36f, 0.05f}}, {d, kRestHand}}},
        {"06_gesture_beat", "Emphasis beat", d, {}, 0.012f,
         {{0.f, kRestHand}, {1.0f, {-0.33f, 0.22f, 0.05f}}, {1.6f, {-0.36f, 0.10f, 0.05f}},
          {2.2f, {-0.33f, 0.22f, 0.05f}}, {4.0f, kRestHand}, {d, kRestHand}}},
        {"07_thinking_tilt", "Thinking pose", d,
         {{0.f, 0.f}, {1.5f, -0.10f}, {4.0f, -0.10f}, {d, 0.f}}, 0.012f,
         {{0.f, kRestHand}, {1.5f, {-0.30f, 0.25f, 0.05f}}, {4.5f, {-0.30f, 0.25f, 0.05f}}, {d, kRestHand}}},
        {"08_lean_in_breath", "Leaning in", d,
         {{0.f, 0.f}, {2.5f, 0.10f}, {4.5f, 0.10f}, {d, 0.f}}, 0.030f,
         {{0.f, kRestHand}, {d, kRestHand}}},
        {"09_reassure_low_hand", "Reassuring low gesture", d,
         {{0.f, 0.f}, {d, 0.f}}, 0.015f,
         {{0.f, kRestHand}, {2.0f, {-0.38f, -0.04f, 0.05f}}, {4.5f, {-0.38f, -0.04f, 0.05f}}, {d, kRestHand}}},
        {"10_combined_documentary", "Combined light performance", d,
         {{0.f, 0.f}, {1.5f, 0.08f}, {3.0f, -0.06f}, {4.5f, 0.02f}, {d, 0.f}}, 0.020f,
         {{0.f, kRestHand}, {1.2f, {-0.33f, 0.22f, 0.05f}}, {3.0f, {-0.30f, 0.25f, 0.05f}},
          {4.8f, {-0.33f, 0.22f, 0.05f}}, {d, kRestHand}}},
    };
}

std::shared_ptr<threepp::Texture> loadLayer(threepp::ImageLoader& loader, const std::string& dir,
                                            const std::string& name) {
    return threepp::Texture::create(std::move(*loader.load(dir + "/layer_" + name + ".png", 4, true)));
}

std::size_t countDiffPixels(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b,
                            int threshold = 8) {
    if (a.size() != b.size()) return 0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); i += 3) {
        if (std::abs(int(a[i]) - int(b[i])) > threshold || std::abs(int(a[i + 1]) - int(b[i + 1])) > threshold ||
            std::abs(int(a[i + 2]) - int(b[i + 2])) > threshold) {
            ++n;
        }
    }
    return n;
}

} // namespace

// Renders one preset to MP4. Returns true on success.
bool renderPreset(const Preset& p, threepp::GLRenderer& renderer, const std::string& layersDir,
                  const std::string& outDir, int width, int height, float fps,
                  const std::shared_ptr<threepp::Texture>& bgTex,
                  const std::vector<std::shared_ptr<threepp::Texture>>& layerTex) {
    const float imageAspect = 1376.f / 768.f;
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    const float camAspect = static_cast<float>(width) / static_cast<float>(height);
    const int totalFrames = static_cast<int>(p.durationSec * fps) + 1;
    const float durationSec = p.durationSec;

    const chrononmotion::Vector3 pivotNeck(-0.057f * planeW, 0.076f * planeH, 0.f);
    const chrononmotion::Vector3 pivotShoulder(-0.160f * planeW, -0.125f * planeH, 0.f);
    const chrononmotion::Vector3 pivotElbow(-0.245f * planeW, -0.288f * planeH, 0.f);
    const chrononmotion::Vector3 pivotWrist(-0.187f * planeW, -0.047f * planeH, 0.f);

    auto quadGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 2, 2);

    chrononmotion::motion::MotionScene scene(p.id, fps);

    // Backdrop (same geometry as the V2.1 spike)
    const float bgDist = 3.85f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAtt;
    bgAtt.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAtt.material = chrononmotion::MaterialDescriptor::unlit(5, chrononmotion::Color(0xffffff));
    auto& bgLayer = scene.addMesh(5, "backdrop", std::move(bgAtt));
    bgLayer.transform.position.set(0.f, 0.f, -0.85f);

    auto addCutout = [&](chrononmotion::motion::Layer::Id id, const std::string& name, float z)
        -> chrononmotion::motion::Layer& {
        chrononmotion::motion::MeshAttachment att;
        att.geometry = quadGeom;
        att.material = chrononmotion::MaterialDescriptor::unlit(id, chrononmotion::Color(0xffffff));
        att.material.transparent = true;
        auto& l = scene.addMesh(id, name, std::move(att));
        l.transform.position.z = z;
        return l;
    };

    auto& torso = addCutout(10, "layer_torso", 0.000f);
    auto& head = addCutout(11, "layer_head", 0.010f);
    auto& upperArm = addCutout(12, "layer_upper_arm", -0.005f);
    auto& forearm = addCutout(13, "layer_forearm", 0.015f);
    auto& hand = addCutout(14, "layer_hand", 0.025f);

    const std::vector<std::pair<chrononmotion::motion::Layer*, chrononmotion::Vector3>> pivoted = {
        {&head, pivotNeck}, {&upperArm, pivotShoulder}, {&forearm, pivotElbow}, {&hand, pivotWrist}};
    for (const auto& entry : pivoted) {
        entry.first->transform.anchor = entry.second;
        entry.first->transform.position.x = entry.second.x;
        entry.first->transform.position.y = entry.second.y;
    }

    // Light documentary push-in
    chrononmotion::motion::CameraRig cameraRig(100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.05f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.04f, 2.65f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.04f, 2.58f), chrononmotion::motion::Easing::easeInOut());

    // Head roll keys
    for (const auto& k : p.head) {
        head.tracks.rotation.add(k.t, chrononmotion::Vector3(0.f, 0.f, k.roll), chrononmotion::motion::Easing::easeInOut());
    }

    // Torso breathing
    if (p.breathAmount > 0.f) {
        const float cycle = durationSec / 3.0f;
        for (int b = 0; b <= 3; ++b) {
            const float t0 = b * cycle;
            if (t0 <= durationSec) {
                torso.tracks.scale.add(t0, chrononmotion::Vector3(1.f, 1.f, 1.f), chrononmotion::motion::Easing::easeInOut());
            }
            const float tPeak = t0 + cycle * 0.45f;
            if (tPeak < durationSec) {
                torso.tracks.scale.add(tPeak,
                    chrononmotion::Vector3(1.f + p.breathAmount, 1.f + p.breathAmount * 0.7f, 1.f),
                    chrononmotion::motion::Easing::easeInOut());
            }
        }
        torso.tracks.scale.add(durationSec, chrononmotion::Vector3(1.f, 1.f, 1.f));
    }

    // Right-arm two-bone IK (same skeleton as V2.1)
    chrononmotion::motion::RigProgram rig;
    {
        chrononmotion::motion::RigNode root;
        root.id = 10; root.kind = chrononmotion::motion::RigNodeKind::Bone;
        root.rest.position = chrononmotion::Vector3(0.f, -1.0f, 0.f); root.bind = root.rest;

        chrononmotion::motion::RigNode torsoN;
        torsoN.id = 20; torsoN.parent = 10; torsoN.kind = chrononmotion::motion::RigNodeKind::Bone;
        torsoN.rest.position = chrononmotion::Vector3(-0.02f * planeW, 0.55f, 0.f); torsoN.bind = torsoN.rest;

        chrononmotion::motion::RigNode shoulder;
        shoulder.id = 50; shoulder.parent = 20; shoulder.kind = chrononmotion::motion::RigNodeKind::Bone;
        shoulder.rest.position = chrononmotion::Vector3(-0.15f * planeW, 0.15f, 0.f); shoulder.bind = shoulder.rest;

        chrononmotion::motion::RigNode upper;
        upper.id = 51; upper.parent = 50; upper.kind = chrononmotion::motion::RigNodeKind::Bone;
        upper.rest.position = chrononmotion::Vector3(-0.06f * planeW, -0.42f, 0.f); upper.bind = upper.rest;

        chrononmotion::motion::RigNode fore;
        fore.id = 52; fore.parent = 51; fore.kind = chrononmotion::motion::RigNodeKind::Bone;
        fore.rest.position = chrononmotion::Vector3(0.08f * planeW, 0.58f, 0.f); fore.bind = fore.rest;

        chrononmotion::motion::RigNode handN;
        handN.id = 53; handN.parent = 52; handN.kind = chrononmotion::motion::RigNodeKind::Bone;
        handN.rest.position = chrononmotion::Vector3(0.03f * planeW, 0.22f, 0.f); handN.bind = handN.rest;

        rig.graph.nodes = {root, torsoN, shoulder, upper, fore, handN};

        chrononmotion::motion::RigScheduledIK ik;
        ik.id = 1;
        ik.chain.joints = {50, 51, 52, 53};
        ik.pole.add(0.f, chrononmotion::Vector3(-1.55f, -0.65f, -0.35f));
        for (const auto& k : p.hand) {
            ik.target.add(k.t, k.target, chrononmotion::motion::Easing::easeInOut());
        }
        ik.amount.add(0.f, 1.0f);
        rig.ik.push_back(std::move(ik));
    }
    chrononmotion::motion::RigProgramEvaluator ikEvaluator(rig);

    // Textures
    threepp::motion::MotionSceneBridge bridge(45.f, camAspect, 0.1f, 100.f);
    bridge.bindLayerTexture(5, bgTex);
    for (int i = 0; i < 5; ++i) {
        bridge.bindLayerTexture(10 + i, layerTex[i]);
    }

    auto renderFrame = [&](int frame) {
        const auto ikFrame = ikEvaluator.evaluate(frame, fps);
        const auto& pose = ikFrame.pose;
        scene.findLayer(12)->transform.rotation.z = pose.local[pose.index(51)].rotation.z;
        scene.findLayer(13)->transform.rotation.z = pose.local[pose.index(52)].rotation.z;
        scene.findLayer(14)->transform.rotation.z = pose.local[pose.index(53)].rotation.z;
        bridge.evaluateAndSync(scene, frame, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    // Sanity: the clip must actually move (compare first frame with the mid frame).
    const auto firstPixels = renderFrame(0);
    const auto midPixels = renderFrame(totalFrames / 2);
    const std::size_t motion = countDiffPixels(firstPixels, midPixels);
    std::cout << p.id << ": motion diff=" << motion << " px\n";
    if (motion == 0) {
        std::cerr << p.id << ": no visible motion\n";
        return false;
    }

    const std::string outPath = outDir + "/" + p.id + ".mp4";
    const std::string filter =
        "vflip,"
        "drawtext=text='" + p.title + "':fontsize=28:fontcolor=white:box=1:boxcolor=black@0.70:boxborderw=10:x=(w-text_w)/2:y=h-70";
    const std::string cmd =
        "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
        std::to_string(width) + "x" + std::to_string(height) +
        " -framerate " + std::to_string(static_cast<int>(fps)) +
        " -i - -vf \"" + filter + "\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";

    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
        std::cerr << p.id << ": failed to open ffmpeg pipe\n";
        return false;
    }
    for (int f = 0; f < totalFrames; ++f) {
        const auto px = renderFrame(f);
        std::fwrite(px.data(), 1, px.size(), pipe);
    }
    const int rc = pclose(pipe);
    if (rc != 0) {
        std::cerr << p.id << ": ffmpeg exited with " << rc << "\n";
        return false;
    }
    std::cout << p.id << ": wrote " << totalFrames << " frames to " << outPath << "\n";
    return true;
}

int main(int argc, char** argv) {
    std::string outDir = "documentary_presets";
    std::string only;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--only") == 0 && i + 1 < argc) {
            only = argv[++i];
        } else {
            outDir = argv[i];
        }
    }
    std::filesystem::create_directories(outDir);

    const std::string layersDir = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/layers_v2_1";
    const int width = 1920;
    const int height = 1080;
    const float fps = 30.f;

    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x13171f));
    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    threepp::ImageLoader loader;
    std::vector<std::shared_ptr<threepp::Texture>> layerTex = {
        loadLayer(loader, layersDir, "torso"),
        loadLayer(loader, layersDir, "head"),
        loadLayer(loader, layersDir, "upper_arm"),
        loadLayer(loader, layersDir, "forearm"),
        loadLayer(loader, layersDir, "hand"),
    };

    // Same atmospheric backdrop as the V2.1 spike, built inline (1024x576 dark vignette).
    std::vector<unsigned char> rgb(static_cast<std::size_t>(1024 * 576 * 3));
    for (int y = 0; y < 576; ++y) {
        for (int x = 0; x < 1024; ++x) {
            const std::size_t idx = static_cast<std::size_t>((y * 1024 + x) * 3);
            const float v = (y + 0.5f) / 576.f;
            const float u = (x + 0.5f) / 1024.f;
            const float dx = (u - 0.5f) * 1.5f, dy = (v - 0.52f) * 1.25f;
            const float vig = std::clamp(1.0f - (dx * dx + dy * dy) * 0.58f, 0.0f, 1.0f);
            rgb[idx + 0] = static_cast<unsigned char>(std::clamp((28.f + 32.f * vig) * (0.85f + 0.28f * v), 0.f, 255.f));
            rgb[idx + 1] = static_cast<unsigned char>(std::clamp((35.f + 40.f * vig) * (0.85f + 0.28f * v), 0.f, 255.f));
            rgb[idx + 2] = static_cast<unsigned char>(std::clamp((48.f + 56.f * vig) * (0.85f + 0.28f * v), 0.f, 255.f));
        }
    }
    auto bgTex = threepp::DataTexture::create(rgb, 1024, 576);
    bgTex->format = threepp::Format::RGB;
    bgTex->needsUpdate();

    int ok = 0, fail = 0;
    for (const auto& p : makePresets()) {
        if (!only.empty() && p.id != only) continue;
        if (renderPreset(p, renderer, layersDir, outDir, width, height, fps, bgTex, layerTex)) {
            ++ok;
        } else {
            ++fail;
        }
    }
    std::cout << "documentary_presets: ok=" << ok << " failed=" << fail << "\n";
    return fail == 0 ? 0 : 1;
}
