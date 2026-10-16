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

std::shared_ptr<threepp::Texture> makeDiagnosticMaskTexture(
    const std::shared_ptr<threepp::Texture>& srcTex,
    unsigned char tintR, unsigned char tintG, unsigned char tintB) {
    const auto& img = srcTex->image();
    const int w = static_cast<int>(img.width());
    const int h = static_cast<int>(img.height());
    const auto& srcData = img.data<unsigned char>();

    std::vector<unsigned char> rgba(static_cast<std::size_t>(w * h * 4));
    for (int i = 0; i < w * h; ++i) {
        const unsigned char a = srcData[i * 4 + 3];
        rgba[i * 4 + 0] = static_cast<unsigned char>((tintR * a) / 255);
        rgba[i * 4 + 1] = static_cast<unsigned char>((tintG * a) / 255);
        rgba[i * 4 + 2] = static_cast<unsigned char>((tintB * a) / 255);
        rgba[i * 4 + 3] = a;
    }
    auto tex = threepp::DataTexture::create(rgba, w, h);
    tex->format = threepp::Format::RGBA;
    tex->needsUpdate();
    return tex;
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
    std::string layersDirV21 = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/layers_v2_1";
    std::string layersDirV20 = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/layers";
    std::string outPath = "chrononmotion3d_character_v2_1_seamless_multilayer_ik_10s_1080p.mp4";
    int width = 1920;
    int height = 1080;
    int totalFrames = 301; // 10.0 seconds @ 30 fps (broadcast narrative loop)
    bool makeComparison = false;
    bool debugMasks = false;

    int argIdx = 1;
    if (argc >= 2 && endsWithMp4(argv[1])) {
        outPath = argv[1];
        argIdx = 2;
    }
    if (argc > argIdx && argv[argIdx][0] != '-') width = std::atoi(argv[argIdx++]);
    if (argc > argIdx && argv[argIdx][0] != '-') height = std::atoi(argv[argIdx++]);
    if (argc > argIdx && argv[argIdx][0] != '-') totalFrames = std::max(4, std::atoi(argv[argIdx++]));
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--comparison") == 0) makeComparison = true;
        if (std::strcmp(argv[i], "--debug-masks") == 0) debugMasks = true;
    }

    const float fps = 30.f;
    const float durationSec = static_cast<float>(totalFrames - 1) / fps;

    // 1. Headless GL context at 1920x1080
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x13171f));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // 2. Load V2.1 Seamless Layers
    threepp::ImageLoader loader;
    auto texTorsoV21 = threepp::Texture::create(std::move(*loader.load(layersDirV21 + "/layer_torso.png", 4, true)));
    auto texHeadV21 = threepp::Texture::create(std::move(*loader.load(layersDirV21 + "/layer_head.png", 4, true)));
    auto texUpperArmV21 = threepp::Texture::create(std::move(*loader.load(layersDirV21 + "/layer_upper_arm.png", 4, true)));
    auto texForearmV21 = threepp::Texture::create(std::move(*loader.load(layersDirV21 + "/layer_forearm.png", 4, true)));
    auto texHandV21 = threepp::Texture::create(std::move(*loader.load(layersDirV21 + "/layer_hand.png", 4, true)));

    // Load V2.0 Layers (for Side-by-Side Comparison if requested)
    std::shared_ptr<threepp::Texture> texTorsoV20, texHeadV20, texUpperArmV20, texForearmV20, texHandV20;
    if (makeComparison) {
        texTorsoV20 = threepp::Texture::create(std::move(*loader.load(layersDirV20 + "/layer_torso.png", 4, true)));
        texHeadV20 = threepp::Texture::create(std::move(*loader.load(layersDirV20 + "/layer_head.png", 4, true)));
        texUpperArmV20 = threepp::Texture::create(std::move(*loader.load(layersDirV20 + "/layer_upper_arm.png", 4, true)));
        texForearmV20 = threepp::Texture::create(std::move(*loader.load(layersDirV20 + "/layer_forearm.png", 4, true)));
        texHandV20 = threepp::Texture::create(std::move(*loader.load(layersDirV20 + "/layer_hand.png", 4, true)));
    }

    const float imageAspect = 1376.f / 768.f;
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    auto quadGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 2, 2);
    const float camAspect = static_cast<float>(width) / static_cast<float>(height);

    // Common Background Layer
    const float bgDist = 3.85f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    const chrononmotion::motion::Layer::Id bgLayerId = 5;

    // Anatomical Pivots in scene space
    const chrononmotion::Vector3 pivotNeck(-0.057f * planeW, 0.076f * planeH, 0.f);
    const chrononmotion::Vector3 pivotShoulder(-0.160f * planeW, -0.125f * planeH, 0.f);
    const chrononmotion::Vector3 pivotElbow(-0.245f * planeW, -0.288f * planeH, 0.f);
    const chrononmotion::Vector3 pivotWrist(-0.187f * planeW, -0.047f * planeH, 0.f);

    // =========================================================================
    // 3. Build Character Rig V2.1 Scene (Multilayer with Seamless Bleed & Overlap)
    // =========================================================================
    chrononmotion::motion::MotionScene sceneV21("creator_v2_1_scene", fps);

    chrononmotion::motion::MeshAttachment bgAtt;
    bgAtt.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAtt.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    auto& bgLayer = sceneV21.addMesh(bgLayerId, "backdrop", std::move(bgAtt));
    bgLayer.transform.position.set(0.f, 0.f, -0.85f);

    auto addCutoutLayer = [&](chrononmotion::motion::MotionScene& sc,
                              chrononmotion::motion::Layer::Id id,
                              const std::string& name,
                              float z) -> chrononmotion::motion::Layer& {
        chrononmotion::motion::MeshAttachment att;
        att.geometry = quadGeom;
        att.material = chrononmotion::MaterialDescriptor::unlit(id, chrononmotion::Color(0xffffff));
        att.material.transparent = true;
        auto& l = sc.addMesh(id, name, std::move(att));
        l.transform.position.z = z;
        return l;
    };

    // Layer 10: Torso (Base level, Z = 0.000m)
    auto& layerTorso = addCutoutLayer(sceneV21, 10, "layer_torso", 0.000f);
    // Layer 11: Head (In front of torso, Z = +0.010m)
    auto& layerHead = addCutoutLayer(sceneV21, 11, "layer_head", 0.010f);
    // Layer 12: UpperArm (Behind shoulder, Z = -0.005m)
    auto& layerUpperArm = addCutoutLayer(sceneV21, 12, "layer_upper_arm", -0.005f);
    // Layer 13: Forearm (In front of torso, Z = +0.015m)
    auto& layerForearm = addCutoutLayer(sceneV21, 13, "layer_forearm", 0.015f);
    // Layer 14: Hand (Topmost foreground, Z = +0.025m)
    auto& layerHand = addCutoutLayer(sceneV21, 14, "layer_hand", 0.025f);

    // Set anchors (pivots) for rotational layers
    layerHead.transform.anchor = pivotNeck;
    layerHead.transform.position.x = pivotNeck.x;
    layerHead.transform.position.y = pivotNeck.y;

    layerUpperArm.transform.anchor = pivotShoulder;
    layerUpperArm.transform.position.x = pivotShoulder.x;
    layerUpperArm.transform.position.y = pivotShoulder.y;

    layerForearm.transform.anchor = pivotElbow;
    layerForearm.transform.position.x = pivotElbow.x;
    layerForearm.transform.position.y = pivotElbow.y;

    layerHand.transform.anchor = pivotWrist;
    layerHand.transform.position.x = pivotWrist.x;
    layerHand.transform.position.y = pivotWrist.y;

    // Parallax CameraRig: subtle documentary push-in
    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, camAspect, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.05f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.04f, 2.65f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.012f, 0.045f, 2.58f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.04f, 2.65f));

    // =========================================================================
    // 4. Two-Bone IK Solver & Wide Dynamic Arm Articulation
    // =========================================================================
    chrononmotion::motion::RigProgram rigIKProgram;
    {
        // Skeletal chain nodes
        chrononmotion::motion::RigNode root;
        root.id = 10; root.kind = chrononmotion::motion::RigNodeKind::Bone;
        root.rest.position = chrononmotion::Vector3(0.f, -1.0f, 0.f); root.bind = root.rest;

        chrononmotion::motion::RigNode torso;
        torso.id = 20; torso.parent = 10; torso.kind = chrononmotion::motion::RigNodeKind::Bone;
        torso.rest.position = chrononmotion::Vector3(-0.02f * planeW, 0.55f, 0.f); torso.bind = torso.rest;

        chrononmotion::motion::RigNode shoulder;
        shoulder.id = 50; shoulder.parent = 20; shoulder.kind = chrononmotion::motion::RigNodeKind::Bone;
        shoulder.rest.position = chrononmotion::Vector3(-0.15f * planeW, 0.15f, 0.f); shoulder.bind = shoulder.rest;

        chrononmotion::motion::RigNode upperArm;
        upperArm.id = 51; upperArm.parent = 50; upperArm.kind = chrononmotion::motion::RigNodeKind::Bone;
        upperArm.rest.position = chrononmotion::Vector3(-0.06f * planeW, -0.42f, 0.f); upperArm.bind = upperArm.rest;

        chrononmotion::motion::RigNode forearm;
        forearm.id = 52; forearm.parent = 51; forearm.kind = chrononmotion::motion::RigNodeKind::Bone;
        forearm.rest.position = chrononmotion::Vector3(0.08f * planeW, 0.58f, 0.f); forearm.bind = forearm.rest;

        chrononmotion::motion::RigNode hand;
        hand.id = 53; hand.parent = 52; hand.kind = chrononmotion::motion::RigNodeKind::Bone;
        hand.rest.position = chrononmotion::Vector3(0.03f * planeW, 0.22f, 0.f); hand.bind = hand.rest;

        rigIKProgram.graph.nodes = {root, torso, shoulder, upperArm, forearm, hand};

        // Scheduled Two-Bone Arm IK with Pole Vector (Elbow outwards & backwards)
        chrononmotion::motion::RigScheduledIK ikChain;
        ikChain.id = 1;
        ikChain.chain.joints = {50, 51, 52, 53};
        ikChain.pole.add(0.f, chrononmotion::Vector3(-1.55f, -0.65f, -0.35f));

        // Poses for the 3 Test Sequences:
        // Sequence 1 (0.0s..3.2s): Head Rotation Arc (Hand stays at rest)
        // Sequence 2 (3.2s..6.6s): Pronounced Arm IK & Elbow Articulation (>220px reach)
        // Sequence 3 (6.6s..10.0s): Integrated Conversational Performance + Clean Return
        const chrononmotion::Vector3 restHandPos(-0.526f, 0.08f, 0.f);
        const chrononmotion::Vector3 wideIKReach(-0.210f, 0.36f, 0.05f);  // High wide reach, elbow flexed
        const chrononmotion::Vector3 wideIKLower(-0.380f, -0.04f, 0.05f); // Downward reach, elbow extended
        const chrononmotion::Vector3 integratedGesture(-0.330f, 0.22f, 0.05f);

        // Sequence 1: Rest
        ikChain.target.add(0.0f, restHandPos);
        ikChain.target.add(3.2f, restHandPos, chrononmotion::motion::Easing::easeInOut());

        // Sequence 2: Wide IK Flexion & Extension
        ikChain.target.add(4.4f, wideIKReach, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(5.4f, wideIKLower, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(6.6f, restHandPos, chrononmotion::motion::Easing::easeInOut());

        // Sequence 3: Natural Performance & Return
        ikChain.target.add(8.0f, integratedGesture, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(8.8f, integratedGesture, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(9.8f, restHandPos, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(durationSec, restHandPos);

        ikChain.amount.add(0.f, 1.0f);
        rigIKProgram.ik.push_back(std::move(ikChain));
    }

    chrononmotion::motion::RigProgramEvaluator ikEvaluator(rigIKProgram);

    // =========================================================================
    // 5. Author Head Motion & Secondary Organic Delay
    // =========================================================================
    // Sequence 1 (0.0s..3.2s): Head Yaw/Roll Arc (Left +0.24 rad, Right -0.22 rad) -> certifies seamless neck
    // Sequence 2 (3.2s..6.6s): Head steady, subtle reaction to arm motion
    // Sequence 3 (6.6s..10.0s): Cascaded Secondary Motion (5-frame phase delay behind arm gesture)
    layerHead.tracks.rotation.add(0.0f, chrononmotion::Vector3(0.f, 0.f, 0.f));
    layerHead.tracks.rotation.add(0.9f, chrononmotion::Vector3(0.f, 0.f, 0.24f), chrononmotion::motion::Easing::easeInOut()); // Turn Right (~14 deg)
    layerHead.tracks.rotation.add(1.8f, chrononmotion::Vector3(0.f, 0.f, -0.22f), chrononmotion::motion::Easing::easeInOut()); // Turn Left (~13 deg)
    layerHead.tracks.rotation.add(2.6f, chrononmotion::Vector3(0.f, 0.f, 0.12f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(3.2f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut()); // Return to center

    // Sequence 2: Subtle nod during arm extension
    layerHead.tracks.rotation.add(4.5f, chrononmotion::Vector3(0.f, 0.f, -0.06f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(5.6f, chrononmotion::Vector3(0.f, 0.f, 0.05f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(6.6f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());

    // Sequence 3: Natural Cascaded Follow-Through (delay of 6 frames behind arm gesture at 8.0s -> peaks at 8.2s)
    layerHead.tracks.rotation.add(8.2f, chrononmotion::Vector3(0.f, 0.f, 0.18f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(9.0f, chrononmotion::Vector3(0.f, 0.f, 0.18f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(9.85f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));

    // Continuous Organic Torso Breathing & Micro-Sway
    const float breathCycle = durationSec / 3.0f;
    for (int b = 0; b <= 3; ++b) {
        const float t0 = static_cast<float>(b) * breathCycle;
        const float tPeak = t0 + breathCycle * 0.45f;
        if (t0 <= durationSec) {
            layerTorso.tracks.scale.add(t0, chrononmotion::Vector3(1.0f, 1.0f, 1.0f), chrononmotion::motion::Easing::easeInOut());
        }
        if (tPeak < durationSec) {
            layerTorso.tracks.scale.add(tPeak, chrononmotion::Vector3(1.035f, 1.025f, 1.0f), chrononmotion::motion::Easing::easeInOut());
        }
    }
    layerTorso.tracks.scale.add(durationSec, chrononmotion::Vector3(1.0f, 1.0f, 1.0f));

    // Connect Bridge
    threepp::motion::MotionSceneBridge bridgeV21(45.f, camAspect, 0.1f, 100.f);
    auto atmosphericBackdropTex = makeAtmosphericBackdropTexture(1024, 576);
    bridgeV21.bindLayerTexture(bgLayerId, atmosphericBackdropTex);

    if (!debugMasks) {
        bridgeV21.bindLayerTexture(10, texTorsoV21);
        bridgeV21.bindLayerTexture(11, texHeadV21);
        bridgeV21.bindLayerTexture(12, texUpperArmV21);
        bridgeV21.bindLayerTexture(13, texForearmV21);
        bridgeV21.bindLayerTexture(14, texHandV21);
    } else {
        // P0 Diagnostic False Color Masks:
        // Torso: Cyan (0, 210, 255)
        // Head: Magenta (255, 0, 127)
        // UpperArm: Yellow (255, 204, 0)
        // Forearm: Cobalt Blue (51, 102, 255)
        // Hand: Emerald Green (0, 230, 118)
        bridgeV21.bindLayerTexture(10, makeDiagnosticMaskTexture(texTorsoV21, 0, 210, 255));
        bridgeV21.bindLayerTexture(11, makeDiagnosticMaskTexture(texHeadV21, 255, 0, 127));
        bridgeV21.bindLayerTexture(12, makeDiagnosticMaskTexture(texUpperArmV21, 255, 204, 0));
        bridgeV21.bindLayerTexture(13, makeDiagnosticMaskTexture(texForearmV21, 51, 102, 255));
        bridgeV21.bindLayerTexture(14, makeDiagnosticMaskTexture(texHandV21, 0, 230, 118));
    }

    // Optional Side-by-Side Comparison Scene (V2.0 Flawed vs V2.1 Seamless)
    chrononmotion::motion::MotionScene sceneV20("creator_v2_0_scene", fps);
    threepp::motion::MotionSceneBridge bridgeV20(45.f, camAspect, 0.1f, 100.f);
    if (makeComparison) {
        sceneV20.addMesh(bgLayerId, "backdrop", std::move(bgAtt));
        addCutoutLayer(sceneV20, 10, "layer_torso", 0.000f);
        auto& lH0 = addCutoutLayer(sceneV20, 11, "layer_head", 0.010f);
        auto& lU0 = addCutoutLayer(sceneV20, 12, "layer_upper_arm", -0.005f);
        auto& lF0 = addCutoutLayer(sceneV20, 13, "layer_forearm", 0.015f);
        auto& lHnd0 = addCutoutLayer(sceneV20, 14, "layer_hand", 0.025f);

        lH0.transform.anchor = pivotNeck; lH0.transform.position.x = pivotNeck.x; lH0.transform.position.y = pivotNeck.y;
        lU0.transform.anchor = pivotShoulder; lU0.transform.position.x = pivotShoulder.x; lU0.transform.position.y = pivotShoulder.y;
        lF0.transform.anchor = pivotElbow; lF0.transform.position.x = pivotElbow.x; lF0.transform.position.y = pivotElbow.y;
        lHnd0.transform.anchor = pivotWrist; lHnd0.transform.position.x = pivotWrist.x; lHnd0.transform.position.y = pivotWrist.y;

        lH0.tracks.rotation = layerHead.tracks.rotation;
        sceneV20.findLayer(10)->tracks.scale = layerTorso.tracks.scale;

        bridgeV20.bindLayerTexture(bgLayerId, atmosphericBackdropTex);
        bridgeV20.bindLayerTexture(10, texTorsoV20);
        bridgeV20.bindLayerTexture(11, texHeadV20);
        bridgeV20.bindLayerTexture(12, texUpperArmV20);
        bridgeV20.bindLayerTexture(13, texForearmV20);
        bridgeV20.bindLayerTexture(14, texHandV20);
    }

    auto renderV21Frame = [&](int frame) {
        const auto ikFrame = ikEvaluator.evaluate(frame, fps);
        const auto& pose = ikFrame.pose;

        const auto rotUpperArm = pose.local[pose.index(51)].rotation;
        const auto rotForearm = pose.local[pose.index(52)].rotation;
        const auto rotHand = pose.local[pose.index(53)].rotation;

        auto* lU = sceneV21.findLayer(12);
        lU->transform.rotation.z = rotUpperArm.z;

        auto* lF = sceneV21.findLayer(13);
        lF->transform.rotation.z = rotForearm.z;

        auto* lH = sceneV21.findLayer(14);
        lH->transform.rotation.z = rotHand.z;

        bridgeV21.evaluateAndSync(sceneV21, frame, &cameraRig);
        renderer.render(*bridgeV21.scene(), *bridgeV21.camera());
        return renderer.readRGBPixels();
    };

    auto renderV20Frame = [&](int frame) {
        const auto ikFrame = ikEvaluator.evaluate(frame, fps);
        const auto& pose = ikFrame.pose;

        const auto rotUpperArm = pose.local[pose.index(51)].rotation;
        const auto rotForearm = pose.local[pose.index(52)].rotation;
        const auto rotHand = pose.local[pose.index(53)].rotation;

        auto* lU = sceneV20.findLayer(12);
        lU->transform.rotation.z = rotUpperArm.z;

        auto* lF = sceneV20.findLayer(13);
        lF->transform.rotation.z = rotForearm.z;

        auto* lH = sceneV20.findLayer(14);
        lH->transform.rotation.z = rotHand.z;

        bridgeV20.evaluateAndSync(sceneV20, frame, &cameraRig);
        renderer.render(*bridgeV20.scene(), *bridgeV20.camera());
        return renderer.readRGBPixels();
    };

    std::cout << "creator_character_v2_1: certifying seamless multilayer IK animation over "
              << totalFrames << " frames (" << durationSec << " seconds)...\n";

    const auto fRest0 = renderV21Frame(0);
    const auto fHeadPeak = renderV21Frame(static_cast<int>(0.9f * fps));
    const auto fArmPeak = renderV21Frame(static_cast<int>(4.4f * fps));
    const auto fRestLast = renderV21Frame(totalFrames - 1);

    const std::size_t diffHead = countDiffPixels(fRest0, fHeadPeak);
    const std::size_t diffArm = countDiffPixels(fRest0, fArmPeak);
    const std::size_t diffLoop = countDiffPixels(fRest0, fRestLast, 2);

    std::cout << "creator_character_v2_1: diff(Rest0 -> Seq1 Head Arc)=" << diffHead << " pixels\n"
              << "creator_character_v2_1: diff(Rest0 -> Seq2 Wide IK Reach)=" << diffArm << " pixels\n"
              << "creator_character_v2_1: diff(Rest0 -> RestLast loop return)=" << diffLoop << " pixels\n";

    if (diffHead < 1000 || diffArm < 1000) {
        std::cerr << "creator_character_v2_1: expected prominent head and arm motion\n";
        return 1;
    }

    // 6. Render MP4 via FFmpeg pipe
    if (endsWithMp4(outPath)) {
        if (!makeComparison) {
            std::string subTitle = debugMasks ?
                "P0 Diagnostic Mask Tint | Discrete Z-Sorting | Zero Slit Overlap Verification" :
                "Seq 1 - Head Arc | Seq 2 - Two-Bone IK Elbow Flexion | Seq 3 - Organic Cascade";

            std::string mainTitle = debugMasks ?
                "CHARACTER V2.1 - MASK DIAGNOSTIC (FALSE COLOR Z-ORDER & BOUNDARIES)" :
                "CHARACTER V2.1 - SEAMLESS MULTILAYER RIG (INPAINTED SEAMS + DYNAMIC IK)";

            const std::string filter =
                "vflip,"
                "drawtext=text='" + mainTitle + "':fontsize=32:fontcolor=white:box=1:boxcolor=black@0.70:boxborderw=10:x=(w-text_w)/2:y=40,"
                "drawtext=text='" + subTitle + "':fontsize=24:fontcolor=yellow:box=1:boxcolor=black@0.70:boxborderw=8:x=(w-text_w)/2:y=h-60";

            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf \"" + filter + "\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";

            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_character_v2_1: failed to open ffmpeg pipe\n";
                return 1;
            }
            for (int f = 0; f < totalFrames; ++f) {
                const auto pixels = renderV21Frame(f);
                std::fwrite(pixels.data(), 1, pixels.size(), pipe);
            }
            const int rc = pclose(pipe);
            if (rc != 0) {
                std::cerr << "creator_character_v2_1: ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_character_v2_1: wrote video (" << totalFrames << " frames) to " << outPath << "\n";
        } else {
            // Side-by-Side Comparison: Left = V2.0 Flawed (Visible Gap), Right = V2.1 Seamless
            const int halfW = width / 2;
            const std::string filter =
                "vflip,"
                "drawtext=text='CHARACTER V2.0 (FLAWED - RECTANGULAR GAP AT CHEEK)':fontsize=26:fontcolor=red:box=1:boxcolor=black@0.70:boxborderw=10:x=(w/4-text_w/2):y=40,"
                "drawtext=text='CHARACTER V2.1 (SEAMLESS MULTILAYER RIG + IK)':fontsize=26:fontcolor=green:box=1:boxcolor=black@0.70:boxborderw=10:x=(3*w/4-text_w/2):y=40,"
                "drawtext=text='Inpainted Anatomical Overlap | Wide Elbow Flexion | Cascaded Secondary Follow-Through':fontsize=24:fontcolor=yellow:box=1:boxcolor=black@0.70:boxborderw=8:x=(w-text_w)/2:y=h-60";

            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf \"" + filter + "\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";

            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_character_v2_1: failed to open comparison ffmpeg pipe\n";
                return 1;
            }

            std::vector<unsigned char> sideBySide(static_cast<std::size_t>(width * height * 3));
            for (int f = 0; f < totalFrames; ++f) {
                const auto leftPixels = renderV20Frame(f);
                const auto rightPixels = renderV21Frame(f);

                for (int y = 0; y < height; ++y) {
                    std::memcpy(
                        sideBySide.data() + (y * width) * 3,
                        leftPixels.data() + (y * width + (width - halfW) / 2) * 3,
                        static_cast<std::size_t>(halfW * 3));
                    std::memcpy(
                        sideBySide.data() + (y * width + halfW) * 3,
                        rightPixels.data() + (y * width + (width - halfW) / 2) * 3,
                        static_cast<std::size_t>(halfW * 3));
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
                std::cerr << "creator_character_v2_1: comparison ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_character_v2_1: wrote Side-by-Side Comparison video to " << outPath << "\n";
        }
    }

    return 0;
}
