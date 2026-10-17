// T07-HUMANOID-SHADOW: skinned GLB humanoid with an embedded clip, a
// shadow-receiving floor and a shadow-casting key light. Frames are produced
// through GLTFLoader -> MotionSceneBridge -> GLRenderer -> GL PBO -> CUDA ->
// NVENC. Diagnostics compare shadow-on and shadow-off renders of the same
// Chronon frame and check deterministic random access.
#include "gpu_certification_gate.hpp"
#include <algorithm>
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
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {
constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;
constexpr const char* kId = "T07_HUMANOID_SHADOW";
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
    size_t changed = 0;      // pixels whose colour differs with shadows on vs off
    size_t lowerHalf = 0;    // changed pixels in the bottom 60% of the frame (floor band)
    std::pair<int, int> yRange{-1, -1};
};

// GL readback rows are bottom-up, so low row indices are the bottom of the frame.
ShadowDiff shadowDiff(const std::vector<unsigned char>& on, const std::vector<unsigned char>& off) {
    ShadowDiff d;
    const size_t floorRows = size_t(H * 0.6);
    for (size_t p = 0; p < size_t(W) * H; ++p) {
        const size_t i = p * 3;
        if (!differs(on, i, off)) continue;
        ++d.changed;
        const size_t row = p / W;
        if (row < floorRows) ++d.lowerHalf;
    }
    return d;
}

struct PoseSignals {
    std::vector<float> bones;
};

PoseSignals poseSignals(const std::shared_ptr<Object3D>& root) {
    PoseSignals s;
    root->traverse([&](Object3D& o) {
        if (auto* sk = dynamic_cast<SkinnedMesh*>(&o); sk && sk->skeleton)
            s.bones.insert(s.bones.end(), sk->skeleton->boneMatrices.begin(), sk->skeleton->boneMatrices.end());
    });
    return s;
}

float vectorDelta(const std::vector<float>& a, const std::vector<float>& b) {
    if (a.size() != b.size()) return std::numeric_limits<float>::infinity();
    float d = 0;
    for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
    return d;
}

int run(const fs::path& asset, const fs::path& out, const std::string& requestedClip) {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)asset;
    (void)out;
    (void)requestedClip;
    throw std::runtime_error("T07-HUMANOID-SHADOW requires the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    fs::create_directories(out);
    GLTFLoader loader;
    auto result = loader.load(asset);
    if (!result || !result->scene) throw std::runtime_error("GLTFLoader failed: " + asset.string());
    if (result->animations.empty()) throw std::runtime_error("GLB contains no animation clips");

    int skinMeshes = 0, totalMeshes = 0, texturedMeshes = 0;
    result->scene->traverse([&](Object3D& o) {
        if (auto* m = dynamic_cast<Mesh*>(&o)) {
            ++totalMeshes;
            if (dynamic_cast<SkinnedMesh*>(m)) ++skinMeshes;
            if (auto material = m->materialAs<MeshStandardMaterial>(); material && material->map) ++texturedMeshes;
        }
    });
    if (skinMeshes == 0) throw std::runtime_error("humanoid fixture has no skinned mesh");

    size_t clipIndex = 0;
    if (!requestedClip.empty()) {
        const auto it = std::find_if(result->animations.begin(), result->animations.end(),
                                     [&](const auto& candidate) { return candidate && candidate->name() == requestedClip; });
        if (it == result->animations.end()) throw std::runtime_error("GLB has no animation clip named " + requestedClip);
        clipIndex = std::size_t(std::distance(result->animations.begin(), it));
    }
    auto clip = result->animations.at(clipIndex);

    chrononmotion::motion::MotionScene motion(kId, float(FPS));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.assetId = "humanoid_asset";
    auto& humanoidLayer = motion.addMesh(1, "humanoid", std::move(attachment));
    humanoidLayer.castsShadow = true;
    humanoidLayer.acceptsShadow = true;

    auto& fill = motion.addAmbientLight(100, "fill");
    fill.color = chrononmotion::Color(0xffffff);
    fill.intensity = 0.35f;

    auto& key = motion.addDirectionalLight(101, "key");
    key.color = chrononmotion::Color(0xffedcf);
    key.intensity = 2.8f;
    key.castsShadow = true;
    key.shadowBias = -0.00015f;
    key.shadowNormalBias = 0.025f;
    key.shadowRadius = 3.f;
    key.positionTrack.add(0.f, {-3.5f, 7.f, 3.4f});
    key.directionTrack.add(0.f, {3.5f, -7.f, -3.4f});

    threepp::motion::MotionSceneBridge bridge(40.f, float(W) / H, 0.05f, 1000.f);
    bridge.bindAsset("humanoid_asset", result->scene);
    bridge.evaluateAndSync(motion, 0);
    bridge.bindLayerAnimation(1, clip);
    auto node = bridge.layerNode(1);
    if (!node) throw std::runtime_error("Motion bridge did not materialize the humanoid subtree");

    // The bridge must carry the layer's shadow flags onto the imported meshes.
    int shadowCasters = 0;
    node->traverse([&](Object3D& o) {
        if (dynamic_cast<Mesh*>(&o) && o.castShadow) ++shadowCasters;
    });

    Box3 box;
    box.setFromObject(*node, true);
    Vector3 center, size;
    box.getCenter(center);
    box.getSize(size);
    const float span = std::max({size.x, size.y, size.z, 0.25f});

    // Shadow receiver: a floor placed just under the character's lowest point.
    auto floorMaterial = MeshStandardMaterial::create();
    floorMaterial->color.setHex(0xb7b8bd);
    floorMaterial->roughness = 0.9f;
    floorMaterial->metalness = 0.f;
    auto floor = Mesh::create(BoxGeometry::create(span * 60.f, 0.02f, span * 60.f), floorMaterial);
    floor->position.set(center.x, center.y - size.y * 0.5f - 0.01f, center.z);
    floor->castShadow = false;
    floor->receiveShadow = true;
    bridge.scene()->add(floor);

    bridge.camera()->position.set(center.x + span * 0.75f, center.y + span * 0.25f, center.z + span * 2.4f);
    bridge.camera()->lookAt(center);

    std::shared_ptr<DirectionalLight> shadowLight;
    bridge.scene()->traverse([&](Object3D& object) {
        if (auto* directional = dynamic_cast<DirectionalLight*>(&object))
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

    const fs::path videoPath = out / "T07_humanoid_shadow_1080p30.mp4";
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder gpuEncoder(W, H, FPS, videoPath.string());
    const auto begin = std::chrono::steady_clock::now();
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f);
        renderer.render(*bridge.scene(), *bridge.camera());
        gpuEncoder.encode_current_target(f);
    }
    gpuEncoder.finish();
    if (gpuEncoder.frames_submitted() != FRAMES || gpuEncoder.packets_written() != FRAMES)
        throw std::runtime_error("T07-HUMANOID-SHADOW GPU encoder did not submit and mux all frames");

    const std::array<int, 3> probes{0, 75, 149};
    std::array<std::vector<unsigned char>, 3> probeOn;
    std::vector<unsigned char> frame77, mid;
    PoseSignals pose0, poseMid;
    // Clear colour set on the renderer (0x151922); the floor covers most of
    // the frame, so subject coverage is measured on the floor-hidden render.
    const std::array<unsigned char, 3> background{0x15, 0x19, 0x22};
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("invalid diagnostic pixel readback");
        if (f == 0) pose0 = poseSignals(node);
        if (f == 75) {
            poseMid = poseSignals(node);
            mid = pixels;
        }
        if (f == 77) frame77 = pixels;
        for (size_t i = 0; i < probes.size(); ++i) {
            if (f == probes[i]) {
                probeOn[i] = pixels;
                writePng(out / ("frame-" + std::to_string(f) + ".png"), pixels);
            }
        }
    }
    const auto renderDone = std::chrono::steady_clock::now();

    // Shadow-off renders of the same Chronon frames; the on-render must be
    // reproduced exactly after the toggle (deterministic shadow pass).
    std::array<ShadowDiff, 3> diffs;
    bool toggleDeterministic = true;
    for (size_t i = 0; i < probes.size(); ++i) {
        bridge.evaluateAndSync(motion, probes[i]);
        renderer.shadowMap().enabled = false;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto off = renderer.readRGBPixels();
        renderer.shadowMap().enabled = true;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto restored = renderer.readRGBPixels();
        if (restored != probeOn[i]) toggleDeterministic = false;
        diffs[i] = shadowDiff(probeOn[i], off);
        if (i == 1) writePng(out / "frame-75-no-shadows.png", off);
    }

    bridge.evaluateAndSync(motion, 77);
    renderer.render(*bridge.scene(), *bridge.camera());
    const bool randomOk = renderer.readRGBPixels() == frame77;

    // Subject-only silhouette for framing: hide the floor and shadows so the
    // floor cannot be mistaken for the character.
    floor->visible = false;
    renderer.shadowMap().enabled = false;
    bridge.evaluateAndSync(motion, 75);
    renderer.render(*bridge.scene(), *bridge.camera());
    const auto subjectOnly = renderer.readRGBPixels();
    floor->visible = true;
    renderer.shadowMap().enabled = true;
    mid = subjectOnly;

    const float boneDelta = vectorDelta(pose0.bones, poseMid.bones);
    int subjectMinX = W, subjectMaxX = -1, subjectMinY = H, subjectMaxY = -1;
    size_t subjectCoverage = 0;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t(y) * W + size_t(x)) * 3;
            const int delta = std::abs(int(mid[i]) - int(background[0])) + std::abs(int(mid[i + 1]) - int(background[1])) +
                              std::abs(int(mid[i + 2]) - int(background[2]));
            if (delta <= 6) continue;
            ++subjectCoverage;
            subjectMinX = std::min(subjectMinX, x);
            subjectMaxX = std::max(subjectMaxX, x);
            subjectMinY = std::min(subjectMinY, y);
            subjectMaxY = std::max(subjectMaxY, y);
        }
    }
    const float subjectHeightRatio = subjectMaxY >= subjectMinY ? float(subjectMaxY - subjectMinY + 1) / float(H) : 0.f;

    const bool shadowsPresent = std::all_of(diffs.begin(), diffs.end(), [](const ShadowDiff& d) { return d.changed > 2000; });
    const bool shadowsOnFloor = std::all_of(diffs.begin(), diffs.end(), [](const ShadowDiff& d) {
        return d.changed > 0 && double(d.lowerHalf) / double(d.changed) >= 0.5;
    });

    std::cout << kId << " metrics: clip=" << clip->name() << " meshes=" << totalMeshes << " skinned=" << skinMeshes
              << " textured=" << texturedMeshes << " shadow_casters=" << shadowCasters << " subject_coverage=" << subjectCoverage << " bone_delta=" << boneDelta << " random=" << randomOk
              << " toggle_deterministic=" << toggleDeterministic << std::endl;
    for (size_t i = 0; i < probes.size(); ++i) {
        std::cout << kId << " frame" << probes[i] << " shadow_changed=" << diffs[i].changed
                  << " on_floor_rows=" << diffs[i].lowerHalf << std::endl;
    }
    std::cout << kId << " frame75 subject bounds: x=" << subjectMinX << ".." << subjectMaxX << " y=" << subjectMinY
              << ".." << subjectMaxY << " subject_height_ratio=" << subjectHeightRatio << std::endl;

    if (!randomOk || !toggleDeterministic || shadowCasters == 0 || texturedMeshes == 0 ||         subjectCoverage < size_t(W) * H / 100 || boneDelta < 1e-4f || subjectHeightRatio < 0.20f || !shadowsPresent ||
        !shadowsOnFloor) {
        throw std::runtime_error(std::string(kId) +
                                 " gate failed: skinning, textures, shadow cast/receive, framing or random access");
    }

    std::ofstream scene(out / "scene.json");
    scene << "{\n  \"asset\": \"" << asset.filename().string() << "\",\n  \"clip\": \"" << clip->name()
          << "\",\n  \"clip_duration_seconds\": " << clip->getDuration()
          << ",\n  \"frame_source\": \"Chronon MotionScene frame / fps\",\n  \"shadow_caster_layer\": \"humanoid\",\n"
             "  \"shadow_receiver\": \"floor (threepp Mesh, receiveShadow=true)\",\n  \"shadow_light\": \"key directional (castsShadow=true, mapSize 2048)\",\n"
             "  \"output\": \"1920x1080 30fps 150 frames\"\n}\n";

    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream meta(out / "metadata.json");
    meta << std::fixed << std::setprecision(5) << "{\n  \"test_id\": \"" << kId
         << "\",\n  \"status\": \"PARTIAL\",\n  \"technical_checks_passed\": true,\n"
            "  \"pipeline\": \"GLTFLoader animation -> MotionSceneBridge absolute Chronon sample -> threepp GLRenderer (shadow map) -> GL PBO -> CUDA -> NVENC\",\n"
            "  \"asset\": \"" << asset.filename().string() << "\",\n  \"asset_license\": \"CC BY 4.0 (Cesium, Khronos glTF-Sample-Assets)\",\n"
            "  \"clip_name\": \"" << clip->name() << "\",\n  \"mesh_count\": " << totalMeshes
         << ",\n  \"skinned_mesh_count\": " << skinMeshes << ",\n  \"textured_mesh_count\": " << texturedMeshes
         << ",\n  \"shadow_caster_count\": " << shadowCasters << ",\n  \"bone_matrix_delta_frame_0_to_75\": " << boneDelta
         << ",\n  \"shadow_changed_pixels\": [" << diffs[0].changed << ", " << diffs[1].changed << ", " << diffs[2].changed
         << "],\n  \"shadow_changed_pixels_on_floor_rows\": [" << diffs[0].lowerHalf << ", " << diffs[1].lowerHalf << ", "
         << diffs[2].lowerHalf << "],\n  \"shadow_toggle_deterministic\": " << (toggleDeterministic ? "true" : "false")
         << ",\n  \"subject_coverage_pixels_frame_75\": " << subjectCoverage
         << ",\n  \"subject_bbox_frame_75\": [" << subjectMinX << ", " << subjectMinY << ", " << subjectMaxX << ", "
         << subjectMaxY << "],\n  \"subject_height_ratio_frame_75\": " << subjectHeightRatio
         << ",\n  \"random_access_frame_77_identical\": " << (randomOk ? "true" : "false")
         << ",\n  \"render_seconds\": " << std::chrono::duration<double>(renderDone - begin).count()
         << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss << ",\n  \"gl_renderer\": \"" << glRenderer
         << "\",\n  \"gl_version\": \"" << (version ? version : "unknown") << "\",\n  \"video_encoder\": \"h264_nvenc\",\n"
            "  \"gpu_device\": \"" << gpuEncoder.device_name() << "\",\n  \"frames_submitted\": " << gpuEncoder.frames_submitted()
         << ",\n  \"packets_muxed\": " << gpuEncoder.packets_written() << ",\n  \"visual_judgement\": \"pending (not assessed by test)\"\n}\n";

    renderer.dispose();
    std::cout << kId << " PARTIAL (technical gates passed; visual judgement pending) clip=" << clip->name()
              << " skinned=" << skinMeshes << " shadow_casters=" << shadowCasters << " bone_delta=" << boneDelta
              << " random_access=" << randomOk << "\n";
    return 0;
#endif
}
} // namespace

int main(int argc, char** argv) try {
    chrononmotion::certification::prepare_nvidia_egl_headless();
    if (argc < 2)
        throw std::invalid_argument("usage: humanoid_shadow_certification output_dir [asset.glb] [clip_name]");
    const fs::path out = argv[1];
    const fs::path asset = argc > 2 ? fs::path(argv[2]) : fs::path("render/spike/assets/threepp-certification/CesiumMan.glb");
    const std::string clip = argc > 3 ? argv[3] : "";
    return run(asset, out, clip);
} catch (const std::exception& e) {
    std::cerr << "Humanoid shadow certification FAIL: " << e.what() << '\n';
    return 1;
}
