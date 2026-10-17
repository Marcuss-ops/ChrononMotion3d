// SCENE-01 low-poly crowd: a skinned lead character (CesiumMan walk clip)
// in front of static low-poly mannequins, on a shadow-receiving floor with a
// cinematic orbit/dolly camera. Frames go through GLTFLoader ->
// MotionSceneBridge -> GLRenderer -> GL PBO -> CUDA -> NVENC.
//
// The crowd is procedurally built from threepp primitives (icosahedron heads,
// capsule limbs) so the scene stays asset-free beyond the pinned CesiumMan.glb.
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
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/CylinderGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {
constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;
constexpr const char* kId = "SCENE01_LOWPOLY_CROWD";
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

struct ShadowDiff {
    size_t changed = 0;
    size_t lowerHalf = 0;
};

ShadowDiff shadowDiff(const std::vector<unsigned char>& on, const std::vector<unsigned char>& off) {
    ShadowDiff d;
    const size_t floorRows = size_t(H * 0.6);
    for (size_t p = 0; p < size_t(W) * H; ++p) {
        const size_t i = p * 3;
        if (!differs(on, i, off)) continue;
        ++d.changed;
        if (p / W < floorRows) ++d.lowerHalf;
    }
    return d;
}

std::vector<float> boneMatrices(const std::shared_ptr<Object3D>& root) {
    std::vector<float> bones;
    root->traverse([&](Object3D& o) {
        if (auto* sk = dynamic_cast<SkinnedMesh*>(&o); sk && sk->skeleton)
            bones.insert(bones.end(), sk->skeleton->boneMatrices.begin(), sk->skeleton->boneMatrices.end());
    });
    return bones;
}

float vectorDelta(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<float>::infinity();
    float d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
    return d;
}

// One static low-poly mannequin, feet at y=0, total height ~1.8 world units.
std::shared_ptr<Group> makeMannequin(unsigned int seedColor, float yaw) {
    auto mat = MeshStandardMaterial::create();
    mat->color.setHex(seedColor);
    mat->roughness = 0.95f;
    mat->metalness = 0.f;

    auto darker = MeshStandardMaterial::create();
    darker->color.setHex(seedColor).multiplyScalar(0.7f);
    darker->roughness = 0.95f;
    darker->metalness = 0.f;

    auto g = Group::create();
    g->rotation.y = yaw;

    auto add = [&](std::shared_ptr<BufferGeometry> geo, float x, float y, float z,
                   std::shared_ptr<MeshStandardMaterial> material) {
        auto mesh = Mesh::create(std::move(geo), material);
        mesh->position.set(x, y, z);
        mesh->castShadow = true;
        mesh->receiveShadow = true;
        g->add(mesh);
    };

    // Head: faceted low-poly sphere reads sharp at any orbit angle.
    add(SphereGeometry::create(0.14f, 8, 6), 0.f, 1.64f, 0.f, mat);
    add(CylinderGeometry::create(CylinderGeometry::Params{0.05f, 0.055f, 0.1f, 6}), 0.f, 1.49f, 0.f, darker);
    add(BoxGeometry::create(0.42f, 0.52f, 0.2f), 0.f, 1.18f, 0.f, mat);
    add(BoxGeometry::create(0.34f, 0.2f, 0.18f), 0.f, 0.83f, 0.f, darker);
    // Arms hang slightly out of the torso for readable silhouettes.
    for (int side = -1; side <= 1; side += 2) {
        auto arm = Mesh::create(CylinderGeometry::create(CylinderGeometry::Params{0.045f, 0.05f, 0.5f, 6}), mat);
        arm->position.set(float(side) * 0.28f, 1.22f, 0.f);
        arm->rotation.z = float(side) * -0.12f;
        arm->castShadow = true;
        arm->receiveShadow = true;
        g->add(arm);
        auto leg = Mesh::create(CylinderGeometry::create(CylinderGeometry::Params{0.065f, 0.075f, 0.62f, 6}), darker);
        leg->position.set(float(side) * 0.12f, 0.45f, 0.f);
        leg->castShadow = true;
        leg->receiveShadow = true;
        g->add(leg);
    }
    return g;
}

int run(const fs::path& asset, const fs::path& out) {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)asset;
    (void)out;
    throw std::runtime_error("SCENE-01 requires the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    fs::create_directories(out);
    GLTFLoader loader;
    auto result = loader.load(asset);
    if (!result || !result->scene) throw std::runtime_error("GLTFLoader failed: " + asset.string());
    if (result->animations.empty()) throw std::runtime_error("GLB contains no animation clips");
    auto clip = result->animations.front();

    // Lead actor: skinned handler of the imported subtree.
    chrononmotion::motion::MotionScene motion(kId, float(FPS));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.assetId = "humanoid_asset";
    auto& lead = motion.addMesh(1, "lead", std::move(attachment));
    lead.castsShadow = true;
    lead.acceptsShadow = true;

    auto& fill = motion.addAmbientLight(100, "fill");
    fill.color = chrononmotion::Color(0xffffff);
    fill.intensity = 0.55f;

    // Warm key, same tuning as the humanoid shadow certification.
    auto& key = motion.addDirectionalLight(101, "key");
    key.color = chrononmotion::Color(0xffedcf);
    key.intensity = 2.2f;
    key.castsShadow = true;
    key.shadowBias = -0.00015f;
    key.shadowNormalBias = 0.025f;
    key.shadowRadius = 3.f;
    key.positionTrack.add(0.f, {2.5f, 8.f, 3.f});
    key.directionTrack.add(0.f, {-2.5f, -8.f, -3.f});

    // Cool rim from behind for separation against the darker set.
    auto& rim = motion.addDirectionalLight(102, "rim");
    rim.color = chrononmotion::Color(0x9ecbff);
    rim.intensity = 0.8f;
    rim.positionTrack.add(0.f, {1.5f, 3.f, -4.f});
    rim.directionTrack.add(0.f, {-1.5f, -3.f, 4.f});

    threepp::motion::MotionSceneBridge bridge(40.f, float(W) / H, 0.05f, 1000.f);
    bridge.bindAsset("humanoid_asset", result->scene);
    bridge.evaluateAndSync(motion, 0);
    bridge.bindLayerAnimation(1, clip);
    auto leadNode = bridge.layerNode(1);
    if (!leadNode) throw std::runtime_error("Motion bridge did not materialize the lead subtree");

    Box3 leadBox;
    leadBox.setFromObject(*leadNode, true);
    Vector3 leadCenter, leadSize;
    leadBox.getCenter(leadCenter);
    leadBox.getSize(leadSize);
    const float leadSpan = std::max({leadSize.x, leadSize.y, leadSize.z, 0.25f});
    // Crate mannequins to the lead height so the crowd reads as cast, not set dressing.
    const float mannequinScale = leadSize.y / 1.78f;
    const float floorY = leadCenter.y - leadSize.y * 0.5f;

    // Crowd: lead front and centre, mannequins in two staggered rows behind.
    std::vector<std::pair<Vector3, float>> spots{
        {{-1.4f, 0.f, -0.9f}, 0.15f},  {{0.f, 0.f, -1.35f}, -0.1f},
        {{1.4f, 0.f, -0.9f}, -0.15f},  {{-0.75f, 0.f, -2.3f}, 0.35f},
        {{0.75f, 0.f, -2.3f}, -0.35f},
    };
    const std::array<unsigned int, 5> palette{0x8d99ae, 0xa68a64, 0x6b7f6f, 0x9c6b62, 0x7d7f85};
    int mannequinMeshes = 0;
    for (size_t i = 0; i < spots.size(); ++i) {
        auto mannequin = makeMannequin(palette[i], spots[i].second);
        mannequin->scale.setScalar(mannequinScale);
        mannequin->position.set(
            spots[i].first.x, floorY, spots[i].first.z);
        bridge.scene()->add(mannequin);
        mannequin->traverse([&](Object3D& o) {
            if (dynamic_cast<Mesh*>(&o)) ++mannequinMeshes;
        });
    }

    // Shadow-receiving floor just under the lead's lowest point.
    auto floorMaterial = MeshStandardMaterial::create();
    floorMaterial->color.setHex(0x82858c);
    floorMaterial->roughness = 0.9f;
    floorMaterial->metalness = 0.f;
    auto floor = Mesh::create(BoxGeometry::create(60.f, 0.02f, 60.f), floorMaterial);
    floor->position.set(0.f, floorY - 0.01f, 0.f);
    floor->castShadow = false;
    floor->receiveShadow = true;
    bridge.scene()->add(floor);

    // Cinematic orbit: sweep ~100 degrees around the crowd while dollying in
    // and easing the height down, then lift off. Target rides the lead chest.
    chrononmotion::motion::CameraRig camera(700, 44.f, float(W) / H, 0.05f, 1000.f);
    const float radius0 = 6.4f, radius1 = 3.9f;
    const float azimuth0 = 0.28f, azimuth1 = 2.1f;   // radians, CCW sweep
    const float height0 = 2.f, height1 = 1.05f;
    const Vector3 pivot{0.f, floorY + leadSize.y * 0.55f, -0.5f};
    for (int k = 0; k <= 4; ++k) {
        const float t = float(k) * 1.25f;
        const float s = float(k) / 4.f;
        // Smoothstep so the orbit eases in and out instead of constant spin.
        const float e = s * s * (3.f - 2.f * s);
        const float azimuth = azimuth0 + (azimuth1 - azimuth0) * e;
        const float radius = radius0 + (radius1 - radius0) * e;
        const float height = height0 + (height1 - height0) * e;
        camera.positionTrack().add(t, {pivot.x + radius * std::sin(azimuth), height, pivot.z + radius * std::cos(azimuth)});
        camera.targetTrack().add(t, {pivot.x, pivot.y + (k % 2 == 0 ? 0.f : -0.08f), pivot.z});
    }

    std::shared_ptr<DirectionalLight> shadowLight;
    bridge.scene()->traverse([&](Object3D& object) {
        if (auto* directional = dynamic_cast<DirectionalLight*>(&object))
            if (!shadowLight || directional->castShadow)
                shadowLight = std::shared_ptr<DirectionalLight>(directional, [](DirectionalLight*) {});
    });
    if (!shadowLight || !shadowLight->castShadow || !shadowLight->shadow)
        throw std::runtime_error("MotionSceneBridge did not create a shadow-casting directional light");
    shadowLight->shadow->mapSize.set(2048.f, 2048.f);

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = rendererName ? rendererName : "unknown";
    chrononmotion::certification::require_nvidia_renderer(glRenderer);
    renderer.setClearColor(Color(0x151922));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.f;
    renderer.shadowMap().enabled = true;
    renderer.shadowMap().type = ShadowMap::PFCSoft;

    const fs::path videoPath = out / "scene01_lowpoly_crowd_1080p30.mp4";
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder gpuEncoder(W, H, FPS, videoPath.string());
    const auto begin = std::chrono::steady_clock::now();
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &camera);
        renderer.render(*bridge.scene(), *bridge.camera());
        gpuEncoder.encode_current_target(f);
    }
    gpuEncoder.finish();
    if (gpuEncoder.frames_submitted() != FRAMES || gpuEncoder.packets_written() != FRAMES)
        throw std::runtime_error("SCENE-01 GPU encoder did not submit and mux all frames");

    // Diagnostics: probes at 0/75/149, random access at 77, bones moving.
    const std::array<int, 3> probes{0, 75, 149};
    std::array<std::vector<unsigned char>, 3> probeOn;
    std::vector<unsigned char> frame77, frame0Pixels;
    std::vector<float> bones0;
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &camera);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("invalid diagnostic pixel readback");
        if (f == 0) {
            bones0 = boneMatrices(leadNode);
            frame0Pixels = pixels;
        }
        if (f == 77) frame77 = pixels;
        for (size_t i = 0; i < probes.size(); ++i)
            if (f == probes[i]) {
                probeOn[i] = pixels;
                writePng(out / ("frame-" + std::to_string(f) + ".png"), pixels);
            }
    }
    const auto renderDone = std::chrono::steady_clock::now();

    std::array<ShadowDiff, 3> diffs;
    bool toggleDeterministic = true;
    for (size_t i = 0; i < probes.size(); ++i) {
        bridge.evaluateAndSync(motion, probes[i], &camera);
        renderer.shadowMap().enabled = false;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto off = renderer.readRGBPixels();
        renderer.shadowMap().enabled = true;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto restored = renderer.readRGBPixels();
        if (restored != probeOn[i]) toggleDeterministic = false;
        diffs[i] = shadowDiff(probeOn[i], off);
    }

    bridge.evaluateAndSync(motion, 77, &camera);
    renderer.render(*bridge.scene(), *bridge.camera());
    const bool randomOk = renderer.readRGBPixels() == frame77;

    const float boneDelta = vectorDelta(bones0, boneMatrices(leadNode));
    // Framing: the lead must stay visible between probe 0 and 149 while the
    // camera crosses behind the crowd. Use the non-background pixel count.
    int subjectMinY = H, subjectMaxY = -1;
    size_t subjectCoverage = 0;
    const std::array<unsigned char, 3> background{0x15, 0x19, 0x22};
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t(y) * W + size_t(x)) * 3;
            const int delta = std::abs(int(frame0Pixels[i]) - int(background[0])) +
                              std::abs(int(frame0Pixels[i + 1]) - int(background[1])) +
                              std::abs(int(frame0Pixels[i + 2]) - int(background[2]));
            if (delta <= 6) continue;
            ++subjectCoverage;
            subjectMinY = std::min(subjectMinY, y);
            subjectMaxY = std::max(subjectMaxY, y);
        }
    const float subjectHeightRatio = subjectMaxY >= subjectMinY ? float(subjectMaxY - subjectMinY + 1) / float(H) : 0.f;

    float cameraTravel = 0.f;
    for (int f = 1; f < FRAMES; ++f)
        cameraTravel += (camera.sample(float(f) / FPS).position - camera.sample(float(f - 1) / FPS).position).length();

    const bool shadowsPresent = std::all_of(diffs.begin(), diffs.end(), [](const ShadowDiff& d) { return d.changed > 2000; });
    const bool shadowsOnFloor = std::all_of(diffs.begin(), diffs.end(), [](const ShadowDiff& d) {
        return d.changed > 0 && double(d.lowerHalf) / double(d.changed) >= 0.5;
    });

    std::cout << kId << " metrics: mannequin_meshes=" << mannequinMeshes << " subject_coverage=" << subjectCoverage
              << " subject_height_ratio=" << subjectHeightRatio << " bone_delta=" << boneDelta
              << " camera_travel=" << cameraTravel << " random=" << randomOk
              << " toggle_deterministic=" << toggleDeterministic << std::endl;
    for (size_t i = 0; i < probes.size(); ++i)
        std::cout << kId << " frame" << probes[i] << " shadow_changed=" << diffs[i].changed
                  << " on_floor_rows=" << diffs[i].lowerHalf << std::endl;

    if (!randomOk || !toggleDeterministic || mannequinMeshes < 20 || boneDelta < 1e-4f || subjectHeightRatio < 0.20f ||
        cameraTravel < 3.f || !shadowsPresent || !shadowsOnFloor) {
        throw std::runtime_error(std::string(kId) + " gate failed: crowd, bones, orbit, shadows or random access");
    }

    std::ofstream scene(out / "scene.json");
    scene << "{\n  \"scene\": \"scene01_lowpoly_crowd\",\n"
             "  \"lead_asset\": \"" << asset.filename().string() << "\",\n  \"lead_clip\": \"" << clip->name()
          << "\",\n  \"mannequins\": 5,\n  \"mannequin_style\": \"procedural low-poly primitives\",\n"
             "  \"camera\": \"orbit + dolly-in 100deg, CameraRig tracks\",\n"
             "  \"shadow_light\": \"key directional (castsShadow=true, mapSize 2048)\",\n"
             "  \"output\": \"1920x1080 30fps 150 frames\"\n}\n";

    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream meta(out / "metadata.json");
    meta << std::fixed << std::setprecision(5) << "{\n  \"test_id\": \"" << kId
         << "\",\n  \"pipeline\": \"GLTFLoader animation + procedural crowd -> MotionSceneBridge (CameraRig orbit) -> threepp GLRenderer -> GL PBO -> CUDA -> NVENC\",\n"
            "  \"lead_asset\": \"" << asset.filename().string()
         << "\",\n  \"lead_asset_license\": \"CC BY 4.0 (Cesium, Khronos glTF-Sample-Assets)\",\n"
            "  \"lead_clip_name\": \"" << clip->name()
         << "\",\n  \"mannequin_mesh_count\": " << mannequinMeshes
         << ",\n  \"bone_matrix_delta_frame_0_to_last\": " << boneDelta
         << ",\n  \"camera_travel_world_units\": " << cameraTravel
         << ",\n  \"subject_coverage_frame_0\": " << subjectCoverage
         << ",\n  \"subject_height_ratio_frame_0\": " << subjectHeightRatio
         << ",\n  \"shadow_changed_pixels\": [" << diffs[0].changed << ", " << diffs[1].changed << ", " << diffs[2].changed
         << "],\n  \"shadow_changed_pixels_on_floor_rows\": [" << diffs[0].lowerHalf << ", " << diffs[1].lowerHalf
         << ", " << diffs[2].lowerHalf << "],\n  \"shadow_toggle_deterministic\": " << (toggleDeterministic ? "true" : "false")
         << ",\n  \"random_access_frame_77_identical\": " << (randomOk ? "true" : "false")
         << ",\n  \"render_seconds\": " << std::chrono::duration<double>(renderDone - begin).count()
         << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss
         << ",\n  \"gl_renderer\": \"" << glRenderer
         << "\",\n  \"gl_version\": \"" << (version ? version : "unknown")
         << "\",\n  \"video_encoder\": \"h264_nvenc\",\n  \"gpu_device\": \"" << gpuEncoder.device_name()
         << "\",\n  \"frames_submitted\": " << gpuEncoder.frames_submitted()
         << ",\n  \"packets_muxed\": " << gpuEncoder.packets_written() << "\n}\n";

    renderer.dispose();
    std::cout << kId << " OK crowd=" << mannequinMeshes << " travel=" << cameraTravel
              << " random_access=" << randomOk << "\n";
    return 0;
#endif
}
} // namespace

int main(int argc, char** argv) try {
    chrononmotion::certification::prepare_nvidia_egl_headless();
    if (argc < 2) throw std::invalid_argument("usage: scene01_lowpoly_crowd output_dir [asset.glb]");
    const fs::path out = argv[1];
    const fs::path asset = argc > 2 ? fs::path(argv[2]) : fs::path("render/spike/assets/threepp-certification/CesiumMan.glb");
    return run(asset, out);
} catch (const std::exception& e) {
    std::cerr << "SCENE-01 FAIL: " << e.what() << '\n';
    return 1;
}
