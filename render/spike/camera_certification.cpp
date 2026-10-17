// T01/T09: MotionScene geometry and animated camera through the production
// MotionSceneBridge -> threepp GL renderer -> CUDA/NVENC certification lane.
#include "gpu_certification_gate.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#include "gl_cuda_nvenc_encoder.hpp"
#include "stb_image_write.h"

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/renderers/GLRenderer.hpp"

namespace fs = std::filesystem;
using namespace threepp;
namespace {
constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;

void write_png(const fs::path& path, const std::vector<unsigned char>& rgb) {
    std::vector<unsigned char> top(rgb.size());
    const size_t stride = size_t(W) * 3;
    for (int y = 0; y < H; ++y)
        std::copy_n(rgb.data() + size_t(H - 1 - y) * stride, stride, top.data() + size_t(y) * stride);
    if (!stbi_write_png(path.c_str(), W, H, 3, top.data(), int(stride)))
        throw std::runtime_error("failed to write diagnostic PNG: " + path.string());
}

size_t frame_delta(const std::vector<unsigned char>& a, const std::vector<unsigned char>& b) {
    if (a.size() != b.size()) return std::numeric_limits<size_t>::max();
    size_t changed = 0;
    for (size_t i = 0; i < a.size(); i += 3)
        if (std::abs(int(a[i]) - int(b[i])) + std::abs(int(a[i + 1]) - int(b[i + 1])) +
            std::abs(int(a[i + 2]) - int(b[i + 2])) > 8) ++changed;
    return changed;
}

size_t coverage(const std::vector<unsigned char>& pixels, const std::array<unsigned char, 3>& bg) {
    size_t count = 0;
    for (size_t i = 0; i < pixels.size(); i += 3)
        if (std::abs(int(pixels[i]) - bg[0]) + std::abs(int(pixels[i + 1]) - bg[1]) +
            std::abs(int(pixels[i + 2]) - bg[2]) > 8) ++count;
    return count;
}

struct PixelBounds {
    int minX{W}, minY{H}, maxX{-1}, maxY{-1};
    void add(int x, int y) {
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y);
    }
    [[nodiscard]] size_t overlap(const PixelBounds& other) const {
        const int left = std::max(minX, other.minX), top = std::max(minY, other.minY);
        const int right = std::min(maxX, other.maxX), bottom = std::min(maxY, other.maxY);
        return right < left || bottom < top ? 0 : size_t(right - left + 1) * size_t(bottom - top + 1);
    }
};

chrononmotion::motion::Layer& add_box(chrononmotion::motion::MotionScene& scene,
        uint32_t id, const std::string& name, const chrononmotion::Vector3& size,
        const chrononmotion::Vector3& position, uint32_t color) {
    chrononmotion::motion::MeshAttachment a;
    a.geometry = chrononmotion::BoxGeometry::create(size.x, size.y, size.z);
    a.material = chrononmotion::MaterialDescriptor::emissive(id, chrononmotion::Color(color), 0.8f);
    auto& layer = scene.addMesh(id, name, std::move(a));
    layer.transform.position = position;
    return layer;
}

chrononmotion::motion::Layer& add_sphere(chrononmotion::motion::MotionScene& scene,
        uint32_t id, const std::string& name, float radius,
        const chrononmotion::Vector3& position, uint32_t color) {
    chrononmotion::motion::MeshAttachment a;
    a.geometry = chrononmotion::SphereGeometry::create(radius, 48, 32);
    a.material = chrononmotion::MaterialDescriptor::emissive(id, chrononmotion::Color(color), 0.7f);
    auto& layer = scene.addMesh(id, name, std::move(a));
    layer.transform.position = position;
    return layer;
}

chrononmotion::motion::Layer& add_lambert_box(chrononmotion::motion::MotionScene& scene,
        uint32_t id, const std::string& name, const chrononmotion::Vector3& size,
        const chrononmotion::Vector3& position, uint32_t color) {
    chrononmotion::motion::MeshAttachment a;
    a.geometry = chrononmotion::BoxGeometry::create(size.x, size.y, size.z);
    a.material = chrononmotion::MaterialDescriptor::lambert(id, chrononmotion::Color(color));
    auto& layer = scene.addMesh(id, name, std::move(a));
    layer.transform.position = position;
    return layer;
}

chrononmotion::motion::Layer& add_lambert_sphere(chrononmotion::motion::MotionScene& scene,
        uint32_t id, const std::string& name, float radius,
        const chrononmotion::Vector3& position, uint32_t color) {
    chrononmotion::motion::MeshAttachment a;
    a.geometry = chrononmotion::SphereGeometry::create(radius, 48, 32);
    a.material = chrononmotion::MaterialDescriptor::lambert(id, chrononmotion::Color(color));
    auto& layer = scene.addMesh(id, name, std::move(a));
    layer.transform.position = position;
    return layer;
}

void prepare_t01(chrononmotion::motion::MotionScene& scene,
                 chrononmotion::motion::CameraRig& camera) {
    // Six thin, colored threepp-backed box faces form a cube shell. The camera
    // orbits with pitch so every face must appear in the 150-frame sequence.
    const float h = 1.15f, t = 0.06f;
    add_box(scene, 1, "face_front", {2.f, 2.f, t}, {0.f, 0.f, h}, 0xff382f);
    add_box(scene, 2, "face_back",  {2.f, 2.f, t}, {0.f, 0.f, -h}, 0x00d8c7);
    add_box(scene, 3, "face_left",  {t, 2.f, 2.f}, {-h, 0.f, 0.f}, 0x44e05f);
    add_box(scene, 4, "face_right", {t, 2.f, 2.f}, {h, 0.f, 0.f}, 0xec42dc);
    add_box(scene, 5, "face_top",   {2.f, t, 2.f}, {0.f, h, 0.f}, 0x377cff);
    add_box(scene, 6, "face_bottom",{2.f, t, 2.f}, {0.f, -h, 0.f}, 0xffce36);
    auto& ambient = scene.addAmbientLight(90, "ambient");
    ambient.intensity = 1.f;
    scene.addDirectionalLight(91, "key").intensity = 0.35f;
    camera.setTarget({0.f, 0.f, 0.f}).setFov(42.f);
    for (int k = 0; k <= 4; ++k) {
        const float s = 1.25f * k;
        const float a = 1.57079632679f * float(k);
        camera.positionTrack().add(s, {5.4f * std::sin(a), 1.7f * std::sin(a * 2.f), 5.4f * std::cos(a)});
    }
}

void prepare_t09(chrononmotion::motion::MotionScene& scene,
                 chrononmotion::motion::CameraRig& camera) {
    // Three independently colored objects occupy different depth planes. The
    // near box overlaps the middle/far silhouettes; a dolly-orbit camera makes
    // parallax and perspective scale change deterministic and measurable.
    add_lambert_box(scene, 1, "near_object", {1.35f, 1.35f, 1.0f}, {-0.55f, 0.f, 0.85f}, 0xe8543c);
    add_lambert_sphere(scene, 2, "middle_object", 0.68f, {0.25f, 0.02f, -0.10f}, 0x43bd78);
    add_lambert_box(scene, 3, "far_object", {0.92f, 0.92f, 0.72f}, {1.05f, -0.02f, -1.25f}, 0x477bd2);
    auto& ambient = scene.addAmbientLight(90, "ambient");
    ambient.intensity = 1.f;
    scene.addDirectionalLight(91, "key").intensity = 0.3f;
    camera.setTarget({0.f, 0.f, 0.f}).setFov(45.f);
    camera.positionTrack().add(0.f, {-1.15f, 0.15f, 7.3f});
    camera.positionTrack().add(1.25f, {-0.55f, 0.30f, 6.7f});
    camera.positionTrack().add(2.5f, {0.0f, 0.42f, 6.1f});
    camera.positionTrack().add(3.75f, {0.55f, 0.32f, 5.6f});
    camera.positionTrack().add(5.f, {1.05f, 0.12f, 5.2f});
    camera.targetTrack().add(0.f, {-0.1f, 0.f, 0.f});
    camera.targetTrack().add(5.f, {0.1f, 0.f, 0.f});
}

int run(const std::string& id, const fs::path& out) {
    fs::create_directories(out);
    chrononmotion::motion::MotionScene scene(id + "_camera_geometry", float(FPS));
    chrononmotion::motion::CameraRig camera(700, 45.f, float(W) / H, 0.1f, 100.f);
    if (id == "T01") prepare_t01(scene, camera);
    else prepare_t09(scene, camera);

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const char* gl = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glName = gl ? gl : "unknown";
    chrononmotion::certification::require_nvidia_renderer(glName);
    chrononmotion::certification::require_nvenc_encoder();
    renderer.setClearColor(Color(0x080b12));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.f;
    threepp::motion::MotionSceneBridge bridge(45.f, float(W) / H, 0.1f, 100.f);
    bridge.evaluateAndSync(scene, 0, &camera);

    const std::string filename = id == "T01" ? "T01_geometry_camera_1080p30.mp4" : "T09_camera_depth_1080p30.mp4";
    chrononmotion::certification::GlCudaNvencEncoder encoder(W, H, FPS, (out / filename).string());
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(scene, f, &camera);
        renderer.render(*bridge.scene(), *bridge.camera());
        encoder.encode_current_target(f);
    }
    encoder.finish();
    if (encoder.frames_submitted() != FRAMES || encoder.packets_written() != FRAMES)
        throw std::runtime_error(id + " did not submit/mux every NVENC frame");

    std::array<std::vector<unsigned char>, 3> samples;
    std::vector<unsigned char> frame77;
    std::array<unsigned char, 3> bg{};
    size_t minCoverage = std::numeric_limits<size_t>::max(), maxCoverage = 0;
    std::array<size_t, 6> facePixelCounts{};
    std::array<std::array<size_t, 3>, 3> t09ObjectPixels{};
    std::array<std::array<PixelBounds, 3>, 3> t09ObjectBounds{};
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(scene, f, &camera);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto rgb = renderer.readRGBPixels();
        if (rgb.size() != size_t(W) * H * 3) throw std::runtime_error("invalid diagnostic framebuffer size");
        if (f == 0) bg = {rgb[0], rgb[1], rgb[2]};
        const size_t cov = coverage(rgb, bg);
        minCoverage = std::min(minCoverage, cov);
        maxCoverage = std::max(maxCoverage, cov);
        if (f == 0) samples[0] = rgb;
        if (f == 75) { samples[1] = rgb; write_png(out / "frame-75.png", rgb); }
        if (f == 149) { samples[2] = rgb; write_png(out / "frame-149.png", rgb); }
        if (f == 0) write_png(out / "frame-0.png", rgb);
        if (f == 77) frame77 = rgb;

        if (id == "T01") {
            // The emissive face colors remain identifiable after tone mapping;
            // accumulate nearest palette hits while excluding the background.
            const std::array<std::array<int, 3>, 6> palette{{
                {{255,56,47}}, {{0,216,199}}, {{68,224,95}},
                {{236,66,220}}, {{55,124,255}}, {{255,206,54}}
            }};
            for (size_t p = 0; p < rgb.size(); p += 3) {
                if (std::abs(int(rgb[p])-bg[0])+std::abs(int(rgb[p+1])-bg[1])+std::abs(int(rgb[p+2])-bg[2]) < 30) continue;
                int best = 100000; size_t face = 0;
                for (size_t c = 0; c < palette.size(); ++c) {
                    const int d = std::abs(int(rgb[p])-palette[c][0]) + std::abs(int(rgb[p+1])-palette[c][1]) + std::abs(int(rgb[p+2])-palette[c][2]);
                    if (d < best) { best = d; face = c; }
                }
                if (best < 220) ++facePixelCounts[face];
            }
        } else if (f == 0 || f == 75 || f == 149) {
            const size_t sample = f == 0 ? 0 : (f == 75 ? 1 : 2);
            for (size_t p = 0; p < rgb.size(); p += 3) {
                const int r = rgb[p], g = rgb[p + 1], b = rgb[p + 2];
                if (std::abs(r - bg[0]) + std::abs(g - bg[1]) + std::abs(b - bg[2]) < 35) continue;
                const size_t pixel = p / 3;
                const int x = int(pixel % W), y = int(pixel / W);
                if (r > g * 1.20 && r > b * 1.10) { ++t09ObjectPixels[0][sample]; t09ObjectBounds[0][sample].add(x, y); }
                if (g > r * 1.15 && g > b * 1.05) { ++t09ObjectPixels[1][sample]; t09ObjectBounds[1][sample].add(x, y); }
                if (b > r * 1.15 && b > g * 1.02) { ++t09ObjectPixels[2][sample]; t09ObjectBounds[2][sample].add(x, y); }
            }
        }
    }
    bridge.evaluateAndSync(scene, 77, &camera);
    renderer.render(*bridge.scene(), *bridge.camera());
    const bool randomAccess = renderer.readRGBPixels() == frame77;
    const size_t d0 = frame_delta(samples[0], samples[1]);
    const size_t d1 = frame_delta(samples[1], samples[2]);
    const auto firstPose = camera.sample(0.f);
    const auto middlePose = camera.sample(2.5f);
    const auto lastPose = camera.sample(149.f / FPS);
    float cameraTravel = 0.f;
    auto previousCameraPosition = firstPose.position;
    for (int f = 1; f < FRAMES; ++f) {
        const auto position = camera.sample(float(f) / FPS).position;
        cameraTravel += (position - previousCameraPosition).length();
        previousCameraPosition = position;
    }
    const float focalChange = std::abs((firstPose.position - firstPose.target).length() -
                                       (middlePose.position - middlePose.target).length());
    float nearDepth = 0.f, middleDepth = 0.f, farDepth = 0.f;
    bool depthOrder = true;
    size_t maxProjectedOcclusionOverlap = 0;
    if (id == "T09") {
        auto forward = middlePose.target - middlePose.position;
        forward.normalize();
        nearDepth = (chrononmotion::Vector3(-0.55f, 0.f, 0.85f) - middlePose.position).dot(forward);
        middleDepth = (chrononmotion::Vector3(0.25f, 0.02f, -0.10f) - middlePose.position).dot(forward);
        farDepth = (chrononmotion::Vector3(1.05f, -0.02f, -1.25f) - middlePose.position).dot(forward);
        depthOrder = nearDepth > 0.f && nearDepth < middleDepth && middleDepth < farDepth && farDepth < camera.farPlane();
        for (size_t sample = 0; sample < 3; ++sample)
            for (size_t a = 0; a < 3; ++a)
                for (size_t b = a + 1; b < 3; ++b)
                    maxProjectedOcclusionOverlap = std::max(maxProjectedOcclusionOverlap,
                        t09ObjectBounds[a][sample].overlap(t09ObjectBounds[b][sample]));
    }
    if (minCoverage < size_t(W) * H / 30 || maxCoverage <= minCoverage ||
        d0 < 50000 || d1 < 50000 || !randomAccess)
        throw std::runtime_error(id + " failed coverage, temporal movement, or random-access checks");
    if (id == "T01" && *std::min_element(facePixelCounts.begin(), facePixelCounts.end()) < 1500) {
        std::cerr << "T01 face palette hits=" << facePixelCounts[0] << ',' << facePixelCounts[1] << ','
                  << facePixelCounts[2] << ',' << facePixelCounts[3] << ',' << facePixelCounts[4] << ','
                  << facePixelCounts[5] << '\n';
        throw std::runtime_error("T01 failed: one or more of six cube faces were not visibly sampled");
    }
    if (id == "T09" && (cameraTravel < 1.0f || focalChange < 0.25f || !depthOrder || maxProjectedOcclusionOverlap < 1000))
        throw std::runtime_error("T09 failed: camera dolly, clipping, depth ordering, or projected occlusion is invalid");
    if (id == "T09") {
        for (size_t object = 0; object < 3; ++object)
            for (size_t sample = 0; sample < 3; ++sample)
                if (t09ObjectPixels[object][sample] < 1000)
                    throw std::runtime_error("T09 failed: a depth subject is absent from one of frames 0/75/149");
    }

    std::ofstream config(out / "scene.json");
    config << "{\n  \"test_id\": \"" << id << "\",\n  \"source\": \"Chronon MotionScene -> threepp MotionSceneBridge\",\n"
           << "  \"camera\": {\"fov_degrees\": " << camera.fov() << ", \"near\": " << camera.nearPlane()
           << ", \"far\": " << camera.farPlane() << "},\n  \"duration_frames\": 150,\n  \"fps\": 30,\n"
           << "  \"layers\": " << scene.layerCount() << ",\n  \"lights\": " << scene.lightCount() << "\n}\n";
    std::ofstream meta(out / "metadata.json");
    meta << std::fixed << std::setprecision(4)
         << "{\n  \"test_id\": \"" << id << "\",\n  \"status\": \"PARTIAL\",\n"
         << "  \"technical_checks_passed\": true,\n  \"pipeline\": \"MotionScene -> MotionSceneBridge -> threepp GLRenderer -> GL PBO -> CUDA -> NVENC\",\n"
         << "  \"width\": 1920,\n  \"height\": 1080,\n  \"fps\": 30,\n  \"frames\": 150,\n"
         << "  \"gl_renderer\": \"" << glName << "\",\n  \"gpu_device\": \"" << encoder.device_name() << "\",\n"
         << "  \"video_encoder\": \"h264_nvenc\",\n  \"frames_submitted\": " << encoder.frames_submitted()
         << ",\n  \"packets_muxed\": " << encoder.packets_written()
         << ",\n  \"cpu_framebuffer_readback_in_encode_path\": false"
         << ",\n  \"diagnostic_cpu_readback_passes\": 151"
         << ",\n  \"coverage_min\": " << minCoverage << ",\n  \"coverage_max\": " << maxCoverage
         << ",\n  \"sampled_frame_differences\": [" << d0 << "," << d1 << "]"
         << ",\n  \"random_access_frame_77_identical\": " << (randomAccess ? "true" : "false")
         << ",\n  \"camera_travel_world_units\": " << cameraTravel
         << ",\n  \"camera_distance_change_world_units\": " << focalChange;
    if (id == "T01") meta << ",\n  \"cube_face_palette_hits\": [" << facePixelCounts[0] << ',' << facePixelCounts[1] << ',' << facePixelCounts[2] << ',' << facePixelCounts[3] << ',' << facePixelCounts[4] << ',' << facePixelCounts[5] << ']';
    if (id == "T09") meta << ",\n  \"camera_space_depths_near_mid_far\": [" << nearDepth << ',' << middleDepth << ',' << farDepth << ']'
                          << ",\n  \"depth_order_valid\": " << (depthOrder ? "true" : "false")
                          << ",\n  \"object_mask_pixels_frames_0_75_149\": [[" << t09ObjectPixels[0][0] << ',' << t09ObjectPixels[0][1] << ',' << t09ObjectPixels[0][2]
                          << "],[" << t09ObjectPixels[1][0] << ',' << t09ObjectPixels[1][1] << ',' << t09ObjectPixels[1][2]
                          << "],[" << t09ObjectPixels[2][0] << ',' << t09ObjectPixels[2][1] << ',' << t09ObjectPixels[2][2] << "]]"
                          << ",\n  \"max_projected_object_bounds_overlap_pixels\": " << maxProjectedOcclusionOverlap;
    meta << ",\n  \"gpu_resident_zero_copy\": false,\n  \"vram_bytes\": null\n}\n";
    if (!meta || !config) throw std::runtime_error("failed to write T01/T09 metadata or scene config");
    renderer.dispose();
    std::cout << id << " PARTIAL GPU encode verified; coverage=" << minCoverage << ".." << maxCoverage
              << " diffs=" << d0 << ',' << d1 << " random=" << randomAccess << '\n';
    return 0;
}
}

int main(int argc, char** argv) try {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)argc; (void)argv;
    throw std::runtime_error("T01/T09 require NVIDIA GL plus GL PBO -> CUDA -> NVENC; CPU fallback is disabled");
#else
    chrononmotion::certification::prepare_nvidia_egl_headless();
    if (argc < 3) throw std::invalid_argument("usage: chrononmotion_render_camera_certification T01|T09 output_dir");
    const std::string id = argv[1];
    if (id != "T01" && id != "T09") throw std::invalid_argument("test id must be T01 or T09");
    return run(id, argv[2]);
#endif
} catch (const std::exception& e) {
    std::cerr << "Camera certification FAIL: " << e.what() << '\n';
    return 1;
}
