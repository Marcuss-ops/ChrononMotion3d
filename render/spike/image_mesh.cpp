// Proof & Tool: Deformable Image Mesh Animation (RigProgram + PinWarp/Wave -> MotionSceneBridge -> GLRenderer).
//
// Takes an input image (or synthesizes a detailed portrait/subject illustration
// if none is provided), maps it onto a subdivided 32x32 PlaneGeometry, and
// animates the image via mesh deformation (PinWarp + Wave + 2.5D camera drift)
// through RigProgramEvaluator and MotionSceneBridge.
//
// Usage:
//   1. Self-contained proof (renders frames 0, 15, 30, 60 and writes PPM or MP4):
//        chrononmotion_render_image_mesh [out.ppm|out.mp4 [w h [frames]]]
//   2. Custom input image (PNG/JPG/WebP -> PPM or MP4):
//        chrononmotion_render_image_mesh <input_image> <out.ppm|out.mp4> [w h [frames]]

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

// Generates a rich procedural subject portrait/card with high-contrast
// features (head, eyes, torso, badge, grid lines) so mesh deformation is
// visually unmistakable and quantitatively measurable on every region.
std::shared_ptr<threepp::DataTexture> makeSyntheticSubjectTexture(int texW, int texH) {
    std::vector<unsigned char> rgb(static_cast<std::size_t>(texW * texH * 3));
    for (int y = 0; y < texH; ++y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(texH); // 0 bottom .. 1 top
        for (int x = 0; x < texW; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(texW);
            const float nx = (u - 0.5f) * 2.f;
            const float ny = (v - 0.5f) * 2.f;

            // Subtle backdrop gradient + fine reference grid
            float r = 28.f + 35.f * v;
            float g = 34.f + 45.f * (1.f - std::abs(nx));
            float b = 52.f + 60.f * v;

            if (x % 32 == 0 || y % 32 == 0) {
                r += 22.f;
                g += 26.f;
                b += 32.f;
            }

            // Torso / shoulders silhouette (centered lower-middle)
            const float torsoDx = nx / 0.62f;
            const float torsoDy = (ny + 0.42f) / 0.58f;
            if (torsoDx * torsoDx + torsoDy * torsoDy < 1.0f && ny < 0.08f) {
                r = 55.f + 90.f * (0.5f + 0.5f * nx);
                g = 115.f + 70.f * (v);
                b = 195.f;
                // Lapel / center stripe
                if (std::abs(nx) < 0.07f) {
                    r = 235.f;
                    g = 195.f;
                    b = 75.f;
                }
            }

            // Head circle (upper center)
            const float headDx = (nx) / 0.28f;
            const float headDy = (ny - 0.34f) / 0.30f;
            const float headR2 = headDx * headDx + headDy * headDy;
            if (headR2 < 1.0f) {
                r = 238.f - 30.f * headR2;
                g = 190.f - 25.f * headR2;
                b = 152.f - 20.f * headR2;

                // Eyes
                const float leftEye = (nx + 0.09f) * (nx + 0.09f) + (ny - 0.38f) * (ny - 0.38f);
                const float rightEye = (nx - 0.09f) * (nx - 0.09f) + (ny - 0.38f) * (ny - 0.38f);
                if (leftEye < 0.0012f || rightEye < 0.0012f) {
                    r = 25.f;
                    g = 30.f;
                    b = 42.f;
                }
                // Smile / mouth line
                if (std::abs(ny - 0.25f - 0.12f * nx * nx) < 0.016f && std::abs(nx) < 0.11f) {
                    r = 185.f;
                    g = 65.f;
                    b = 65.f;
                }
            }

            // Cape / banner wave band in upper-right
            if (nx > 0.22f && nx < 0.78f && ny > 0.05f && ny < 0.65f && headR2 >= 1.0f) {
                const float stripe = std::sin((nx * 10.f + ny * 6.f) * 3.14159265f);
                r = stripe > 0.f ? 225.f : 175.f;
                g = stripe > 0.f ? 75.f : 45.f;
                b = 60.f;
            }

            const std::size_t idx = static_cast<std::size_t>(y * texW + x) * 3;
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

std::size_t countDiffPixels(
    const std::vector<unsigned char>& a,
    const std::vector<unsigned char>& b,
    int threshold = 6) {
    std::size_t diff = 0;
    for (std::size_t i = 0; i + 2 < a.size(); i += 3) {
        const int dr = std::abs(int(a[i]) - int(b[i]));
        const int dg = std::abs(int(a[i + 1]) - int(b[i + 1]));
        const int db = std::abs(int(a[i + 2]) - int(b[i + 2]));
        if (dr + dg + db > threshold) ++diff;
    }
    return diff;
}

void addPerimeterStiffnessPins(
    chrononmotion::motion::RigDeformer& deformer,
    float planeW,
    float planeH) {
    const float hx = 0.5f * planeW;
    const float hy = 0.5f * planeH;
    const float borderRadius = 0.22f * planeH;

    // Pin 8 points around the outer perimeter so the image border never warps
    const chrononmotion::Vector3 borderPts[] = {
        {-hx, -hy, 0.f}, {0.f, -hy, 0.f}, {hx, -hy, 0.f},
        {-hx,  0.f, 0.f},                  {hx,  0.f, 0.f},
        {-hx,  hy, 0.f}, {0.f,  hy, 0.f}, {hx,  hy, 0.f},
    };
    for (const auto& pt : borderPts) {
        chrononmotion::motion::RigPin pin;
        pin.kind = chrononmotion::motion::RigPinKind::Stiffness;
        pin.rest = pt;
        pin.radius = borderRadius;
        pin.weight = 0.95f;
        deformer.pins.push_back(pin);
    }

    // Keep the left desk/monitor zone rigid so straight architectural lines stay straight
    chrononmotion::motion::RigPin leftPropAnchor;
    leftPropAnchor.kind = chrononmotion::motion::RigPinKind::Stiffness;
    leftPropAnchor.rest = chrononmotion::Vector3(-0.34f * planeW, -0.02f * planeH, 0.f);
    leftPropAnchor.radius = 0.24f * planeH;
    leftPropAnchor.weight = 0.88f;
    deformer.pins.push_back(leftPropAnchor);
}

} // namespace

int main(int argc, char** argv) {
    std::string inputImagePath;
    std::string outPath = "image_mesh.ppm";
    int width = 512;
    int height = 512;
    int totalFrames = 61; // frames 0..60 at 30 fps (2 seconds loop)

    int argIdx = 1;
    if (argc >= 3 && isImagePath(argv[1])) {
        inputImagePath = argv[1];
        outPath = argv[2];
        argIdx = 3;
    } else if (argc >= 2) {
        outPath = argv[1];
        argIdx = 2;
    }
    if (argc > argIdx) width = std::atoi(argv[argIdx++]);
    if (argc > argIdx) height = std::atoi(argv[argIdx++]);
    if (argc > argIdx) totalFrames = std::max(4, std::atoi(argv[argIdx++]));

    const float fps = 30.f;
    const float durationSec = static_cast<float>(totalFrames - 1) / fps;

    // 1. Setup headless GL context & load/create the image texture
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x0d1017));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    std::shared_ptr<threepp::Texture> imageTexture;
    float imageAspect = static_cast<float>(width) / static_cast<float>(height);
    if (!inputImagePath.empty()) {
        threepp::ImageLoader loader;
        auto imgOpt = loader.load(inputImagePath, 4, true);
        if (!imgOpt || imgOpt->width() == 0 || imgOpt->height() == 0) {
            std::cerr << "image_mesh: failed to load image " << inputImagePath << "\n";
            return 1;
        }
        imageAspect = static_cast<float>(imgOpt->width()) /
                      static_cast<float>(imgOpt->height());
        std::cout << "image_mesh: loaded " << inputImagePath << " ("
                  << imgOpt->width() << "x" << imgOpt->height() << ")\n";
        imageTexture = threepp::Texture::create(std::move(*imgOpt));
        imageTexture->needsUpdate();
    } else {
        imageTexture = makeSyntheticSubjectTexture(256, 256);
        imageAspect = 1.0f;
        std::cout << "image_mesh: using synthetic portrait texture (256x256)\n";
    }

    // 2. Build a finely subdivided PlaneGeometry (48x48 grid = 2401 vertices)
    const float planeH = 2.4f;
    const float planeW = planeH * imageAspect;
    auto planeGeom = chrononmotion::PlaneGeometry::create(planeW, planeH, 48, 48);
    const auto restPositions = threepp::motion::extractGeometryPositions(*planeGeom);

    // 3. Author the MotionScene (darkened ambient backdrop plane + hero deformable mesh + camera parallax)
    chrononmotion::motion::MotionScene motionScene("image_mesh_scene", fps);

    // Dimmed ambient backdrop plane at z = -0.55 for cinematic depth
    chrononmotion::motion::MeshAttachment bgAttachment;
    bgAttachment.geometry = chrononmotion::PlaneGeometry::create(planeW * 1.42f, planeH * 1.42f, 2, 2);
    bgAttachment.material = chrononmotion::MaterialDescriptor::unlit(2, chrononmotion::Color(0x2c3444));
    const chrononmotion::motion::Layer::Id bgLayerId = 5;
    auto& bgLayer = motionScene.addMesh(bgLayerId, "ambient_backdrop", std::move(bgAttachment));
    bgLayer.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, -0.55f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec * 0.5f, chrononmotion::Vector3(-0.04f, -0.02f, -0.55f), chrononmotion::motion::Easing::easeInOut());
    bgLayer.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, -0.55f));

    chrononmotion::motion::MeshAttachment attachment;
    attachment.geometry = planeGeom;
    attachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    const chrononmotion::motion::Layer::Id imageLayerId = 10;
    auto& imageLayer = motionScene.addMesh(imageLayerId, "deformable_image", std::move(attachment));
    // Subtle breathing scale on the hero layer itself, returning to rest at durationSec
    imageLayer.tracks.scale.add(0.f, chrononmotion::Vector3(1.f, 1.f, 1.f), chrononmotion::motion::Easing::easeInOut());
    imageLayer.tracks.scale.add(durationSec * 0.5f, chrononmotion::Vector3(1.018f, 1.018f, 1.f), chrononmotion::motion::Easing::easeInOut());
    imageLayer.tracks.scale.add(durationSec, chrononmotion::Vector3(1.f, 1.f, 1.f));

    chrononmotion::motion::CameraRig cameraRig(
        100, 45.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.f, 2.72f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec * 0.5f, chrononmotion::Vector3(0.06f, 0.03f, 2.58f), chrononmotion::motion::Easing::easeInOut());
    cameraRig.positionTrack().add(durationSec, chrononmotion::Vector3(0.f, 0.f, 2.72f));

    // 4. Author the RigProgram that deforms the image mesh vertices over time
    //    with 3 independent, anatomically natural PinWarp operators while keeping
    //    the outer image frame and background architecture anchored:
    //    - Operator 0 (Controller 1): Head gaze/nod + 2.5D Z-relief + gentle tilt
    //    - Operator 1 (Controller 2): Chest & shoulder breathing expansion (2 cycles)
    //    - Operator 2 (Controller 3): Forearm/pen writing & secondary shoulder sway
    chrononmotion::motion::RigProgram rigProgram;
    {
        for (chrononmotion::motion::RigNodeId id = 1; id <= 3; ++id) {
            chrononmotion::motion::RigNode ctrl;
            ctrl.id = id;
            ctrl.kind = chrononmotion::motion::RigNodeKind::Controller;
            rigProgram.graph.nodes.push_back(ctrl);
        }

        // Controller 1: smooth head look & nod curve
        chrononmotion::motion::RigAnimatedNode headAnim;
        headAnim.id = 1;
        headAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.30f, chrononmotion::Vector3(0.85f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.55f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec * 0.80f, chrononmotion::Vector3(0.55f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        headAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(headAnim));

        // Controller 2: two natural breathing cycles over durationSec
        chrononmotion::motion::RigAnimatedNode breathAnim;
        breathAnim.id = 2;
        breathAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.25f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.50f, chrononmotion::Vector3(0.20f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec * 0.75f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        breathAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(breathAnim));

        // Controller 3: forearm/pen micro-action + torso sway
        chrononmotion::motion::RigAnimatedNode handAnim;
        handAnim.id = 3;
        handAnim.tracks.position.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.20f, chrononmotion::Vector3(0.95f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.40f, chrononmotion::Vector3(0.30f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.65f, chrononmotion::Vector3(1.0f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec * 0.85f, chrononmotion::Vector3(0.45f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
        handAnim.tracks.position.add(durationSec, chrononmotion::Vector3(0.f, 0.f, 0.f));
        rigProgram.animation.push_back(std::move(handAnim));

        // Operator 0: Head gaze / nod / 2.5D relief
        chrononmotion::motion::RigDeformer headWarp;
        headWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        headWarp.amount = 1.f;
        addPerimeterStiffnessPins(headWarp, planeW, planeH);

        chrononmotion::motion::RigPin headMove;
        headMove.kind = chrononmotion::motion::RigPinKind::Position;
        headMove.rest = chrononmotion::Vector3(-0.01f * planeW, 0.20f * planeH, 0.f);
        headMove.target = chrononmotion::Vector3(-0.042f * planeW, 0.178f * planeH, 0.12f * planeH);
        headMove.radius = 0.34f * planeH;
        headMove.weight = 1.0f;
        headWarp.pins.push_back(headMove);

        chrononmotion::motion::RigPin headTilt;
        headTilt.kind = chrononmotion::motion::RigPinKind::Rotation;
        headTilt.rest = chrononmotion::Vector3(-0.01f * planeW, 0.18f * planeH, 0.f);
        headTilt.rotation.setFromAxisAngle(
            chrononmotion::Vector3(0.f, -0.55f, -0.835f).normalize(), 0.085f);
        headTilt.radius = 0.32f * planeH;
        headTilt.weight = 0.85f;
        headWarp.pins.push_back(headTilt);

        // Operator 1: Chest & shoulder breathing
        chrononmotion::motion::RigDeformer breathWarp;
        breathWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        breathWarp.amount = 1.f;
        addPerimeterStiffnessPins(breathWarp, planeW, planeH);

        chrononmotion::motion::RigPin chestBreath;
        chestBreath.kind = chrononmotion::motion::RigPinKind::Scale;
        chestBreath.rest = chrononmotion::Vector3(0.05f * planeW, -0.14f * planeH, 0.f);
        chestBreath.scale = chrononmotion::Vector3(1.065f, 1.055f, 1.f);
        chestBreath.radius = 0.36f * planeH;
        chestBreath.weight = 0.95f;
        breathWarp.pins.push_back(chestBreath);

        chrononmotion::motion::RigPin shoulderRise;
        shoulderRise.kind = chrononmotion::motion::RigPinKind::Position;
        shoulderRise.rest = chrononmotion::Vector3(0.12f * planeW, -0.06f * planeH, 0.f);
        shoulderRise.target = chrononmotion::Vector3(0.135f * planeW, -0.032f * planeH, 0.08f * planeH);
        shoulderRise.radius = 0.34f * planeH;
        shoulderRise.weight = 0.90f;
        breathWarp.pins.push_back(shoulderRise);

        // Operator 2: Writing hand/forearm movement + right torso/banner sway
        chrononmotion::motion::RigDeformer handWarp;
        handWarp.kind = chrononmotion::motion::RigDeformerKind::PinWarp;
        handWarp.amount = 1.f;
        addPerimeterStiffnessPins(handWarp, planeW, planeH);

        chrononmotion::motion::RigPin writingHand;
        writingHand.kind = chrononmotion::motion::RigPinKind::Position;
        writingHand.rest = chrononmotion::Vector3(-0.20f * planeW, -0.32f * planeH, 0.f);
        writingHand.target = chrononmotion::Vector3(-0.165f * planeW, -0.295f * planeH, 0.06f * planeH);
        writingHand.radius = 0.25f * planeH;
        writingHand.weight = 1.0f;
        handWarp.pins.push_back(writingHand);

        chrononmotion::motion::RigPin rightSway;
        rightSway.kind = chrononmotion::motion::RigPinKind::Position;
        rightSway.rest = chrononmotion::Vector3(0.24f * planeW, 0.12f * planeH, 0.f);
        rightSway.target = chrononmotion::Vector3(0.275f * planeW, 0.145f * planeH, 0.05f * planeH);
        rightSway.radius = 0.30f * planeH;
        rightSway.weight = 0.90f;
        handWarp.pins.push_back(rightSway);

        chrononmotion::motion::RigGeometryBinding geomBinding;
        geomBinding.id = 1;
        geomBinding.source = restPositions;
        geomBinding.deformers.operators.push_back(std::move(headWarp));
        geomBinding.deformers.operators.push_back(std::move(breathWarp));
        geomBinding.deformers.operators.push_back(std::move(handWarp));
        rigProgram.geometry.push_back(std::move(geomBinding));

        rigProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
            1, chrononmotion::motion::RigWeightKind::Deformer, 1, 0});
        rigProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
            2, chrononmotion::motion::RigWeightKind::Deformer, 1, 1});
        rigProgram.weights.push_back(chrononmotion::motion::RigWeightBinding{
            3, chrononmotion::motion::RigWeightKind::Deformer, 1, 2});
    }

    chrononmotion::motion::RigProgramEvaluator rigEvaluator(rigProgram);

    // 5. Connect MotionSceneBridge + textures
    threepp::motion::MotionSceneBridge bridge(
        45.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
    bridge.bindLayerTexture(bgLayerId, imageTexture);
    bridge.bindLayerTexture(imageLayerId, imageTexture);

    auto renderAtFrame = [&](int frame) {
        const auto rigFrame = rigEvaluator.evaluate(frame, fps);
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        bridge.applyRigFrame(imageLayerId, rigFrame.geometry.front(), false);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    const int midFrame1 = totalFrames / 4;
    const int midFrame2 = totalFrames / 2;
    const int lastFrame = totalFrames - 1;

    const auto f0 = renderAtFrame(0);
    const auto fMid1 = renderAtFrame(midFrame1);
    const auto fMid2 = renderAtFrame(midFrame2);
    const auto fLast = renderAtFrame(lastFrame);
    const auto fMid1Repeat = renderAtFrame(midFrame1);

    const std::size_t diff0ToMid1 = countDiffPixels(f0, fMid1);
    const std::size_t diffMid1ToMid2 = countDiffPixels(fMid1, fMid2);
    const std::size_t diff0ToMid2 = countDiffPixels(f0, fMid2);

    std::cout << "image_mesh: changed_pixels(0->" << midFrame1 << ")=" << diff0ToMid1
              << " changed_pixels(" << midFrame1 << "->" << midFrame2 << ")=" << diffMid1ToMid2
              << " changed_pixels(0->" << midFrame2 << ")=" << diff0ToMid2 << "\n";

    if (diff0ToMid1 < 100 || diffMid1ToMid2 < 50 || diff0ToMid2 < 100) {
        std::cerr << "image_mesh: expected significant pixel motion from mesh deformation\n";
        return 1;
    }
    if (f0 != fLast) {
        std::cerr << "image_mesh: expected final rest frame " << lastFrame << " to match frame 0\n";
        return 1;
    }
    if (fMid1 != fMid1Repeat) {
        std::cerr << "image_mesh: out-of-order frame evaluation was not deterministic\n";
        return 1;
    }

    // 6. Write output (MP4 video via ffmpeg pipe, or representative deformed PPM)
    if (endsWithMp4(outPath)) {
        const std::string cmd =
            "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
            std::to_string(width) + "x" + std::to_string(height) +
            " -framerate 30 -i - -vf vflip -c:v libx264 -pix_fmt yuv420p \"" + outPath + "\"";
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) {
            std::cerr << "image_mesh: failed to open ffmpeg pipe\n";
            return 1;
        }
        for (int f = 0; f < totalFrames; ++f) {
            const auto pixels = renderAtFrame(f);
            std::fwrite(pixels.data(), 1, pixels.size(), pipe);
        }
        const int rc = pclose(pipe);
        if (rc != 0) {
            std::cerr << "image_mesh: ffmpeg exited with " << rc << "\n";
            return 1;
        }
        std::cout << "image_mesh: wrote MP4 video (" << totalFrames << " frames) to " << outPath << "\n";
    } else {
        std::ofstream ppm(outPath, std::ios::binary);
        ppm << "P6\n" << width << " " << height << "\n255\n";
        for (int y = height - 1; y >= 0; --y) {
            ppm.write(reinterpret_cast<const char*>(&fMid2[y * width * 3]), width * 3);
        }
        std::cout << "image_mesh: wrote deformed frame PPM to " << outPath << "\n";
    }

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    return 0;
}
