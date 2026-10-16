// Proof & Tool: High-Quality Creator 2.5D Puppet Rig with Natural Side-to-Side Sway at 1920x1080.
//
// Implements the exact organic floating sway requested:
//   1. Smooth sinusoidal pendulum sway (left-to-right gentle waving motion with coordinated organic tilt).
//   2. High-density 64x64 deformable PlaneGeometry with razor-sharp alpha transparency (no compression artifacts).
//   3. Anatomical Puppet Rig with phase-lagged head flex (2.5D volumetric Z-pop),
//      thoracic breathing, and secondary flex on the raised pointing finger.
//   4. Three-tier parallax camera rig over a cinematic atmospheric backdrop at full 1920x1080 Full HD @ 30fps.
//
// Usage:
//   chrononmotion_render_creator_sway [input_cutout.png] [out.mp4] [w h [frames]]

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

// Atmospheric studio backdrop with slate charcoal gradient and glowing bokeh motes
std::shared_ptr<threepp::DataTexture> makeAtmosphericBackdropTexture(int texW, int texH) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(texW * texH * 3));
    for (int y = 0; y < texH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(texH);
        for (int x = 0; x < texW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(texW);
            const float dx = (u - 0.5f) * 1.5f;
            const float dy = (v - 0.52f) * 1.25f;
            const float dist2 = dx * dx + dy * dy;

            // Deep charcoal to dark navy-slate vignette
            const float vignette = std::clamp(1.0f - dist2 * 0.58f, 0.0f, 1.0f);
            float r = (28.f + 32.f * vignette) * (0.85f + 0.28f * v);
            float g = (35.f + 40.f * vignette) * (0.85f + 0.28f * v);
            float b = (48.f + 56.f * vignette) * (0.85f + 0.28f * v);

            // Floating bokeh light particles (matching high-end creator video style)
            const float spot1 = std::exp(-((u - 0.20f) * (u - 0.20f) + (v - 0.72f) * (v - 0.72f)) / 0.0035f);
            const float spot2 = std::exp(-((u - 0.82f) * (u - 0.82f) + (v - 0.45f) * (v - 0.45f)) / 0.0070f);
            const float spot3 = std::exp(-((u - 0.88f) * (u - 0.88f) + (v - 0.18f) * (v - 0.18f)) / 0.0050f);
            const float spot4 = std::exp(-((u - 0.12f) * (u - 0.12f) + (v - 0.32f) * (v - 0.32f)) / 0.0030f);

            r += spot1 * 85.f + spot2 * 55.f + spot3 * 80.f + spot4 * 70.f;
            g += spot1 * 105.f + spot2 * 70.f + spot3 * 95.f + spot4 * 85.f;
            b += spot1 * 145.f + spot2 * 105.f + spot3 * 130.f + spot4 * 120.f;

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
    std::string inputImagePath = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/creator_transparent.png";
    std::string outPath = "creator_sway_1080p.mp4";
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
    renderer.setClearColor(threepp::Color(0x13171f));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // 2. Load razor-sharp transparent creator texture
    threepp::ImageLoader loader;
    auto imgOpt = loader.load(inputImagePath, 4, true);
    if (!imgOpt || imgOpt->width() == 0 || imgOpt->height() == 0) {
        std::cerr << "creator_sway: failed to load image " << inputImagePath << "\n";
        return 1;
    }
    const float imageAspect = static_cast<float>(imgOpt->width()) /
                              static_cast<float>(imgOpt->height());
    std::cout << "creator_sway: loaded " << inputImagePath << " ("
              << imgOpt->width() << "x" << imgOpt->height() << ")\n";
    auto creatorTexture = threepp::Texture::create(std::move(*imgOpt));
    creatorTexture->needsUpdate();

    // 3. Dense 64x64 subdivided plane mesh (4225 vertices)
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    auto planeGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    const auto restPositions = threepp::motion::extractGeometryPositions(*planeGeom);

    // 4. Author MotionScene with natural SIDE-TO-SIDE SWAYING
    chrononmotion::motion::MotionScene motionScene("creator_scene", fps);

    // Dynamic wide backdrop plane covering 16:9 field of view at z = -0.85
    const float camAspect = static_cast<float>(width) / static_cast<float>(height);
    const float bgDist = 3.85f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAttachment;
    bgAttachment.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAttachment.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = motionScene.addMesh(bgLayerId, "atmospheric_backdrop", std::move(bgAttachment));
    // Slower parallax sway on backdrop for deep multi-plane sensation
    bgLayer.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec * 0.25f, chrononmotion::Vector3(-0.025f, -0.008f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(0.f, 0.f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(0.025f, -0.008f, -0.85f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, -0.85f));

    // Foreground hero creator layer:
    // Implements the continuous gentle side-to-side floating sway:
    //   t = 0.0s: center (0, 0)
    //   t = 1.0s: sway right (+0.075f) with subtle +2.2 deg tilt
    //   t = 2.0s: pass smoothly through center (0, 0)
    //   t = 3.0s: sway left (-0.075f) with subtle -2.2 deg tilt
    //   t = 4.0s: return smoothly to center (0, 0) for seamless loop
    chrononmotion::motion::MeshAttachment heroAttachment;
    heroAttachment.geometry = planeGeom;
    heroAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    heroAttachment.material.transparent = true;
    const chrononmotion::motion::Layer::Id heroLayerId = 10;
    auto& heroLayer = motionScene.addMesh(heroLayerId, "deformable_creator", std::move(heroAttachment));

    heroLayer.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.position.add(durationSec * 0.25f, chrononmotion::Vector3(0.082f, 0.018f, 0.f), chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(0.f, -0.010f, 0.f), chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(-0.082f, 0.018f, 0.f), chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));

    // Subtle natural body tilt coordinated with the sway
    chrononmotion::Quaternion qRest{};
    chrononmotion::Quaternion qRight;
    qRight.setFromAxisAngle(chrononmotion::Vector3(0.f, 0.f, 1.f), -0.038f); // slight right tilt
    chrononmotion::Quaternion qLeft;
    qLeft.setFromAxisAngle(chrononmotion::Vector3(0.f, 0.f, 1.f), 0.038f); // slight left tilt

    heroLayer.tracks.rotation.add(0.f, qRest, chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.rotation.add(durationSec * 0.25f, qRight, chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.rotation.add(durationSec * 0.50f, qRest, chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.rotation.add(durationSec * 0.75f, qLeft, chrononmotion::motion::Easing::easeInOut());
    heroLayer.tracks.rotation.add(durationSec, qRest);

    // Parallax CameraRig: gentle push-in to heighten 3D depth
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.06f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.04f, 2.68f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.02f, 0.05f, 2.52f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.04f, 2.68f));

    // 5. Anatomical RigProgram with 3 Controllers:
    //    - Controller 1: Organic head lag & 2.5D volumetric Z-pop (head flexes naturally during sway)
    //    - Controller 2: Diaphragmatic breathing (chest expansion)
    //    - Controller 3: Pointing finger secondary elasticity (reacts smoothly to body movement)
    chrononmotion::motion::RigProgram rigProgram;
    {
        for (chrononmotion::motion::RigNodeId id = 1; id <= 3; ++id) {
            chrononmotion::motion::RigNode ctrl;
            ctrl.id = id;
            ctrl.kind = chrononmotion::motion::RigNodeKind::Controller;
            rigProgram.graph.nodes.push_back(ctrl);
        }

        // Controller 1 Animation: Head lag cycle (slightly phase-shifted for organic weight)
        chrononmotion::motion::RigAnimatedNode headAnim;
        headAnim.id = 1;
        headAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.22f, chrononmotion::Vector3(0.95f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.48f, chrononmotion::Vector3(0.10f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.72f, chrononmotion::Vector3(-0.95f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(headAnim));

        // Controller 2 Animation: 2 Breathing cycles
        chrononmotion::motion::RigAnimatedNode breathAnim;
        breathAnim.id = 2;
        breathAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.25f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(0.15f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(breathAnim));

        // Controller 3 Animation: Finger secondary action
        chrononmotion::motion::RigAnimatedNode fingerAnim;
        fingerAnim.id = 3;
        fingerAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        fingerAnim.tracks.position.add(durationSec * 0.20f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        fingerAnim.tracks.position.add(durationSec * 0.45f, chrononmotion::Vector3(0.15f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        fingerAnim.tracks.position.add(durationSec * 0.70f, chrononmotion::Vector3(0.90f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        fingerAnim.tracks.position.add(durationSec * 0.88f, chrononmotion::Vector3(0.35f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        fingerAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(fingerAnim));

        // Operator 0: Head Flex, Nod & 2.5D Z-Pop
        chrononmotion::motion::RigDeformer headWarp;
        headWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        headWarp.amount = 1.f;
        addCreatorAnchors(headWarp, planeW, planeH);

        // Position pin on head center with Z-pop (dz = +0.11f)
        chrononmotion::motion::RigPin headMove;
        headMove.kind = chrononmotion::motion::RigPinKind::Position;
        headMove.rest = chrononmotion::Vector3(-0.015f * planeW, 0.21f * planeH, 0.f);
        headMove.target = chrononmotion::Vector3(0.018f * planeW, 0.198f * planeH, 0.11f * planeH);
        headMove.radius = 0.38f * planeH;
        headMove.weight = 1.0f;
        headWarp.pins.push_back(headMove);

        // Rotation pin on neck pivot (subtle counter-tilt: ~2.5 degrees)
        chrononmotion::motion::RigPin headTilt;
        headTilt.kind = chrononmotion::motion::RigPinKind::Rotation;
        headTilt.rest = chrononmotion::Vector3(-0.015f * planeW, 0.08f * planeH, 0.f);
        headTilt.rotation.setFromAxisAngle(
            chrononmotion::Vector3(0.10f, -0.65f, -0.75f).normalize(), 0.046f);
        headTilt.radius = 0.40f * planeH;
        headTilt.weight = 0.88f;
        headWarp.pins.push_back(headTilt);

        // Operator 1: Thoracic Breathing
        chrononmotion::motion::RigDeformer breathWarp;
        breathWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        breathWarp.amount = 1.f;
        addCreatorAnchors(breathWarp, planeW, planeH);

        chrononmotion::motion::RigPin chestBreath;
        chestBreath.kind = chrononmotion::motion::RigPinKind::Scale;
        chestBreath.rest = chrononmotion::Vector3(-0.05f * planeW, -0.22f * planeH, 0.f);
        chestBreath.scale = chrononmotion::Vector3(1.045f, 1.036f, 1.025f);
        chestBreath.radius = 0.38f * planeH;
        chestBreath.weight = 1.0f;
        breathWarp.pins.push_back(chestBreath);

        // Operator 2: Pointing finger micro-flex
        chrononmotion::motion::RigDeformer fingerWarp;
        fingerWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        fingerWarp.amount = 1.f;
        addCreatorAnchors(fingerWarp, planeW, planeH);

        chrononmotion::motion::RigPin fingerTip;
        fingerTip.kind = chrononmotion::motion::RigPinKind::Position;
        fingerTip.rest = chrononmotion::Vector3(-0.155f * planeW, -0.018f * planeH, 0.f);
        fingerTip.target = chrononmotion::Vector3(-0.145f * planeW, 0.008f * planeH, 0.045f * planeH);
        fingerTip.radius = 0.22f * planeH;
        fingerTip.weight = 1.0f;
        fingerWarp.pins.push_back(fingerTip);

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
    bridge.bindLayerTexture(heroLayerId, creatorTexture);

    auto renderAtFrame = [&](int frame) {
        const auto rigFrame = rigEvaluator.evaluate(frame, fps);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        bridge.applyRigFrame(heroLayerId, rigFrame.geometry.front(), false);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    const int midFrame1 = totalFrames / 4;
    const int midFrame2 = totalFrames / 2;
    const int lastFrame = totalFrames - 1;

    std::cout << "creator_sway: validating deformation and determinism...\n";
    const auto f0 = renderAtFrame(0);
    const auto fMid1 = renderAtFrame(midFrame1);
    const auto fMid2 = renderAtFrame(midFrame2);
    const auto fLast = renderAtFrame(lastFrame);
    const auto fMid1Repeat = renderAtFrame(midFrame1);

    const std::size_t diff0ToMid1 = countDiffPixels(f0, fMid1);
    const std::size_t diffMid1ToMid2 = countDiffPixels(fMid1, fMid2);
    const std::size_t diff0ToMid2 = countDiffPixels(f0, fMid2);

    std::cout << "creator_sway: changed_pixels(0->" << midFrame1 << ")=" << diff0ToMid1
              << " changed_pixels(" << midFrame1 << "->" << midFrame2 << ")=" << diffMid1ToMid2
              << " changed_pixels(0->" << midFrame2 << ")=" << diff0ToMid2 << "\n";

    if (diff0ToMid1 < 100 || diffMid1ToMid2 < 50 || diff0ToMid2 < 100) {
        std::cerr << "creator_sway: expected significant pixel motion from mesh deformation\n";
        return 1;
    }
    if (f0 != fLast) {
        std::cerr << "creator_sway: expected final rest frame " << lastFrame << " to match frame 0\n";
        return 1;
    }
    if (fMid1 != fMid1Repeat) {
        std::cerr << "creator_sway: out-of-order frame evaluation was not deterministic\n";
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
            std::cerr << "creator_sway: failed to open ffmpeg pipe\n";
            return 1;
        }
        for (int f = 0; f < totalFrames; ++f) {
            const auto pixels = renderAtFrame(f);
            std::fwrite(pixels.data(), 1, pixels.size(), pipe);
        }
        const int rc = pclose(pipe);
        if (rc != 0) {
            std::cerr << "creator_sway: ffmpeg exited with " << rc << "\n";
            return 1;
        }
        std::cout << "creator_sway: wrote Full HD 1080p MP4 video (" << totalFrames << " frames) to " << outPath << "\n";
    }

    return 0;
}
