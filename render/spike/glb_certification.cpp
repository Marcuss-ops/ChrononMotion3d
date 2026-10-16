// T06 certification: real Khronos GLB -> threepp GLTFLoader -> PBR render -> MP4.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#include "stb_image_write.h"

#include "gpu_certification_gate.hpp"
#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {
    constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;
    void writePng(const fs::path& path, const std::vector<unsigned char>& rgb) {
        std::vector<unsigned char> topDown(rgb.size());
        const size_t stride = size_t(W) * 3;
        for (int y = 0; y < H; ++y)
            std::copy_n(rgb.data() + size_t(H - y - 1) * stride, stride, topDown.data() + size_t(y) * stride);
        if (!stbi_write_png(path.c_str(), W, H, 3, topDown.data(), static_cast<int>(stride)))
            throw std::runtime_error("failed writing frame PNG");
    }
    size_t changedPixels(const std::vector<unsigned char>& p, const std::array<unsigned char, 3>& bg) {
        size_t n = 0;
        for (size_t i = 0; i < p.size(); i += 3)
            if (p[i] != bg[0] || p[i + 1] != bg[1] || p[i + 2] != bg[2]) ++n;
        return n;
    }
    size_t diffPixels(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        if (a.size() != b.size()) return std::numeric_limits<size_t>::max();
        size_t n = 0;
        for (size_t i = 0; i < a.size(); i += 3) {
            int d = std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) + std::abs(int(a[i + 2]) - int(b[i + 2]));
            if (d > 3) ++n;
        }
        return n;
    }
    std::string quote(const std::string& v) {
        std::string q = "'";
        for (char c : v) {
            if (c == '\'') q += "'\\''";
            else
                q += c;
        }
        return q + "'";
    }
    std::vector<std::string> hierarchySignature(const Object3D& root) {
        std::vector<std::string> result;
        std::function<void(const Object3D&)> visit = [&](const Object3D& node) {
            result.push_back(node.type() + "|" + node.name + "|" + std::to_string(node.children.size()));
            for (const auto* child : node.children) {
                if (child) visit(*child);
            }
        };
        visit(root);
        return result;
    }
}// namespace

int main(int argc, char** argv) try {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)argc;
    (void)argv;
    throw std::runtime_error("T06/T14 requires the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    const fs::path asset = argc > 1 ? argv[1] : "render/spike/assets/threepp-certification/CesiumMilkTruck.glb";
    const fs::path out = argc > 2 ? argv[2] : "out/threepp_certification/T06";
    const int frameCount = argc > 3 ? std::stoi(argv[3]) : 150;
    const std::string testId = argc > 4 ? argv[4] : "T06";
    if (frameCount < 2 || frameCount > 10000)
        throw std::runtime_error("frame count must be in [2, 10000]");
    const float duration = static_cast<float>(frameCount) / FPS;
    fs::create_directories(out);
    GLTFLoader loader;
    auto loaded = loader.load(asset);
    if (!loaded || !loaded->scene) throw std::runtime_error("GLTFLoader failed to load asset");
    int meshes = 0, standard = 0, textured = 0;
    loaded->scene->traverse([&](Object3D& object) {
        if (auto* mesh = dynamic_cast<Mesh*>(&object)) {
            ++meshes;
            if (auto material = mesh->materialAs<MeshStandardMaterial>()) {
                ++standard;
                if (material->map) ++textured;
            }
        }
    });
    if (meshes < 1 || standard < 1 || textured < 1) throw std::runtime_error("fixture did not retain imported PBR texture material");

    chrononmotion::motion::MotionScene motion(testId + "_full_gltf_import", float(FPS));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.assetId = "certification_glb";
    auto& model = motion.addMesh(1, "imported_model", std::move(attachment));
    model.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
    model.tracks.rotation.add(duration, chrononmotion::Vector3(0.f, 6.2831853f, 0.f));
    auto& ambient = motion.addAmbientLight(101, "ambient");
    ambient.color = chrononmotion::Color(0xffffff);
    ambient.intensity = 0.8f;
    auto& key = motion.addDirectionalLight(102, "key");
    key.color = chrononmotion::Color(0xffffff);
    key.intensity = 2.5f;
    key.transform.position = chrononmotion::Vector3(2.f, 3.f, 4.f);

    threepp::motion::MotionSceneBridge bridge(45.f, float(W) / H, 0.1f, 100.f);
    bridge.bindAsset("certification_glb", loaded->scene);
    bridge.evaluateAndSync(motion, 0);
    auto node = bridge.layerNode(1);
    if (!node) throw std::runtime_error("MotionSceneBridge did not materialize imported GLB");
    if (node->children.size() != 1 || !node->children.front())
        throw std::runtime_error("MotionSceneBridge asset wrapper did not retain one GLB root");
    const auto* clonedAssetRoot = node->children.front();
    const auto sourceHierarchy = hierarchySignature(*loaded->scene);
    const auto clonedHierarchy = hierarchySignature(*clonedAssetRoot);
    int clonedMeshes = 0, clonedStandard = 0, clonedTextured = 0, uvValidated = 0;
    node->traverse([&](Object3D& object) {
        auto* mesh = dynamic_cast<Mesh*>(&object);
        if (!mesh) return;
        ++clonedMeshes;
        if (auto material = mesh->materialAs<MeshStandardMaterial>()) {
            ++clonedStandard;
            if (material->map) ++clonedTextured;
        }
        if (!mesh->geometry()) return;
        const auto* positions = mesh->geometry()->getAttribute<float>("position");
        const auto* uvs = mesh->geometry()->getAttribute<float>("uv");
        if (!positions || positions->count() == 0 || !uvs || uvs->count() != positions->count()) return;
        bool finiteUvs = true;
        for (const float value : uvs->array()) finiteUvs = finiteUvs && std::isfinite(value);
        if (finiteUvs) ++uvValidated;
    });
    if (clonedMeshes != meshes || clonedStandard != standard || clonedTextured != textured ||
        sourceHierarchy != clonedHierarchy || uvValidated != meshes || meshes < 2) {
        throw std::runtime_error("cloned GLB hierarchy, mesh/material count or UV attributes differ from source");
    }
    Box3 bounds;
    bounds.setFromObject(*node, true);
    Vector3 center, size;
    bounds.getCenter(center);
    bounds.getSize(size);
    Box3 sourceBounds;
    sourceBounds.setFromObject(*loaded->scene, true);
    Vector3 sourceCenter, sourceSize;
    sourceBounds.getCenter(sourceCenter);
    sourceBounds.getSize(sourceSize);
    const bool boundsMatch = std::abs(center.x - sourceCenter.x) < 1e-3f &&
                             std::abs(center.y - sourceCenter.y) < 1e-3f &&
                             std::abs(center.z - sourceCenter.z) < 1e-3f &&
                             std::abs(size.x - sourceSize.x) < 1e-3f &&
                             std::abs(size.y - sourceSize.y) < 1e-3f &&
                             std::abs(size.z - sourceSize.z) < 1e-3f;
    if (!boundsMatch) throw std::runtime_error("cloned GLB bounds differ from GLTFLoader source");
    const float span = std::max({size.x, size.y, size.z});
    if (!(span > 1e-5f) || !std::isfinite(span)) throw std::runtime_error("imported GLB has invalid bounds");
    constexpr float desiredSpan = 1.8f;
    const float fit = desiredSpan / span;
    model.transform.position = chrononmotion::Vector3(-center.x * fit, -center.y * fit, -center.z * fit);
    model.transform.scale = chrononmotion::Vector3(fit, fit, fit);

    chrononmotion::motion::CameraRig cameraRig(201, 45.f, float(W) / H, 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.25f, 3.5f));
    cameraRig.positionTrack().add(duration * 0.5f, chrononmotion::Vector3(0.35f, 0.15f, 3.1f));
    cameraRig.positionTrack().add(duration, chrononmotion::Vector3(0.f, 0.25f, 3.5f));

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* selectedGlRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    chrononmotion::certification::require_nvidia_renderer(selectedGlRenderer ? selectedGlRenderer : "unknown");
    renderer.setClearColor(Color(0x1a1d24));
    auto target = RenderTarget::create(W, H, RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    const std::string video = (out / (testId + "_gltf_pbr_1080p30.mp4")).string();
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder encoder(W, H, FPS, video);
    std::array<unsigned char, 3> background{};
    std::vector<unsigned char> first, middle, last, frame77;
    size_t minCoverage = std::numeric_limits<size_t>::max(), maxCoverage = 0;
    auto t0 = std::chrono::steady_clock::now();
    // Production path: threepp GL target -> CUDA-registered PBO -> NVENC.
    // This loop never maps a frame into host memory.
    for (int f = 0; f < frameCount; ++f) {
        bridge.evaluateAndSync(motion, f, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        encoder.encode_current_target(f);
    }
    encoder.finish();
    if (encoder.frames_submitted() != frameCount)
        throw std::runtime_error("GPU encoder did not accept every frame");
    auto renderDone = std::chrono::steady_clock::now();

    // A second render pass supplies image diagnostics and PNGs. These CPU
    // readbacks are deliberately outside the encoded frame handoff above.
    for (int f = 0; f < frameCount; ++f) {
        bridge.evaluateAndSync(motion, f, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("invalid diagnostic readback size");
        if (f == 0) {
            background = {pixels[0], pixels[1], pixels[2]};
            first = pixels;
        }
        if (f == frameCount / 2) middle = pixels;
        if (f == frameCount - 1) last = pixels;
        if (f == 77) frame77 = pixels;
        const auto coverage = changedPixels(pixels, background);
        minCoverage = std::min(minCoverage, coverage);
        maxCoverage = std::max(maxCoverage, coverage);
        if (f == 0 || f == frameCount / 2 || f == frameCount - 1)
            writePng(out / ("frame-" + std::to_string(f) + ".png"), pixels);
    }
    const int randomFrame = std::min(77, frameCount - 1);
    bridge.evaluateAndSync(motion, randomFrame, &cameraRig);
    renderer.render(*bridge.scene(), *bridge.camera());
    auto random = renderer.readRGBPixels();
    const bool randomAccess = random == (randomFrame == 0 ? first :
                                        randomFrame == frameCount / 2 ? middle :
                                        randomFrame == frameCount - 1 ? last : frame77);
    const size_t temporalDiff = diffPixels(first, last);
    std::cout << testId << " metrics: meshes=" << meshes << " standard=" << standard << " textured=" << textured
              << " uv_validated=" << uvValidated << " hierarchy_match=" << (sourceHierarchy == clonedHierarchy)
              << " bounds_match=" << boundsMatch
              << " coverage=" << minCoverage << ".." << maxCoverage << " temporal_diff=" << temporalDiff
              << " random_access=" << randomAccess << std::endl;
    if (!randomAccess || temporalDiff < 1000 || minCoverage < 1000 || maxCoverage > size_t(W) * H * 3 / 4)
        throw std::runtime_error(testId + " acceptance failed: random access, motion, or coverage");

    std::ofstream sceneJson(out / "scene.json");
    sceneJson << "{\n  \"asset\": \"" << asset.filename().string() << "\",\n  \"rotation\": \"MotionScene model turntable, 360 degrees over " << frameCount << " frames\",\n  \"camera\": \"Chronon CameraRig dolly\",\n  \"lights\": [\"Chronon ambient 0.8\", \"Chronon directional 2.5\"],\n  \"fit_scale\": " << fit << "\n}\n";
    std::ofstream meta(out / "metadata.json");
    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const auto* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const bool hardwareRenderer = rendererName && std::string_view(rendererName).find("NVIDIA") != std::string_view::npos;
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const bool displayServerRequired = (display && *display) || (wayland && *wayland);
    const bool gpuEncodeComplete = encoder.frames_submitted() == frameCount;
    const char* testStatus = testId == "T14" && hardwareRenderer && !displayServerRequired && gpuEncodeComplete
        ? "PASS" : "PARTIAL";
    meta << std::fixed << std::setprecision(3)
         << "{\n  \"test_id\": \"" << testId << "\",\n  \"status\": \"" << testStatus << "\",\n  \"width\": 1920,\n  \"height\": 1080,\n  \"fps\": 30,\n  \"frames\": " << frameCount << ",\n  \"duration_seconds\": " << duration << ",\n  \"pipeline\": \"GLTFLoader -> MotionScene asset layer -> MotionSceneBridge -> threepp GLRenderer -> ffmpeg\",\n  \"mesh_count\": " << meshes << ",\n  \"standard_material_count\": " << standard << ",\n  \"textured_standard_material_count\": " << textured << ",\n  \"cloned_mesh_count\": " << clonedMeshes << ",\n  \"cloned_hierarchy_matches\": " << (sourceHierarchy == clonedHierarchy ? "true" : "false") << ",\n  \"uv_validated_mesh_count\": " << uvValidated << ",\n  \"cloned_bounds_match_source\": " << (boundsMatch ? "true" : "false") << ",\n  \"min_changed_pixels\": " << minCoverage << ",\n  \"max_changed_pixels\": " << maxCoverage << ",\n  \"first_last_diff_pixels\": " << temporalDiff << ",\n  \"random_access_frame_77_identical\": " << (randomAccess ? "true" : "false") << ",\n  \"render_seconds\": " << std::chrono::duration<double>(renderDone - t0).count() << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss << ",\n  \"cpu_user_seconds\": " << (double(usage.ru_utime.tv_sec) + double(usage.ru_utime.tv_usec) / 1.0e6) << ",\n  \"cpu_system_seconds\": " << (double(usage.ru_stime.tv_sec) + double(usage.ru_stime.tv_usec) / 1.0e6) << ",\n  \"gl_renderer\": \"" << (rendererName ? rendererName : "unknown") << "\",\n  \"gl_version\": \"" << (version ? version : "unknown") << "\",\n  \"display_server_required\": " << (displayServerRequired ? "true" : "false") << ",\n  \"vram_bytes\": null\n}\n";
    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    std::cout << testId << ' ' << testStatus << " (mesh import and bridge checks passed; texture/material golden comparison remains open) meshes=" << meshes << " standard=" << standard << " textured=" << textured << " coverage=" << minCoverage << ".." << maxCoverage << " temporal_diff=" << temporalDiff << " random_access=" << randomAccess << " renderer=" << (rendererName ? rendererName : "unknown") << "\n";
    return 0;
#endif
} catch (const std::exception& e) {
    std::cerr << "certification FAIL: " << e.what() << '\n';
    return 1;
}
