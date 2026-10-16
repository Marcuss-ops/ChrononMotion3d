// Proof & Tool: MrBeast 2.5D Puppet Rigging & Parallax Animation at 1920x1080 Full HD.
//
// Reproduces and enhances the reference video effect:
// Takes the freeze-frame cutout of MrBeast pointing his index finger with an open mouth,
// maps it onto a high-density deformable 64x64 PlaneGeometry with alpha transparency,
// and brings it to life via ChrononMotion3D's anatomical RigProgram:
//   1. Head look/nod/tilt with true 2.5D volumetric Z-pop out toward the camera.
//   2. Rhythmic thoracic breathing (natural chest expansion).
//   3. Subtle finger & hand micro-sway emphasizing the iconic pointing gesture.
//   4. Three-tier parallax camera rig against a cinematic dark-slate vignette studio backdrop.
//   5. Output rendered at full broadcast standard 1920x1080 @ 30fps.
//
// Usage:
//   chrononmotion_render_mrbeast_rig [input_cutout.png] [out.mp4] [w h [frames]]

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

// Procedural dark slate atmospheric backdrop matching the reference video
// with deep charcoal radial gradient and subtle bokeh light motes
std::shared_ptr<threepp::DataTexture> makeAtmosphericBackdropTexture(int texW, int texH) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(texW * texH * 3));
    for (int y = 0; y < texH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(texH);
        for (int x = 0; x < texW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(texW);
            const float dx = (u - 0.5f) * 1.6f;
            const float dy = (v - 0.5f) * 1.2f;
            const float dist2 = dx * dx + dy * dy;

            // Base slate gradient
            const float vignette = std::clamp(1.0f - dist2 * 0.55f, 0.0f, 1.0f);
            float r = (32.f + 25.f * vignette) * (0.85f + 0.3f * v);
            float g = (38.f + 30.f * vignette) * (0.85f + 0.3f * v);
            float b = (46.f + 40.f * vignette) * (0.85f + 0.3f * v);

            // Subtle glowing bokeh spots in background (matching reference particles)
            const float spot1 = std::exp(-((u - 0.22f) * (u - 0.22f) + (v - 0.68f) * (v - 0.68f)) / 0.004f);
            const float spot2 = std::exp(-((u - 0.78f) * (u - 0.78f) + (v - 0.42f) * (v - 0.42f)) / 0.008f);
            const float spot3 = std::exp(-((u - 0.85f) * (u - 0.85f) + (v - 0.20f) * (v - 0.20f)) / 0.006f);
            const float spot4 = std::exp(-((u - 0.15f) * (u - 0.15f) + (v - 0.35f) * (v - 0.35f)) / 0.003f);

            r += spot1 * 80.f + spot2 * 50.f + spot3 * 75.f + spot4 * 65.f;
            g += spot1 * 95.f + spot2 * 60.f + spot3 * 85.f + spot4 * 75.f;
            b += spot1 * 130.f + spot2 * 90.f + spot3 * 115.f + spot4 * 105.f;

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

void addMrBeastAnchors(
    chrononmotion::motion::RigDeformer& deformer,
    float planeW,
    float planeH) {
    const float hx = 0.5f * planeW;
    const float hy = 0.5f * planeH;

    // Anchor the bottom edge of the black shirt so torso sits firmly at the frame bottom
    for (float fx = -0.9f; fx <= 0.95f; fx += 0.25f) {
        chrononmotion::motion::RigPin pin;
        pin.kind = chrononmotion::motion::RigPinKind::Stiffness;
        pin.rest = chrononmotion::Vector3(fx * hx, -hy + 0.04f * planeH, 0.f);
        pin.radius = 0.20f * planeH;
        pin.weight = 1.0f;
        deformer.pins.push_back(pin);
    }

    // Anchor far right shoulder / outer perimeter
    const chrononmotion::Vector3 borderPins[] = {
        {0.44f * planeW, -0.22f * planeH, 0.f},
        {0.48f * planeW,  0.05f * planeH, 0.f},
        {-0.48f * planeW, 0.05f * planeH, 0.f},
        {0.0f,            hy,             0.f},
    };
    for (const auto& pt : borderPins) {
        chrononmotion::motion::RigPin pin;
        pin.kind = chrononmotion::motion::RigPinKind::Stiffness;
        pin.rest = pt;
        pin.radius = 0.26f * planeH;
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
    std::string inputImagePath = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/mrbeast_cutout_perfect.png";
    std::string outPath = "mrbeast_rig_1080p.mp4";
    int width = 1920;
    int height = 1080;
    int totalFrames = 121; // 4.0 seconds @ 30 fps (seamless seamless loop)

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
    renderer.setClearColor(threepp::Color(0x161b22));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // 2. Load MrBeast cutout texture
    threepp::ImageLoader loader;
    auto imgOpt = loader.load(inputImagePath, 4, true);
    if (!imgOpt || imgOpt->width() == 0 || imgOpt->height() == 0) {
        std::cerr << "mrbeast_rig: failed to load image " << inputImagePath << "\n";
        return 1;
    }
    const float imageAspect = static_cast<float>(imgOpt->width()) /
                              static_cast<float>(imgOpt->height());
    std::cout << "mrbeast_rig: loaded " << inputImagePath << " ("
              << imgOpt->width() << "x" << imgOpt->height() << ")\n";
    auto mrbeastTexture = threepp::Texture::create(std::move(*imgOpt));
    mrbeastTexture->needsUpdate();

    // 3. Subdivided PlaneGeometry (64x64 = 4225 vertices)
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    auto planeGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    const auto restPositions = threepp::motion::extractGeometryPositions(*planeGeom);

    // 4. Author MotionScene (atmospheric studio backdrop + hero MrBeast layer + camera parallax)
    chrononmotion::motion::MotionScene motionScene("mrbeast_scene", fps);

    // Atmospheric backdrop plane at z = -0.85
    const float camAspect = static_cast<float>(width) / static_cast<float>(height);
    const float bgDist = 3.8f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAttachment;
    bgAttachment.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAttachment.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = motionScene.addMesh(bgLayerId, "atmospheric_backdrop", std::move(bgAttachment));
    bgLayer.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec * 0.5f, chrononmotion::Vector3(-0.045f, -0.02f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, -0.85f));

    // Foreground hero layer: transparent deformable MrBeast mesh at z = 0.0
    chrononmotion::motion::MeshAttachment beastAttachment;
    beastAttachment.geometry = planeGeom;
    beastAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    beastAttachment.material.transparent = true;
    const chrononmotion::motion::Layer::Id beastLayerId = 10;
    auto& beastLayer = motionScene.addMesh(beastLayerId, "deformable_mrbeast", std::move(beastAttachment));

    // Parallax CameraRig: subtle push-in & cinematic drift at 1920x1080
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.04f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.02f, 2.70f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.06f, 0.04f, 2.54f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.02f, 2.70f));

    // 5. Anatomical RigProgram:
    //    - Controller 1: Head look, nod, and 2.5D relief (head tilts, looks slightly, and projects forward in Z)
    //    - Controller 2: Thoracic breathing (chest expansion)
    //    - Controller 3: Pointing finger micro-sway and hand pulse (enlivening the pose)
    chrononmotion::motion::RigProgram rigProgram;
    {
        for (chrononmotion::motion::RigNodeId id = 1; id <= 3; ++id) {
            chrononmotion::motion::RigNode ctrl;
            ctrl.id = id;
            ctrl.kind = chrononmotion::motion::RigNodeKind::Controller;
            rigProgram.graph.nodes.push_back(ctrl);
        }

        // Controller 1 Animation: Head cycle
        chrononmotion::motion::RigAnimatedNode headAnim;
        headAnim.id = 1;
        headAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.28f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.52f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.78f, chrononmotion::Vector3(0.55f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(headAnim));

        // Controller 2 Animation: Breathing cycle
        chrononmotion::motion::RigAnimatedNode breathAnim;
        breathAnim.id = 2;
        breathAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.25f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(0.18f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(breathAnim));

        // Controller 3 Animation: Pointing hand micro-sway
        chrononmotion::motion::RigAnimatedNode handAnim;
        handAnim.id = 3;
        handAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.22f, chrononmotion::Vector3(0.90f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.45f, chrononmotion::Vector3(0.25f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.70f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.88f, chrononmotion::Vector3(0.40f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(handAnim));

        // Operator 0: Head Gaze / Nod / 2.5D Volumetric Pop
        chrononmotion::motion::RigDeformer headWarp;
        headWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        headWarp.amount = 1.f;
        addMrBeastAnchors(headWarp, planeW, planeH);

        // Position pin on head center with Z-pop
        chrononmotion::motion::RigPin headMove;
        headMove.kind = chrononmotion::motion::RigPinKind::Position;
        headMove.rest = chrononmotion::Vector3(0.045f * planeW, 0.22f * planeH, 0.f);
        headMove.target = chrononmotion::Vector3(0.065f * planeW, 0.205f * planeH, 0.105f * planeH);
        headMove.radius = 0.38f * planeH;
        headMove.weight = 1.0f;
        headWarp.pins.push_back(headMove);

        // Rotation pin on neck pivot (head tilt: ~2.8 degrees)
        chrononmotion::motion::RigPin headTilt;
        headTilt.kind = chrononmotion::motion::RigPinKind::Rotation;
        headTilt.rest = chrononmotion::Vector3(0.04f * planeW, 0.08f * planeH, 0.f);
        headTilt.rotation.setFromAxisAngle(
            chrononmotion::Vector3(0.12f, -0.60f, -0.79f).normalize(), 0.052f);
        headTilt.radius = 0.40f * planeH;
        headTilt.weight = 0.88f;
        headWarp.pins.push_back(headTilt);

        // Operator 1: Thoracic Breathing
        chrononmotion::motion::RigDeformer breathWarp;
        breathWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        breathWarp.amount = 1.f;
        addMrBeastAnchors(breathWarp, planeW, planeH);

        chrononmotion::motion::RigPin chestBreath;
        chestBreath.kind = chrononmotion::motion::RigPinKind::Scale;
        chestBreath.rest = chrononmotion::Vector3(0.05f * planeW, -0.18f * planeH, 0.f);
        chestBreath.scale = chrononmotion::Vector3(1.045f, 1.035f, 1.025f);
        chestBreath.radius = 0.36f * planeH;
        chestBreath.weight = 1.0f;
        breathWarp.pins.push_back(chestBreath);

        // Operator 2: Pointing finger micro-sway
        chrononmotion::motion::RigDeformer fingerWarp;
        fingerWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        fingerWarp.amount = 1.f;
        addMrBeastAnchors(fingerWarp, planeW, planeH);

        // Index fingertip position pin
        chrononmotion::motion::RigPin fingerTip;
        fingerTip.kind = chrononmotion::motion::RigPinKind::Position;
        fingerTip.rest = chrononmotion::Vector3(-0.285f * planeW, 0.02f * planeH, 0.f);
        fingerTip.target = chrononmotion::Vector3(-0.272f * planeW, 0.042f * planeH, 0.055f * planeH);
        fingerTip.radius = 0.22f * planeH;
        fingerTip.weight = 1.0f;
        fingerWarp.pins.push_back(fingerTip);

        // Hand base rotation
        chrononmotion::motion::RigPin handBase;
        handBase.kind = chrononmotion::motion::RigPinKind::Position;
        handBase.rest = chrononmotion::Vector3(-0.26f * planeW, -0.22f * planeH, 0.f);
        handBase.target = chrononmotion::Vector3(-0.252f * planeW, -0.21f * planeH, 0.025f * planeH);
        handBase.radius = 0.25f * planeH;
        handBase.weight = 0.85f;
        fingerWarp.pins.push_back(handBase);

        chrononmotion::motion::RigGeometryBinding geomBinding;
        geomBinding.id = 1;
        geomBinding.source = restPositions;
        geomBinding.deformers.operators.push_back(std::move(headWarp));
        geomBinding.deformers.operators.push_back(std::move(breathWarp));
        geomBinding.deformers.operators.push_back(std::move(fingerWarp));
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
        45.f, camAspect, 0.1f, 100.f);
    auto atmosphericBackdropTex = makeAtmosphericBackdropTexture(1024, 576);
    bridge.bindLayerTexture(bgLayerId, atmosphericBackdropTex);
    bridge.bindLayerTexture(beastLayerId, mrbeastTexture);

    auto renderAtFrame = [&](int frame) {
        const auto rigFrame = rigEvaluator.evaluate(frame, fps);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        bridge.applyRigFrame(beastLayerId, rigFrame.geometry.front(), false);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    const int midFrame1 = totalFrames / 4;
    const int midFrame2 = totalFrames / 2;
    const int lastFrame = totalFrames - 1;

    std::cout << "mrbeast_rig: validating deformation and determinism...\n";
    const auto f0 = renderAtFrame(0);
    const auto fMid1 = renderAtFrame(midFrame1);
    const auto fMid2 = renderAtFrame(midFrame2);
    const auto fLast = renderAtFrame(lastFrame);
    const auto fMid1Repeat = renderAtFrame(midFrame1);

    const std::size_t diff0ToMid1 = countDiffPixels(f0, fMid1);
    const std::size_t diffMid1ToMid2 = countDiffPixels(fMid1, fMid2);
    const std::size_t diff0ToMid2 = countDiffPixels(f0, fMid2);

    std::cout << "mrbeast_rig: changed_pixels(0->" << midFrame1 << ")=" << diff0ToMid1
              << " changed_pixels(" << midFrame1 << "->" << midFrame2 << ")=" << diffMid1ToMid2
              << " changed_pixels(0->" << midFrame2 << ")=" << diff0ToMid2 << "\n";

    if (diff0ToMid1 < 100 || diffMid1ToMid2 < 50 || diff0ToMid2 < 100) {
        std::cerr << "mrbeast_rig: expected significant pixel motion from mesh deformation\n";
        return 1;
    }
    if (f0 != fLast) {
        std::cerr << "mrbeast_rig: expected final rest frame " << lastFrame << " to match frame 0\n";
        return 1;
    }
    if (fMid1 != fMid1Repeat) {
        std::cerr << "mrbeast_rig: out-of-order frame evaluation was not deterministic\n";
        return 1;
    }

    // 7. Render output MP4 at 1920x1080 via ffmpeg pipe
    if (endsWithMp4(outPath)) {
        const std::string cmd =
            "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
            std::to_string(width) + "x" + std::to_string(height) +
            " -framerate 30 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) {
            std::cerr << "mrbeast_rig: failed to open ffmpeg pipe\n";
            return 1;
        }
        for (int f = 0; f < totalFrames; ++f) {
            const auto pixels = renderAtFrame(f);
            std::fwrite(pixels.data(), 1, pixels.size(), pipe);
        }
        const int rc = pclose(pipe);
        if (rc != 0) {
            std::cerr << "mrbeast_rig: ffmpeg exited with " << rc << "\n";
            return 1;
        }
        std::cout << "mrbeast_rig: wrote Full HD 1080p MP4 video (" << totalFrames << " frames) to " << outPath << "\n";
    }

    return 0;
}
