// SCENE-02 cinematic portrait at 854x480: a static upper-body hoodie
// mannequin GLB (Sketchfab "Vroid Hoodie Mannequin", Free Standard license)
// framed waist-up behind a desk, cool window light from the left, warm lamp
// point light, soft shadows and a BokehPass background blur. Slow push-in
// through CameraRig; no rigging, no animation, no locomotion — the figure is
// an unposed static mesh imported by the existing GLTFLoader bridge.
//
// Pipeline: GLTFLoader -> MotionSceneBridge -> GLRenderer + EffectComposer
// (RenderPass -> BokehPass) -> GL PBO -> CUDA -> NVENC, same certified path
// as the T10 depth-of-field certification.
#include "gpu_certification_gate.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif
#include "stb_image_write.h"

#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/ConeGeometry.hpp"
#include "threepp/geometries/CylinderGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/lights/PointLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/postprocessing/BokehPass.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/renderers/GLRenderer.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {
constexpr int W = 854, H = 480, FPS = 30, FRAMES = 150;
constexpr const char* kId = "SCENE02_HOODIE_PORTRAIT";
constexpr unsigned char kTolerance = 6;

void writePng(const fs::path& p, const std::vector<unsigned char>& rgb) {
    std::vector<unsigned char> top(rgb.size());
    const size_t row = size_t(W) * 3;
    for (int y = 0; y < H; ++y) std::copy_n(rgb.data() + size_t(H - y - 1) * row, row, top.data() + size_t(y) * row);
    if (!stbi_write_png(p.c_str(), W, H, 3, top.data(), int(row))) throw std::runtime_error("PNG write failed");
}

bool differs(const std::vector<unsigned char>& a, size_t i, const std::vector<unsigned char>& b) {
    return std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
               std::abs(int(a[i + 2]) - int(b[i + 2])) >
           kTolerance;
}

std::shared_ptr<Mesh> box(float w, float h, float d, unsigned int hex, float rough = 0.9f, float metal = 0.f) {
    auto mat = MeshStandardMaterial::create();
    mat->color.setHex(hex);
    mat->roughness = rough;
    mat->metalness = metal;
    return Mesh::create(BoxGeometry::create(w, h, d), mat);
}

int run(const fs::path& asset, const fs::path& out) {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)asset;
    (void)out;
    throw std::runtime_error("SCENE-02 requires the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    fs::create_directories(out);
    GLTFLoader loader;
    auto result = loader.load(asset);
    if (!result || !result->scene) throw std::runtime_error("GLTFLoader failed: " + asset.string());
    if (!result->animations.empty())
        throw std::runtime_error("SCENE-02 forbids animated assets: the subject must stay still");

    chrononmotion::motion::MotionScene motion(kId, float(FPS));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.assetId = "hoodie_bust";
    auto& bust = motion.addMesh(1, "hoodie_bust", std::move(attachment));
    bust.castsShadow = true;
    bust.acceptsShadow = true;
    // Slight portrait yaw toward the window side; pose itself stays untouched.
    bust.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, -0.18f, 0.f));

    // Soft low ambient so the motivated sources stay dominant.
    auto& ambient = motion.addAmbientLight(100, "ambient");
    ambient.color = chrononmotion::Color(0x5a6a86);
    ambient.intensity = 0.8f;

    // Cool light through the side window: grazing key from the left plus a
    // diffuse bounce that reaches the front of the subject and the set.
    auto& windowLight = motion.addDirectionalLight(101, "window");
    windowLight.color = chrononmotion::Color(0x9ecbff);
    windowLight.intensity = 3.f;
    windowLight.castsShadow = true;
    windowLight.shadowBias = -0.0002f;
    windowLight.shadowNormalBias = 0.02f;
    windowLight.shadowRadius = 4.f;
    windowLight.positionTrack.add(0.f, {-2.8f, 2.8f, 1.4f});
    windowLight.directionTrack.add(0.f, {3.f, -1.3f, -0.6f});

    auto& bounce = motion.addDirectionalLight(103, "bounce");
    bounce.color = chrononmotion::Color(0x86b7e0);
    bounce.intensity = 1.6f;
    bounce.positionTrack.add(0.f, {-2.4f, 2.6f, 2.6f});
    bounce.directionTrack.add(0.f, {2.4f, -1.05f, -2.2f});

    // Warm desk lamp on the right; intensity tuned for the physical falloff.
    auto& lamp = motion.addPointLight(102, "lamp");
    lamp.color = chrononmotion::Color(0xffa95c);
    lamp.intensity = 9.f;
    lamp.positionTrack.add(0.f, {0.85f, 1.52f, 0.15f});
    lamp.intensityTrack.add(0.f, 9.f);
    // Range+decay land on the bridged threepp PointLight (distance/decay).

    threepp::motion::MotionSceneBridge bridge(38.f, float(W) / H, 0.1f, 100.f);
    bridge.bindAsset("hoodie_bust", result->scene);
    bridge.evaluateAndSync(motion, 0);
    auto bustNode = bridge.layerNode(1);
    if (!bustNode) throw std::runtime_error("Motion bridge did not materialize the bust subtree");

    int bustMeshes = 0;
    bustNode->traverse([&](Object3D& o) {
        if (auto* m = dynamic_cast<Mesh*>(&o)) {
            ++bustMeshes;
            o.castShadow = true;
            o.receiveShadow = true;
            if (auto material = m->materialAs<MeshStandardMaterial>()) {
                // Sketchfab ships metallic ~0.56 for cloth; fabric reads as
                // matte, so the portrait dress-downs metalness per material.
                material->metalness = 0.08f;
                material->roughness = 0.88f;
            }
        }
    });
    if (bustMeshes == 0) throw std::runtime_error("bust subtree has no meshes");

    Box3 bounds;
    bounds.setFromObject(*bustNode, true);
    Vector3 center, size;
    bounds.getCenter(center);
    bounds.getSize(size);
    if (!(size.y > 0.3f)) throw std::runtime_error("bust bounds unreasonable: " + std::to_string(size.y));

    // --- set ---------------------------------------------------------------
    // The bust starts floating at its waist (no legs in the model), so the
    // desk between the subject and the camera hides the cut.
    auto scene2 = bridge.scene();
    // Back wall, warm neutral, behind the subject.
    auto backWall = box(7.f, 5.f, 0.1f, 0x2b303a);
    backWall->position.set(0.f, 2.f, -1.55f);
    backWall->receiveShadow = true;
    scene2->add(backWall);
    // Left wall with the window glow plane; window is the cold source.
    // Kept near the frustum edge (half-width ~1.24 u at the subject plane).
    auto leftWall = box(0.1f, 5.f, 4.5f, 0x33383f);
    leftWall->position.set(-1.25f, 2.f, -1.f);
    leftWall->receiveShadow = true;
    scene2->add(leftWall);
    auto glowMat = MeshBasicMaterial::create();
    glowMat->color.setHex(0xd6e9ff);
    auto windowGlow = Mesh::create(BoxGeometry::create(0.02f, 1.9f, 1.15f), glowMat);
    windowGlow->position.set(-1.18f, 1.75f, -0.25f);
    scene2->add(windowGlow);
    // Dark window frame read against the glow.
    auto frameMat = MeshStandardMaterial::create();
    frameMat->color.setHex(0x1d1f26);
    frameMat->roughness = 0.7f;
    auto frameTop = Mesh::create(BoxGeometry::create(0.04f, 0.06f, 1.25f), frameMat);
    frameTop->position.set(-1.13f, 2.72f, -0.25f);
    scene2->add(frameTop);
    auto frameBottom = Mesh::create(BoxGeometry::create(0.04f, 0.06f, 1.25f), frameMat);
    frameBottom->position.set(-1.13f, 0.82f, -0.25f);
    scene2->add(frameBottom);

    // Desk: hides the bust cut; wood tone.
    auto deskTop = box(2.6f, 0.06f, 1.0f, 0x584131, 0.8f, 0.02f);
    deskTop->position.set(0.f, 1.0f, 0.15f);
    deskTop->castShadow = true;
    deskTop->receiveShadow = true;
    scene2->add(deskTop);
    auto deskFront = box(2.6f, 0.85f, 0.06f, 0x4a3529, 0.85f, 0.02f);
    deskFront->position.set(0.f, 0.58f, 0.66f);
    deskFront->receiveShadow = true;
    scene2->add(deskFront);

    // Desk lamp (geometry) + its bridge light lives at the same spot.
    auto lampMat = MeshStandardMaterial::create();
    lampMat->color.setHex(0x23252c);
    lampMat->roughness = 0.4f;
    lampMat->metalness = 0.6f;
    auto lampBase = Mesh::create(CylinderGeometry::create(CylinderGeometry::Params{0.09f, 0.11f, 0.04f, 16}), lampMat);
    lampBase->position.set(0.85f, 1.05f, 0.15f);
    scene2->add(lampBase);
    auto lampStem = Mesh::create(CylinderGeometry::create(CylinderGeometry::Params{0.015f, 0.015f, 0.5f, 8}), lampMat);
    lampStem->position.set(0.85f, 1.3f, 0.15f);
    scene2->add(lampStem);
    auto lampShade = Mesh::create(ConeGeometry::create(0.14f, 0.14f, 16), lampMat);
    lampShade->position.set(0.85f, 1.58f, 0.15f);
    scene2->add(lampShade);
    auto bulbMat = MeshBasicMaterial::create();
    bulbMat->color.setHex(0xffd9ad);
    auto bulb = Mesh::create(SphereGeometry::create(0.045f, 10, 8), bulbMat);
    bulb->position.set(0.85f, 1.5f, 0.15f);
    scene2->add(bulb);

    // --- camera ------------------------------------------------------------
    // Fixed waist-up framing with a very slow push-in; target rides the chest.
    chrononmotion::motion::CameraRig camera(700, 36.f, float(W) / H, 0.1f, 50.f);
    camera.setTarget({0.f, 1.33f, 0.f});
    camera.positionTrack().add(0.f, {0.04f, 1.46f, 2.2f});
    camera.positionTrack().add(5.f, {0.f, 1.42f, 1.78f});
    camera.targetTrack().add(0.f, {0.f, 1.33f, 0.f});
    camera.targetTrack().add(5.f, {0.f, 1.31f, 0.f});
    // Focus sits on the face so the backdrop falls off via Bokeh.
    camera.setFocusDistance(2.05f);
    camera.focusDistanceTrack().add(0.f, 2.05f);
    camera.focusDistanceTrack().add(5.f, 1.62f);

    std::shared_ptr<DirectionalLight> windowLightNode;
    scene2->traverse([&](Object3D& object) {
        if (auto* directional = dynamic_cast<DirectionalLight*>(&object))
            if (directional->castShadow) windowLightNode = std::shared_ptr<DirectionalLight>(directional, [](DirectionalLight*) {});
    });
    if (!windowLightNode || !windowLightNode->shadow)
        throw std::runtime_error("bridge did not create the shadow-casting window light");
    windowLightNode->shadow->mapSize.set(2048.f, 2048.f);

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = rendererName ? rendererName : "unknown";
    chrononmotion::certification::require_nvidia_renderer(glRenderer);
    renderer.setClearColor(Color(0x0a0d14));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.15f;
    renderer.shadowMap().enabled = true;
    renderer.shadowMap().type = ShadowMap::PFCSoft;

    EffectComposer::Options options;
    options.samples = 4;
    EffectComposer composer(renderer, options);
    composer.renderToScreen = false;
    composer.setSize(W, H);
    composer.addPass(std::make_shared<RenderPass>(*scene2, *bridge.camera()));
    auto bokeh = std::make_shared<BokehPass>(*scene2, *bridge.camera(), 2.05f, 0.01f, 0.025f);
    composer.addPass(bokeh);

    const fs::path videoPath = out / "scene02_hoodie_portrait_854x480.mp4";
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder gpuEncoder(W, H, FPS, videoPath.string());
    const auto begin = std::chrono::steady_clock::now();
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &camera);
        const float seconds = float(f) / float(FPS);
        bokeh->focus = camera.sample(seconds).focusDistance;
        composer.render(1.f / float(FPS));
        renderer.setRenderTarget(&composer.readBuffer());
        gpuEncoder.encode_current_target(f);
        renderer.setRenderTarget(nullptr);
    }
    gpuEncoder.finish();
    if (gpuEncoder.frames_submitted() != FRAMES || gpuEncoder.packets_written() != FRAMES)
        throw std::runtime_error("SCENE-02 GPU encoder did not submit and mux all frames");

    // Diagnostics on the composer output: probes, random access, shadow diff.
    const std::array<int, 3> probes{0, 75, 149};
    std::array<std::vector<unsigned char>, 3> probeOn;
    std::vector<unsigned char> frame77, frame0;
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &camera);
        bokeh->focus = camera.sample(float(f) / FPS).focusDistance;
        composer.render(1.f / float(FPS));
        auto pixels = composer.readRGBPixels();
        if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("unexpected SCENE-02 readback size");
        if (f == 0) frame0 = pixels;
        if (f == 77) frame77 = pixels;
        for (size_t i = 0; i < probes.size(); ++i)
            if (f == probes[i]) {
                probeOn[i] = pixels;
                writePng(out / ("frame-" + std::to_string(f) + ".png"), pixels);
            }
    }
    const auto renderDone = std::chrono::steady_clock::now();

    std::array<size_t, 3> shadowChanged{};
    bool toggleDeterministic = true;
    for (size_t i = 0; i < probes.size(); ++i) {
        bridge.evaluateAndSync(motion, probes[i], &camera);
        bokeh->focus = camera.sample(float(probes[i]) / FPS).focusDistance;
        renderer.shadowMap().enabled = false;
        composer.render(1.f / float(FPS));
        const auto off = composer.readRGBPixels();
        renderer.shadowMap().enabled = true;
        composer.render(1.f / float(FPS));
        const auto restored = composer.readRGBPixels();
        if (restored != probeOn[i]) toggleDeterministic = false;
        size_t changed = 0;
        for (size_t p = 0; p < off.size() / 3; ++p)
            if (differs(probeOn[i], p * 3, off)) ++changed;
        shadowChanged[i] = changed;
    }

    bridge.evaluateAndSync(motion, 77, &camera);
    bokeh->focus = camera.sample(77.f / float(FPS)).focusDistance;
    composer.render(1.f / float(FPS));
    const bool randomOk = composer.readRGBPixels() == frame77;

    // Framing and exposure: count actually LIT pixels (luma thresholds), not
    // a clear-color diff — the composer output color space shifts the black.
    size_t litCoverage = 0;
    int subjectMinY = H, subjectMaxY = -1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t(y) * W + size_t(x)) * 3;
            const unsigned int lum = unsigned(frame0[i] + frame0[i + 1] + frame0[i + 2]) / 3;
            if (lum <= 30) continue;
            ++litCoverage;
            subjectMinY = std::min(subjectMinY, y);
            subjectMaxY = std::max(subjectMaxY, y);
        }
    const float litRatio = float(litCoverage) / float(size_t(W) * H);
    const float subjectHeightRatio = subjectMaxY >= subjectMinY ? float(subjectMaxY - subjectMinY + 1) / float(H) : 0.f;
    // Colour temperature read: cool vs warm tinted pixels between warm and cold
    size_t coolPixels = 0, warmPixels = 0;
    for (int y = H * 3 / 10; y < H * 7 / 10; y += 2)
        for (int x = 0; x < W / 3; x += 2) {
            const size_t i = (size_t(y) * W + size_t(x)) * 3;
            if (short(frame0[i + 2]) > short(frame0[i]) + 12 && frame0[i + 2] > 70) ++coolPixels;
        }
    for (int y = H * 3 / 10; y < H * 7 / 10; y += 2)
        for (int x = W * 2 / 3; x < W; x += 2) {
            const size_t i = (size_t(y) * W + size_t(x)) * 3;
            if (short(frame0[i]) > short(frame0[i + 2]) + 15 && frame0[i] > 90) ++warmPixels;
        }

    float cameraTravel = 0.f;
    for (int f = 1; f < FRAMES; ++f)
        cameraTravel += (camera.sample(float(f) / FPS).position - camera.sample(float(f - 1) / FPS).position).length();

    std::cout << kId << " metrics: bust_meshes=" << bustMeshes << " lit_ratio=" << litRatio
              << " subject_height_ratio=" << subjectHeightRatio
              << " cool_left=" << coolPixels << " warm_right=" << warmPixels
              << " camera_travel=" << cameraTravel
              << " random=" << randomOk << " toggle_deterministic=" << toggleDeterministic << std::endl;
    for (size_t i = 0; i < probes.size(); ++i)
        std::cout << kId << " frame" << probes[i] << " shadow_changed=" << shadowChanged[i] << std::endl;
    std::cout << kId << " bust_bounds_center=" << center.x << "," << center.y << "," << center.z
              << " size=" << size.x << "," << size.y << "," << size.z << std::endl;

    const bool shadowsPresent = shadowChanged[0] > 800 && shadowChanged[1] > 800 && shadowChanged[2] > 800;
    const bool compositeLit = litRatio > 0.12f && litRatio < 0.9f;
    const bool colourMixed = coolPixels > 200 && warmPixels > 30;
    if (!randomOk || !toggleDeterministic || bustMeshes == 0 || subjectHeightRatio < 0.45f ||
        cameraTravel < 0.25f || !shadowsPresent || !compositeLit || !colourMixed) {
        throw std::runtime_error(std::string(kId) +
                                 " gate failed: framing, exposure, colour mix, shadows, deterministic access or push-in");
    }

    std::ofstream scene(out / "scene.json");
    scene << "{\n  \"scene\": \"scene02_hoodie_portrait\",\n"
             "  \"subject_asset\": \"vroid_hoodie_mannequin.glb\",\n"
             "  \"pose\": \"static GLB, no rigging, no clip\",\n"
             "  \"framing\": \"waist-up, slow push-in, focus on face (BokehPass)\",\n"
             "  \"lights\": \"cool directional from window + warm lamp point + low ambient\",\n"
             "  \"output\": \"854x480 30fps 150 frames\"\n}\n";

    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream meta(out / "metadata.json");
    meta << std::fixed << std::setprecision(5) << "{\n  \"test_id\": \"" << kId
         << "\",\n  \"pipeline\": \"GLTFLoader static GLB -> MotionSceneBridge (CameraRig push-in) -> threepp GLRenderer -> EffectComposer(RenderPass+BokehPass) -> GL PBO -> CUDA -> NVENC\",\n"
            "  \"subject_asset\": \"vroid_hoodie_mannequin.glb\",\n  \"subject_asset_sha256\": \"7ff5ae6864eb2b8f0b3ff3d7ed64ab285453855dcfef1487100cbf7a81551df9\",\n"
            "  \"subject_asset_license\": \"Sketchfab Free Standard (author lukpatez), commercially restrictive redistrib\",\n"
            "  \"bust_mesh_count\": " << bustMeshes
         << ",\n  \"bust_bounds_center\": [" << center.x << "," << center.y << "," << center.z
         << "],\n  \"bust_bounds_size\": [" << size.x << "," << size.y << "," << size.z
         << "],\n  \"subject_lit_ratio_frame_0\": " << litRatio
         << ",\n  \"subject_height_ratio_frame_0\": " << subjectHeightRatio
         << ",\n  \"cool_tinted_pixels_left\": " << coolPixels
         << ",\n  \"warm_tinted_pixels_right\": " << warmPixels
         << ",\n  \"shadow_changed_pixels\": [" << shadowChanged[0] << ", " << shadowChanged[1] << ", " << shadowChanged[2]
         << "],\n  \"shadow_toggle_deterministic\": " << (toggleDeterministic ? "true" : "false")
         << ",\n  \"random_access_frame_77_identical\": " << (randomOk ? "true" : "false")
         << ",\n  \"camera_travel_world_units\": " << cameraTravel
         << ",\n  \"material_override\": \"bust metalness 0.56->0.08, roughness 0.80->0.88 (fabric matte)\",\n"
            "  \"missing_features\": [\"no glasses asset\", \"T-pose of the GLB is part of the free asset, unposed\"],\n"
            "  \"render_seconds\": " << std::chrono::duration<double>(renderDone - begin).count()
         << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss
         << ",\n  \"gl_renderer\": \"" << glRenderer
         << "\",\n  \"gl_version\": \"" << (version ? version : "unknown")
         << "\",\n  \"video_encoder\": \"h264_nvenc\",\n  \"gpu_device\": \"" << gpuEncoder.device_name()
         << "\",\n  \"frames_submitted\": " << gpuEncoder.frames_submitted()
         << ",\n  \"packets_muxed\": " << gpuEncoder.packets_written()
         << ",\n  \"visual_judgement\": \"pending (not assessed by test)\"\n}\n";

    renderer.dispose();
    std::cout << kId << " OK bust=" << bustMeshes << " travel=" << cameraTravel
              << " random_access=" << randomOk << "\n";
    return 0;
#endif
}
} // namespace

int main(int argc, char** argv) try {
    chrononmotion::certification::prepare_nvidia_egl_headless();
    if (argc < 2) throw std::invalid_argument("usage: scene02_hoodie_portrait output_dir [asset.glb]");
    const fs::path out = argv[1];
    const fs::path asset = argc > 2 ? fs::path(argv[2]) : fs::path("render/spike/assets/scene02/vroid_hoodie_mannequin.glb");
    return run(asset, out);
} catch (const std::exception& e) {
    std::cerr << "SCENE-02 FAIL: " << e.what() << '\n';
    return 1;
}
