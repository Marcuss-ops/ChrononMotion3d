// T03 certification: Chronon PBR descriptors -> MotionSceneBridge -> threepp PBR renderer.
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
#include <set>
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

#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/textures/Texture.hpp"

using namespace threepp;
namespace fs = std::filesystem;
namespace {
    constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;
    void png(const fs::path& p, const std::vector<unsigned char>& rgb) {
        std::vector<unsigned char> top(rgb.size());
        size_t stride = size_t(W) * 3;
        for (int y = 0; y < H; ++y) std::copy_n(rgb.data() + size_t(H - y - 1) * stride, stride, top.data() + size_t(y) * stride);
        if (!stbi_write_png(p.c_str(), W, H, 3, top.data(), int(stride))) throw std::runtime_error("PNG write failed");
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
    size_t changed(const std::vector<unsigned char>& p, const std::array<unsigned char, 3>& bg) {
        size_t n = 0;
        for (size_t i = 0; i < p.size(); i += 3)
            if (p[i] != bg[0] || p[i + 1] != bg[1] || p[i + 2] != bg[2]) ++n;
        return n;
    }
    size_t diff(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
        size_t n = 0;
        if (a.size() != b.size()) return std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < a.size(); i += 3)
            if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) + std::abs(int(a[i + 2]) - int(b[i + 2])) > 3) ++n;
        return n;
    }
    std::array<double, 2> sphereLightCentroid(const std::vector<unsigned char>& rgb) {
        // Track the bright region on the fixed sphere, excluding the black
        // background. Renderer readback is bottom-up, so positive Y is up.
        constexpr double cx = W * 0.5, cy = H * 0.5, radius = 325.0;
        double weightSum = 0.0, xSum = 0.0, ySum = 0.0;
        const int minX = int(cx - radius), maxX = int(cx + radius);
        const int minY = int(cy - radius), maxY = int(cy + radius);
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const double dx = (x + 0.5 - cx) / radius;
                const double dy = (y + 0.5 - cy) / radius;
                if (dx * dx + dy * dy > 1.0) continue;
                const size_t i = (size_t(y) * W + size_t(x)) * 3;
                const double luminance = 0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2];
                const double weight = std::max(0.0, luminance - 24.0);
                weightSum += weight;
                xSum += weight * dx;
                ySum += weight * dy;
            }
        }
        if (weightSum <= 0.0) return {0.0, 0.0};
        return {xSum / weightSum, ySum / weightSum};
    }
    std::shared_ptr<Texture> studioEnvironment() {
        constexpr int ew = 256, eh = 128;
        std::vector<float> px(size_t(ew) * eh * 3);
        for (int y = 0; y < eh; ++y)
            for (int x = 0; x < ew; ++x) {
                float u = float(x) / ew, v = float(y) / eh;
                float base = 0.035f + 0.10f * (1.f - v);
                float panel1 = (u > 0.12f && u < 0.25f && v > 0.12f && v < 0.63f) ? 7.f : 0.f;
                float panel2 = (u > 0.56f && u < 0.70f && v > 0.08f && v < 0.58f) ? 4.5f : 0.f;
                float horizon = (v > 0.46f && v < 0.54f) ? 0.45f : 0.f;
                size_t i = (size_t(y) * ew + x) * 3;
                px[i] = base + panel1 + 0.2f * panel2 + horizon;
                px[i + 1] = base + 0.86f * panel1 + 0.72f * panel2 + horizon;
                px[i + 2] = base + 0.65f * panel1 + panel2 + horizon;
            }
        auto t = Texture::create(Image(std::move(px), ew, eh));
        t->mapping = Mapping::EquirectangularReflection;
        t->format = Format::RGB;
        t->type = Type::Float;
        t->colorSpace = ColorSpace::Linear;
        t->magFilter = Filter::Linear;
        t->minFilter = Filter::Linear;
        return t;
    }
    std::shared_ptr<Texture> proceduralNormalMap() {
        constexpr int size = 128;
        std::vector<unsigned char> pixels(size_t(size) * size * 3);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const float u = float(x) / float(size), v = float(y) / float(size);
                // Broad, low-amplitude brushed variation stays readable at
                // delivery resolution without aliasing into harsh stripes.
                const float nx = 0.18f * std::sin(6.2831853f * 3.f * u);
                const float ny = 0.18f * std::cos(6.2831853f * 2.f * v);
                const float nz = std::sqrt(std::max(0.f, 1.f - nx * nx - ny * ny));
                const size_t p = (size_t(y) * size + size_t(x)) * 3;
                pixels[p] = static_cast<unsigned char>(std::clamp((nx * 0.5f + 0.5f) * 255.f, 0.f, 255.f));
                pixels[p + 1] = static_cast<unsigned char>(std::clamp((ny * 0.5f + 0.5f) * 255.f, 0.f, 255.f));
                pixels[p + 2] = static_cast<unsigned char>(std::clamp((nz * 0.5f + 0.5f) * 255.f, 0.f, 255.f));
            }
        }
        auto texture = DataTexture::create(std::move(pixels), size, size);
        texture->format = Format::RGB;
        texture->magFilter = Filter::Linear;
        texture->minFilter = Filter::Linear;
        texture->needsUpdate();
        return texture;
    }
}// namespace
int main(int argc, char** argv) try {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)argc;
    (void)argv;
    throw std::runtime_error("T03/T04 require the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    chrononmotion::certification::prepare_nvidia_egl_headless();
    fs::path out = argc > 1 ? argv[1] : "out/threepp_certification/T03";
    fs::create_directories(out);
    chrononmotion::motion::MotionScene motion("T03_pbr_materials", float(FPS));
    auto& ambient = motion.addAmbientLight(101, "CertAmbient");
    ambient.intensity = 0.08f;
    ambient.color = chrononmotion::Color(0xffffff);
    auto& directional = motion.addDirectionalLight(102, "CertDirectional");
    directional.intensity = 1.25f;
    directional.color = chrononmotion::Color(0xffffff);
    directional.directionTrack.add(0.f, chrononmotion::Vector3(-0.4f, -0.65f, -1.f));
    directional.directionTrack.add(5.f, chrononmotion::Vector3(0.45f, -0.45f, -1.f));
    auto& point = motion.addPointLight(103, "CertPoint");
    point.intensity = 14.f;
    point.range = 8.f;
    point.decay = 2.f;
    point.positionTrack.add(0.f, chrononmotion::Vector3(-1.8f, 1.2f, 2.f));
    point.positionTrack.add(5.f, chrononmotion::Vector3(1.8f, 1.2f, 2.f));
    auto& spot = motion.addSpotLight(104, "CertSpot");
    spot.intensity = 20.f;
    spot.range = 9.f;
    spot.decay = 2.f;
    spot.angle = 0.7f;
    spot.penumbra = 0.6f;
    spot.positionTrack.add(0.f, chrononmotion::Vector3(1.7f, 1.8f, 2.3f));
    spot.positionTrack.add(5.f, chrononmotion::Vector3(-1.7f, 1.8f, 2.3f));
    struct Sample {
        const char* name;
        float rough;
        float metal;
        chrononmotion::Color color;
        chrononmotion::Color emission;
        float emissionStrength;
    };
    const Sample samples[] = {
            {"polished_metal", 0.08f, 1.f, chrononmotion::Color(0xd0d7df), chrononmotion::Color(0x000000), 0.f},
            {"rough_metal", 0.88f, 1.f, chrononmotion::Color(0xd0d7df), chrononmotion::Color(0x000000), 0.f},
            {"matte_plastic", 0.92f, 0.f, chrononmotion::Color(0xe54b35), chrononmotion::Color(0x000000), 0.f},
            {"emissive", 0.3f, 0.05f, chrononmotion::Color(0x20252d), chrononmotion::Color(0x16bfff), 3.5f}};
    for (unsigned i = 0; i < 4; ++i) {
        chrononmotion::motion::MeshAttachment a;
        a.geometry = chrononmotion::SphereGeometry::create(0.70f, 64, 48);
        a.material = chrononmotion::MaterialDescriptor::pbr(i + 1, samples[i].color, samples[i].rough, samples[i].metal, samples[i].emission, samples[i].emissionStrength);
        a.material.name = samples[i].name;
        auto& layer = motion.addMesh(i + 1, samples[i].name, std::move(a));
        layer.tracks.position.add(0.f, chrononmotion::Vector3(-2.25f + 1.5f * float(i), 0.f, 0.f));
        layer.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
        layer.tracks.rotation.add(5.f, chrononmotion::Vector3(0.f, 6.2831853f, 0.f));
    }
    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* selectedRenderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string selectedRendererName = selectedRenderer ? selectedRenderer : "unknown";
    chrononmotion::certification::require_nvidia_renderer(selectedRendererName);
    renderer.setClearColor(Color(0x090c13));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.f;
    threepp::motion::MotionSceneBridge bridge(38.f, float(W) / H, 0.1f, 100.f);
    bridge.camera()->position.set(0.f, 0.f, 5.5f);
    bridge.camera()->lookAt(0.f, 0.f, 0.f);
    bridge.scene()->environment = studioEnvironment();
    auto normalMap = proceduralNormalMap();
    bridge.bindMaterialNormalMap(1, normalMap);
    bridge.evaluateAndSync(motion, 0);
    for (unsigned i = 0; i < 4; ++i) {
        auto node = bridge.layerNode(i + 1);
        auto* mesh = node ? dynamic_cast<Mesh*>(node.get()) : nullptr;
        auto material = mesh ? mesh->materialAs<MeshStandardMaterial>() : nullptr;
        if (!material || std::abs(material->roughness - samples[i].rough) > 1e-5f ||
            std::abs(material->metalness - samples[i].metal) > 1e-5f ||
            std::abs(material->emissiveIntensity - samples[i].emissionStrength) > 1e-5f ||
            (i == 0 && material->normalMap != normalMap))
            throw std::runtime_error("MotionSceneBridge did not preserve PBR values for " + std::string(samples[i].name));
    }
    auto polishedNode = bridge.layerNode(1);
    auto* polishedMesh = polishedNode ? dynamic_cast<Mesh*>(polishedNode.get()) : nullptr;
    auto polishedMaterial = polishedMesh ? polishedMesh->materialAs<MeshStandardMaterial>() : nullptr;
    if (!polishedMaterial) throw std::runtime_error("polished PBR material is missing");
    polishedMaterial->normalScale.set(0.45f, 0.45f);
    const std::string video = (out / "T03_pbr_1080p30.mp4").string();
    fs::path lightOut = out.parent_path() / "T04";
    fs::create_directories(lightOut);
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder pbrEncoder(W, H, FPS, video);
    auto start = std::chrono::steady_clock::now();
    // Encode on the GPU in a dedicated pass. CPU snapshots below are only
    // diagnostic evidence and are not fed to the production video encoder.
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f);
        renderer.render(*bridge.scene(), *bridge.camera());
        pbrEncoder.encode_current_target(f);
    }
    pbrEncoder.finish();
    if (pbrEncoder.frames_submitted() != FRAMES || pbrEncoder.packets_written() != FRAMES)
        throw std::runtime_error("T03 GPU encoder did not submit and mux all frames");
    std::array<unsigned char, 3> bg{};
    std::vector<unsigned char> first, mid, last, at77;
    size_t minCov = std::numeric_limits<size_t>::max(), maxCov = 0;
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("bad readback size");
        if (f == 0) {
            bg = {pixels[0], pixels[1], pixels[2]};
            first = pixels;
        }
        if (f == 75) mid = pixels;
        if (f == 149) last = pixels;
        if (f == 77) at77 = pixels;
        size_t cov = changed(pixels, bg);
        minCov = std::min(minCov, cov);
        maxCov = std::max(maxCov, cov);
        if (f == 0 || f == 75 || f == 149) {
            png(out / ("frame-" + std::to_string(f) + ".png"), pixels);
        }
    }
    auto done = std::chrono::steady_clock::now();
    bridge.evaluateAndSync(motion, 77);
    renderer.render(*bridge.scene(), *bridge.camera());
    auto random = renderer.readRGBPixels();
    bool randomOk = random == at77;
    auto firstNode = bridge.layerNode(1);
    auto* firstMesh = firstNode ? dynamic_cast<Mesh*>(firstNode.get()) : nullptr;
    auto firstMaterial = firstMesh ? firstMesh->materialAs<MeshStandardMaterial>() : nullptr;
    if (!firstMaterial || !firstMaterial->normalMap) throw std::runtime_error("normal map was not bound to the polished PBR material");
    const auto savedNormalMap = firstMaterial->normalMap;
    firstMaterial->normalMap.reset();
    firstMaterial->needsUpdate();
    bridge.evaluateAndSync(motion, 75);
    renderer.render(*bridge.scene(), *bridge.camera());
    const auto noNormalMapFrame = renderer.readRGBPixels();
    png(out / "normalmap-disabled-frame-75.png", noNormalMapFrame);
    const size_t normalMapChangedPixels = diff(mid, noNormalMapFrame);
    firstMaterial->normalMap = savedNormalMap;
    firstMaterial->needsUpdate();
    // Measure visual occupancy as well as distinct color response. PBR samples
    // must be large enough to inspect at the stated delivery resolution.
    int subjectMinY = H, subjectMaxY = -1, subjectMinX = W, subjectMaxX = -1;
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            const size_t i = (size_t(y) * W + size_t(x)) * 3;
            if (std::abs(int(mid[i]) - int(bg[0])) + std::abs(int(mid[i + 1]) - int(bg[1])) +
                std::abs(int(mid[i + 2]) - int(bg[2])) < 12) continue;
            subjectMinX = std::min(subjectMinX, x);
            subjectMaxX = std::max(subjectMaxX, x);
            subjectMinY = std::min(subjectMinY, y);
            subjectMaxY = std::max(subjectMaxY, y);
        }
    }
    const float subjectHeightRatio = subjectMaxY >= subjectMinY
            ? float(subjectMaxY - subjectMinY + 1) / float(H) : 0.f;
    // Diversity scores are computed from the center ROI of each sphere.
    std::array<size_t, 4> distinct{};
    for (int s = 0; s < 4; ++s) {
        std::set<uint32_t> colors;
        int cx = int((float(s) - 1.5f) * 1.5f * (H / 2.f) / (5.5f * std::tan(38.f * 3.14159265f / 360.f)) + W / 2.f);
        int r = 180;
        for (int y = H / 2 - r; y < H / 2 + r; ++y)
            for (int x = std::max(0, cx - r); x < std::min(W, cx + r); ++x) {
                size_t i = (size_t(y) * W + x) * 3;
                colors.insert((uint32_t(mid[i]) << 16) | (uint32_t(mid[i + 1]) << 8) | mid[i + 2]);
            }
        distinct[s] = colors.size();
    }
    if (!randomOk || diff(first, last) < 1000 || minCov < 1000 || subjectHeightRatio < 0.32f || normalMapChangedPixels < 1000 ||
        *std::min_element(distinct.begin(), distinct.end()) < 5)
        throw std::runtime_error("T03 gate failed: motion/random-access/material response/frame occupancy");
    std::ofstream scene(out / "scene.json");
    scene << "{\n  \"fixture\": \"four large Chronon PBR spheres, generated linear HDR studio environment\",\n  \"materials\": [\"polished metal r=0.08 m=1 plus procedural normal map\",\"rough metal r=0.88 m=1\",\"matte plastic r=0.92 m=0\",\"emissive r=0.3 m=0.05\"],\n  \"normal_map\": \"broad brushed variation (3x2 UV cycles, normal scale 0.45), stored as RGB and toggled off at frame 75 for pixel-response measurement\",\n  \"output\": \"1920x1080 30fps 150 frames\"\n}\n";
    struct rusage ru {};
    getrusage(RUSAGE_SELF, &ru);
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const char* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream meta(out / "metadata.json");
    meta << std::fixed << std::setprecision(3) << "{\n  \"test_id\": \"T03\",\n  \"status\": \"PARTIAL\",\n  \"pipeline\": \"PBR MaterialDescriptor -> MotionScene -> MotionSceneBridge -> MeshStandardMaterial -> GLRenderer -> GL PBO -> CUDA -> NVENC\",\n  \"width\": 1920,\n  \"height\": 1080,\n  \"fps\": 30,\n  \"frames\": 150,\n  \"duration_seconds\": 5.0,\n  \"environment\": \"deterministic procedural linear HDR equirectangular, PMREM generated by threepp\",\n  \"material_roi_distinct_colors\": [" << distinct[0] << "," << distinct[1] << "," << distinct[2] << "," << distinct[3] << "],\n  \"material_1_normal_map_procedural\": true,\n  \"normal_map_enabled_vs_disabled_changed_pixels_frame_75\": " << normalMapChangedPixels << ",\n  \"subject_bbox_frame_75\": [" << subjectMinX << ", " << subjectMinY << ", " << subjectMaxX << ", " << subjectMaxY << "],\n  \"subject_height_ratio_frame_75\": " << subjectHeightRatio << ",\n  \"minimum_subject_height_ratio\": 0.32,\n  \"min_changed_pixels\": " << minCov << ",\n  \"max_changed_pixels\": " << maxCov << ",\n  \"first_last_diff_pixels\": " << diff(first, last) << ",\n  \"random_access_frame_77_identical\": " << (randomOk ? "true" : "false") << ",\n  \"render_seconds\": " << std::chrono::duration<double>(done - start).count() << ",\n  \"peak_rss_kib\": " << ru.ru_maxrss << ",\n  \"gl_renderer\": \"" << (gl ? gl : "unknown") << "\",\n  \"gl_version\": \"" << (ver ? ver : "unknown") << "\",\n  \"video_encoder\": \"h264_nvenc\",\n  \"gpu_device\": \"" << pbrEncoder.device_name() << "\",\n  \"frames_submitted\": " << pbrEncoder.frames_submitted() << ",\n  \"packets_muxed\": " << pbrEncoder.packets_written() << ",\n  \"hardware_gpu_render\": true,\n  \"cpu_framebuffer_readback_in_encode_path\": false,\n  \"diagnostic_cpu_readback_passes\": 152,\n  \"gpu_resident_zero_copy\": false,\n  \"vram_bytes\": null\n}\n";
    std::ofstream lightScene(lightOut / "scene.json");
    lightScene << "{\n  \"fixture\": \"one large matte sphere, fixed low ambient and one animated point light\",\n  \"light_motion\": \"point light orbits the sphere over five seconds\",\n  \"material\": \"fixed Lambert diffuse, unchanged throughout the clip\",\n  \"output\": \"1920x1080 30fps 150 frames\"\n}\n";
    std::ofstream lm(lightOut / "metadata.json");
    chrononmotion::motion::MotionScene lighting("T04_lighting_orbit", float(FPS));
    chrononmotion::motion::MeshAttachment lightingSubject;
    lightingSubject.geometry = chrononmotion::SphereGeometry::create(1.25f, 64, 48);
    lightingSubject.material = chrononmotion::MaterialDescriptor::lambert(201, chrononmotion::Color(0xb9c7d8));
    lighting.addMesh(201, "fixed_matte_subject", std::move(lightingSubject));
    auto& fill = lighting.addAmbientLight(202, "fixed_low_fill");
    fill.color = chrononmotion::Color(0xffffff);
    fill.intensity = 0.12f;
    auto& orbit = lighting.addPointLight(203, "orbiting_key");
    orbit.color = chrononmotion::Color(0xffd18a);
    orbit.intensity = 24.f;
    orbit.range = 12.f;
    orbit.decay = 2.f;
    orbit.positionTrack.add(0.f, chrononmotion::Vector3(-2.8f, 1.5f, 2.4f));
    orbit.positionTrack.add(1.25f, chrononmotion::Vector3(0.f, 2.8f, 2.4f));
    orbit.positionTrack.add(2.5f, chrononmotion::Vector3(2.8f, 1.5f, 2.4f));
    orbit.positionTrack.add(3.75f, chrononmotion::Vector3(0.f, -1.0f, 2.4f));
    orbit.positionTrack.add(5.f, chrononmotion::Vector3(-2.8f, 1.5f, 2.4f));
    threepp::motion::MotionSceneBridge lightingBridge(38.f, float(W) / H, 0.1f, 100.f);
    lightingBridge.camera()->position.set(0.f, 0.f, 6.2f);
    lightingBridge.camera()->lookAt(0.f, 0.f, 0.f);
    const std::string lightVideo = (lightOut / "T04_lighting_1080p30.mp4").string();
    chrononmotion::certification::GlCudaNvencEncoder lightingEncoder(W, H, FPS, lightVideo);
    for (int f = 0; f < FRAMES; ++f) {
        lightingBridge.evaluateAndSync(lighting, f);
        renderer.render(*lightingBridge.scene(), *lightingBridge.camera());
        lightingEncoder.encode_current_target(f);
    }
    lightingEncoder.finish();
    if (lightingEncoder.frames_submitted() != FRAMES || lightingEncoder.packets_written() != FRAMES)
        throw std::runtime_error("T04 GPU encoder did not submit and mux all frames");
    std::array<std::vector<unsigned char>, 3> lightingSamples;
    std::vector<unsigned char> lightingFrame77;
    std::array<std::array<double, 2>, FRAMES> lightingCentroids{};
    for (int f = 0; f < FRAMES; ++f) {
        lightingBridge.evaluateAndSync(lighting, f);
        renderer.render(*lightingBridge.scene(), *lightingBridge.camera());
        auto pixels = renderer.readRGBPixels();
        lightingCentroids[size_t(f)] = sphereLightCentroid(pixels);
        if (f == 0) lightingSamples[0] = pixels;
        if (f == 75) lightingSamples[1] = pixels;
        if (f == 149) lightingSamples[2] = pixels;
        if (f == 77) lightingFrame77 = pixels;
        if (f == 0 || f == 75 || f == 149) png(lightOut / ("frame-" + std::to_string(f) + ".png"), pixels);
    }
    lightingBridge.evaluateAndSync(lighting, 77);
    renderer.render(*lightingBridge.scene(), *lightingBridge.camera());
    const bool lightingRandomOk = renderer.readRGBPixels() == lightingFrame77;
    const std::array<size_t, 3> temporalLightDiffs{
            diff(lightingSamples[0], lightingSamples[1]),
            diff(lightingSamples[1], lightingSamples[2]),
            diff(lightingSamples[0], lightingSamples[2])};
    const auto& lightStart = lightingCentroids[0];
    const auto& lightTop = lightingCentroids[37];
    const auto& lightRight = lightingCentroids[75];
    const auto& lightBottom = lightingCentroids[112];
    const auto& lightEnd = lightingCentroids[149];
    const bool orbitTracksSphere = lightStart[0] < -0.10 && lightRight[0] > 0.10 &&
            lightTop[1] > 0.08 && lightBottom[1] < -0.08 &&
            std::hypot(lightStart[0] - lightEnd[0], lightStart[1] - lightEnd[1]) < 0.04;
    const bool lightingPass = lightingRandomOk && orbitTracksSphere &&
            *std::min_element(temporalLightDiffs.begin(), temporalLightDiffs.end()) > 10000;
    lm << std::fixed << std::setprecision(6)
       << "{\n  \"test_id\": \"T04\",\n  \"status\": \"" << (!lightingPass ? "FAIL" : "PARTIAL")
       << "\",\n  \"pipeline\": \"independent MotionScene -> MotionSceneBridge -> fixed Lambert sphere + moving point light -> GLRenderer\",\n"
       << "  \"width\": 1920,\n  \"height\": 1080,\n  \"fps\": 30,\n  \"frames\": 150,\n  \"duration_seconds\": 5.0,\n"
       << "  \"temporal_changed_pixels_0_75_149\": [" << temporalLightDiffs[0] << "," << temporalLightDiffs[1] << "," << temporalLightDiffs[2] << "],\n"
       << "  \"sphere_brightness_centroid_normalized_xy_all_frames\": [";
    for (int f = 0; f < FRAMES; ++f) {
        if (f) lm << ',';
        lm << '[' << lightingCentroids[size_t(f)][0] << ',' << lightingCentroids[size_t(f)][1] << ']';
    }
    lm << "],\n  \"sphere_brightness_centroid_normalized_xy_frames_0_37_75_112_149\": [["
       << lightStart[0] << ',' << lightStart[1] << "],[" << lightTop[0] << ',' << lightTop[1]
       << "],[" << lightRight[0] << ',' << lightRight[1] << "],[" << lightBottom[0] << ',' << lightBottom[1]
       << "],[" << lightEnd[0] << ',' << lightEnd[1] << "]],\n"
       << "  \"full_orbit_tracks_sphere_brightness\": " << (orbitTracksSphere ? "true" : "false")
       << ",\n  \"fixed_material\": true,\n"
       << "  \"random_access_frame_77_identical\": " << (lightingRandomOk ? "true" : "false") << ",\n"
       << "  \"render_seconds\": " << std::chrono::duration<double>(done - start).count() << ",\n"
       << "  \"peak_rss_kib\": " << ru.ru_maxrss << ",\n"
       << "  \"gl_renderer\": \"" << selectedRendererName << "\",\n"
       << "  \"video_encoder\": \"h264_nvenc\",\n"
       << "  \"hardware_gpu_render\": " << ((selectedRendererName.find("llvmpipe") == std::string::npos && selectedRendererName.find("softpipe") == std::string::npos && selectedRendererName.find("Software") == std::string::npos) ? "true" : "false") << ",\n"
       << "  \"encode_path\": \"GL PBO -> CUDA device copy -> NVENC\",\n"
       << "  \"gpu_device\": \"" << lightingEncoder.device_name() << "\",\n"
       << "  \"frames_submitted\": " << lightingEncoder.frames_submitted() << ",\n"
       << "  \"packets_muxed\": " << lightingEncoder.packets_written() << ",\n"
       << "  \"cpu_framebuffer_readback_in_encode_path\": false,\n"
       << "  \"diagnostic_cpu_readback_passes\": 151,\n"
       << "  \"gpu_resident_zero_copy\": false,\n"
       << "  \"vram_bytes\": null\n}\n";
    if (!lightingPass) throw std::runtime_error("T04 gate failed: independent moving-light scene did not change enough pixels");
    renderer.dispose();
    std::cout << "T04 PARTIAL (GPU-resident encode verified; presentation/reference review remains open) independent moving-light temporal diffs=" << temporalLightDiffs[0] << "," << temporalLightDiffs[1] << "," << temporalLightDiffs[2] << "\n";
    std::cout << "T03 PARTIAL (PBR and bound normal-map response measured; trusted material golden remains open) coverage=" << minCov << ".." << maxCov << " subject_height_ratio=" << subjectHeightRatio << " normal_map_changed_pixels=" << normalMapChangedPixels << " roi_colors=" << distinct[0] << "," << distinct[1] << "," << distinct[2] << "," << distinct[3] << " random_access=" << randomOk << "\n";
    return 0;
#endif
} catch (const std::exception& e) {
    std::cerr << "T03 FAIL: " << e.what() << '\n';
    return 1;
}
