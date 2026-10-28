// 3D typography rack-focus showcase using ChrononMotion3D's physical camera
// rig and threepp's real GPU BokehPass (not a glow or multi-draw blur).
#include "gpu_certification_gate.hpp"
#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif
#include "stb_image_write.h"

#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/postprocessing/BokehPass.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/textures/Texture.hpp"

namespace fs = std::filesystem;
namespace {
constexpr unsigned width = 1920;
constexpr unsigned height = 1080;
constexpr unsigned fps = 30;
constexpr unsigned frameCount = 180;

void writePng(const fs::path& path, const std::vector<unsigned char>& pixels) {
    std::vector<unsigned char> topDown(pixels.size());
    const std::size_t stride = std::size_t(width) * 3;
    for (unsigned y = 0; y < height; ++y) {
        std::copy_n(pixels.data() + std::size_t(height - 1 - y) * stride, stride,
                    topDown.data() + std::size_t(y) * stride);
    }
    if (!stbi_write_png(path.c_str(), int(width), int(height), 3,
                        topDown.data(), int(stride))) {
        throw std::runtime_error("could not write preview " + path.string());
    }
}
}

int main(int argc, char** argv) try {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void)argc;
    (void)argv;
    throw std::runtime_error("showcase requires the certified GL PBO -> CUDA -> NVENC path");
#else
    chrononmotion::certification::prepare_nvidia_egl_headless();
    const fs::path output = argc > 1 ? argv[1] : "out/real_depth_focus_v2";
    fs::create_directories(output);
    const fs::path assets = CHRONONMOTION_DEPTH_FOCUS_ASSETS;

    chrononmotion::motion::MotionScene scene("real_depth_focus_chrononmotion3d", float(fps));
    auto addPlane = [&](std::uint32_t id, const std::string& name,
                        float w, float h, chrononmotion::Vector3 position,
                        chrononmotion::Color color, bool transparent = false) -> chrononmotion::motion::Layer& {
        chrononmotion::motion::MeshAttachment attachment;
        attachment.geometry = chrononmotion::PlaneGeometry::create(w, h, 2, 2);
        attachment.material = chrononmotion::MaterialDescriptor::unlit(id, color);
        attachment.material.transparent = transparent;
        auto& layer = scene.addMesh(id, name, std::move(attachment));
        layer.transform.position = position;
        return layer;
    };

    // A deep, restrained stage gives the planes readable parallax without
    // competing with the rack-focus subject.
    addPlane(1, "midnight_stage", 42.f, 24.f, {0.f, 0.f, -6.f},
             chrononmotion::Color(0x24445e));

    struct Word {
        std::uint32_t id;
        const char* label;
        const char* file;
        float width;
        float height;
        chrononmotion::Vector3 position;
        float focus;
    };
    const std::array<Word, 3> words{{
        {10, "DEPTH", "depth.png", 4.35f, 1.16f, {-2.25f, 0.85f, -2.2f}, 11.55f},
        {11, "FOCUS", "focus.png", 4.25f, 1.13f, {0.f, 0.f, -1.1f}, 9.60f},
        {12, "CAMERA", "camera.png", 4.9f, 1.30f, {2.2f, -0.85f, 0.f}, 7.70f},
    }};
    threepp::ImageLoader imageLoader;
    std::array<std::shared_ptr<threepp::Texture>, 3> textures;
    for (std::size_t i = 0; i < words.size(); ++i) {
        auto& layer = addPlane(words[i].id, words[i].label, words[i].width,
                               words[i].height, words[i].position,
                               chrononmotion::Color(0xffffff), true);
        layer.tracks.rotation.add(0.f, {0.f, 0.035f, 0.f});
        layer.tracks.rotation.add(6.f, {0.f, -0.035f, 0.f});
        auto image = imageLoader.load(assets / words[i].file, 4, true);
        if (!image || image->width() == 0 || image->height() == 0)
            throw std::runtime_error(std::string("could not load Inter word texture: ") + words[i].file);
        textures[i] = threepp::Texture::create(std::move(*image));
        textures[i]->needsUpdate();
    }

    auto& ambient = scene.addAmbientLight(20, "soft_stage_fill");
    ambient.intensity = 0.7f;
    auto& key = scene.addDirectionalLight(21, "cool_edge_light");
    key.intensity = 1.2f;
    key.transform.position = {-3.f, 5.f, 4.f};

    constexpr float fov = 42.f;
    constexpr float aspect = float(width) / float(height);
    chrononmotion::motion::CameraRig camera(100, fov, aspect, 0.1f, 80.f);
    camera.setTarget({0.f, 0.f, -1.1f})
          .setPosition({0.f, 0.f, 9.2f})
          .setFStop(chrononmotion::motion::fStopFromBlurGain(0.06f))
          .setMaxBlur(52.f)
          .setFocusDistance(words[0].focus);
    camera.positionTrack().add(0.f, {-0.34f, 0.08f, 9.2f}, chrononmotion::motion::Easing::easeInOut());
    camera.positionTrack().add(3.f, {0.f, 0.f, 8.35f}, chrononmotion::motion::Easing::easeInOut());
    camera.positionTrack().add(6.f, {0.52f, -0.08f, 7.5f}, chrononmotion::motion::Easing::easeInOut());
    camera.targetTrack().add(0.f, {-0.45f, 0.10f, -1.8f}, chrononmotion::motion::Easing::easeInOut());
    camera.targetTrack().add(3.f, {0.f, 0.f, -1.1f}, chrononmotion::motion::Easing::easeInOut());
    camera.targetTrack().add(6.f, {0.42f, -0.10f, -0.15f}, chrononmotion::motion::Easing::easeInOut());
    camera.focusDistanceTrack().add(0.f, words[0].focus, chrononmotion::motion::Easing::easeInOut());
    camera.focusDistanceTrack().add(2.8f, words[1].focus, chrononmotion::motion::Easing::easeInOut());
    camera.focusDistanceTrack().add(3.2f, words[1].focus, chrononmotion::motion::Easing::easeInOut());
    camera.focusDistanceTrack().add(6.f, words[2].focus, chrononmotion::motion::Easing::easeInOut());

    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).antialiasing(4).headless(true));
    threepp::GLRenderer renderer(canvas);
    const char* rendererNameRaw = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string rendererName = rendererNameRaw ? rendererNameRaw : "unknown";
    chrononmotion::certification::require_nvidia_renderer(rendererName);
    renderer.setClearColor(threepp::Color(0x050a12));

    threepp::motion::MotionSceneBridge bridge(fov, aspect, 0.1f, 80.f);
    for (std::size_t i = 0; i < words.size(); ++i) bridge.bindLayerTexture(words[i].id, textures[i]);
    threepp::EffectComposer::Options composerOptions;
    composerOptions.samples = 4;
    threepp::EffectComposer composer(renderer, composerOptions);
    composer.renderToScreen = false;
    composer.setSize(width, height);
    composer.addPass(std::make_shared<threepp::RenderPass>(*bridge.scene(), *bridge.camera()));
    auto bokeh = std::make_shared<threepp::BokehPass>(
        *bridge.scene(), *bridge.camera(), words[0].focus, 0.014f, 0.026f);
    composer.addPass(bokeh);

    const fs::path video = output / "real_depth_focus_v2_1920x1080.mp4";
    chrononmotion::certification::require_nvenc_encoder();
    chrononmotion::certification::GlCudaNvencEncoder encoder(width, height, fps, video.string());
    const std::array<unsigned, 3> previewFrames{0, 90, 179};
    std::size_t previewIndex = 0;
    for (unsigned frame = 0; frame < frameCount; ++frame) {
        bridge.evaluateAndSync(scene, int(frame), &camera);
        bokeh->focus = camera.sample(float(frame) / float(fps)).focusDistance;
        composer.render(1.f / float(fps));
        renderer.setRenderTarget(&composer.readBuffer());
        encoder.encode_current_target(frame);
        renderer.setRenderTarget(nullptr);
        if (previewIndex < previewFrames.size() && frame == previewFrames[previewIndex]) {
            writePng(output / ("preview_" + std::to_string(frame) + ".png"), composer.readRGBPixels());
            ++previewIndex;
        }
    }
    encoder.finish();
    if (encoder.frames_submitted() != frameCount || encoder.packets_written() != frameCount)
        throw std::runtime_error("NVENC did not mux every frame");
    renderer.dispose();
    std::cout << "Rendered " << video << " using ChrononMotion3D CameraRig + threepp BokehPass; renderer="
              << rendererName << ", GPU=" << encoder.device_name()
              << ", frames=" << encoder.frames_submitted() << '\n';
    return 0;
#endif
} catch (const std::exception& error) {
    std::cerr << "depth-focus showcase failed: " << error.what() << '\n';
    return 1;
}
