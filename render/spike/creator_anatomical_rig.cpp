#include <iostream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <span>

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

// Compute normalized skinning weights for anatomical skeletal rig
std::vector<chrononmotion::motion::RigSkinVertex> computeAnatomicalWeights(
    std::span<const chrononmotion::Vector3> source,
    float planeW,
    float planeH) {

    std::vector<chrononmotion::motion::RigSkinVertex> skins(source.size());

    for (std::size_t i = 0; i < source.size(); ++i) {
        const float x = source[i].x;
        const float y = source[i].y;

        auto& v = skins[i];
        v.joints = {0, 0, 0, 0};
        v.weights = {0.f, 0.f, 0.f, 0.f};

        // Check arm region (creator's right arm / viewer's left)
        const bool isArm = (x < -0.10f * planeW) && (y < 0.12f * planeH) && (y > -0.80f * planeH);

        if (isArm) {
            // Hand & Pointing Finger: 100% rigid on Hand bone (53)
            if (y >= -0.02f * planeH) {
                v.joints[0] = 53; // Hand
                v.weights[0] = 1.0f;
            } else if (y >= -0.10f * planeH) {
                // Wrist blend zone
                const float t = (y - (-0.10f * planeH)) / (0.08f * planeH);
                v.joints[0] = 53; // Hand
                v.weights[0] = t;
                v.joints[1] = 52; // Forearm
                v.weights[1] = 1.0f - t;
            } else if (y >= -0.45f * planeH) {
                // Forearm core
                v.joints[0] = 52; // Forearm
                v.weights[0] = 1.0f;
            } else if (y >= -0.62f * planeH) {
                // Elbow hinge blend zone
                const float t = (y - (-0.62f * planeH)) / (0.17f * planeH);
                v.joints[0] = 52; // Forearm
                v.weights[0] = t;
                v.joints[1] = 51; // UpperArm
                v.weights[1] = 1.0f - t;
            } else {
                // Shoulder / UpperArm blend
                const float t = std::clamp((y - (-0.80f * planeH)) / (0.18f * planeH), 0.0f, 1.0f);
                v.joints[0] = 51; // UpperArm
                v.weights[0] = t;
                v.joints[1] = 50; // Shoulder
                v.weights[1] = 1.0f - t;
            }
        } else if (y >= 0.18f * planeH && std::abs(x - (-0.03f * planeW)) < 0.28f * planeW) {
            // Head and Facial Core: 100% RIGID on Head bone (40)! Zero distortion on eyes/mouth!
            v.joints[0] = 40; // Head
            v.weights[0] = 1.0f;
        } else if (y >= 0.02f * planeH && std::abs(x - (-0.03f * planeW)) < 0.28f * planeW) {
            // Neck smooth vertical transition
            const float t = (y - 0.02f * planeH) / (0.16f * planeH);
            v.joints[0] = 40; // Head
            v.weights[0] = t;
            v.joints[1] = 20; // Torso
            v.weights[1] = 1.0f - t;
        } else {
            // Torso and Ground Base
            if (y <= -0.42f * planeH) {
                // Root base anchor (100% stationary ground)
                v.joints[0] = 10; // Root
                v.weights[0] = 1.0f;
            } else if (y <= -0.22f * planeH) {
                // Lower torso transition to base
                const float t = (y - (-0.42f * planeH)) / (0.20f * planeH);
                v.joints[0] = 20; // Torso
                v.weights[0] = t;
                v.joints[1] = 10; // Root
                v.weights[1] = 1.0f - t;
            } else {
                // Torso / Chest core
                v.joints[0] = 20; // Torso
                v.weights[0] = 1.0f;
            }
        }

        // Enforce exact mathematical normalization (sum == 1.0f)
        float sum = 0.f;
        for (float w : v.weights) sum += w;
        if (sum > 1e-6f) {
            for (float& w : v.weights) w /= sum;
        } else {
            v.joints[0] = 10;
            v.weights[0] = 1.0f;
        }
    }

    return skins;
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
    std::string outPath = "creator_anatomical_rig_1080p.mp4";
    int width = 1920;
    int height = 1080;
    int totalFrames = 301; // 10.0 seconds @ 30 fps (comprehensive broadcast narrative loop)
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
        std::cerr << "creator_anatomical: failed to load image " << inputImagePath << "\n";
        return 1;
    }
    const float imageAspect = static_cast<float>(imgOpt->width()) /
                              static_cast<float>(imgOpt->height());
    std::cout << "creator_anatomical: loaded " << inputImagePath << " ("
              << imgOpt->width() << "x" << imgOpt->height() << ")\n";
    auto creatorTexture = threepp::Texture::create(std::move(*imgOpt));
    creatorTexture->needsUpdate();

    // 3. Dense 64x64 subdivided plane mesh (4225 vertices)
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    auto planeGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    const auto restPositions = threepp::motion::extractGeometryPositions(*planeGeom);

    // 4. Author MotionScene:
    // IMPORTANT: The hero layer container has NO global layer transformation!
    // Its position and rotation tracks stay 100% strictly stationary at identity.
    chrononmotion::motion::MotionScene motionScene("creator_anatomical_scene", fps);

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
    motionScene.addMesh(heroLayerId, "anatomical_creator", std::move(heroAttachment));

    // Parallax CameraRig: subtle cinematic breathing push-in (0.04m)
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.05f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.04f, 2.65f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.015f, 0.045f, 2.58f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.04f, 2.65f));

    // =========================================================================
    // 5. Build Rig 1: Anatomical Skeletal Rig + Linear Blend Skinning
    // =========================================================================
    chrononmotion::motion::RigProgram anatomicalProgram;
    {
        // Skeletal Bone Hierarchy
        chrononmotion::motion::RigNode root;
        root.id = 10; root.kind = chrononmotion::motion::RigNodeKind::Bone;
        root.rest.position = chrononmotion::Vector3(0.f, -1.0f, 0.f);
        root.bind = root.rest;

        chrononmotion::motion::RigNode torso;
        torso.id = 20; torso.parent = 10; torso.kind = chrononmotion::motion::RigNodeKind::Bone;
        torso.rest.position = chrononmotion::Vector3(-0.02f * planeW, 0.55f, 0.f);
        torso.bind = torso.rest;

        chrononmotion::motion::RigNode neck;
        neck.id = 30; neck.parent = 20; neck.kind = chrononmotion::motion::RigNodeKind::Bone;
        neck.rest.position = chrononmotion::Vector3(-0.01f * planeW, 0.52f, 0.f);
        neck.bind = neck.rest;

        chrononmotion::motion::RigNode head;
        head.id = 40; head.parent = 30; head.kind = chrononmotion::motion::RigNodeKind::Bone;
        head.rest.position = chrononmotion::Vector3(0.0f, 0.40f, 0.f);
        head.bind = head.rest;

        chrononmotion::motion::RigNode shoulder;
        shoulder.id = 50; shoulder.parent = 20; shoulder.kind = chrononmotion::motion::RigNodeKind::Bone;
        shoulder.rest.position = chrononmotion::Vector3(-0.15f * planeW, 0.15f, 0.f);
        shoulder.bind = shoulder.rest;

        chrononmotion::motion::RigNode upperArm;
        upperArm.id = 51; upperArm.parent = 50; upperArm.kind = chrononmotion::motion::RigNodeKind::Bone;
        upperArm.rest.position = chrononmotion::Vector3(-0.06f * planeW, -0.42f, 0.f);
        upperArm.bind = upperArm.rest;

        chrononmotion::motion::RigNode forearm;
        forearm.id = 52; forearm.parent = 51; forearm.kind = chrononmotion::motion::RigNodeKind::Bone;
        forearm.rest.position = chrononmotion::Vector3(0.08f * planeW, 0.58f, 0.f);
        forearm.bind = forearm.rest;

        chrononmotion::motion::RigNode hand;
        hand.id = 53; hand.parent = 52; hand.kind = chrononmotion::motion::RigNodeKind::Bone;
        hand.rest.position = chrononmotion::Vector3(0.03f * planeW, 0.22f, 0.f);
        hand.bind = hand.rest;

        anatomicalProgram.graph.nodes = {root, torso, neck, head, shoulder, upperArm, forearm, hand};

        // --- ANIMATIONS ---
        // 1. Head rotation (nods & tilts +/-14 degrees, 100% rigid face core)
        chrononmotion::motion::RigAnimatedNode animHead;
        animHead.id = 40;
        animHead.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        // Right nod / tilt
        animHead.tracks.rotation.add(1.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animHead.tracks.rotation.add(2.2f, chrononmotion::Vector3(0.f, 0.f, 0.244f), chrononmotion::motion::Easing::easeInOut()); // +14 deg
        animHead.tracks.rotation.add(3.0f, chrononmotion::Vector3(0.f, 0.f, 0.244f), chrononmotion::motion::Easing::easeInOut());
        animHead.tracks.rotation.add(4.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        // Left nod / tilt
        animHead.tracks.rotation.add(4.8f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animHead.tracks.rotation.add(6.0f, chrononmotion::Vector3(0.f, 0.f, -0.244f), chrononmotion::motion::Easing::easeInOut()); // -14 deg
        animHead.tracks.rotation.add(6.8f, chrononmotion::Vector3(0.f, 0.f, -0.244f), chrononmotion::motion::Easing::easeInOut());
        animHead.tracks.rotation.add(8.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animHead.tracks.rotation.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        anatomicalProgram.animation.push_back(std::move(animHead));

        // 2. Arm Shoulder / UpperArm lift (+10 degrees)
        chrononmotion::motion::RigAnimatedNode animUpperArm;
        animUpperArm.id = 51;
        animUpperArm.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        animUpperArm.tracks.rotation.add(1.4f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animUpperArm.tracks.rotation.add(2.5f, chrononmotion::Vector3(0.f, 0.f, 0.14f), chrononmotion::motion::Easing::easeInOut());
        animUpperArm.tracks.rotation.add(4.5f, chrononmotion::Vector3(0.f, 0.f, 0.14f), chrononmotion::motion::Easing::easeInOut());
        animUpperArm.tracks.rotation.add(5.2f, chrononmotion::Vector3(0.f, 0.f, 0.05f), chrononmotion::motion::Easing::easeInOut());
        animUpperArm.tracks.rotation.add(6.5f, chrononmotion::Vector3(0.f, 0.f, 0.16f), chrononmotion::motion::Easing::easeInOut());
        animUpperArm.tracks.rotation.add(7.8f, chrononmotion::Vector3(0.f, 0.f, 0.16f), chrononmotion::motion::Easing::easeInOut());
        animUpperArm.tracks.rotation.add(9.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animUpperArm.tracks.rotation.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        anatomicalProgram.animation.push_back(std::move(animUpperArm));

        // 3. Elbow Joint Flexion (-28 degrees hinge rotation, moving hand >60px naturally!)
        chrononmotion::motion::RigAnimatedNode animForearm;
        animForearm.id = 52;
        animForearm.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        animForearm.tracks.rotation.add(1.5f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animForearm.tracks.rotation.add(2.7f, chrononmotion::Vector3(0.f, 0.f, -0.488f), chrononmotion::motion::Easing::easeInOut()); // -28 deg elbow flex
        animForearm.tracks.rotation.add(4.2f, chrononmotion::Vector3(0.f, 0.f, -0.488f), chrononmotion::motion::Easing::easeInOut());
        animForearm.tracks.rotation.add(5.2f, chrononmotion::Vector3(0.f, 0.f, -0.15f), chrononmotion::motion::Easing::easeInOut());
        animForearm.tracks.rotation.add(6.6f, chrononmotion::Vector3(0.f, 0.f, -0.384f), chrononmotion::motion::Easing::easeInOut()); // -22 deg flex
        animForearm.tracks.rotation.add(7.8f, chrononmotion::Vector3(0.f, 0.f, -0.384f), chrononmotion::motion::Easing::easeInOut());
        animForearm.tracks.rotation.add(9.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animForearm.tracks.rotation.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        anatomicalProgram.animation.push_back(std::move(animForearm));

        // 4. Wrist Counter-Rotation (+12 degrees, keeps pointing finger straight & aimed)
        chrononmotion::motion::RigAnimatedNode animHand;
        animHand.id = 53;
        animHand.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        animHand.tracks.rotation.add(1.5f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animHand.tracks.rotation.add(2.7f, chrononmotion::Vector3(0.f, 0.f, 0.21f), chrononmotion::motion::Easing::easeInOut());
        animHand.tracks.rotation.add(4.2f, chrononmotion::Vector3(0.f, 0.f, 0.21f), chrononmotion::motion::Easing::easeInOut());
        animHand.tracks.rotation.add(5.2f, chrononmotion::Vector3(0.f, 0.f, 0.05f), chrononmotion::motion::Easing::easeInOut());
        animHand.tracks.rotation.add(6.6f, chrononmotion::Vector3(0.f, 0.f, 0.16f), chrononmotion::motion::Easing::easeInOut());
        animHand.tracks.rotation.add(7.8f, chrononmotion::Vector3(0.f, 0.f, 0.16f), chrononmotion::motion::Easing::easeInOut());
        animHand.tracks.rotation.add(9.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        animHand.tracks.rotation.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        anatomicalProgram.animation.push_back(std::move(animHand));

        // 5. Torso Thoracic Breathing (3 sinusoidal cycles of expansion & subtle delayed pitch)
        chrononmotion::motion::RigAnimatedNode animTorso;
        animTorso.id = 20;
        const float breathCycle = durationSec / 3.0f;
        for (int b = 0; b <= 3; ++b) {
            const float t0 = static_cast<float>(b) * breathCycle;
            const float tPeak = t0 + breathCycle * 0.45f;
            if (t0 <= durationSec) {
                animTorso.tracks.scale.add(t0, chrononmotion::Vector3(1.0f, 1.0f, 1.0f), chrononmotion::motion::Easing::easeInOut());
                animTorso.tracks.rotation.add(t0, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
            }
            if (tPeak < durationSec) {
                animTorso.tracks.scale.add(tPeak, chrononmotion::Vector3(1.045f, 1.032f, 1.020f), chrononmotion::motion::Easing::easeInOut());
                animTorso.tracks.rotation.add(tPeak, chrononmotion::Vector3(0.f, 0.f, 0.025f), chrononmotion::motion::Easing::easeInOut());
            }
        }
        animTorso.tracks.scale.add(durationSec, chrononmotion::Vector3(1.0f, 1.0f, 1.0f));
        animTorso.tracks.rotation.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        anatomicalProgram.animation.push_back(std::move(animTorso));

        // Geometry Binding with Linear Blend Skinning
        chrononmotion::motion::RigGeometryBinding geomBinding;
        geomBinding.id = 1;
        geomBinding.source = restPositions;
        geomBinding.skin = computeAnatomicalWeights(restPositions, planeW, planeH);
        anatomicalProgram.geometry.push_back(std::move(geomBinding));
    }

    chrononmotion::motion::RigProgramEvaluator anatomicalEvaluator(anatomicalProgram);

    // =========================================================================
    // 6. Build Rig 2: Radial PinWarp (for comparison showing elastic distortion)
    // =========================================================================
    chrononmotion::motion::RigProgram pinWarpProgram;
    {
        for (chrononmotion::motion::RigNodeId id = 1; id <= 3; ++id) {
            chrononmotion::motion::RigNode ctrl;
            ctrl.id = id;
            ctrl.kind = chrononmotion::motion::RigNodeKind::Controller;
            pinWarpProgram.graph.nodes.push_back(ctrl);
        }

        // Controller 1: Head Tilt
        chrononmotion::motion::RigAnimatedNode c1;
        c1.id = 1;
        c1.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        c1.tracks.position.add(1.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(2.2f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(3.0f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(4.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(4.8f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(6.0f, chrononmotion::Vector3(-1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(6.8f, chrononmotion::Vector3(-1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(8.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c1.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        pinWarpProgram.animation.push_back(std::move(c1));

        // Controller 2: Arm / Hand gesture
        chrononmotion::motion::RigAnimatedNode c2;
        c2.id = 2;
        c2.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        c2.tracks.position.add(1.5f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(2.7f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(4.2f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(5.2f, chrononmotion::Vector3(0.3f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(6.6f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(7.8f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(9.0f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        c2.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        pinWarpProgram.animation.push_back(std::move(c2));

        // Controller 3: Torso Breathing
        chrononmotion::motion::RigAnimatedNode c3;
        c3.id = 3;
        const float breathCycle = durationSec / 3.0f;
        for (int b = 0; b <= 3; ++b) {
            const float t0 = static_cast<float>(b) * breathCycle;
            const float tPeak = t0 + breathCycle * 0.45f;
            if (t0 <= durationSec) c3.tracks.position.add(t0, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
            if (tPeak < durationSec) c3.tracks.position.add(tPeak, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        }
        c3.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        pinWarpProgram.animation.push_back(std::move(c3));

        // Deformer Op 0: Head PinWarp (radial influence deforms eyes/face)
        chrononmotion::motion::RigDeformer opHead;
        opHead.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opHead.amount = 1.f;
        chrononmotion::motion::RigPin pinHead;
        pinHead.kind = chrononmotion::motion::RigPinKind::Position;
        pinHead.rest = chrononmotion::Vector3(-0.015f * planeW, 0.22f * planeH, 0.f);
        pinHead.target = chrononmotion::Vector3(0.048f * planeW, 0.205f * planeH, 0.f);
        pinHead.radius = 0.38f * planeH;
        pinHead.weight = 1.0f;
        opHead.pins.push_back(pinHead);

        // Deformer Op 1: Arm Hand PinWarp (radial position pin stretches forearm elastica)
        chrononmotion::motion::RigDeformer opArm;
        opArm.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opArm.amount = 1.f;
        chrononmotion::motion::RigPin pinArm;
        pinArm.kind = chrononmotion::motion::RigPinKind::Position;
        pinArm.rest = chrononmotion::Vector3(-0.16f * planeW, -0.05f * planeH, 0.f);
        pinArm.target = chrononmotion::Vector3(-0.115f * planeW, 0.035f * planeH, 0.f);
        pinArm.radius = 0.35f * planeH;
        pinArm.weight = 1.0f;
        opArm.pins.push_back(pinArm);

        // Deformer Op 2: Breathing
        chrononmotion::motion::RigDeformer opBreath;
        opBreath.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        opBreath.amount = 1.f;
        chrononmotion::motion::RigPin pinBreath;
        pinBreath.kind = chrononmotion::motion::RigPinKind::Scale;
        pinBreath.rest = chrononmotion::Vector3(-0.05f * planeW, -0.22f * planeH, 0.f);
        pinBreath.scale = chrononmotion::Vector3(1.05f, 1.035f, 1.0f);
        pinBreath.radius = 0.38f * planeH;
        pinBreath.weight = 1.0f;
        opBreath.pins.push_back(pinBreath);

        chrononmotion::motion::RigGeometryBinding geomBinding;
        geomBinding.id = 1;
        geomBinding.source = restPositions;
        geomBinding.deformers.operators.push_back(std::move(opHead));
        geomBinding.deformers.operators.push_back(std::move(opArm));
        geomBinding.deformers.operators.push_back(std::move(opBreath));
        pinWarpProgram.geometry.push_back(std::move(geomBinding));

        for (std::uint32_t op = 0; op < 3; ++op) {
            pinWarpProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
                static_cast<chrononmotion::motion::RigNodeId>(op + 1),
                chrononmotion::motion::RigWeightKind::Deformer,
                1,
                op
            });
        }
    }

    chrononmotion::motion::RigProgramEvaluator pinWarpEvaluator(pinWarpProgram);

    // =========================================================================
    // 7. Connect MotionSceneBridge and render
    // =========================================================================
    threepp::motion::MotionSceneBridge bridge(
        45.f, camAspect, 0.1f, 100.f);
    auto atmosphericBackdropTex = makeAtmosphericBackdropTexture(1024, 576);
    bridge.bindLayerTexture(bgLayerId, atmosphericBackdropTex);
    bridge.bindLayerTexture(heroLayerId, creatorTexture);

    auto renderAnatomicalFrame = [&](int frame) {
        const auto rigFrame = anatomicalEvaluator.evaluate(frame, fps);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        bridge.applyRigFrame(heroLayerId, rigFrame.geometry.front(), false);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    auto renderPinWarpFrame = [&](int frame) {
        const auto rigFrame = pinWarpEvaluator.evaluate(frame, fps);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        bridge.applyRigFrame(heroLayerId, rigFrame.geometry.front(), false);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    std::cout << "creator_anatomical: validating deformations over "
              << totalFrames << " frames (" << durationSec << " seconds)...\n";

    const auto fRest0 = renderAnatomicalFrame(0);
    const auto fHeadR = renderAnatomicalFrame(static_cast<int>(2.2f * fps));
    const auto fArmFlex = renderAnatomicalFrame(static_cast<int>(2.7f * fps));
    const auto fHeadL = renderAnatomicalFrame(static_cast<int>(6.0f * fps));
    const auto fRestLast = renderAnatomicalFrame(totalFrames - 1);

    const std::size_t diffHeadR = countDiffPixels(fRest0, fHeadR);
    const std::size_t diffArmFlex = countDiffPixels(fRest0, fArmFlex);
    const std::size_t diffHeadL = countDiffPixels(fRest0, fHeadL);
    const std::size_t diffLoop = countDiffPixels(fRest0, fRestLast, 2);

    std::cout << "creator_anatomical: diff(Rest -> HeadRight Tilt)=" << diffHeadR << " pixels\n"
              << "creator_anatomical: diff(Rest -> ElbowFlex Arm)=" << diffArmFlex << " pixels\n"
              << "creator_anatomical: diff(Rest -> HeadLeft Tilt)=" << diffHeadL << " pixels\n"
              << "creator_anatomical: diff(Rest0 -> RestLast loop)=" << diffLoop << " pixels\n";

    if (diffHeadR < 1000 || diffArmFlex < 1000 || diffHeadL < 1000) {
        std::cerr << "creator_anatomical: expected prominent anatomical bone deformation\n";
        return 1;
    }
    if (diffLoop > 0) {
        std::cerr << "creator_anatomical: warning: slight loop residue of " << diffLoop << " pixels\n";
    }

    // 8. Render MP4 video via ffmpeg pipe
    if (endsWithMp4(outPath)) {
        if (!makeComparison) {
            // Standard Solo Full HD video
            const std::string filter =
                "vflip,"
                "drawtext=text='CHRONONMOTION3D: ANATOMICAL BONES + LINEAR BLEND SKINNING':fontsize=32:fontcolor=white:box=1:boxcolor=black@0.65:boxborderw=10:x=(w-text_w)/2:y=40,"
                "drawtext=text='14 deg Head Tilt | Rigid Face Core | Elbow Joint Flexion >60px | Diaphragmatic Breathing':fontsize=24:fontcolor=yellow:box=1:boxcolor=black@0.65:boxborderw=8:x=(w-text_w)/2:y=h-60";
            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf \"" + filter + "\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_anatomical: failed to open ffmpeg pipe\n";
                return 1;
            }
            for (int f = 0; f < totalFrames; ++f) {
                const auto pixels = renderAnatomicalFrame(f);
                std::fwrite(pixels.data(), 1, pixels.size(), pipe);
            }
            const int rc = pclose(pipe);
            if (rc != 0) {
                std::cerr << "creator_anatomical: ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_anatomical: wrote Full HD Solo video (" << totalFrames << " frames, "
                      << durationSec << "s) to " << outPath << "\n";
        } else {
            // Side-by-Side Comparison: Left = Radial PinWarp (Elastic), Right = Anatomical Bones + LBS (ChrononMotion3D)
            const int halfW = width / 2;
            const std::string filter =
                "vflip,"
                "drawtext=text='RADIAL PINWARP (ELASTIC DISTORTION)':fontsize=30:fontcolor=white:box=1:boxcolor=black@0.65:boxborderw=10:x=(w/4-text_w/2):y=40,"
                "drawtext=text='ANATOMICAL BONES + LBS (CHRONONMOTION3D)':fontsize=30:fontcolor=white:box=1:boxcolor=black@0.65:boxborderw=10:x=(3*w/4-text_w/2):y=40,"
                "drawtext=text='14 deg Head Tilt | Rigid Face Core | Elbow Joint Flexion >60px | Diaphragmatic Breathing':fontsize=24:fontcolor=yellow:box=1:boxcolor=black@0.65:boxborderw=8:x=(w-text_w)/2:y=h-60";
            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf \"" + filter + "\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_anatomical: failed to open comparison ffmpeg pipe\n";
                return 1;
            }

            std::vector<unsigned char> sideBySide(static_cast<std::size_t>(width * height * 3));
            for (int f = 0; f < totalFrames; ++f) {
                const auto leftPixels = renderPinWarpFrame(f);
                const auto rightPixels = renderAnatomicalFrame(f);

                for (int y = 0; y < height; ++y) {
                    // Copy left half (Radial PinWarp)
                    std::memcpy(
                        sideBySide.data() + (y * width) * 3,
                        leftPixels.data() + (y * width + (width - halfW) / 2) * 3,
                        static_cast<std::size_t>(halfW * 3));
                    // Copy right half (Anatomical Bones + Skinning)
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
                std::cerr << "creator_anatomical: comparison ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_anatomical: wrote Side-by-Side Comparison video to " << outPath << "\n";
        }
    }

    return 0;
}
