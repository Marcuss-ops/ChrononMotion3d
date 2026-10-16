// Proof & Tool: Advanced Local Mesh Puppet Rigging & Multi-Articular Animation at 1920x1080.
//
// Answers the core question: True local mesh vertex deformation vs. rigid layer transform.
//
// Key Features:
//   1. ZERO GLOBAL LAYER TRANSFORM: The mesh container (layer) is 100% static at (0,0,0).
//      All motion originates EXCLUSIVELY from local mesh vertex deformation (RigProgram + PinWarp).
//   2. Multi-Articular Independent Controllers (9-second broadcast narrative @ 30fps):
//      - Head & Neck (Controllers 1 & 2): Bidirectional rotation/tilt (±6.2 deg) with neck pivot
//        and volumetric 2.5D Z-pop (dz = +0.13 * planeH), flexing the neck while maintaining facial rigidity.
//      - Arm & Finger (Controllers 3 & 4): Autonomous pointing gesture (hand/finger moves independently
//        inward/upward by ~0.12 * planeW with wrist rotation, leaving the torso and left shoulder static).
//      - Thoracic Breathing (Controller 5): Independent chest lung expansion (2.5 cycles).
//      - Solid base stiffness anchoring: Shirt bottom remains firmly anchored.
//   3. Supports:
//      - Full HD 1920x1080 solo video export.
//      - Side-by-side comparison video (Rigid Layer Transform vs. ChrononMotion Mesh Puppet Rig).
//
// Usage:
//   chrononmotion_render_creator_puppet [input_cutout.png] [out.mp4] [w h [frames]] [--comparison]

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

std::shared_ptr<threepp::DataTexture> makeAtmosphericBackdropTexture(int texW, int texH) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(texW * texH * 3));
    for (int y = 0; y < texH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(texH);
        for (int x = 0; x < texW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(texW);
            const float dx = (u - 0.5f) * 1.5f;
            const float dy = (v - 0.52f) * 1.25f;
            const float dist2 = dx * dx + dy * dy;

            const float vignette = std::clamp(1.0f - dist2 * 0.58f, 0.0f, 1.0f);
            float r = (28.f + 32.f * vignette) * (0.85f + 0.28f * v);
            float g = (35.f + 40.f * vignette) * (0.85f + 0.28f * v);
            float b = (48.f + 56.f * vignette) * (0.85f + 0.28f * v);

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

// Solid perimeter and base anchoring
void addSolidBaseAnchors(
    chrononmotion::motion::RigDeformer& deformer,
    float planeW,
    float planeH) {
    const float hx = 0.5f * planeW;
    const float hy = 0.5f * planeH;

    // Firm bottom shirt boundary anchors
    for (float fx = -0.95f; fx <= 0.95f; fx += 0.18f) {
        chrononmotion::motion::RigPin pin;
        pin.kind = chrononmotion::motion::RigPinKind::Stiffness;
        pin.rest = chrononmotion::Vector3(fx * hx, -hy + 0.035f * planeH, 0.f);
        pin.radius = 0.20f * planeH;
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
        pin.weight = 1.0f;
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
    std::string outPath = "creator_puppet_advanced_1080p.mp4";
    int width = 1920;
    int height = 1080;
    int totalFrames = 271; // 9.0 seconds @ 30 fps (comprehensive broadcast narrative loop)
    bool makeComparison = false;

    int argIdx = 1;
    if (argc >= 3 && isImagePath(argv[1])) {
        inputImagePath = argv[1];
        outPath = argv[2];
        argIdx = 3;
    } else if (argc >= 2 && !isImagePath(argv[1]) && (endsWithMp4(argv[1]) || argv[1][0] != '-')) {
        outPath = argv[1];
        argIdx = 2;
    }
    if (argc > argIdx && argv[argIdx][0] != '-') width = std::atoi(argv[argIdx++]);
    if (argc > argIdx && argv[argIdx][0] != '-') height = std::atoi(argv[argIdx++]);
    if (argc > argIdx && argv[argIdx][0] != '-') totalFrames = std::max(4, std::atoi(argv[argIdx++]));
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--comparison") == 0) makeComparison = true;
    }

    const float fps = 30.f;
    const float durationSec = static_cast<float>(totalFrames - 1) / fps;

    // 1. Setup headless GL context at 1920x1080
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x13171f));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // 2. Load high-resolution transparent creator texture
    threepp::ImageLoader loader;
    auto imgOpt = loader.load(inputImagePath, 4, true);
    if (!imgOpt || imgOpt->width() == 0 || imgOpt->height() == 0) {
        std::cerr << "creator_puppet: failed to load image " << inputImagePath << "\n";
        return 1;
    }
    const float imageAspect = static_cast<float>(imgOpt->width()) /
                              static_cast<float>(imgOpt->height());
    std::cout << "creator_puppet: loaded " << inputImagePath << " ("
              << imgOpt->width() << "x" << imgOpt->height() << ")\n";
    auto creatorTexture = threepp::Texture::create(std::move(*imgOpt));
    creatorTexture->needsUpdate();

    // 3. Dense 64x64 subdivided plane mesh (4225 vertices)
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    auto planeGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    const auto restPositions = threepp::motion::extractGeometryPositions(*planeGeom);

    // 4. Author MotionScene:
    // IMPORTANT: The hero layer has NO global layer transformation!
    // Its position and rotation tracks stay 100% strictly stationary at identity.
    chrononmotion::motion::MotionScene motionScene("creator_puppet_scene", fps);

    const float camAspect = static_cast<float>(width) / static_cast<float>(height);
    const float bgDist = 3.85f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAttachment;
    bgAttachment.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAttachment.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = motionScene.addMesh(bgLayerId, "atmospheric_backdrop", std::move(bgAttachment));
    bgLayer.transform.position.set(0.f, 0.f, -0.85f);

    // Foreground hero layer: stationary container at identity
    chrononmotion::motion::MeshAttachment heroAttachment;
    heroAttachment.geometry = planeGeom;
    heroAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    heroAttachment.material.transparent = true;
    const chrononmotion::motion::Layer::Id heroLayerId = 10;
    motionScene.addMesh(heroLayerId, "puppet_creator", std::move(heroAttachment));

    // Parallax CameraRig: subtle cinematic breathing push-in (0.04m)
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.05f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.04f, 2.65f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.015f, 0.045f, 2.58f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.04f, 2.65f));

    // 5. Author Advanced Multi-Articular RigProgram (5 Independent Articulations):
    //    - Controller 1: Head Nod & Tilt Right (Neck pivot rotation +0.108 rad / 6.2 deg + 2.5D Z-pop)
    //    - Controller 2: Head Look & Tilt Left (Neck pivot rotation -0.108 rad / -6.2 deg + 2.5D Z-pop)
    //    - Controller 3: Arm / Finger Gesture Inward (Hand & pointing index finger flex inward toward chest)
    //    - Controller 4: Arm / Finger Gesture Upward (Hand & finger push upward emphasizing the key point)
    //    - Controller 5: Thoracic Breathing (2.5 cycles of diaphragmatic chest expansion)
    chrononmotion::motion::RigProgram rigProgram;
    {
        for (chrononmotion::motion::RigNodeId id = 1; id <= 5; ++id) {
            chrononmotion::motion::RigNode ctrl;
            ctrl.id = id;
            ctrl.kind = chrononmotion::motion::RigNodeKind::Controller;
            rigProgram.graph.nodes.push_back(ctrl);
        }

        // --- Controller 1: Head Tilt Right & Nod (seconds 1.0 .. 3.5) ---
        chrononmotion::motion::RigAnimatedNode c1;
        c1.id = 1;
        c1.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        c1.tracks.position.add(1.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(2.2f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(3.8f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(c1));

        // --- Controller 2: Head Tilt Left & Nod (seconds 4.2 .. 7.2) ---
        chrononmotion::motion::RigAnimatedNode c2;
        c2.id = 2;
        c2.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        c2.tracks.position.add(4.2f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(5.6f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(7.2f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(c2));

        // --- Controller 3: Arm / Finger Gesture Inward (seconds 1.5 .. 4.5) ---
        chrononmotion::motion::RigAnimatedNode c3;
        c3.id = 3;
        c3.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        c3.tracks.position.add(1.2f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c3.tracks.position.add(2.6f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c3.tracks.position.add(4.5f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c3.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(c3));

        // --- Controller 4: Arm / Finger Gesture Upward (seconds 5.0 .. 8.0) ---
        chrononmotion::motion::RigAnimatedNode c4;
        c4.id = 4;
        c4.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        c4.tracks.position.add(5.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c4.tracks.position.add(6.4f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c4.tracks.position.add(8.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c4.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(c4));

        // --- Controller 5: Thoracic Breathing (Continuous smooth sinusoidal cycles) ---
        chrononmotion::motion::RigAnimatedNode c5;
        c5.id = 5;
        const float cycle = durationSec / 3.0f; // 3 full breath cycles
        for (int b = 0; b <= 3; ++b) {
            const float t0 = static_cast<float>(b) * cycle;
            const float tPeak = t0 + cycle * 0.45f;
            if (t0 <= durationSec) c5.tracks.position.add(t0, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
            if (tPeak < durationSec) c5.tracks.position.add(tPeak, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        }
        c5.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(c5));

        // ---------------- DEFORMER OPERATORS ----------------

        // Op 0 (Head Tilt Right & Z-Pop):
        chrononmotion::motion::RigDeformer opHeadRight;
        opHeadRight.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opHeadRight.amount = 1.f;
        addSolidBaseAnchors(opHeadRight, planeW, planeH);

        // Neck pivot rotation (+6.2 degrees tilt)
        chrononmotion::motion::RigPin pinHeadRotR;
        pinHeadRotR.kind = chrononmotion::motion::RigPinKind::Rotation;
        pinHeadRotR.rest = chrononmotion::Vector3(-0.015f * planeW, 0.08f * planeH, 0.f);
        pinHeadRotR.rotation.setFromAxisAngle(
            chrononmotion::Vector3(0.12f, -0.62f, -0.77f).normalize(), 0.108f);
        pinHeadRotR.radius = 0.42f * planeH;
        pinHeadRotR.weight = 1.0f;
        opHeadRight.pins.push_back(pinHeadRotR);

        // Head position displacement & 3D forward pop
        chrononmotion::motion::RigPin pinHeadMoveR;
        pinHeadMoveR.kind = chrononmotion::motion::RigPinKind::Position;
        pinHeadMoveR.rest = chrononmotion::Vector3(-0.015f * planeW, 0.22f * planeH, 0.f);
        pinHeadMoveR.target = chrononmotion::Vector3(0.045f * planeW, 0.205f * planeH, 0.13f * planeH);
        pinHeadMoveR.radius = 0.38f * planeH;
        pinHeadMoveR.weight = 1.0f;
        opHeadRight.pins.push_back(pinHeadMoveR);

        // Op 1 (Head Tilt Left & Nod):
        chrononmotion::motion::RigDeformer opHeadLeft;
        opHeadLeft.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opHeadLeft.amount = 1.f;
        addSolidBaseAnchors(opHeadLeft, planeW, planeH);

        // Neck pivot rotation (-6.2 degrees tilt)
        chrononmotion::motion::RigPin pinHeadRotL;
        pinHeadRotL.kind = chrononmotion::motion::RigPinKind::Rotation;
        pinHeadRotL.rest = chrononmotion::Vector3(-0.015f * planeW, 0.08f * planeH, 0.f);
        pinHeadRotL.rotation.setFromAxisAngle(
            chrononmotion::Vector3(-0.12f, 0.62f, 0.77f).normalize(), 0.108f);
        pinHeadRotL.radius = 0.42f * planeH;
        pinHeadRotL.weight = 1.0f;
        opHeadLeft.pins.push_back(pinHeadRotL);

        // Head position displacement & nod
        chrononmotion::motion::RigPin pinHeadMoveL;
        pinHeadMoveL.kind = chrononmotion::motion::RigPinKind::Position;
        pinHeadMoveL.rest = chrononmotion::Vector3(-0.015f * planeW, 0.22f * planeH, 0.f);
        pinHeadMoveL.target = chrononmotion::Vector3(-0.055f * planeW, 0.202f * planeH, 0.12f * planeH);
        pinHeadMoveL.radius = 0.38f * planeH;
        pinHeadMoveL.weight = 1.0f;
        opHeadLeft.pins.push_back(pinHeadMoveL);

        // Op 2 (Arm & Finger Gesture Inward):
        // Moves the raised pointing finger and hand distinctly toward the chest center
        // as a rigid articulated unit hinged at the wrist.
        chrononmotion::motion::RigDeformer opArmIn;
        opArmIn.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opArmIn.amount = 1.f;
        addSolidBaseAnchors(opArmIn, planeW, planeH);

        // Hand & finger unified translation pin (moves entire hand/finger +0.038 * planeW inward, +0.02 * planeH)
        chrononmotion::motion::RigPin pinHandMoveIn;
        pinHandMoveIn.kind = chrononmotion::motion::RigPinKind::Position;
        pinHandMoveIn.rest = chrononmotion::Vector3(-0.16f * planeW, -0.05f * planeH, 0.f);
        pinHandMoveIn.target = chrononmotion::Vector3(-0.122f * planeW, -0.032f * planeH, 0.06f * planeH);
        pinHandMoveIn.radius = 0.32f * planeH;
        pinHandMoveIn.weight = 1.0f;
        opArmIn.pins.push_back(pinHandMoveIn);

        // Wrist pivot rotation pin (subtle inward pointing tilt of the hand)
        chrononmotion::motion::RigPin pinWristIn;
        pinWristIn.kind = chrononmotion::motion::RigPinKind::Rotation;
        pinWristIn.rest = chrononmotion::Vector3(-0.165f * planeW, -0.12f * planeH, 0.f);
        pinWristIn.rotation.setFromAxisAngle(
            chrononmotion::Vector3(0.f, 0.f, 1.f), -0.062f);
        pinWristIn.radius = 0.30f * planeH;
        pinWristIn.weight = 0.85f;
        opArmIn.pins.push_back(pinWristIn);

        // Op 3 (Arm & Finger Gesture Upward Emphasis):
        // Hand extends higher with index pointing sharply up as a solid unit
        chrononmotion::motion::RigDeformer opArmUp;
        opArmUp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opArmUp.amount = 1.f;
        addSolidBaseAnchors(opArmUp, planeW, planeH);

        // Hand & finger unified upward lift
        chrononmotion::motion::RigPin pinHandMoveUp;
        pinHandMoveUp.kind = chrononmotion::motion::RigPinKind::Position;
        pinHandMoveUp.rest = chrononmotion::Vector3(-0.16f * planeW, -0.05f * planeH, 0.f);
        pinHandMoveUp.target = chrononmotion::Vector3(-0.155f * planeW, -0.015f * planeH, 0.065f * planeH);
        pinHandMoveUp.radius = 0.34f * planeH;
        pinHandMoveUp.weight = 1.0f;
        opArmUp.pins.push_back(pinHandMoveUp);

        // Right forearm stretch
        chrononmotion::motion::RigPin pinForearmUp;
        pinForearmUp.kind = chrononmotion::motion::RigPinKind::Position;
        pinForearmUp.rest = chrononmotion::Vector3(-0.20f * planeW, -0.16f * planeH, 0.f);
        pinForearmUp.target = chrononmotion::Vector3(-0.192f * planeW, -0.138f * planeH, 0.035f * planeH);
        pinForearmUp.radius = 0.28f * planeH;
        pinForearmUp.weight = 0.85f;
        opArmUp.pins.push_back(pinForearmUp);

        // Op 4 (Thoracic Breathing):
        chrononmotion::motion::RigDeformer opBreath;
        opBreath.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opBreath.amount = 1.f;
        addSolidBaseAnchors(opBreath, planeW, planeH);

        chrononmotion::motion::RigPin chestBreath;
        chestBreath.kind = chrononmotion::motion::RigPinKind::Scale;
        chestBreath.rest = chrononmotion::Vector3(-0.05f * planeW, -0.22f * planeH, 0.f);
        chestBreath.scale = chrononmotion::Vector3(1.055f, 1.042f, 1.030f);
        chestBreath.radius = 0.38f * planeH;
        chestBreath.weight = 1.0f;
        opBreath.pins.push_back(chestBreath);

        // Assemble Geometry Binding with all 5 Operators
        chrononmotion::motion::RigGeometryBinding geomBinding;
        geomBinding.id = 1;
        geomBinding.source = restPositions;
        geomBinding.deformers.operators.push_back(std::move(opHeadRight));
        geomBinding.deformers.operators.push_back(std::move(opHeadLeft));
        geomBinding.deformers.operators.push_back(std::move(opArmIn));
        geomBinding.deformers.operators.push_back(std::move(opArmUp));
        geomBinding.deformers.operators.push_back(std::move(opBreath));
        rigProgram.geometry.push_back(std::move(geomBinding));

        // Connect Controller 1..5 to Operators 0..4
        for (std::uint32_t op = 0; op < 5; ++op) {
            rigProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
                static_cast<chrononmotion::motion::RigNodeId>(op + 1),
                chrononmotion::motion::RigWeightKind::Deformer,
                1,
                op
            });
        }
    }

    chrononmotion::motion::RigProgramEvaluator rigEvaluator(rigProgram);

    // 6. Connect MotionSceneBridge and bind textures
    threepp::motion::MotionSceneBridge bridge(
        45.f, camAspect, 0.1f, 100.f);
    auto atmosphericBackdropTex = makeAtmosphericBackdropTexture(1024, 576);
    bridge.bindLayerTexture(bgLayerId, atmosphericBackdropTex);
    bridge.bindLayerTexture(heroLayerId, creatorTexture);

    // Renderer for the True Mesh Puppet Rig (deforming vertices locally)
    auto renderPuppetFrame = [&](int frame) {
        const auto rigFrame = rigEvaluator.evaluate(frame, fps);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        bridge.applyRigFrame(heroLayerId, rigFrame.geometry.front(), false);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    // Renderer for the Rigid Layer Transform comparison (no vertex deformation, just moving the card)
    auto renderRigidLayerFrame = [&](int frame) {
        const float t = static_cast<float>(frame) / fps;
        const float sway = 0.08f * std::sin(2.f * 3.14159265f * t / 4.0f);
        const float tilt = -0.04f * std::sin(2.f * 3.14159265f * t / 4.0f);

        auto* heroLayerPtr = motionScene.findLayer(heroLayerId);
        heroLayerPtr->tracks.position.clear();
        heroLayerPtr->tracks.position.add(t, chrononmotion::Vector3(sway, 0.f, 0.f));
        chrononmotion::Quaternion q;
        q.setFromAxisAngle(chrononmotion::Vector3(0.f, 0.f, 1.f), tilt);
        heroLayerPtr->tracks.rotation.clear();
        heroLayerPtr->tracks.rotation.add(t, q);

        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        // Reset mesh to rest positions (no deformation)
        bridge.applyDeformedGeometry(heroLayerId, restPositions, false);
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto pixels = renderer.readRGBPixels();

        // Restore stationary heroLayer
        heroLayerPtr->tracks.position.clear();
        heroLayerPtr->tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        heroLayerPtr->tracks.rotation.clear();
        heroLayerPtr->tracks.rotation.add(0.f, chrononmotion::Quaternion{});
        return pixels;
    };

    std::cout << "creator_puppet: validating multi-articular local deformations over "
              << totalFrames << " frames (" << durationSec << " seconds)...\n";

    const auto fRest0 = renderPuppetFrame(0);
    const auto fArmIn = renderPuppetFrame(static_cast<int>(2.6f * fps));
    const auto fHeadL = renderPuppetFrame(static_cast<int>(5.6f * fps));
    const auto fArmUp = renderPuppetFrame(static_cast<int>(6.4f * fps));
    const auto fRestLast = renderPuppetFrame(totalFrames - 1);

    const std::size_t diffArmIn = countDiffPixels(fRest0, fArmIn);
    const std::size_t diffHeadL = countDiffPixels(fRest0, fHeadL);
    const std::size_t diffArmUp = countDiffPixels(fRest0, fArmUp);

    std::cout << "creator_puppet: diff(Rest -> ArmIn Gesture)=" << diffArmIn << " pixels\n"
              << "creator_puppet: diff(Rest -> HeadLeft Nod)=" << diffHeadL << " pixels\n"
              << "creator_puppet: diff(Rest -> ArmUp Emphasis)=" << diffArmUp << " pixels\n";

    if (diffArmIn < 1000 || diffHeadL < 1000 || diffArmUp < 1000) {
        std::cerr << "creator_puppet: expected prominent local mesh deformation\n";
        return 1;
    }
    if (fRest0 != fRestLast) {
        std::cerr << "creator_puppet: expected seamless loop matching rest frame\n";
        return 1;
    }

    // 7. Render MP4 video via ffmpeg pipe
    if (endsWithMp4(outPath)) {
        if (!makeComparison) {
            // Standard Solo Full HD video
            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_puppet: failed to open ffmpeg pipe\n";
                return 1;
            }
            for (int f = 0; f < totalFrames; ++f) {
                const auto pixels = renderPuppetFrame(f);
                std::fwrite(pixels.data(), 1, pixels.size(), pipe);
            }
            const int rc = pclose(pipe);
            if (rc != 0) {
                std::cerr << "creator_puppet: ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_puppet: wrote Full HD Solo video (" << totalFrames << " frames, "
                      << durationSec << "s) to " << outPath << "\n";
        } else {
            // Side-by-Side Comparison: Left = Rigid Layer Transform, Right = ChrononMotion Puppet Mesh Rig
            const int halfW = width / 2;
            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_puppet: failed to open comparison ffmpeg pipe\n";
                return 1;
            }

            std::vector<unsigned char> sideBySide(static_cast<std::size_t>(width * height * 3));
            for (int f = 0; f < totalFrames; ++f) {
                const auto leftPixels = renderRigidLayerFrame(f);
                const auto rightPixels = renderPuppetFrame(f);

                for (int y = 0; y < height; ++y) {
                    // Copy left half (Rigid Layer Transform)
                    std::memcpy(
                        sideBySide.data() + (y * width) * 3,
                        leftPixels.data() + (y * width + (width - halfW) / 2) * 3,
                        static_cast<std::size_t>(halfW * 3));
                    // Copy right half (Mesh Puppet Rig)
                    std::memcpy(
                        sideBySide.data() + (y * width + halfW) * 3,
                        rightPixels.data() + (y * width + (width - halfW) / 2) * 3,
                        static_cast<std::size_t>(halfW * 3));
                    // Vertical divider line in center
                    const int divX = halfW;
                    const std::size_t divIdx = static_cast<std::size_t>((y * width + divX) * 3);
                    sideBySide[divIdx + 0] = 255;
                    sideBySide[divIdx + 1] = 255;
                    sideBySide[divIdx + 2] = 255;
                }
                std::fwrite(sideBySide.data(), 1, sideBySide.size(), pipe);
            }
            const int rc = pclose(pipe);
            if (rc != 0) {
                std::cerr << "creator_puppet: comparison ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_puppet: wrote Side-by-Side Comparison video to " << outPath << "\n";
        }
    }

    return 0;
}
