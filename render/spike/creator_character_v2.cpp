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
    std::string layersDir = "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/layers";
    std::string outPath = "creator_character_v2_1080p.mp4";
    int width = 1920;
    int height = 1080;
    int totalFrames = 301; // 10.0 seconds @ 30 fps (broadcast narrative loop)
    bool makeComparison = false;

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
    }

    const float fps = 30.f;
    const float durationSec = static_cast<float>(totalFrames - 1) / fps;

    // 1. Setup headless GL context at 1920x1080
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x13171f));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // 2. Load 5 segmented high-resolution layer textures
    threepp::ImageLoader loader;
    auto texTorso = threepp::Texture::create(std::move(*loader.load(layersDir + "/layer_torso.png", 4, true)));
    auto texHead = threepp::Texture::create(std::move(*loader.load(layersDir + "/layer_head.png", 4, true)));
    auto texUpperArm = threepp::Texture::create(std::move(*loader.load(layersDir + "/layer_upper_arm.png", 4, true)));
    auto texForearm = threepp::Texture::create(std::move(*loader.load(layersDir + "/layer_forearm.png", 4, true)));
    auto texHand = threepp::Texture::create(std::move(*loader.load(layersDir + "/layer_hand.png", 4, true)));

    // Load full single-sheet texture for comparison
    auto texFull = threepp::Texture::create(std::move(*loader.load(
        "/home/pierone/src/go-master/projects/Pyt/VeloxEditing/ChrononMotion3D/render/spike/assets/creator_transparent.png", 4, true)));

    const float imageAspect = 1376.f / 768.f;
    const float planeH = 2.45f;
    const float planeW = planeH * imageAspect;
    auto quadGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 2, 2);

    const float camAspect = static_cast<float>(width) / static_cast<float>(height);

    // =========================================================================
    // 3. Build Character Rig V2: Multilayer Scene with Discrete Z-Depth Layers
    // =========================================================================
    chrononmotion::motion::MotionScene sceneV2("creator_v2_scene", fps);

    // Background layer
    const float bgDist = 3.85f;
    const float bgH = 2.f * std::tan(45.f * 0.5f * 3.14159265f / 180.f) * bgDist * 1.35f;
    const float bgW = bgH * camAspect * 1.35f;
    chrononmotion::motion::MeshAttachment bgAtt;
    bgAtt.geometry = chrononmotion::PlaneGeometry::create(bgW, bgH, 2, 2);
    bgAtt.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = sceneV2.addMesh(bgLayerId, "backdrop", std::move(bgAtt));
    bgLayer.transform.position.set(0.f, 0.f, -0.85f);

    // Multilayer Anatomical Cutout: 5 Layers with discrete Z-depth
    auto addCutoutLayer = [&](chrononmotion::motion::Layer::Id id, const std::string& name, float z) -> chrononmotion::motion::Layer& {
        chrononmotion::motion::MeshAttachment att;
        att.geometry = quadGeom;
        att.material = chrononmotion::MaterialDescriptor::unlit(id, chrononmotion::Color(0xffffff));
        att.material.transparent = true;
        auto& l = sceneV2.addMesh(id, name, std::move(att));
        l.transform.position.z = z;
        return l;
    };

    // Layer 10: Torso (Base level, Z = 0.000m)
    auto& layerTorso = addCutoutLayer(10, "layer_torso", 0.000f);
    // Layer 11: Head (Slightly in front, Z = +0.010m)
    auto& layerHead = addCutoutLayer(11, "layer_head", 0.010f);
    // Layer 12: UpperArm (Behind shoulder, Z = -0.005m)
    auto& layerUpperArm = addCutoutLayer(12, "layer_upper_arm", -0.005f);
    // Layer 13: Forearm (In front of torso, Z = +0.015m)
    auto& layerForearm = addCutoutLayer(13, "layer_forearm", 0.015f);
    // Layer 14: Hand (Topmost foreground, Z = +0.025m, clean overlap)
    auto& layerHand = addCutoutLayer(14, "layer_hand", 0.025f);

    // Anatomical Pivots in scene space
    const chrononmotion::Vector3 pivotNeck(-0.057f * planeW, 0.076f * planeH, 0.f);
    const chrononmotion::Vector3 pivotShoulder(-0.160f * planeW, -0.125f * planeH, 0.f);
    const chrononmotion::Vector3 pivotElbow(-0.245f * planeW, -0.288f * planeH, 0.f);
    const chrononmotion::Vector3 pivotWrist(-0.187f * planeW, -0.047f * planeH, 0.f);

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
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.015f, 0.045f, 2.58f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.04f, 2.65f));

    // =========================================================================
    // 4. Two-Bone IK Solver & Scheduled Animation
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

        // Scheduled Two-Bone Arm IK with Pole Vector
        chrononmotion::motion::RigScheduledIK ikChain;
        ikChain.id = 1;
        ikChain.chain.joints = {50, 51, 52, 53};
        ikChain.pole.add(0.f, chrononmotion::Vector3(-1.45f, -0.65f, -0.3f)); // Elbow pole vector keeps elbow outwards

        // Animated Hand Target Path (Documentary pointing gesture)
        const chrononmotion::Vector3 restHandPos(-0.526f, 0.08f, 0.f);
        const chrononmotion::Vector3 gesturePeak1(-0.320f, 0.26f, 0.05f); // Upward & inward point (>85px travel)
        const chrononmotion::Vector3 gesturePeak2(-0.380f, 0.18f, 0.05f); // Emphasis beat

        ikChain.target.add(0.f, restHandPos);
        ikChain.target.add(1.5f, restHandPos, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(3.0f, gesturePeak1, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(4.5f, gesturePeak1, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(6.0f, gesturePeak2, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(7.5f, gesturePeak2, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(9.0f, restHandPos, chrononmotion::motion::Easing::easeInOut());
        ikChain.target.add(durationSec, restHandPos);

        ikChain.amount.add(0.f, 1.0f);
        rigIKProgram.ik.push_back(std::move(ikChain));
    }

    chrononmotion::motion::RigProgramEvaluator ikEvaluator(rigIKProgram);

    // Author Head Secondary Motion (Phase delay of 5 frames behind gesture)
    layerHead.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
    layerHead.tracks.rotation.add(1.65f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(3.2f, chrononmotion::Vector3(0.f, 0.f, 0.21f), chrononmotion::motion::Easing::easeInOut()); // Tilts 12 deg toward target
    layerHead.tracks.rotation.add(4.7f, chrononmotion::Vector3(0.f, 0.f, 0.21f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(6.2f, chrononmotion::Vector3(0.f, 0.f, -0.16f), chrononmotion::motion::Easing::easeInOut()); // Secondary nod left
    layerHead.tracks.rotation.add(7.6f, chrononmotion::Vector3(0.f, 0.f, -0.16f), chrononmotion::motion::Easing::easeInOut());
    layerHead.tracks.rotation.add(9.1f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut()); // Return
    layerHead.tracks.rotation.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));

    // Author Torso Breathing (Continuous sinusoidal cycles)
    const float breathCycle = durationSec / 3.0f;
    for (int b = 0; b <= 3; ++b) {
        const float t0 = static_cast<float>(b) * breathCycle;
        const float tPeak = t0 + breathCycle * 0.45f;
        if (t0 <= durationSec) {
            layerTorso.tracks.scale.add(t0, chrononmotion::Vector3(1.0f, 1.0f, 1.0f), chrononmotion::motion::Easing::easeInOut());
        }
        if (tPeak < durationSec) {
            layerTorso.tracks.scale.add(tPeak, chrononmotion::Vector3(1.042f, 1.030f, 1.0f), chrononmotion::motion::Easing::easeInOut());
        }
    }
    layerTorso.tracks.scale.add(durationSec, chrononmotion::Vector3(1.0f, 1.0f, 1.0f));

    // Connect Bridge
    threepp::motion::MotionSceneBridge bridgeV2(45.f, camAspect, 0.1f, 100.f);
    auto atmosphericBackdropTex = makeAtmosphericBackdropTexture(1024, 576);
    bridgeV2.bindLayerTexture(bgLayerId, atmosphericBackdropTex);
    bridgeV2.bindLayerTexture(10, texTorso);
    bridgeV2.bindLayerTexture(11, texHead);
    bridgeV2.bindLayerTexture(12, texUpperArm);
    bridgeV2.bindLayerTexture(13, texForearm);
    bridgeV2.bindLayerTexture(14, texHand);

    // =========================================================================
    // 5. Build Scene 1 (Single Sheet Mesh for Comparison)
    // =========================================================================
    chrononmotion::motion::MotionScene sceneSingleMesh("creator_single_scene", fps);
    auto& singleBg = sceneSingleMesh.addMesh(bgLayerId, "backdrop", std::move(bgAtt));
    singleBg.transform.position.set(0.f, 0.f, -0.85f);

    chrononmotion::motion::MeshAttachment singleHeroAtt;
    singleHeroAtt.geometry = chrononmotion::PlaneGeometry::create(planeW, planeH, 64, 64);
    singleHeroAtt.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    singleHeroAtt.material.transparent = true;
    const chrononmotion::motion::Layer::Id singleHeroId = 10;
    sceneSingleMesh.addMesh(singleHeroId, "hero_single", std::move(singleHeroAtt));

    threepp::motion::MotionSceneBridge bridgeSingle(45.f, camAspect, 0.1f, 100.f);
    bridgeSingle.bindLayerTexture(bgLayerId, atmosphericBackdropTex);
    bridgeSingle.bindLayerTexture(singleHeroId, texFull);

    // Renderer for Character Rig V2 (Multilayer + IK + Clean Occlusion)
    auto renderV2Frame = [&](int frame) {
        // Evaluate IK pose
        const auto ikFrame = ikEvaluator.evaluate(frame, fps);
        const auto& pose = ikFrame.pose;

        // Extract solved joint rotations and apply to arm layers
        const auto rotUpperArm = pose.local[pose.index(51)].rotation;
        const auto rotForearm = pose.local[pose.index(52)].rotation;
        const auto rotHand = pose.local[pose.index(53)].rotation;

        auto* lU = sceneV2.findLayer(12);
        lU->transform.rotation.z = rotUpperArm.z;

        auto* lF = sceneV2.findLayer(13);
        lF->transform.rotation.z = rotForearm.z;

        auto* lH = sceneV2.findLayer(14);
        lH->transform.rotation.z = rotHand.z;

        bridgeV2.evaluateAndSync(sceneV2, frame, &cameraRig);
        renderer.render(*bridgeV2.scene(), *bridgeV2.camera());
        return renderer.readRGBPixels();
    };

    // Renderer for Single Sheet Mesh (showing texture stretch and occlusion collision)
    auto renderSingleMeshFrame = [&](int frame) {
        bridgeSingle.evaluateAndSync(sceneSingleMesh, frame, &cameraRig);
        renderer.render(*bridgeSingle.scene(), *bridgeSingle.camera());
        return renderer.readRGBPixels();
    };

    std::cout << "creator_character_v2: validating multilayer IK animation over "
              << totalFrames << " frames (" << durationSec << " seconds)...\n";

    const auto fRest0 = renderV2Frame(0);
    const auto fGesturePeak = renderV2Frame(static_cast<int>(3.0f * fps));
    const auto fRestLast = renderV2Frame(totalFrames - 1);

    const std::size_t diffGesture = countDiffPixels(fRest0, fGesturePeak);
    const std::size_t diffLoop = countDiffPixels(fRest0, fRestLast, 2);

    std::cout << "creator_character_v2: diff(Rest -> IK Gesture Peak)=" << diffGesture << " pixels\n"
              << "creator_character_v2: diff(Rest0 -> RestLast loop)=" << diffLoop << " pixels\n";

    if (diffGesture < 1000) {
        std::cerr << "creator_character_v2: expected prominent IK arm displacement\n";
        return 1;
    }

    // 6. Render MP4 video via ffmpeg pipe
    if (endsWithMp4(outPath)) {
        if (!makeComparison) {
            // Standard Solo Full HD video
            const std::string filter =
                "vflip,"
                "drawtext=text='CHRONONMOTION3D: CHARACTER RIG V2 (MULTILAYER + TWO-BONE IK)':fontsize=32:fontcolor=white:box=1:boxcolor=black@0.65:boxborderw=10:x=(w-text_w)/2:y=40,"
                "drawtext=text='Two-Bone IK Arm Solver | Discrete Z-Depth Occlusion | Secondary Head Lag | Zero Texture Warp':fontsize=24:fontcolor=yellow:box=1:boxcolor=black@0.65:boxborderw=8:x=(w-text_w)/2:y=h-60";
            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf \"" + filter + "\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_character_v2: failed to open ffmpeg pipe\n";
                return 1;
            }
            for (int f = 0; f < totalFrames; ++f) {
                const auto pixels = renderV2Frame(f);
                std::fwrite(pixels.data(), 1, pixels.size(), pipe);
            }
            const int rc = pclose(pipe);
            if (rc != 0) {
                std::cerr << "creator_character_v2: ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_character_v2: wrote Full HD Solo video (" << totalFrames << " frames, "
                      << durationSec << "s) to " << outPath << "\n";
        } else {
            // Side-by-Side Comparison: Left = Single-Sheet Mesh, Right = Character Rig V2 Multilayer IK
            const int halfW = width / 2;
            const std::string filter =
                "vflip,"
                "drawtext=text='SINGLE-SHEET MESH (OCCLUSION COLLISION)':fontsize=30:fontcolor=white:box=1:boxcolor=black@0.65:boxborderw=10:x=(w/4-text_w/2):y=40,"
                "drawtext=text='CHARACTER RIG V2 (MULTILAYER + IK + Z-OCCLUSION)':fontsize=30:fontcolor=white:box=1:boxcolor=black@0.65:boxborderw=10:x=(3*w/4-text_w/2):y=40,"
                "drawtext=text='Two-Bone IK Arm Solver | Discrete Z-Depth Occlusion | Secondary Head Lag | Zero Texture Warp':fontsize=24:fontcolor=yellow:box=1:boxcolor=black@0.65:boxborderw=8:x=(w-text_w)/2:y=h-60";
            const std::string cmd =
                "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                std::to_string(width) + "x" + std::to_string(height) +
                " -framerate 30 -i - -vf \"" + filter + "\" -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
            FILE* pipe = popen(cmd.c_str(), "w");
            if (!pipe) {
                std::cerr << "creator_character_v2: failed to open comparison ffmpeg pipe\n";
                return 1;
            }

            std::vector<unsigned char> sideBySide(static_cast<std::size_t>(width * height * 3));
            for (int f = 0; f < totalFrames; ++f) {
                const auto leftPixels = renderSingleMeshFrame(f);
                const auto rightPixels = renderV2Frame(f);

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
                std::cerr << "creator_character_v2: comparison ffmpeg exited with " << rc << "\n";
                return 1;
            }
            std::cout << "creator_character_v2: wrote Side-by-Side Comparison video to " << outPath << "\n";
        }
    }

    return 0;
}
