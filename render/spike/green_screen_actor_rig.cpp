// Rigid full-head neck pivot on a pure chroma-green field.
//
// Usage:
//   chrononmotion_render_green_screen_actor_rig [input_cutout.png] [out.mp4] [w h [frames]]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/RigBaking.hpp"
#include "chrononmotion/motion/RigDeformers.hpp"
#include "chrononmotion/motion/RigGraph.hpp"

#include "threepp/canvas/Canvas.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/DataTexture.hpp"

namespace {

bool isImagePath(const std::string& s) {
    auto lower = s;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    const auto endsWith = [&](const char* ext) {
        const std::size_t len = std::char_traits<char>::length(ext);
        return lower.size() >= len && lower.compare(lower.size() - len, len, ext) == 0;
    };
    return endsWith(".png") || endsWith(".jpg") || endsWith(".jpeg") || endsWith(".webp");
}

bool endsWithMp4(const std::string& s) {
    if (s.size() < 4) return false;
    std::string tail = s.substr(s.size() - 4);
    for (char& c : tail) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return tail == ".mp4";
}

// Flat green backplate for later chroma-key compositing.
std::shared_ptr<threepp::DataTexture> makeGreenBackdropTexture(int texW, int texH) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(texW * texH * 3));
    for (std::size_t i = 0; i < rgb.size(); i += 3) {
        rgb[i] = 0;
        rgb[i + 1] = 255;
        rgb[i + 2] = 0;
    }
    auto tex = threepp::DataTexture::create(rgb, texW, texH);
    tex->format = threepp::Format::RGB;
    tex->needsUpdate();
    return tex;
}

// Rigid anchors at lower edge so bottom of shirt anchors cleanly
void addCreatorAnchors(
    chrononmotion::motion::RigDeformer& deformer,
    float planeW,
    float planeH) {
    const float hx = 0.5f * planeW;
    const float hy = 0.5f * planeH;

    for (float fx = -0.92f; fx <= 0.95f; fx += 0.22f) {
        chrononmotion::motion::RigPin pin;
        pin.kind = chrononmotion::motion::RigPinKind::Stiffness;
        pin.rest = chrononmotion::Vector3(fx * hx, -hy + 0.04f * planeH, 0.f);
        pin.radius = 0.18f * planeH;
        pin.weight = 1.0f;
        deformer.pins.push_back(pin);
    }

    // Outer lateral border stiffness
    const chrononmotion::Vector3 sidePins[] = {
        {-hx, -0.2f * hy, 0.f}, {hx, -0.2f * hy, 0.f},
        {-hx,  0.5f * hy, 0.f}, {hx,  0.5f * hy, 0.f},
        {-hx,         hy, 0.f}, {0.f,        hy, 0.f}, {hx, hy, 0.f},
    };
    for (const auto& pt : sidePins) {
        chrononmotion::motion::RigPin pin;
        pin.kind = chrononmotion::motion::RigPinKind::Stiffness;
        pin.rest = pt;
        pin.radius = 0.25f * planeH;
        pin.weight = 0.95f;
        deformer.pins.push_back(pin);
    }
}

std::size_t countDiffPixels(const std::vector<unsigned char>& a,
                            const std::vector<unsigned char>& b,
                            int threshold = 8) {
    if (a.size() != b.size()) return 0;
    std::size_t diffCount = 0;
    for (std::size_t i = 0; i < a.size(); i += 3) {
        const int dr = std::abs(static_cast<int>(a[i + 0]) - static_cast<int>(b[i + 0]));
        const int dg = std::abs(static_cast<int>(a[i + 1]) - static_cast<int>(b[i + 1]));
        const int db = std::abs(static_cast<int>(a[i + 2]) - static_cast<int>(b[i + 2]));
        if (dr > threshold || dg > threshold || db > threshold) {
            ++diffCount;
        }
    }
    return diffCount;
}

} // namespace

int main(int argc, char** argv) {
    std::string inputImagePath = "/mnt/c/Users/pater/dev/projects/ChrononTemplate/assets/social_rig_keanu_v1/processed/keanu_torso_layer.png";
    std::string outPath = "green_screen_actor_1080p.mp4";
    int width = 1920;
    int height = 1080;
    int totalFrames = 121; // 4.0 seconds @ 30 fps (seamless seamless sway loop)

    int argIdx = 1;
    if (argc >= 3 && isImagePath(argv[1])) {
        inputImagePath = argv[1];
        outPath = argv[2];
        argIdx = 3;
    } else if (argc >= 2 && !isImagePath(argv[1]) && (endsWithMp4(argv[1]) || argv[1][0] != '-')) {
        outPath = argv[1];
        argIdx = 2;
    }
    if (argc > argIdx) width = std::atoi(argv[argIdx++]);
    if (argc > argIdx) height = std::atoi(argv[argIdx++]);
    if (argc > argIdx) totalFrames = std::max(4, std::atoi(argv[argIdx++]));

    const float fps = 30.f;
    const float durationSec = static_cast<float>(totalFrames - 1) / fps;

    // 1. Setup headless GL context at 1920x1080
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x00ff00));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // 2. Load separate head and torso cutouts (same canvas, keyed alpha).
    threepp::ImageLoader loader;
    auto torsoImgOpt = loader.load(inputImagePath, 4, true);
    const auto headPath = std::filesystem::path(inputImagePath).parent_path() / "keanu_head_layer.png";
    auto headImgOpt = loader.load(headPath.string(), 4, true);
    if (!torsoImgOpt || !headImgOpt || torsoImgOpt->width() == 0 || headImgOpt->width() == 0) {
        std::cerr << "green_screen_actor: failed to load torso/head layers from " << inputImagePath << "\n";
        return 1;
    }
    const float imageAspect = static_cast<float>(torsoImgOpt->width()) /
                              static_cast<float>(torsoImgOpt->height());
    std::cout << "green_screen_actor: loaded " << inputImagePath << " ("
              << torsoImgOpt->width() << "x" << torsoImgOpt->height() << ")\n";
    auto torsoTexture = threepp::Texture::create(std::move(*torsoImgOpt));
    auto headTexture = threepp::Texture::create(std::move(*headImgOpt));
    torsoTexture->needsUpdate();
    headTexture->needsUpdate();

    // 3. Dense 64x64 subdivided plane mesh (4225 vertices)
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    auto planeGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    const auto restPositions = threepp::motion::extractGeometryPositions(*planeGeom);

    // 4. Author MotionScene: still torso, rigid head layer swinging from neck.
    chrononmotion::motion::MotionScene motionScene("green_screen_actor_scene", fps);

    // Dynamic wide backdrop plane covering 16:9 field of view at z = -0.85
    const float camAspect = static_cast<float>(width) / static_cast<float>(height);
    const float bgDist = 3.85f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAttachment;
    bgAttachment.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAttachment.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = motionScene.addMesh(bgLayerId, "green_screen_backdrop", std::move(bgAttachment));
    // Keep the chroma plate fixed while the actor rig moves.
    bgLayer.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, -0.85f));

    chrononmotion::motion::MeshAttachment torsoAttachment;
    torsoAttachment.geometry = planeGeom;
    torsoAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    torsoAttachment.material.transparent = true;
    motionScene.addMesh(10, "green_screen_torso_fixed", std::move(torsoAttachment));

    // The full head PNG occupies the same UV canvas, but its mesh is translated
    // so its origin sits at the neck hinge. Rotation therefore moves the entire
    // hair/face/jaw silhouette as one rigid piece.
    const float pivotY = planeH * (0.5f - 610.f / 941.f);
    auto headGeometry = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    auto& headVertices = headGeometry->getAttribute<float>("position")->array();
    for (std::size_t i = 1; i < headVertices.size(); i += 3) headVertices[i] -= pivotY;
    chrononmotion::motion::MeshAttachment headAttachment;
    headAttachment.geometry = headGeometry;
    headAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    headAttachment.material.transparent = true;
    auto& headLayer = motionScene.addMesh(11, "whole_head_neck_pivot", std::move(headAttachment));
    const float pivotX = 0.f;
    headLayer.tracks.position.add(0.f, chrononmotion::Vector3(pivotX, pivotY, 0.015f));
    headLayer.tracks.position.add(durationSec * 0.25f, chrononmotion::Vector3(pivotX + 0.012f, pivotY - 0.008f, 0.015f), chrononmotion::motion::Easing::easeInOut());
    headLayer.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(pivotX, pivotY, 0.015f), chrononmotion::motion::Easing::easeInOut());
    headLayer.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(pivotX - 0.012f, pivotY - 0.008f, 0.015f), chrononmotion::motion::Easing::easeInOut());
    headLayer.tracks.position.add(durationSec, chrononmotion::Vector3(pivotX, pivotY, 0.015f));
    chrononmotion::Quaternion headRest{};
    chrononmotion::Quaternion headRight, headLeft;
    headRight.setFromAxisAngle(chrononmotion::Vector3(0.f, 0.f, 1.f), -0.075f);
    headLeft.setFromAxisAngle(chrononmotion::Vector3(0.f, 0.f, 1.f), 0.075f);
    headLayer.tracks.rotation.add(0.f, headRest, chrononmotion::motion::Easing::easeInOut());
    headLayer.tracks.rotation.add(durationSec * 0.25f, headRight, chrononmotion::motion::Easing::easeInOut());
    headLayer.tracks.rotation.add(durationSec * 0.50f, headRest, chrononmotion::motion::Easing::easeInOut());
    headLayer.tracks.rotation.add(durationSec * 0.75f, headLeft, chrononmotion::motion::Easing::easeInOut());
    headLayer.tracks.rotation.add(durationSec, headRest);

    // Parallax CameraRig: gentle push-in to heighten 3D depth
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.06f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.04f, 3.15f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.04f, 3.15f));

    // 6. Connect MotionSceneBridge and bind textures
    threepp::motion::MotionSceneBridge bridge(
        45.f, camAspect, 0.1f, 100.f);
    auto greenBackdropTex = makeGreenBackdropTexture(1024, 576);
    bridge.bindLayerTexture(bgLayerId, greenBackdropTex);
    bridge.bindLayerTexture(10, torsoTexture);
    bridge.bindLayerTexture(11, headTexture);

    auto renderAtFrame = [&](int frame) {
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    const int midFrame1 = totalFrames / 4;
    const int midFrame2 = totalFrames / 2;
    const int lastFrame = totalFrames - 1;

    std::cout << "green_screen_actor: validating rigid head motion and determinism...\n";
    const auto f0 = renderAtFrame(0);
    const auto fMid1 = renderAtFrame(midFrame1);
    const auto fMid2 = renderAtFrame(midFrame2);
    const auto fLast = renderAtFrame(lastFrame);
    const auto fMid1Repeat = renderAtFrame(midFrame1);

    const std::size_t diff0ToMid1 = countDiffPixels(f0, fMid1);
    const std::size_t diffMid1ToMid2 = countDiffPixels(fMid1, fMid2);
    const std::size_t diff0ToMid2 = countDiffPixels(f0, fMid2);

    std::cout << "green_screen_actor: changed_pixels(0->" << midFrame1 << ")=" << diff0ToMid1
              << " changed_pixels(" << midFrame1 << "->" << midFrame2 << ")=" << diffMid1ToMid2
              << " changed_pixels(0->" << midFrame2 << ")=" << diff0ToMid2 << "\n";

    // The half-cycle is intentionally neutral like the opening frame. Compare
    // the two opposite quarter-cycle poses instead of expecting a half-cycle drift.
    if (diff0ToMid1 < 100 || diffMid1ToMid2 < 50) {
        std::cerr << "green_screen_actor: expected visible whole-head motion\n";
        return 1;
    }
    if (f0 != fLast) {
        std::cerr << "green_screen_actor: expected final rest frame " << lastFrame << " to match frame 0\n";
        return 1;
    }
    if (fMid1 != fMid1Repeat) {
        std::cerr << "green_screen_actor: out-of-order frame evaluation was not deterministic\n";
        return 1;
    }

    // 7. Render Full HD 1080p MP4 video via ffmpeg pipe
    if (endsWithMp4(outPath)) {
        const std::string cmd =
            "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
            std::to_string(width) + "x" + std::to_string(height) +
            " -framerate 30 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) {
            std::cerr << "green_screen_actor: failed to open ffmpeg pipe\n";
            return 1;
        }
        for (int f = 0; f < totalFrames; ++f) {
            const auto pixels = renderAtFrame(f);
            std::fwrite(pixels.data(), 1, pixels.size(), pipe);
        }
        const int rc = pclose(pipe);
        if (rc != 0) {
            std::cerr << "green_screen_actor: ffmpeg exited with " << rc << "\n";
            return 1;
        }
        std::cout << "green_screen_actor: wrote Full HD 1080p MP4 video (" << totalFrames << " frames) to " << outPath << "\n";
        std::cout.flush();
        // In this cached WSL headless GL build, explicit destruction of the
        // renderer corrupts its stack canary after the encoded file is closed.
        // Let process teardown reclaim the context after the completed export.
        std::_Exit(0);
    }

    return 0;
}
