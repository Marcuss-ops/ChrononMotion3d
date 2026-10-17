// T11 certification scene: MotionScene -> production bridge -> GL -> Bloom.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
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
#include <utility>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include "gpu_certification_gate.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/postprocessing/UnrealBloomPass.hpp"
#include "threepp/renderers/GLRenderer.hpp"

#include "glad/glad.h"
#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif

namespace fs = std::filesystem;
using namespace threepp;

namespace {

    constexpr unsigned kWidth = 1920;
    constexpr unsigned kHeight = 1080;
    constexpr unsigned kFps = 30;
    constexpr unsigned kFrames = 150;

    std::string shellQuote(const std::string& value) {
        std::string result{"'"};
        for (const char c : value) {
            if (c == '\'') result += "'\\''";
            else
                result += c;
        }
        result += '\'';
        return result;
    }

    void writePng(const fs::path& path, const std::vector<unsigned char>& rgb) {
        std::vector<unsigned char> topDown(rgb.size());
        const std::size_t rowBytes = std::size_t(kWidth) * 3;
        for (unsigned y = 0; y < kHeight; ++y) {
            std::copy_n(rgb.data() + std::size_t(kHeight - 1 - y) * rowBytes,
                        rowBytes, topDown.data() + std::size_t(y) * rowBytes);
        }
        if (stbi_write_png(path.c_str(), static_cast<int>(kWidth), static_cast<int>(kHeight),
                           3, topDown.data(), static_cast<int>(rowBytes)) == 0) {
            throw std::runtime_error("failed to write PNG snapshot: " + path.string());
        }
    }

    std::size_t countChangedPixels(const std::vector<unsigned char>& pixels,
                                   const std::array<unsigned char, 3>& background) {
        std::size_t count = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 3) {
            if (pixels[i] != background[0] || pixels[i + 1] != background[1] || pixels[i + 2] != background[2]) ++count;
        }
        return count;
    }

    std::size_t countFrameDiff(const std::vector<unsigned char>& a,
                               const std::vector<unsigned char>& b) {
        if (a.size() != b.size()) return std::numeric_limits<std::size_t>::max();
        std::size_t count = 0;
        for (std::size_t i = 0; i < a.size(); i += 3) {
            const int delta = std::abs(int(a[i]) - int(b[i])) +
                              std::abs(int(a[i + 1]) - int(b[i + 1])) +
                              std::abs(int(a[i + 2]) - int(b[i + 2]));
            if (delta > 3) ++count;
        }
        return count;
    }

}// namespace

int main(int argc, char** argv) {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)argc;
    (void)argv;
    throw std::runtime_error("T11 requires the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    chrononmotion::certification::prepare_nvidia_egl_headless();
    const fs::path outputDir = argc > 1 ? argv[1] : "out/threepp_certification/T11";
    fs::create_directories(outputDir);
    const fs::path videoPath = outputDir / "T11_bloom_1080p30.mp4";
    const fs::path metadataPath = outputDir / "metadata.json";

    chrononmotion::motion::MotionScene motionScene("T11_threepp_bloom", float(kFps));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.geometry = chrononmotion::BoxGeometry::create(0.8f, 0.8f, 0.8f);
    attachment.material = chrononmotion::MaterialDescriptor::emissive(
            1, chrononmotion::Color(0x16aaff), 1.6f);
    auto& cube = motionScene.addMesh(1, "emissive_cube", std::move(attachment));
    cube.tracks.position.add(0.f, chrononmotion::Vector3(-1.1f, 0.f, 0.f));
    cube.tracks.position.add(2.5f, chrononmotion::Vector3(0.f, 0.35f, 0.f));
    cube.tracks.position.add(5.f, chrononmotion::Vector3(1.1f, 0.f, 0.f));
    cube.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
    cube.tracks.rotation.add(5.f, chrononmotion::Vector3(1.2f, 6.2831853f, 0.5f));

    chrononmotion::motion::CameraRig cameraRig(
            1, 42.f, float(kWidth) / float(kHeight), 0.1f, 50.f);
    cameraRig.setPosition({0.f, 0.f, 4.3f}).setTarget({0.f, 0.f, 0.f});

    Canvas canvas(Canvas::Parameters().size(kWidth, kHeight).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = rendererName ? rendererName : "unknown";
    chrononmotion::certification::require_nvidia_renderer(glRenderer);
    renderer.setClearColor(Color(0x080a12));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.f;
    threepp::motion::MotionSceneBridge bridge(
            42.f, float(kWidth) / float(kHeight), 0.1f, 50.f);
    EffectComposer::Options composerOptions;
    composerOptions.samples = 4;
    EffectComposer composer(renderer, composerOptions);
    composer.setSize(kWidth, kHeight);
    composer.addPass(std::make_shared<RenderPass>(*bridge.scene(), *bridge.camera()));
    composer.addPass(std::make_shared<UnrealBloomPass>(
            Vector2(float(kWidth), float(kHeight)), 0.55f, 0.08f, 0.15f));

    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder gpuEncoder(kWidth, kHeight, kFps, videoPath.string());
    for (unsigned frame = 0; frame < kFrames; ++frame) {
        bridge.evaluateAndSync(motionScene, static_cast<int>(frame), &cameraRig);
        composer.render(1.f / float(kFps));
        gpuEncoder.encode_current_target(frame);
    }
    gpuEncoder.finish();
    if (gpuEncoder.frames_submitted() != int(kFrames) || gpuEncoder.packets_written() != int(kFrames))
        throw std::runtime_error("T11 GPU encoder did not submit and mux all frames");

    const auto start = std::chrono::steady_clock::now();
    std::array<unsigned char, 3> background{};
    std::vector<unsigned char> referenceFrame;
    std::vector<unsigned char> firstFrame;
    std::vector<unsigned char> middleFrame;
    std::vector<unsigned char> lastFrame;
    std::size_t minCoverage = std::numeric_limits<std::size_t>::max();
    std::size_t maxCoverage = 0;
    for (unsigned frame = 0; frame < kFrames; ++frame) {
        bridge.evaluateAndSync(motionScene, static_cast<int>(frame), &cameraRig);
        composer.render(1.f / float(kFps));
        auto pixels = composer.readRGBPixels();
        if (pixels.size() != std::size_t(kWidth) * kHeight * 3)
            throw std::runtime_error("unexpected diagnostic RGB readback size");
        if (frame == 0) background = {pixels[0], pixels[1], pixels[2]};
        const auto coverage = countChangedPixels(pixels, background);
        minCoverage = std::min(minCoverage, coverage);
        maxCoverage = std::max(maxCoverage, coverage);
        if (frame == 0 || frame == 75 || frame == 149) {
            writePng(outputDir / ("frame-" + std::to_string(frame) + ".png"), pixels);
        }
        if (frame == 0) firstFrame = pixels;
        if (frame == 75) middleFrame = pixels;
        if (frame == 149) lastFrame = pixels;
        if (frame == 77) referenceFrame = pixels;
    }
    const auto sequentialDone = std::chrono::steady_clock::now();

    auto bloomPass = composer.passes().back();
    bloomPass->enabled = false;
    bridge.evaluateAndSync(motionScene, 75, &cameraRig);
    composer.render(1.f / float(kFps));
    const auto noBloomFrame = composer.readRGBPixels();
    bloomPass->enabled = true;
    const std::size_t bloomDiffPixels = countFrameDiff(middleFrame, noBloomFrame);
    writePng(outputDir / "frame-75-no-bloom.png", noBloomFrame);

    bridge.evaluateAndSync(motionScene, 77, &cameraRig);
    composer.render(1.f / float(kFps));
    const auto randomAccessFrame = composer.readRGBPixels();
    const bool deterministic = randomAccessFrame == referenceFrame;

    const bool animated = firstFrame != lastFrame;
    std::cout << "T11 metrics: coverage=" << minCoverage << ".." << maxCoverage
              << " bloom_changed_pixels=" << bloomDiffPixels
              << " random_access=" << deterministic << " animated=" << animated << std::endl;
    if (!deterministic || !animated || minCoverage < 1000 || maxCoverage <= minCoverage ||
        maxCoverage > std::size_t(kWidth) * kHeight * 3 / 4 || bloomDiffPixels < 100) {
        throw std::runtime_error("T11 acceptance failed: coverage, animation, bloom, or random-access check");
    }

    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const double renderSeconds = std::chrono::duration<double>(sequentialDone - start).count();
    const auto* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream scene(outputDir / "scene.json");
    scene << motionScene.toJson(2) << '\n';
    if (!scene) throw std::runtime_error("failed to write scene configuration");
    std::ofstream metadata(metadataPath);
    metadata << std::fixed << std::setprecision(3)
             << "{\n  \"test_id\": \"T11\",\n  \"status\": \"" << "PARTIAL" << "\",\n  \"technical_checks_passed\": true,\n"
             << "  \"pipeline\": \"MotionScene -> MotionSceneBridge -> threepp GLRenderer -> EffectComposer -> UnrealBloomPass -> GL PBO -> CUDA -> NVENC\",\n"
             << "  \"width\": " << kWidth << ",\n  \"height\": " << kHeight
             << ",\n  \"fps\": " << kFps << ",\n  \"frames\": " << kFrames
             << ",\n  \"duration_seconds\": 5.0,\n  \"tone_mapping\": \"ACESFilmic\",\n  \"msaa_samples\": 4,\n  \"random_access_frame_77_identical\": "
             << (deterministic ? "true" : "false") << ",\n  \"changed_pixels_min\": " << minCoverage
             << ",\n  \"changed_pixels_max\": " << maxCoverage
             << ",\n  \"bloom_vs_no_bloom_changed_pixels\": " << bloomDiffPixels
             << ",\n  \"frame_0_differs_from_frame_149\": " << (animated ? "true" : "false")
             << ",\n  \"render_wall_seconds\": " << renderSeconds
             << ",\n  \"cpu_user_seconds\": " << (double(usage.ru_utime.tv_sec) + double(usage.ru_utime.tv_usec) / 1.0e6)
             << ",\n  \"cpu_system_seconds\": " << (double(usage.ru_stime.tv_sec) + double(usage.ru_stime.tv_usec) / 1.0e6)
             << ",\n  \"peak_rss_kib\": " << usage.ru_maxrss
             << ",\n  \"gl_renderer\": \"" << glRenderer
             << "\",\n  \"gl_version\": \"" << (glVersion ? glVersion : "unknown")
             << "\",\n  \"video_encoder\": \"h264_nvenc\""
             << ",\n  \"gpu_device\": \"" << gpuEncoder.device_name() << "\""
             << ",\n  \"frames_submitted\": " << gpuEncoder.frames_submitted()
             << ",\n  \"packets_muxed\": " << gpuEncoder.packets_written()
             << ",\n  \"hardware_gpu_render\": "
             << ((glRenderer.find("llvmpipe") == std::string::npos &&
                  glRenderer.find("softpipe") == std::string::npos &&
                  glRenderer.find("Software") == std::string::npos) ? "true" : "false")
             << ",\n  \"cpu_framebuffer_readback_in_encode_path\": false"
             << ",\n  \"diagnostic_cpu_readback_passes\": 153"
             << ",\n  \"gpu_resident_zero_copy\": false"
             << ",\n  \"vram_bytes\": null,\n  \"vram_status\": \"not exposed by the GL backend; device telemetry is sampled separately\"\n}\n";
    if (!metadata) throw std::runtime_error("failed to write metadata");
    renderer.dispose();
    std::cout << "T11 PARTIAL (GPU-resident encode verified; CPU readbacks are diagnostics only): wrote " << videoPath << " with 1080p30, frame snapshots and metadata\n";
    return 0;
#endif
}
