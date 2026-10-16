// T05: Chronon-authored moving light and shadow-casting geometry through the
// production MotionSceneBridge -> threepp shadow-map renderer path.
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
#include <sys/wait.h>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#include "stb_image_write.h"

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "gpu_certification_gate.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/lights/Light.hpp"
#include "threepp/lights/light_interfaces.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"

using namespace threepp;
namespace fs = std::filesystem;
namespace {
    constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;

    void writePng(const fs::path& path, const std::vector<unsigned char>& rgb) {
        std::vector<unsigned char> top(rgb.size());
        const std::size_t stride = std::size_t(W) * 3;
        for (int y = 0; y < H; ++y) {
            std::copy_n(rgb.data() + std::size_t(H - y - 1) * stride, stride,
                        top.data() + std::size_t(y) * stride);
        }
        if (!stbi_write_png(path.c_str(), W, H, 3, top.data(), int(stride))) {
            throw std::runtime_error("failed to write PNG: " + path.string());
        }
    }

    std::string shellQuote(const std::string& value) {
        std::string result = "'";
        for (char c : value) result += c == '\'' ? "'\\''" : std::string(1, c);
        return result + "'";
    }

    struct ShadowMask {
        std::vector<unsigned char> pixels;
        std::size_t count{};
        double centerX{};
        double centerY{};
    };

    ShadowMask makeShadowMask(const std::vector<unsigned char>& lit,
                              const std::vector<unsigned char>& shadowed) {
        ShadowMask result;
        result.pixels.resize(std::size_t(W) * H);
        long long sumX = 0, sumY = 0;
        for (int row = 0; row < H; ++row) {
            const int screenY = H - 1 - row;
            if (screenY < 470 || screenY > 1020) continue;
            for (int x = 60; x < W - 60; ++x) {
                const std::size_t pixel = std::size_t(row) * W + std::size_t(x);
                const std::size_t i = pixel * 3;
                const int delta = std::abs(int(lit[i]) - int(shadowed[i])) +
                                  std::abs(int(lit[i + 1]) - int(shadowed[i + 1])) +
                                  std::abs(int(lit[i + 2]) - int(shadowed[i + 2]));
                if (delta < 45) continue;
                result.pixels[pixel] = 1;
                ++result.count;
                sumX += x;
                sumY += screenY;
            }
        }
        if (result.count) {
            result.centerX = double(sumX) / double(result.count);
            result.centerY = double(sumY) / double(result.count);
        }
        return result;
    }

    std::size_t maskDifference(const ShadowMask& a, const ShadowMask& b) {
        std::size_t result = 0;
        for (std::size_t i = 0; i < a.pixels.size(); ++i) result += a.pixels[i] != b.pixels[i];
        return result;
    }
}

int main(int argc, char** argv) try {
    const fs::path output = argc > 1 ? argv[1] : "out/threepp_certification/T05";
    fs::create_directories(output);

    chrononmotion::motion::MotionScene motion("T05_dynamic_shadow_mapping", float(FPS));
    chrononmotion::motion::MeshAttachment groundMesh;
    groundMesh.geometry = chrononmotion::BoxGeometry::create(9.f, 0.12f, 9.f);
    groundMesh.material = chrononmotion::MaterialDescriptor::lambert(501, chrononmotion::Color(0xb7b8bd));
    auto& ground = motion.addMesh(50, "shadow_receiver_floor", std::move(groundMesh));
    ground.transform.position = {0.f, -1.05f, 0.f};
    ground.castsShadow = false;
    ground.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment blockMesh;
    blockMesh.geometry = chrononmotion::BoxGeometry::create(1.25f, 1.30f, 1.25f);
    blockMesh.material = chrononmotion::MaterialDescriptor::lambert(502, chrononmotion::Color(0xe58a48));
    auto& block = motion.addMesh(51, "orange_shadow_caster", std::move(blockMesh));
    block.transform.position = {-0.65f, -0.40f, 0.1f};
    block.castsShadow = true;
    block.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment sphereMesh;
    sphereMesh.geometry = chrononmotion::SphereGeometry::create(0.62f, 48, 32);
    sphereMesh.material = chrononmotion::MaterialDescriptor::lambert(503, chrononmotion::Color(0x48a9bd));
    auto& sphere = motion.addMesh(52, "teal_shadow_caster", std::move(sphereMesh));
    sphere.transform.position = {0.95f, -0.43f, -0.15f};
    sphere.castsShadow = true;
    sphere.acceptsShadow = true;

    auto& fill = motion.addAmbientLight(500, "fixed_ambient_fill");
    fill.color = chrononmotion::Color(0xffffff);
    fill.intensity = 0.32f;

    auto& key = motion.addDirectionalLight(504, "animated_shadow_key");
    key.color = chrononmotion::Color(0xffedcf);
    key.intensity = 2.8f;
    key.castsShadow = true;
    key.shadowBias = -0.00015f;
    key.shadowNormalBias = 0.025f;
    key.shadowRadius = 3.f;
    key.positionTrack.add(0.f, {-3.5f, 7.f, 3.4f});
    key.positionTrack.add(2.5f, {3.5f, 7.f, 3.4f});
    key.positionTrack.add(5.f, {-3.5f, 7.f, 3.4f});
    key.directionTrack.add(0.f, {3.5f, -7.f, -3.4f});
    key.directionTrack.add(2.5f, {-3.5f, -7.f, -3.4f});
    key.directionTrack.add(5.f, {3.5f, -7.f, -3.4f});

    threepp::motion::MotionSceneBridge bridge(42.f, float(W) / float(H), 0.1f, 100.f);
    bridge.camera()->position.set(5.3f, 4.6f, 7.6f);
    bridge.camera()->lookAt(0.f, -0.35f, 0.f);
    bridge.evaluateAndSync(motion, 0);

    std::shared_ptr<DirectionalLight> shadowLight;
    bridge.scene()->traverse([&](Object3D& object) {
        if (auto* directional = dynamic_cast<DirectionalLight*>(&object)) {
            shadowLight = std::shared_ptr<DirectionalLight>(directional, [](DirectionalLight*) {});
        }
    });
    if (!shadowLight || !shadowLight->castShadow || !shadowLight->shadow) {
        throw std::runtime_error("MotionSceneBridge did not create a shadow-casting directional light");
    }
    shadowLight->shadow->mapSize.set(1024.f, 1024.f);
    if (std::abs(shadowLight->shadow->bias - key.shadowBias) > 1e-7f ||
        std::abs(shadowLight->shadow->normalBias - key.shadowNormalBias) > 1e-7f ||
        std::abs(shadowLight->shadow->radius - key.shadowRadius) > 1e-7f) {
        throw std::runtime_error("MotionSceneBridge did not sync authored shadow bias/radius values");
    }

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* initialRendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = initialRendererName ? initialRendererName : "unknown";
    chrononmotion::certification::require_nvidia_renderer(glRenderer);
    const char* requireHardware = std::getenv("CHRONON_CERT_REQUIRE_HARDWARE_GPU");
    if (requireHardware && std::string(requireHardware) == "1" &&
        (glRenderer.find("llvmpipe") != std::string::npos ||
         glRenderer.find("softpipe") != std::string::npos ||
         glRenderer.find("Software") != std::string::npos)) {
        throw std::runtime_error("hardware GPU required but GL selected: " + glRenderer);
    }
    renderer.setClearColor(Color(0x111823));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.f;
    renderer.shadowMap().enabled = true;
    renderer.shadowMap().type = ShadowMap::PFCSoft;

    const fs::path videoPath = output / "T05_dynamic_shadows_1080p30.mp4";
    const std::string videoEncoder = chrononmotion::certification::require_nvenc_encoder();
    const std::string encodeArgs = "-c:v h264_nvenc -preset p5 -rc vbr -cq 18 -b:v 0 -pix_fmt yuv420p ";
    const std::string ffmpeg = "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size 1920x1080 -framerate 30 -i - -an " + encodeArgs + shellQuote(videoPath.string());
    FILE* pipe = popen(ffmpeg.c_str(), "w");
    if (!pipe) throw std::runtime_error("could not start ffmpeg");

    std::array<std::vector<unsigned char>, 3> shadowedFrames;
    std::vector<unsigned char> sequentialFrame77;
    const std::array<unsigned, 3> probeFrames{0, 75, 149};
    const auto start = std::chrono::steady_clock::now();
    for (unsigned frame = 0; frame < FRAMES; ++frame) {
        bridge.evaluateAndSync(motion, int(frame));
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != std::size_t(W) * H * 3) throw std::runtime_error("unexpected T05 RGB readback size");
        if (frame == 77) sequentialFrame77 = pixels;
        for (std::size_t i = 0; i < probeFrames.size(); ++i) {
            if (frame == probeFrames[i]) {
                shadowedFrames[i] = pixels;
                writePng(output / ("frame-" + std::to_string(frame) + ".png"), pixels);
            }
        }
        if (std::fwrite(pixels.data(), 1, pixels.size(), pipe) != pixels.size()) {
            pclose(pipe);
            throw std::runtime_error("ffmpeg pipe closed before all T05 frames were written");
        }
    }
    const auto sequentialDone = std::chrono::steady_clock::now();

    std::array<ShadowMask, 3> masks;
    for (std::size_t i = 0; i < probeFrames.size(); ++i) {
        bridge.evaluateAndSync(motion, int(probeFrames[i]));
        renderer.shadowMap().enabled = false;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto noShadow = renderer.readRGBPixels();
        renderer.shadowMap().enabled = true;
        renderer.render(*bridge.scene(), *bridge.camera());
        const auto restored = renderer.readRGBPixels();
        if (restored != shadowedFrames[i]) throw std::runtime_error("T05 shadow toggle was not deterministic");
        masks[i] = makeShadowMask(noShadow, shadowedFrames[i]);
        if (i == 1) writePng(output / "frame-75-no-shadows.png", noShadow);
    }

    bridge.evaluateAndSync(motion, 77);
    renderer.render(*bridge.scene(), *bridge.camera());
    const bool randomAccess = renderer.readRGBPixels() == sequentialFrame77;
    const auto movingMaskPixels = maskDifference(masks[0], masks[1]);
    const double shadowCentroidMotion = std::hypot(masks[0].centerX - masks[1].centerX,
                                                   masks[0].centerY - masks[1].centerY);
    const int encoder = pclose(pipe);
    if (encoder == -1 || !WIFEXITED(encoder) || WEXITSTATUS(encoder) != 0) {
        throw std::runtime_error("ffmpeg failed to encode T05 video");
    }

    const bool shadowPresent = std::all_of(masks.begin(), masks.end(), [](const ShadowMask& mask) {
        return mask.count > 1000;
    });
    const bool shadowMoves = movingMaskPixels > 2000 && shadowCentroidMotion > 35.0;
    if (!shadowPresent || !shadowMoves || !randomAccess) {
        throw std::runtime_error("T05 acceptance failed: shadow missing, stationary or random access differs");
    }

    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream sceneFile(output / "scene.json");
    sceneFile << motion.toJson(2) << '\n';
    std::ofstream metadata(output / "metadata.json");
    metadata << std::fixed << std::setprecision(6)
             << "{\n  \"test_id\": \"T05\",\n  \"status\": \"" << "PARTIAL" << "\",\n  \"technical_checks_passed\": true,\n"
             << "  \"pipeline\": \"MotionScene shadow flags and animated light -> MotionSceneBridge -> threepp GLShadowMap PFCSoft -> GLRenderer -> ffmpeg\",\n"
             << "  \"width\": " << W << ",\n  \"height\": " << H << ",\n  \"fps\": " << FPS
             << ",\n  \"frames\": " << FRAMES << ",\n  \"duration_seconds\": 5.0,\n"
             << "  \"shadow_map_resolution\": [1024,1024],\n  \"shadow_bias\": " << key.shadowBias
             << ",\n  \"shadow_normal_bias\": " << key.shadowNormalBias << ",\n  \"shadow_radius\": " << key.shadowRadius << ",\n"
             << "  \"shadow_mask_pixels_frame_0_75_149\": [" << masks[0].count << "," << masks[1].count << "," << masks[2].count << "],\n"
             << "  \"shadow_mask_symmetric_difference_0_75\": " << movingMaskPixels << ",\n"
             << "  \"shadow_centroid_motion_0_75_pixels\": " << shadowCentroidMotion << ",\n"
             << "  \"shadow_centroid_frame_0\": [" << masks[0].centerX << "," << masks[0].centerY << "],\n"
             << "  \"shadow_centroid_frame_75\": [" << masks[1].centerX << "," << masks[1].centerY << "],\n"
             << "  \"random_access_frame_77_identical\": " << (randomAccess ? "true" : "false") << ",\n"
             << "  \"render_wall_seconds\": " << std::chrono::duration<double>(sequentialDone - start).count()
             << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss << ",\n"
             << "  \"gl_renderer\": \"" << glRenderer
             << "\",\n  \"gl_version\": \"" << (glVersion ? glVersion : "unknown")
             << "\",\n  \"video_encoder\": \"" << videoEncoder
             << "\",\n  \"hardware_gpu_render\": "
             << ((glRenderer.find("llvmpipe") == std::string::npos &&
                  glRenderer.find("softpipe") == std::string::npos &&
                  glRenderer.find("Software") == std::string::npos) ? "true" : "false")
             << ",\n  \"cpu_framebuffer_readback_before_encode\": true"
             << ",\n  \"gpu_resident_zero_copy\": false"
             << ",\n  \"vram_bytes\": null\n}\n";
    renderer.dispose();
    std::cout << "T05 PARTIAL (CPU framebuffer readback before encode) shadow mask pixels=" << masks[0].count << ',' << masks[1].count << ',' << masks[2].count
              << " symmetric_difference_0_75=" << movingMaskPixels
              << " centroid_motion=" << shadowCentroidMotion
              << " random_access=" << randomAccess << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "T05 FAIL: " << error.what() << '\n';
    return 1;
}
