// T10: Chronon camera focus distance -> threepp BokehPass -> encoded video.
#include "gpu_certification_gate.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
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
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif
#include "stb_image_write.h"

#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/postprocessing/BokehPass.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/renderers/GLRenderer.hpp"

using namespace threepp;
namespace fs = std::filesystem;
namespace {
    constexpr unsigned W = 1920, H = 1080, FPS = 30, FRAMES = 150;

    std::string shellQuote(const std::string& value) {
        std::string result{"'"};
        for (const char c : value) {
            if (c == '\'') result += "'\\''";
            else
                result += c;
        }
        return result + "'";
    }

    void writePng(const fs::path& path, const std::vector<unsigned char>& pixels) {
        std::vector<unsigned char> topDown(pixels.size());
        const std::size_t stride = std::size_t(W) * 3;
        for (unsigned y = 0; y < H; ++y) {
            std::copy_n(pixels.data() + std::size_t(H - 1 - y) * stride, stride,
                        topDown.data() + std::size_t(y) * stride);
        }
        if (!stbi_write_png(path.c_str(), int(W), int(H), 3, topDown.data(), int(stride))) {
            throw std::runtime_error("failed to write PNG snapshot");
        }
    }

    std::size_t changedPixels(const std::vector<unsigned char>& pixels,
                              const std::array<unsigned char, 3>& background) {
        std::size_t changed = 0;
        for (std::size_t i = 0; i < pixels.size(); i += 3) {
            if (pixels[i] != background[0] || pixels[i + 1] != background[1] || pixels[i + 2] != background[2]) ++changed;
        }
        return changed;
    }

    double roiSharpness(const std::vector<unsigned char>& rgb, int centerX, int centerY, int radius) {
        double gradient = 0.0;
        std::size_t edges = 0;
        const auto luminance = [&](int x, int y) {
            const std::size_t i = (std::size_t(y) * W + std::size_t(x)) * 3;
            return 0.2126 * rgb[i] + 0.7152 * rgb[i + 1] + 0.0722 * rgb[i + 2];
        };
        const int left = std::max(1, centerX - radius), right = std::min(int(W) - 2, centerX + radius);
        const int top = std::max(1, centerY - radius), bottom = std::min(int(H) - 2, centerY + radius);
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                gradient += std::abs(luminance(x + 1, y) - luminance(x, y));
                gradient += std::abs(luminance(x, y + 1) - luminance(x, y));
                edges += 2;
            }
        }
        return edges ? gradient / double(edges) : 0.0;
    }
}// namespace

int main(int argc, char** argv) try {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)argc;
    (void)argv;
    throw std::runtime_error("T10 requires the GL PBO -> CUDA -> NVENC build; CPU encoding fallback is disabled");
#else
    chrononmotion::certification::prepare_nvidia_egl_headless();
    const fs::path output = argc > 1 ? argv[1] : "out/threepp_certification/T10";
    fs::create_directories(output);

    chrononmotion::motion::MotionScene motion("T10_depth_of_field", float(FPS));
    struct Subject {
        const char* name;
        const char* file;
        float viewDepth;
        float centerX;
        float yaw;
    };
    const std::array<Subject, 3> subjects{{
            {"fox", "Fox.glb", 6.f, -2.57f, 0.24f},
            {"avocado", "Avocado.glb", 7.5f, 0.f, -0.32f},
            {"milk_truck", "CesiumMilkTruck.glb", 9.f, 2.58f, -0.18f}}};
    const fs::path assetDirectory = "render/spike/assets/threepp-certification";
    GLTFLoader loader;
    std::array<std::shared_ptr<Object3D>, 3> loadedAssets;
    std::array<Vector3, 3> sourceSizes{};
    std::array<float, 3> fitScales{};
    std::array<float, 3> frontDepths{};
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        const fs::path assetPath = assetDirectory / subjects[i].file;
        auto loaded = loader.load(assetPath);
        if (!loaded || !loaded->scene) throw std::runtime_error("GLTFLoader failed: " + assetPath.string());
        loadedAssets[i] = loaded->scene;
        Box3 bounds;
        bounds.setFromObject(*loadedAssets[i], true);
        Vector3 center;
        bounds.getCenter(center);
        bounds.getSize(sourceSizes[i]);
        const float sourceSpan = std::max({sourceSizes[i].x, sourceSizes[i].y, sourceSizes[i].z});
        if (!(sourceSpan > 1e-5f) || !std::isfinite(sourceSpan))
            throw std::runtime_error("GLB has invalid bounds: " + assetPath.string());
        const float desiredSpan = 1.5f * subjects[i].viewDepth / subjects[0].viewDepth;
        fitScales[i] = desiredSpan / sourceSpan;
        frontDepths[i] = subjects[i].viewDepth;
        const float rootZ = 7.5f - subjects[i].viewDepth - sourceSizes[i].z * fitScales[i] * 0.5f;
        chrononmotion::motion::MeshAttachment attachment;
        attachment.assetId = "dof_asset_" + std::to_string(i);
        auto& layer = motion.addMesh(std::uint32_t(i + 1), subjects[i].name, std::move(attachment));
        layer.transform.position = chrononmotion::Vector3(
                subjects[i].centerX - center.x * fitScales[i],
                -center.y * fitScales[i],
                rootZ - center.z * fitScales[i]);
        layer.transform.scale = chrononmotion::Vector3(fitScales[i], fitScales[i], fitScales[i]);
        layer.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, subjects[i].yaw, 0.f));
    }
    auto& ambient = motion.addAmbientLight(101, "dof_ambient");
    ambient.color = chrononmotion::Color(0xffffff);
    ambient.intensity = 0.75f;
    auto& key = motion.addDirectionalLight(102, "dof_key");
    key.color = chrononmotion::Color(0xffffff);
    key.intensity = 2.2f;
    key.transform.position = chrononmotion::Vector3(-3.f, 5.f, 5.f);

    chrononmotion::motion::CameraRig cameraRig(100, 42.f, float(W) / H, 0.1f, 100.f);
    cameraRig.setPosition({0.f, 0.f, 7.5f}).setTarget({0.f, 0.f, 0.f});
    cameraRig.setLegacyDefocusStrength(0.06f).setMaxBlur(48.f);
    const std::array<float, 3> focusDistances{6.f, 7.5f, 9.f};
    cameraRig.setFocusDistance(focusDistances[0]);
    cameraRig.focusDistanceTrack().add(0.f, focusDistances[0]);
    cameraRig.focusDistanceTrack().add(2.5f, focusDistances[1]);
    cameraRig.focusDistanceTrack().add(5.f, focusDistances[2]);

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = rendererName ? rendererName : "unknown";
    chrononmotion::certification::require_nvidia_renderer(glRenderer);
    renderer.setClearColor(Color(0x090d16));
    threepp::motion::MotionSceneBridge bridge(42.f, float(W) / H, 0.1f, 100.f);
    for (std::size_t i = 0; i < subjects.size(); ++i)
        bridge.bindAsset("dof_asset_" + std::to_string(i), loadedAssets[i]);
    EffectComposer::Options options;
    options.samples = 4;
    EffectComposer composer(renderer, options);
    composer.renderToScreen = false;
    composer.setSize(W, H);
    composer.addPass(std::make_shared<RenderPass>(*bridge.scene(), *bridge.camera()));
    auto bokeh = std::make_shared<BokehPass>(*bridge.scene(), *bridge.camera(), focusDistances[0], 0.008f, 0.02f);
    composer.addPass(bokeh);

    const fs::path videoPath = output / "T10_dof_1080p30.mp4";
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder gpuEncoder(W, H, FPS, videoPath.string());

    const std::array<int, 3> roiCenters{395, 960, 1337};
    std::array<std::array<double, 3>, 3> focusScores{};
    std::array<int, 3> sampleFrames{0, 75, 149};
    std::array<unsigned char, 3> background{};
    std::vector<unsigned char> reference77;
    std::size_t minCoverage = std::numeric_limits<std::size_t>::max(), maxCoverage = 0;
    const auto start = std::chrono::steady_clock::now();

    for (unsigned frame = 0; frame < FRAMES; ++frame) {
        bridge.evaluateAndSync(motion, int(frame), &cameraRig);
        const float seconds = float(frame) / float(FPS);
        bokeh->focus = cameraRig.sample(seconds).focusDistance;
        composer.render(1.f / float(FPS));
        renderer.setRenderTarget(&composer.readBuffer());
        gpuEncoder.encode_current_target(frame);
        renderer.setRenderTarget(nullptr);
    }
    gpuEncoder.finish();
    if (gpuEncoder.frames_submitted() != FRAMES || gpuEncoder.packets_written() != FRAMES)
        throw std::runtime_error("T10 GPU encoder did not submit and mux all frames");

    for (unsigned frame = 0; frame < FRAMES; ++frame) {
        bridge.evaluateAndSync(motion, int(frame), &cameraRig);
        const float seconds = float(frame) / float(FPS);
        bokeh->focus = cameraRig.sample(seconds).focusDistance;
        composer.render(1.f / float(FPS));
        const auto pixels = composer.readRGBPixels();
        if (pixels.size() != std::size_t(W) * H * 3) throw std::runtime_error("unexpected T10 diagnostic readback size");
        if (frame == 0) background = {pixels[0], pixels[1], pixels[2]};
        minCoverage = std::min(minCoverage, changedPixels(pixels, background));
        maxCoverage = std::max(maxCoverage, changedPixels(pixels, background));
        if (frame == 0 || frame == 75 || frame == 149) writePng(output / ("frame-" + std::to_string(frame) + ".png"), pixels);
        if (frame == 77) reference77 = pixels;

        for (std::size_t sample = 0; sample < sampleFrames.size(); ++sample) {
            if (frame != unsigned(sampleFrames[sample])) continue;
            for (std::size_t subject = 0; subject < roiCenters.size(); ++subject)
                focusScores[sample][subject] = roiSharpness(pixels, roiCenters[subject], int(H / 2), 42);
        }
    }
    const auto sequentialDone = std::chrono::steady_clock::now();
    bridge.evaluateAndSync(motion, 77, &cameraRig);
    bokeh->focus = cameraRig.sample(77.f / float(FPS)).focusDistance;
    composer.render(1.f / float(FPS));
    const bool randomAccess = composer.readRGBPixels() == reference77;

    // Compare each same-subject ROI across focus settings. Assets have
    // different intrinsic detail, so comparing different models against one
    // another would confound focus with geometry and texture complexity.
    bool focusFollowsDepth = true;
    for (std::size_t subject = 0; subject < 3; ++subject) {
        const auto focused = focusScores[subject][subject];
        for (std::size_t otherFocus = 0; otherFocus < 3; ++otherFocus) {
            if (otherFocus != subject && focused <= focusScores[otherFocus][subject] * 1.10)
                focusFollowsDepth = false;
        }
    }
    std::cout << "T10 diagnostics sharpness=" << focusScores[0][0] << ',' << focusScores[0][1] << ',' << focusScores[0][2]
              << " / " << focusScores[1][0] << ',' << focusScores[1][1] << ',' << focusScores[1][2]
              << " / " << focusScores[2][0] << ',' << focusScores[2][1] << ',' << focusScores[2][2]
              << " random_access=" << randomAccess << " coverage=" << minCoverage << ".." << maxCoverage << std::endl;
    // Focus is expected to change edge detail while the static scene keeps a
    // nearly constant non-background pixel count; use the measured ROI focus
    // ordering above instead of requiring artificial coverage variation.
    const bool pass = focusFollowsDepth && randomAccess && minCoverage >= 1000;

    struct rusage usage {};
    getrusage(RUSAGE_SELF, &usage);
    const auto* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    std::ofstream scene(output / "scene.json");
    scene << "{\n  \"fixture\": \"three pinned Khronos GLB subjects at fixed depths\",\n"
          << "  \"asset_directory\": \"" << assetDirectory.generic_string() << "\",\n"
          << "  \"assets\": [\"Fox.glb\", \"Avocado.glb\", \"CesiumMilkTruck.glb\"],\n"
          << "  \"depths\": [" << subjects[0].viewDepth << ", " << subjects[1].viewDepth << ", " << subjects[2].viewDepth << "],\n"
          << "  \"camera\": {\"position\": [0, 0, 7.5], \"fov_deg\": 42, \"moves\": false},\n"
          << "  \"focus_distance_track\": [[0,6],[2.5,7.5],[5,9]],\n"
          << "  \"lighting\": {\"ambient\": 0.75, \"directional\": 2.2},\n"
          << "  \"chronon_motion_scene\": " << motion.toJson(2) << "\n}\n";
    std::ofstream metadata(output / "metadata.json");
    metadata << std::fixed << std::setprecision(3)
             << "{\n  \"test_id\": \"T10\",\n  \"status\": \"" << (pass ? "PARTIAL" : "FAIL") << "\",\n  \"technical_checks_passed\": " << (pass ? "true" : "false") << ",\n"
             << "  \"pipeline\": \"MotionScene + CameraRig.focusDistance -> MotionSceneBridge -> EffectComposer -> BokehPass -> GL PBO -> CUDA -> NVENC\",\n"
             << "  \"width\": " << W << ",\n  \"height\": " << H << ",\n  \"fps\": " << FPS
             << ",\n  \"frames\": " << FRAMES << ",\n  \"duration_seconds\": 5.0,\n"
             << "  \"focus_distances\": [" << focusDistances[0] << ", " << focusDistances[1] << ", " << focusDistances[2] << "],\n"
             << "  \"focus_plane_front_depths\": [" << frontDepths[0] << ", " << frontDepths[1] << ", " << frontDepths[2] << "],\n"
             << "  \"subject_assets\": [\"Fox.glb\", \"Avocado.glb\", \"CesiumMilkTruck.glb\"],\n"
             << "  \"subject_fit_scales\": [" << fitScales[0] << ", " << fitScales[1] << ", " << fitScales[2] << "],\n"
             << "  \"subject_roi_mean_neighbor_gradient\": [[" << focusScores[0][0] << "," << focusScores[0][1] << "," << focusScores[0][2]
             << "],[" << focusScores[1][0] << "," << focusScores[1][1] << "," << focusScores[1][2]
             << "],[" << focusScores[2][0] << "," << focusScores[2][1] << "," << focusScores[2][2] << "]],\n"
             << "  \"focus_follows_depth\": " << (focusFollowsDepth ? "true" : "false") << ",\n"
             << "  \"random_access_frame_77_identical\": " << (randomAccess ? "true" : "false") << ",\n"
             << "  \"changed_pixels_min\": " << minCoverage << ",\n  \"changed_pixels_max\": " << maxCoverage << ",\n"
             << "  \"render_wall_seconds\": " << std::chrono::duration<double>(sequentialDone - start).count()
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
             << ",\n  \"diagnostic_cpu_readback_passes\": 151"
             << ",\n  \"gpu_resident_zero_copy\": false"
             << ",\n  \"vram_bytes\": null\n}\n";
    renderer.dispose();
    std::cout << "T10 " << (pass ? "PARTIAL (GPU-resident encode verified; diagnostics read back separately)" : "FAIL") << " sharpness=" << focusScores[0][0] << ',' << focusScores[0][1] << ',' << focusScores[0][2]
              << " / " << focusScores[1][0] << ',' << focusScores[1][1] << ',' << focusScores[1][2]
              << " / " << focusScores[2][0] << ',' << focusScores[2][1] << ',' << focusScores[2][2]
              << " random_access=" << randomAccess << "\n";
    return pass ? 0 : 1;
#endif
} catch (const std::exception& error) {
    std::cerr << "T10 FAIL: " << error.what() << '\n';
    return 1;
}
