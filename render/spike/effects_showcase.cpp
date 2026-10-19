// Render a 45-second, nine-effect post-processing showcase from one image.
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "threepp/canvas/Canvas.hpp"
#include "threepp/cameras/OrthographicCamera.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/postprocessing/ShaderPass.hpp"
#include "threepp/postprocessing/shaders/ChrononPostProcessShader.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/scenes/Scene.hpp"

using namespace threepp;

namespace {
constexpr int kWidth = 1920;
constexpr int kHeight = 1080;
constexpr int kFps = 24;
constexpr int kFramesPerEffect = 5 * kFps;

std::string quote(const std::string& value) {
    std::string out = "'";
    for (const char c : value) out += c == '\'' ? "'\\''" : std::string(1, c);
    return out + "'";
}

struct Effect {
    const char* name;
    float scanline, grid, chromatic, vhs, halation, lens, edge, streak, grain;
};

constexpr std::array<Effect, 9> kEffects{{
    {"scanlines", 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {"glass grid", 0.f, 0.85f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {"chromatic aberration", 0.f, 0.f, 0.28f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {"VHS", 0.f, 0.f, 0.f, 0.8f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {"film halation", 0.f, 0.f, 0.f, 0.f, 0.65f, 0.f, 0.f, 0.f, 0.f},
    {"lens distortion", 0.f, 0.f, 0.f, 0.f, 0.f, 0.22f, 0.7f, 0.f, 0.f},
    {"anamorphic streak", 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.55f, 0.f},
    {"temporal posterization", 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f},
    {"film grain", 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.14f},
}};

void setFloat(ShaderPass& pass, const char* name, float value) {
    pass.uniforms().at(name).value<float>() = value;
}
}

int main(int argc, char** argv) try {
    if (argc != 6) {
        std::cerr << "usage: effects_showcase <image-1920x1080> <blurred-image> <blurred-highlights> <horizontal-streaks> <output.mp4>\n";
        return 2;
    }
    const std::string imagePath = argv[1];
    const std::string backdropPath = argv[2];
    const std::string highlightsPath = argv[3];
    const std::string streakPath = argv[4];
    const std::string outputPath = argv[5];

    ImageLoader loader;
    auto sourceImage = loader.load(imagePath, 4, true);
    auto backdropImage = loader.load(backdropPath, 4, true);
    auto highlightsImage = loader.load(highlightsPath, 4, true);
    auto streakImage = loader.load(streakPath, 4, true);
    if (!sourceImage || !backdropImage || !highlightsImage || !streakImage)
        throw std::runtime_error("could not load showcase image inputs");
    auto sourceTexture = Texture::create(std::move(*sourceImage));
    auto backdropTexture = Texture::create(std::move(*backdropImage));
    auto highlightsTexture = Texture::create(std::move(*highlightsImage));
    auto streakTexture = Texture::create(std::move(*streakImage));
    sourceTexture->needsUpdate(); backdropTexture->needsUpdate(); highlightsTexture->needsUpdate(); streakTexture->needsUpdate();

    Canvas canvas(Canvas::Parameters().size(kWidth, kHeight).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x07111f));
    auto scene = Scene::create();
    auto material = MeshBasicMaterial::create(MeshBasicMaterial::Params().map(sourceTexture));
    auto image = Mesh::create(PlaneGeometry::create(2.f, 1.125f), material);
    scene->add(image);
    auto camera = OrthographicCamera::create(-1.f, 1.f, 0.5625f, -0.5625f, 0.1f, 10.f);
    camera->position.z = 1.f;

    EffectComposer::Options options;
    options.depthBuffer = false;
    options.stencilBuffer = false;
    EffectComposer composer(renderer, options);
    composer.setSize(kWidth, kHeight);
    composer.renderToScreen = false;
    composer.addPass(std::make_shared<RenderPass>(*scene, *camera));
    auto finish = std::make_shared<ShaderPass>(shaders::chrononPostProcessShader());
    finish->uniforms().at("tBackdrop").value<Texture*>() = backdropTexture.get();
    finish->uniforms().at("tHighlights").value<Texture*>() = highlightsTexture.get();
    finish->uniforms().at("uResolution").value<Vector2>() = Vector2(float(kWidth), float(kHeight));
    composer.addPass(finish);

    const std::string command = "ffmpeg -hide_banner -loglevel error -y -f rawvideo -pixel_format rgb24 -video_size 1920x1080 -framerate 24 -i - -an -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p -movflags +faststart " + quote(outputPath);
    FILE* encoder = popen(command.c_str(), "w");
    if (!encoder) throw std::runtime_error("could not start FFmpeg encoder");
    std::vector<unsigned char> topDown(std::size_t(kWidth) * kHeight * 3);
    const std::size_t rowBytes = std::size_t(kWidth) * 3;
    for (std::size_t effectIndex = 0; effectIndex < kEffects.size(); ++effectIndex) {
        const auto& effect = kEffects[effectIndex];
        finish->uniforms().at("tHighlights").value<Texture*>() =
                effectIndex == 6 ? streakTexture.get() : highlightsTexture.get();
        setFloat(*finish, "uScanline", effect.scanline);
        setFloat(*finish, "uGrid", effect.grid);
        setFloat(*finish, "uChromatic", effect.chromatic);
        setFloat(*finish, "uVhs", effect.vhs);
        setFloat(*finish, "uHalation", effect.halation);
        setFloat(*finish, "uLens", effect.lens);
        setFloat(*finish, "uEdgeSoftening", effect.edge);
        setFloat(*finish, "uStreak", effect.streak);
        setFloat(*finish, "uGrain", effect.grain);
        std::cout << "Rendering " << effect.name << " (5 s)\n";
        for (int frame = 0; frame < kFramesPerEffect; ++frame) {
            const float time = float(int(effectIndex) * kFramesPerEffect + frame) / float(kFps);
            setFloat(*finish, "uTime", time);
            // A barely visible drift makes temporal stepping easy to compare.
            const float motionTime = effectIndex == 7 ? shaders::chrononSteppedTime(time, 15.f) : time;
            image->position.x = 0.008f * std::sin(motionTime * 1.4f);
            image->position.y = 0.004f * std::sin(motionTime * 0.8f);
            image->scale.setScalar(1.018f + 0.006f * std::sin(motionTime * 0.45f));
            composer.render(1.f / float(kFps));
            const auto pixels = composer.readRGBPixels();
            for (int y = 0; y < kHeight; ++y)
                std::copy_n(pixels.data() + std::size_t(kHeight - y - 1) * rowBytes, rowBytes,
                            topDown.data() + std::size_t(y) * rowBytes);
            if (std::fwrite(topDown.data(), 1, topDown.size(), encoder) != topDown.size())
                throw std::runtime_error("FFmpeg stopped accepting video frames");
        }
    }
    const int encoderStatus = pclose(encoder);
    if (encoderStatus != 0) throw std::runtime_error("FFmpeg failed while encoding the showcase");
    std::cout << "Wrote " << outputPath << " (45 s, 1920x1080, 24 fps)\n";
    renderer.dispose();
    return 0;
} catch (const std::exception& e) {
    std::cerr << "effects_showcase: " << e.what() << '\n';
    return 1;
}
