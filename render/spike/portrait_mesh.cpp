// Proof & Tool: Portrait Image Mesh Animation with Chroma-Key Transparency & 2.5D Rigging.
//
// Demonstrates realistic 2.5D animation of an isolated foreground portrait (person with
// background removed / alpha channel) mapped onto a deformable 64x64 PlaneGeometry.
//
// Key Features:
//   1. Clean RGBA alpha blending over an offscreen cinematic studio backdrop plane (z = -0.75).
//   2. Anatomical PinWarp RigProgram:
//      - Head look/nod/tilt with 2.5D Z-relief (head pops out toward camera).
//      - Rhythmic thoracic breathing (chest expansion in scale and depth).
//      - Shoulder micro-sway coordinated with breath.
//      - Solid stiffness pinning at the torso base (no floating / no bottom sliding).
//   3. Three-tier parallax camera rig highlighting true 3D depth between backdrop, body, and head.
//   4. Deterministic headless GL rendering exported directly to MP4 via ffmpeg pipe.
//
// Usage:
//   chrononmotion_render_portrait_mesh [input_rgba.png] [out.mp4|out.ppm] [w h [frames]]

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

// Procedural studio backdrop with soft radial vignette and cinematic slate gradient
std::shared_ptr<threepp::DataTexture> makeStudioBackdropTexture(int texW, int texH) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(texW * texH * 3));
    for (int y = 0; y < texH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(texH); // 0 bottom .. 1 top
        for (int x = 0; x < texW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(texW);
            const float dx = (u - 0.5f) * 1.4f;
            const float dy = (v - 0.55f) * 1.4f;
            const float dist2 = dx * dx + dy * dy;

            // Center glow (behind head) fading smoothly to elegant dark navy / slate edges
            const float vignette = std::clamp(1.0f - dist2 * 0.75f, 0.0f, 1.0f);
            const float r = (24.f + 48.f * vignette) * (0.85f + 0.25f * v);
            const float g = (32.f + 62.f * vignette) * (0.85f + 0.25f * v);
            const float b = (48.f + 92.f * vignette) * (0.85f + 0.25f * v);

            const std::size_t idx = static_cast<std::size_t>((y * texW + x) * 3);
            rgb[idx + 0] = static_cast<unsigned char>(std::clamp(r, 0.f, 255.f));
            rgb[idx + 1] = static_cast<unsigned char>(std::clamp(g, 0.f, 255.f));
            rgb[idx + 2] = static_cast<unsigned char>(std::clamp(b, 0.f, 255.f));
        }
    }
    auto tex = threepp::DataTexture::create(rgb, texW, texH);
    tex->format = threepp::Format::RGB;
    tex->needsUpdate();
    return tex;
}

// Anchors base of the torso and outer border so only anatomical features deform
void addPortraitAnchors(
    chrononmotion::motion::RigDeformer& deformer,
    float planeW,
    float planeH) {
    const float hx = 0.5f * planeW;
    const float hy = 0.5f * planeH;

    // 1. Solid bottom anchors along the entire lower torso base (y = -hy)
    for (float fx = -0.9f; fx <= 0.95f; fx += 0.3f) {
        chrononmotion::motion::RigPin pin;
        pin.kind = chrononmotion::motion::RigPinKind::Stiffness;
        pin.rest = chrononmotion::Vector3(fx * hx, -hy + 0.05f * planeH, 0.f);
        pin.radius = 0.22f * planeH;
        pin.weight = 1.0f;
        deformer.pins.push_back(pin);
    }

    // 2. Lateral perimeter boundary pins to keep mesh corners firmly static
    const chrononmotion::Vector3 sidePts[] = {
        {-hx, -0.2f * hy, 0.f}, {hx, -0.2f * hy, 0.f},
        {-hx,  0.5f * hy, 0.f}, {hx,  0.5f * hy, 0.f},
        {-hx,         hy, 0.f}, {0.f,        hy, 0.f}, {hx, hy, 0.f},
    };
    for (const auto& pt : sidePts) {
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
    std::string inputImagePath = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/person_transparent.png";
    std::string outPath = "portrait_rig.mp4";
    int width = 768;
    int height = 768;
    int totalFrames = 121; // 4.0 seconds @ 30 fps (seamless seamless breathing & head look loop)

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

    // 1. Setup headless GL context
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x0f141c));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // 2. Load the transparent portrait image
    threepp::ImageLoader loader;
    auto imgOpt = loader.load(inputImagePath, 4, true);
    if (!imgOpt || imgOpt->width() == 0 || imgOpt->height() == 0) {
        std::cerr << "portrait_mesh: failed to load image " << inputImagePath << "\n";
        return 1;
    }
    const float imageAspect = static_cast<float>(imgOpt->width()) /
                              static_cast<float>(imgOpt->height());
    std::cout << "portrait_mesh: loaded " << inputImagePath << " ("
              << imgOpt->width() << "x" << imgOpt->height() << ")\n";
    auto portraitTexture = threepp::Texture::create(std::move(*imgOpt));
    portraitTexture->needsUpdate();

    // 3. Create fine 64x64 subdivided plane mesh (4225 vertices)
    const float planeH = 2.4f;
    const float planeW = planeH * imageAspect;
    auto planeGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    const auto restPositions = threepp::motion::extractGeometryPositions(*planeGeom);

    // 4. Author the MotionScene: studio backdrop + transparent hero portrait + 3D camera
    chrononmotion::motion::MotionScene motionScene("portrait_scene", fps);

    // Cinematic studio backdrop plane at z = -0.75
    const float camAspect = static_cast<float>(width) / static_cast<float>(height);
    const float bgDist = 3.6f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAttachment;
    bgAttachment.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAttachment.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = motionScene.addMesh(bgLayerId, "studio_backdrop", std::move(bgAttachment));
    bgLayer.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, -0.75f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec * 0.5f, chrononmotion::Vector3(-0.035f, -0.015f, -0.75f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, -0.75f));

    // Foreground hero layer: transparent deformable portrait mesh at z = 0.0
    chrononmotion::motion::MeshAttachment portraitAttachment;
    portraitAttachment.geometry = planeGeom;
    portraitAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    portraitAttachment.material.transparent = true;
    const chrononmotion::motion::Layer::Id portraitLayerId = 10;
    auto& portraitLayer = motionScene.addMesh(portraitLayerId, "deformable_portrait", std::move(portraitAttachment));

    // Parallax CameraRig: subtle push-in and drift creating depth between backdrop, torso, and head
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.08f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.05f, 2.62f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.045f, 0.07f, 2.48f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.05f, 2.62f));

    // 5. Build Anatomical RigProgram with 3 Independent Controllers:
    //    - Controller 1: Head look, nod, and 2.5D relief (head tilts, looks slightly, and projects forward in Z)
    //    - Controller 2: Chest & diaphragmatic breathing (2 full cycles of rhythmic lung expansion)
    //    - Controller 3: Shoulder micro-sway and neck alignment
    chrononmotion::motion::RigProgram rigProgram;
    {
        for (chrononmotion::motion::RigNodeId id = 1; id <= 3; ++id) {
            chrononmotion::motion::RigNode ctrl;
            ctrl.id = id;
            ctrl.kind = chrononmotion::motion::RigNodeKind::Controller;
            rigProgram.graph.nodes.push_back(ctrl);
        }

        // Controller 1 Animation: smooth natural head movement cycle
        chrononmotion::motion::RigAnimatedNode headAnim;
        headAnim.id = 1;
        headAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.28f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.52f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.78f, chrononmotion::Vector3(0.60f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(headAnim));

        // Controller 2 Animation: 2 organic breathing cycles
        chrononmotion::motion::RigAnimatedNode breathAnim;
        breathAnim.id = 2;
        breathAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.25f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(0.15f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(breathAnim));

        // Controller 3 Animation: subtle shoulder articulation
        chrononmotion::motion::RigAnimatedNode shoulderAnim;
        shoulderAnim.id = 3;
        shoulderAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        shoulderAnim.tracks.position.add(durationSec * 0.20f, chrononmotion::Vector3(0.70f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        shoulderAnim.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(0.20f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        shoulderAnim.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        shoulderAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(shoulderAnim));

        // Operator 0: Head Gaze / Nod / 3D Volumetric Pop
        chrononmotion::motion::RigDeformer headWarp;
        headWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        headWarp.amount = 1.f;
        addPortraitAnchors(headWarp, planeW, planeH);

        // Position pin on head center with Z-pop (dx = +0.032, dy = -0.02, dz = +0.11)
        chrononmotion::motion::RigPin headMove;
        headMove.kind = chrononmotion::motion::RigPinKind::Position;
        headMove.rest = chrononmotion::Vector3(0.01f * planeW, 0.18f * planeH, 0.f);
        headMove.target = chrononmotion::Vector3(0.038f * planeW, 0.162f * planeH, 0.115f * planeH);
        headMove.radius = 0.40f * planeH;
        headMove.weight = 1.0f;
        headWarp.pins.push_back(headMove);

        // Rotation pin on neck pivot (gentle head tilt: ~3.8 degrees)
        chrononmotion::motion::RigPin headTilt;
        headTilt.kind = chrononmotion::motion::RigPinKind::Rotation;
        headTilt.rest = chrononmotion::Vector3(0.0f, 0.05f * planeH, 0.f);
        headTilt.rotation.setFromAxisAngle(
            chrononmotion::Vector3(0.15f, -0.65f, -0.74f).normalize(), 0.068f);
        headTilt.radius = 0.42f * planeH;
        headTilt.weight = 0.90f;
        headWarp.pins.push_back(headTilt);

        // Operator 1: Chest & Diaphragmatic Breathing
        chrononmotion::motion::RigDeformer breathWarp;
        breathWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        breathWarp.amount = 1.f;
        addPortraitAnchors(breathWarp, planeW, planeH);

        chrononmotion::motion::RigPin chestBreath;
        chestBreath.kind = chrononmotion::motion::RigPinKind::Scale;
        chestBreath.rest = chrononmotion::Vector3(0.0f, -0.22f * planeH, 0.f);
        chestBreath.scale = chrononmotion::Vector3(1.052f, 1.040f, 1.035f);
        chestBreath.radius = 0.38f * planeH;
        chestBreath.weight = 1.0f;
        breathWarp.pins.push_back(chestBreath);

        // Subtle forward chest lift in Z
        chrononmotion::motion::RigPin chestLift;
        chestLift.kind = chrononmotion::motion::RigPinKind::Position;
        chestLift.rest = chrononmotion::Vector3(0.0f, -0.20f * planeH, 0.f);
        chestLift.target = chrononmotion::Vector3(0.0f, -0.192f * planeH, 0.045f * planeH);
        chestLift.radius = 0.35f * planeH;
        chestLift.weight = 0.85f;
        breathWarp.pins.push_back(chestLift);

        // Operator 2: Shoulder articulation
        chrononmotion::motion::RigDeformer shoulderWarp;
        shoulderWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        shoulderWarp.amount = 1.f;
        addPortraitAnchors(shoulderWarp, planeW, planeH);

        // Left shoulder lift
        chrononmotion::motion::RigPin leftShoulder;
        leftShoulder.kind = chrononmotion::motion::RigPinKind::Position;
        leftShoulder.rest = chrononmotion::Vector3(-0.28f * planeW, -0.22f * planeH, 0.f);
        leftShoulder.target = chrononmotion::Vector3(-0.282f * planeW, -0.208f * planeH, 0.02f * planeH);
        leftShoulder.radius = 0.28f * planeH;
        leftShoulder.weight = 0.90f;
        shoulderWarp.pins.push_back(leftShoulder);

        // Right shoulder micro-tilt
        chrononmotion::motion::RigPin rightShoulder;
        rightShoulder.kind = chrononmotion::motion::RigPinKind::Position;
        rightShoulder.rest = chrononmotion::Vector3(0.28f * planeW, -0.22f * planeH, 0.f);
        rightShoulder.target = chrononmotion::Vector3(0.283f * planeW, -0.214f * planeH, 0.015f * planeH);
        rightShoulder.radius = 0.28f * planeH;
        rightShoulder.weight = 0.90f;
        shoulderWarp.pins.push_back(rightShoulder);

        chrononmotion::motion::RigGeometryBinding geomBinding;
        geomBinding.id = 1;
        geomBinding.source = restPositions;
        geomBinding.deformers.operators.push_back(std::move(headWarp));
        geomBinding.deformers.operators.push_back(std::move(breathWarp));
        geomBinding.deformers.operators.push_back(std::move(shoulderWarp));
        rigProgram.geometry.push_back(std::move(geomBinding));

        rigProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
            1, chrononmotion::motion::RigWeightKind::Deformer, 1, 0});
        rigProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
            2, chrononmotion::motion::RigWeightKind::Deformer, 1, 1});
        rigProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
            3, chrononmotion::motion::RigWeightKind::Deformer, 1, 2});
    }

    chrononmotion::motion::RigProgramEvaluator rigEvaluator(rigProgram);

    // 6. Connect MotionSceneBridge and bind textures
    threepp::motion::MotionSceneBridge bridge(
        45.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
    auto studioBackdropTex = makeStudioBackdropTexture(512, 512);
    bridge.bindLayerTexture(bgLayerId, studioBackdropTex);
    bridge.bindLayerTexture(portraitLayerId, portraitTexture);

    auto renderAtFrame = [&](int frame) {
        const auto rigFrame = rigEvaluator.evaluate(frame, fps);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        bridge.applyRigFrame(portraitLayerId, rigFrame.geometry.front(), false);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    const int midFrame1 = totalFrames / 4;
    const int midFrame2 = totalFrames / 2;
    const int lastFrame = totalFrames - 1;

    std::cout << "portrait_mesh: validating deformation and determinism...\n";
    const auto f0 = renderAtFrame(0);
    const auto fMid1 = renderAtFrame(midFrame1);
    const auto fMid2 = renderAtFrame(midFrame2);
    const auto fLast = renderAtFrame(lastFrame);
    const auto fMid1Repeat = renderAtFrame(midFrame1);

    const std::size_t diff0ToMid1 = countDiffPixels(f0, fMid1);
    const std::size_t diffMid1ToMid2 = countDiffPixels(fMid1, fMid2);
    const std::size_t diff0ToMid2 = countDiffPixels(f0, fMid2);

    std::cout << "portrait_mesh: changed_pixels(0->" << midFrame1 << ")=" << diff0ToMid1
              << " changed_pixels(" << midFrame1 << "->" << midFrame2 << ")=" << diffMid1ToMid2
              << " changed_pixels(0->" << midFrame2 << ")=" << diff0ToMid2 << "\n";

    if (diff0ToMid1 < 100 || diffMid1ToMid2 < 50 || diff0ToMid2 < 100) {
        std::cerr << "portrait_mesh: expected significant pixel motion from mesh deformation\n";
        return 1;
    }
    if (f0 != fLast) {
        std::cerr << "portrait_mesh: expected final rest frame " << lastFrame << " to match frame 0\n";
        return 1;
    }
    if (fMid1 != fMid1Repeat) {
        std::cerr << "portrait_mesh: out-of-order frame evaluation was not deterministic\n";
        return 1;
    }

    // 7. Render output: MP4 video or PPM frame
    if (endsWithMp4(outPath)) {
        const std::string cmd =
            "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
            std::to_string(width) + "x" + std::to_string(height) +
            " -framerate 30 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) {
            std::cerr << "portrait_mesh: failed to open ffmpeg pipe\n";
            return 1;
        }
        for (int f = 0; f < totalFrames; ++f) {
            const auto pixels = renderAtFrame(f);
            std::fwrite(pixels.data(), 1, pixels.size(), pipe);
        }
        const int rc = pclose(pipe);
        if (rc != 0) {
            std::cerr << "portrait_mesh: ffmpeg exited with " << rc << "\n";
            return 1;
        }
        std::cout << "portrait_mesh: wrote MP4 video (" << totalFrames << " frames) to " << outPath << "\n";
    } else {
        std::ofstream ppm(outPath, std::ios::binary);
        if (!ppm) {
            std::cerr << "portrait_mesh: failed to open " << outPath << "\n";
            return 1;
        }
        ppm << "P6\n" << width << " " << height << "\n255\n";
        ppm.write(reinterpret_cast<const char*>(fMid2.data()), static_cast<std::streamsize>(fMid2.size()));
        std::cout << "portrait_mesh: wrote deformed frame PPM to " << outPath << "\n";
    }

    return 0;
}
