// T07/T08: bind a GLTFLoader clip to Chronon time through MotionSceneBridge.
#include "gpu_certification_gate.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif
#include "stb_image_write.h"

#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"

using namespace threepp;
namespace fs = std::filesystem;
namespace {
    constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;
    void png(const fs::path& p, const std::vector<unsigned char>& rgb) {
        std::vector<unsigned char> top(rgb.size());
        size_t row = size_t(W) * 3;
        for (int y = 0; y < H; ++y) std::copy_n(rgb.data() + size_t(H - y - 1) * row, row, top.data() + size_t(y) * row);
        if (!stbi_write_png(p.c_str(), W, H, 3, top.data(), int(row))) throw std::runtime_error("PNG write failed");
    }
    std::string quote(const std::string& x) {
        std::string q = "'";
        for (char c : x) {
            if (c == '\'') q += "'\\''";
            else
                q += c;
        }
        return q + "'";
    }
    size_t changed(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        size_t n = 0;
        if (a.size() != b.size()) return std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < a.size(); i += 3)
            if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) + std::abs(int(a[i + 2]) - int(b[i + 2])) > 3) ++n;
        return n;
    }
    struct PoseSignals {
        std::vector<float> bones;
        std::vector<float> morphs;
    };
    PoseSignals signals(const std::shared_ptr<Object3D>& root) {
        PoseSignals s;
        root->traverse([&](Object3D& o) {if(auto* sk=dynamic_cast<SkinnedMesh*>(&o);sk&&sk->skeleton)s.bones.insert(s.bones.end(),sk->skeleton->boneMatrices.begin(),sk->skeleton->boneMatrices.end());if(auto* mesh=dynamic_cast<Mesh*>(&o)){const auto& m=mesh->morphTargetInfluences();s.morphs.insert(s.morphs.end(),m.begin(),m.end());} });
        return s;
    }
    float vectorDelta(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.size() != b.size()) return std::numeric_limits<float>::infinity();
        float d = 0;
        for (size_t i = 0; i < a.size(); ++i) d += std::abs(a[i] - b[i]);
        return d;
    }
    int run(const std::string& id, const fs::path& asset, const fs::path& out,
            const std::string& requestedClip) {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
        (void)id;
        (void)asset;
        (void)out;
        (void)requestedClip;
        throw std::runtime_error("T07/T08 require the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
        fs::create_directories(out);
        GLTFLoader loader;
        auto result = loader.load(asset);
        if (!result || !result->scene) throw std::runtime_error("GLTFLoader failed: " + asset.string());
        if (result->animations.empty()) throw std::runtime_error("GLB contains no animation clips");
        int skinMeshes = 0, morphMeshes = 0, totalMeshes = 0;
        result->scene->traverse([&](Object3D& o) {if(auto* m=dynamic_cast<Mesh*>(&o)){++totalMeshes;if(dynamic_cast<SkinnedMesh*>(m))++skinMeshes;if(!m->morphTargetInfluences().empty())++morphMeshes;} });
        const bool t07 = id == "T07";
        if (t07 && (skinMeshes == 0 || result->animations.empty())) throw std::runtime_error("T07 fixture has no skinned mesh/clip");
        if (!t07 && (morphMeshes == 0 || result->animations.empty())) throw std::runtime_error("T08 fixture has no morph targets/clip");
        if (!t07) {
            // Give the morph certification fixture visible lighting and a
            // three-quarter silhouette; the imported geometry and morph data
            // remain untouched, only the test material tint changes.
            result->scene->traverse([](Object3D& object) {
                if (auto* mesh = dynamic_cast<Mesh*>(&object)) {
                    if (auto material = mesh->materialAs<MeshStandardMaterial>()) {
                        material->color.setHex(0xe87936);
                        material->roughness = 0.62f;
                        material->metalness = 0.02f;
                    }
                }
            });
        }
        size_t clipIndex = 0;
        if (!requestedClip.empty()) {
            const auto it = std::find_if(result->animations.begin(), result->animations.end(),
                    [&](const auto& candidate) { return candidate && candidate->name() == requestedClip; });
            if (it == result->animations.end())
                throw std::runtime_error("GLB has no animation clip named " + requestedClip);
            clipIndex = std::size_t(std::distance(result->animations.begin(), it));
        }
        auto clip = result->animations.at(clipIndex);
        chrononmotion::motion::MotionScene motion(id + "_gltf_animation", float(FPS));
        chrononmotion::motion::MeshAttachment attachment;
        attachment.assetId = "animated_asset";
        motion.addMesh(1, "animated_model", std::move(attachment));
        threepp::motion::MotionSceneBridge bridge(40.f, float(W) / H, 0.05f, 1000.f);
        bridge.bindAsset("animated_asset", result->scene);
        bridge.evaluateAndSync(motion, 0);
        bridge.bindLayerAnimation(1, clip);
        auto node = bridge.layerNode(1);
        if (!node) throw std::runtime_error("Motion bridge did not materialize the GLB subtree");
        Box3 box;
        box.setFromObject(*node, true);
        Vector3 center, size;
        box.getCenter(center);
        box.getSize(size);
        float span = std::max({size.x, size.y, size.z, 0.25f});
        const float cameraDistance = span * (t07 ? 1.15f : 2.15f);
        bridge.camera()->position.set(center.x + span * (t07 ? 0.75f : 0.72f),
                                      center.y + span * (t07 ? 0.12f : 0.48f),
                                      center.z + cameraDistance);
        const Vector3 cameraPosition = bridge.camera()->position;
        bridge.camera()->lookAt(center);
        bridge.scene()->add(AmbientLight::create(Color(0xffffff), t07 ? 1.1f : 0.38f));
        auto key = DirectionalLight::create(Color(0xffffff), t07 ? 2.4f : 3.6f);
        key->position.set(-3.f, 5.f, 7.f);
        bridge.scene()->add(key);
        Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
        GLRenderer renderer(canvas);
        const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const std::string glRenderer = rendererName ? rendererName : "unknown";
        chrononmotion::certification::require_nvidia_renderer(glRenderer);
        renderer.setClearColor(Color(0x151922));
        renderer.toneMapping = ToneMapping::ACESFilmic;
        renderer.toneMappingExposure = 1.f;
        const fs::path videoPath = out / (id + "_animation_1080p30.mp4");
        chrononmotion::certification::require_nvenc_encoder();
        chrononmotion::certification::GlCudaNvencEncoder gpuEncoder(W, H, FPS, videoPath.string());
        auto begin = std::chrono::steady_clock::now();
        for (int f = 0; f < FRAMES; ++f) {
            bridge.evaluateAndSync(motion, f);
            renderer.render(*bridge.scene(), *bridge.camera());
            gpuEncoder.encode_current_target(f);
        }
        gpuEncoder.finish();
        if (gpuEncoder.frames_submitted() != FRAMES || gpuEncoder.packets_written() != FRAMES)
            throw std::runtime_error(id + " GPU encoder did not submit and mux all frames");
        std::vector<unsigned char> first, mid, last, frame77;
        PoseSignals pose0, poseMid;
        size_t minCoverage = std::numeric_limits<size_t>::max(), maxCoverage = 0;
        std::array<unsigned char, 3> background{};
        for (int f = 0; f < FRAMES; ++f) {
            bridge.evaluateAndSync(motion, f);
            renderer.render(*bridge.scene(), *bridge.camera());
            auto pixels = renderer.readRGBPixels();
            if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("invalid diagnostic pixel readback");
            if (f == 0) {
                first = pixels;
                background = {pixels[0], pixels[1], pixels[2]};
                pose0 = signals(node);
            }
            if (f == 75) {
                mid = pixels;
                poseMid = signals(node);
            }
            if (f == 149) last = pixels;
            if (f == 77) frame77 = pixels;
            size_t cov = 0;
            for (size_t i = 0; i < pixels.size(); i += 3)
                if (std::abs(int(pixels[i]) - int(background[0])) + std::abs(int(pixels[i + 1]) - int(background[1])) + std::abs(int(pixels[i + 2]) - int(background[2])) > 6) ++cov;
            minCoverage = std::min(minCoverage, cov);
            maxCoverage = std::max(maxCoverage, cov);
            if (f == 0 || f == 75 || f == 149) png(out / ("frame-" + std::to_string(f) + ".png"), pixels);
        }
        auto done = std::chrono::steady_clock::now();
        bridge.evaluateAndSync(motion, 77);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto random = renderer.readRGBPixels();
        bool randomOk = random == frame77;
        const float boneDelta = vectorDelta(pose0.bones, poseMid.bones), morphDelta = vectorDelta(pose0.morphs, poseMid.morphs);
        const size_t temporal = changed(first, mid);
        int subjectMinY = H, subjectMaxY = -1;
        int subjectMinX = W, subjectMaxX = -1;
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                const size_t i = (size_t(y) * W + size_t(x)) * 3;
                const int delta = std::abs(int(mid[i]) - int(background[0])) +
                                  std::abs(int(mid[i + 1]) - int(background[1])) +
                                  std::abs(int(mid[i + 2]) - int(background[2]));
                if (delta <= 6) continue;
                subjectMinX = std::min(subjectMinX, x);
                subjectMaxX = std::max(subjectMaxX, x);
                subjectMinY = std::min(subjectMinY, y);
                subjectMaxY = std::max(subjectMaxY, y);
            }
        }
        const float subjectHeightRatio = subjectMaxY >= subjectMinY
                ? float(subjectMaxY - subjectMinY + 1) / float(H) : 0.f;
        std::cout << id << " metrics: clip=" << clip->name() << " meshes=" << totalMeshes << " skinned=" << skinMeshes << " morph=" << morphMeshes << " coverage=" << minCoverage << ".." << maxCoverage << " temporal=" << temporal << " bone_delta=" << boneDelta << " morph_delta=" << morphDelta << " random=" << randomOk << std::endl;
        std::cout << id << " frame75 bounds: x=" << subjectMinX << ".." << subjectMaxX << " y=" << subjectMinY << ".." << subjectMaxY << " subject_height_ratio=" << subjectHeightRatio << std::endl;
        if (!randomOk || minCoverage < 1000 || maxCoverage < size_t(W) * H / 100 || temporal < 1000 ||
            (t07 && (boneDelta < 1e-4f || subjectHeightRatio < 0.50f)) || (!t07 && morphDelta < 1e-4f))
            throw std::runtime_error(id + " gate failed: pose, subject framing, motion or random access");
        std::ofstream scene(out / "scene.json");
        scene << "{\n  \"asset\": \"" << asset.filename().string() << "\",\n  \"clip\": \"" << clip->name() << "\",\n  \"clip_duration_seconds\": " << clip->getDuration() << ",\n  \"frame_source\": \"Chronon MotionScene frame / fps\",\n  \"camera_position\": [" << cameraPosition.x << ", " << cameraPosition.y << ", " << cameraPosition.z << "],\n  \"camera_target\": [" << center.x << ", " << center.y << ", " << center.z << "],\n  \"t08_material_tint_override\": " << (!t07 ? "\"#e87936\"" : "null") << ",\n  \"materialized_meshes\": " << totalMeshes << ",\n  \"skinned_meshes\": " << skinMeshes << ",\n  \"morph_meshes\": " << morphMeshes << ",\n  \"output\": \"1920x1080 30fps 150 frames\"\n}\n";
        struct rusage ru {};
        getrusage(RUSAGE_SELF, &ru);
        const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        std::ofstream meta(out / "metadata.json");
        meta << std::fixed << std::setprecision(5) << "{\n  \"test_id\": \"" << id << "\",\n  \"status\": \"PARTIAL\",\n  \"technical_checks_passed\": true,\n  \"pipeline\": \"GLTFLoader animation -> MotionSceneBridge absolute Chronon sample -> threepp GLRenderer -> GL PBO -> CUDA -> NVENC\",\n  \"width\": 1920,\n  \"height\": 1080,\n  \"fps\": 30,\n  \"frames\": 150,\n  \"duration_seconds\": 5.0,\n  \"clip_name\": \"" << clip->name() << "\",\n  \"mesh_count\": " << totalMeshes << ",\n  \"skinned_mesh_count\": " << skinMeshes << ",\n  \"morph_mesh_count\": " << morphMeshes << ",\n  \"bone_matrix_delta_frame_0_to_75\": " << boneDelta << ",\n  \"morph_weight_delta_frame_0_to_75\": " << morphDelta << ",\n  \"frame_0_to_75_changed_pixels\": " << temporal << ",\n  \"min_coverage\": " << minCoverage << ",\n  \"max_coverage\": " << maxCoverage << ",\n  \"subject_bbox_frame_75\": [" << subjectMinX << ", " << subjectMinY << ", " << subjectMaxX << ", " << subjectMaxY << "],\n  \"subject_height_ratio_frame_75\": " << subjectHeightRatio << ",\n  \"random_access_frame_77_identical\": " << (randomOk ? "true" : "false") << ",\n  \"render_seconds\": " << std::chrono::duration<double>(done - begin).count() << ",\n  \"peak_rss_kib\": " << ru.ru_maxrss << ",\n  \"gl_renderer\": \"" << glRenderer << "\",\n  \"gl_version\": \"" << (ver ? ver : "unknown") << "\",\n  \"video_encoder\": \"h264_nvenc\",\n  \"gpu_device\": \"" << gpuEncoder.device_name() << "\",\n  \"frames_submitted\": " << gpuEncoder.frames_submitted() << ",\n  \"packets_muxed\": " << gpuEncoder.packets_written() << ",\n  \"hardware_gpu_render\": true,\n  \"cpu_framebuffer_readback_in_encode_path\": false,\n  \"diagnostic_cpu_readback_passes\": 151,\n  \"gpu_resident_zero_copy\": false,\n  \"vram_bytes\": null\n}\n";
        renderer.dispose();
        std::cout << id << " PARTIAL (GPU-resident encode verified; diagnostics read back separately) clip=" << clip->name() << " meshes=" << totalMeshes << " skinned=" << skinMeshes << " morph=" << morphMeshes << " bone_delta=" << boneDelta << " morph_delta=" << morphDelta << " pixels=" << temporal << " random_access=" << randomOk << "\n";
        return 0;
#endif
    }
}// namespace
int main(int argc, char** argv) try {
    chrononmotion::certification::prepare_nvidia_egl_headless();
    if (argc < 3) throw std::invalid_argument("usage: animation_certification T07|T08 output_dir [asset.glb] [clip_name]");
    std::string id = argv[1];
    if (id != "T07" && id != "T08") throw std::invalid_argument("test id must be T07 or T08");
    fs::path asset = argc > 3 ? argv[3] : (id == "T07" ? "render/spike/assets/threepp-certification/Fox.glb" : "render/spike/assets/threepp-certification/AnimatedMorphCube.glb");
    const std::string clip = argc > 4 ? argv[4] : (id == "T07" ? "Run" : "");
    return run(id, asset, argv[2], clip);
} catch (const std::exception& e) {
    std::cerr << "Animation certification FAIL: " << e.what() << '\n';
    return 1;
}
