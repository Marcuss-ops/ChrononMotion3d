// YouTube-style layered demo: a clean creator cutout in front of two 3D
// objects, with real depth, camera parallax, PBR materials and independent
// key/rim/fill lighting. The cutout is a 2.5D layer; the props and shadows are
// fully 3D.
//
// Usage:
//   chrononmotion_render_layered_youtube [output_dir] [frame_count] [first_frame] [cutout.png]
//
// Default: out/layered_youtube_20261107 300 0 render/spike/assets/creator_transparent.png
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#include "stb_image_write.h"

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/core/Object3D.hpp"
#include "threepp/geometries/TorusGeometry.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/textures/Texture.hpp"

using namespace threepp;
namespace fs = std::filesystem;
namespace {
constexpr int W = 1920;
constexpr int H = 1080;
constexpr int FPS = 30;
constexpr float kDuration = 10.f;
constexpr float kPi = 3.14159265358979323846f;

#ifndef CHRONONMOTION_SPIKE_ASSET_DIR
#define CHRONONMOTION_SPIKE_ASSET_DIR "render/spike/assets"
#endif

void writePng(const fs::path& path, const std::vector<unsigned char>& rgb) {
    std::vector<unsigned char> top(rgb.size());
    const std::size_t stride = std::size_t(W) * 3;
    for (int y = 0; y < H; ++y) {
        std::copy_n(rgb.data() + std::size_t(H - y - 1) * stride, stride,
                    top.data() + std::size_t(y) * stride);
    }
    if (!stbi_write_png(path.c_str(), W, H, 3, top.data(), int(stride))) {
        throw std::runtime_error("PNG write failed: " + path.string());
    }
}

std::shared_ptr<Texture> studioEnvironment() {
    constexpr int ew = 256, eh = 128;
    std::vector<float> px(std::size_t(ew) * eh * 3);
    for (int y = 0; y < eh; ++y) {
        for (int x = 0; x < ew; ++x) {
            const float u = float(x) / ew, v = float(y) / eh;
            const float base = 0.025f + 0.075f * (1.f - v);
            const float warmPanel = (u > 0.10f && u < 0.24f && v > 0.12f && v < 0.62f) ? 5.5f : 0.f;
            const float coolPanel = (u > 0.60f && u < 0.76f && v > 0.08f && v < 0.55f) ? 4.2f : 0.f;
            const float horizon = (v > 0.47f && v < 0.53f) ? 0.35f : 0.f;
            const std::size_t i = (std::size_t(y) * ew + x) * 3;
            px[i] = base + warmPanel + 0.10f * coolPanel + horizon;
            px[i + 1] = base + 0.72f * warmPanel + 0.55f * coolPanel + horizon;
            px[i + 2] = base + 0.45f * warmPanel + coolPanel + horizon;
        }
    }
    auto texture = Texture::create(Image(std::move(px), ew, eh));
    texture->mapping = Mapping::EquirectangularReflection;
    texture->format = Format::RGB;
    texture->type = Type::Float;
    texture->colorSpace = ColorSpace::Linear;
    texture->magFilter = Filter::Linear;
    texture->minFilter = Filter::Linear;
    return texture;
}

std::shared_ptr<DataTexture> backdropTexture(int width, int height) {
    std::vector<unsigned char> rgb(std::size_t(width) * height * 3);
    for (int y = 0; y < height; ++y) {
        const float v = (float(y) + 0.5f) / float(height);
        for (int x = 0; x < width; ++x) {
            const float u = (float(x) + 0.5f) / float(width);
            const float glowCyan = std::exp(-((u - 0.64f) * (u - 0.64f) * 18.f +
                                              (v - 0.50f) * (v - 0.50f) * 30.f));
            const float glowMagenta = std::exp(-((u - 0.25f) * (u - 0.25f) * 24.f +
                                                 (v - 0.56f) * (v - 0.56f) * 38.f));
            const float vignette = 0.55f + 0.45f * (1.f - std::abs(u - 0.5f) * 1.6f);
            const float top = 5.f, bottom = 2.f;
            float r = (bottom + (top - bottom) * v) * vignette + 4.0f * glowMagenta + 0.8f * glowCyan;
            float g = (bottom + 4.f * (top - bottom) * v) * vignette + 1.5f * glowMagenta + 7.f * glowCyan;
            float b = (bottom + 12.f * (top - bottom) * v) * vignette + 7.f * glowMagenta + 11.f * glowCyan;
            const std::size_t i = (std::size_t(y) * width + x) * 3;
            rgb[i] = static_cast<unsigned char>(std::clamp(r, 0.f, 255.f));
            rgb[i + 1] = static_cast<unsigned char>(std::clamp(g, 0.f, 255.f));
            rgb[i + 2] = static_cast<unsigned char>(std::clamp(b, 0.f, 255.f));
        }
    }
    auto texture = DataTexture::create(rgb, width, height);
    texture->format = Format::RGB;
    texture->magFilter = Filter::Linear;
    texture->minFilter = Filter::Linear;
    texture->needsUpdate();
    return texture;
}

std::shared_ptr<DataTexture> foregroundFadeTexture(int width, int height) {
    std::vector<unsigned char> rgba(std::size_t(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const float v = (float(y) + 0.5f) / float(height);
        const float a = std::pow(1.f - v, 1.35f);
        for (int x = 0; x < width; ++x) {
            const std::size_t i = (std::size_t(y) * width + x) * 4;
            rgba[i] = 5;
            rgba[i + 1] = 7;
            rgba[i + 2] = 12;
            rgba[i + 3] = static_cast<unsigned char>(std::clamp(252.f * a, 0.f, 255.f));
        }
    }
    auto texture = DataTexture::create(rgba, width, height);
    texture->format = Format::RGBA;
    texture->magFilter = Filter::Linear;
    texture->minFilter = Filter::Linear;
    texture->needsUpdate();
    return texture;
}

std::shared_ptr<MeshBasicMaterial> neonMaterial(std::uint32_t hex, float opacity = 1.f) {
    auto material = MeshBasicMaterial::create();
    material->color.setHex(hex);
    material->transparent = opacity < 1.f;
    material->opacity = opacity;
    material->toneMapped = false;
    material->depthWrite = opacity >= 1.f;
    return material;
}

std::string framePath(const fs::path& dir, int frame) {
    std::ostringstream name;
    name << "frame-" << std::setw(4) << std::setfill('0') << frame << ".png";
    return (dir / name.str()).string();
}
}// namespace

int main(int argc, char** argv) try {
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY") &&
        !std::getenv("THREEPP_GLFW_PLATFORM")) {
        setenv("THREEPP_GLFW_PLATFORM", "egl", 0);
    }

    const fs::path out = argc > 1 ? fs::path(argv[1]) : fs::path("out/layered_youtube_20261107");
    const int frameCount = argc > 2 ? std::max(1, std::atoi(argv[2])) : 300;
    const int firstFrame = argc > 3 ? std::max(0, std::atoi(argv[3])) : 0;
    const fs::path cutout = argc > 4
            ? fs::path(argv[4])
            : fs::path(CHRONONMOTION_SPIKE_ASSET_DIR) / "creator_transparent.png";
    const fs::path framesDir = out / "frames";
    fs::create_directories(out);
    fs::remove_all(framesDir);
    fs::create_directories(framesDir);

    ImageLoader imageLoader;
    auto image = imageLoader.load(cutout, 4, true);
    if (!image || image->width() == 0 || image->height() == 0) {
        throw std::runtime_error("failed to load cutout: " + cutout.string());
    }
    const int imageWidth = image->width();
    const int imageHeight = image->height();
    auto cutoutTexture = Texture::create(std::move(*image));
    cutoutTexture->needsUpdate();

    chrononmotion::motion::MotionScene motion("layered_youtube_cutout", float(FPS));

    // ---- Foreground: 2.5D creator, cut below frame for a medium shot ----
    const float planeH = 1.90f;
    const float planeW = planeH * float(imageWidth) / float(imageHeight);
    chrononmotion::motion::MeshAttachment creatorAttachment;
    creatorAttachment.geometry = chrononmotion::PlaneGeometry::create(planeW, planeH, 2, 2);
    creatorAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    creatorAttachment.material.transparent = true;
    auto& creator = motion.addMesh(1, "foreground_creator_cutout", std::move(creatorAttachment));
    creator.transform.position = {-0.80f, 1.00f, 2.65f};
    creator.castsShadow = false;
    creator.acceptsShadow = false;

    // ---- Foreground low desk: hides the cutout's lower seam -------------
    chrononmotion::motion::MeshAttachment deskAttachment;
    deskAttachment.geometry = chrononmotion::BoxGeometry::create(9.0f, 0.90f, 0.90f);
    deskAttachment.material = chrononmotion::MaterialDescriptor::unlit(
            30, chrononmotion::Color(0x02050b));
    auto& desk = motion.addMesh(30, "foreground_desk", std::move(deskAttachment));
    desk.transform.position = {0.f, -0.33f, 3.20f};
    desk.castsShadow = true;
    desk.acceptsShadow = true;

    // ---- Floor ----------------------------------------------------------
    chrononmotion::motion::MeshAttachment floorAttachment;
    floorAttachment.geometry = chrononmotion::BoxGeometry::create(20.f, 0.12f, 20.f);
    floorAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            10, chrononmotion::Color(0x05070c), 0.92f, 0.0f);
    auto& floor = motion.addMesh(10, "studio_floor", std::move(floorAttachment));
    floor.transform.position = {0.f, -0.06f, 0.f};
    floor.castsShadow = false;
    floor.acceptsShadow = true;

    // ---- Midground cube -------------------------------------------------
    chrononmotion::motion::MeshAttachment midPedestalAttachment;
    midPedestalAttachment.geometry = chrononmotion::BoxGeometry::create(1.00f, 0.45f, 1.00f);
    midPedestalAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            11, chrononmotion::Color(0x1a2333), 0.55f, 0.25f);
    auto& midPedestal = motion.addMesh(11, "mid_pedestal", std::move(midPedestalAttachment));
    midPedestal.transform.position = {0.78f, 0.225f, 0.0f};
    midPedestal.castsShadow = true;
    midPedestal.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment cubeAttachment;
    cubeAttachment.geometry = chrononmotion::BoxGeometry::create(0.90f, 0.90f, 0.90f);
    cubeAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            12, chrononmotion::Color(0xff5e3d), 0.32f, 0.22f);
    auto& cube = motion.addMesh(12, "midground_cube", std::move(cubeAttachment));
    cube.transform.position = {0.78f, 0.90f, 0.0f};
    cube.castsShadow = true;
    cube.acceptsShadow = true;
    cube.tracks.rotation.add(0.f, {0.18f, 0.35f, 0.06f});
    cube.tracks.rotation.add(kDuration, {0.18f, 0.35f + 2.f * kPi, 0.06f});

    // ---- Background sphere ---------------------------------------------
    chrononmotion::motion::MeshAttachment backPedestalAttachment;
    backPedestalAttachment.geometry = chrononmotion::BoxGeometry::create(1.15f, 0.65f, 1.15f);
    backPedestalAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            13, chrononmotion::Color(0x161f2e), 0.52f, 0.22f);
    auto& backPedestal = motion.addMesh(13, "back_pedestal", std::move(backPedestalAttachment));
    backPedestal.transform.position = {1.62f, 0.325f, -3.80f};
    backPedestal.castsShadow = true;
    backPedestal.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment sphereAttachment;
    sphereAttachment.geometry = chrononmotion::SphereGeometry::create(0.82f, 64, 48);
    sphereAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            14, chrononmotion::Color(0x24d7cd), 0.08f, 0.88f);
    auto& sphere = motion.addMesh(14, "background_sphere", std::move(sphereAttachment));
    sphere.transform.position = {1.62f, 1.47f, -3.80f};
    sphere.castsShadow = true;
    sphere.acceptsShadow = true;
    sphere.tracks.position.add(0.f, {1.62f, 1.47f, -3.80f});
    sphere.tracks.position.add(kDuration * 0.5f, {1.62f, 1.54f, -3.80f});
    sphere.tracks.position.add(kDuration, {1.62f, 1.47f, -3.80f});

    // ---- Backdrop -------------------------------------------------------
    chrononmotion::motion::MeshAttachment backdropAttachment;
    backdropAttachment.geometry = chrononmotion::PlaneGeometry::create(24.f, 14.f, 1, 1);
    backdropAttachment.material = chrononmotion::MaterialDescriptor::unlit(20, chrononmotion::Color(0xffffff));
    auto& backdrop = motion.addMesh(20, "generated_backdrop", std::move(backdropAttachment));
    backdrop.transform.position = {0.f, 2.0f, -6.90f};
    backdrop.castsShadow = false;
    backdrop.acceptsShadow = false;

    // ---- Lighting -------------------------------------------------------
    auto& ambient = motion.addAmbientLight(100, "cool_ambient");
    ambient.color = chrononmotion::Color(0x9fb4e8);
    ambient.intensity = 0.35f;

    auto& key = motion.addDirectionalLight(101, "warm_key");
    key.color = chrononmotion::Color(0xffefd6);
    key.intensity = 2.45f;
    key.castsShadow = true;
    key.shadowBias = -0.0002f;
    key.shadowNormalBias = 0.012f;
    key.shadowRadius = 2.5f;
    key.positionTrack.add(0.f, {-4.0f, 7.0f, 4.5f});
    key.directionTrack.add(0.f, {4.0f, -7.0f, -4.5f});

    auto& rim = motion.addDirectionalLight(102, "cool_rim");
    rim.color = chrononmotion::Color(0x55b8ff);
    rim.intensity = 2.8f;
    rim.positionTrack.add(0.f, {4.5f, 4.5f, -4.0f});
    rim.directionTrack.add(0.f, {-4.5f, -4.5f, 4.0f});

    auto& warmFill = motion.addPointLight(103, "warm_fill");
    warmFill.color = chrononmotion::Color(0xff6f45);
    warmFill.intensity = 6.5f;
    warmFill.range = 7.f;
    warmFill.decay = 2.f;
    warmFill.positionTrack.add(0.f, {-2.6f, 2.2f, 3.0f});

    auto& cyanPractical = motion.addPointLight(104, "cyan_practical");
    cyanPractical.color = chrononmotion::Color(0x3fe7ff);
    cyanPractical.intensity = 16.f;
    cyanPractical.range = 8.f;
    cyanPractical.decay = 2.f;
    cyanPractical.positionTrack.add(0.f, {0.65f, 2.4f, -4.6f});

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = rendererName ? rendererName : "unknown";
    const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    if (glRenderer.find("NVIDIA") == std::string::npos) {
        throw std::runtime_error("layered YouTube demo requires NVIDIA hardware GL; selected: " + glRenderer);
    }
    renderer.setClearColor(Color(0x060a12));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.0f;
    renderer.shadowMap().enabled = true;
    renderer.shadowMap().type = ShadowMap::PFCSoft;

    threepp::motion::MotionSceneBridge bridge(38.f, float(W) / float(H), 0.05f, 100.f);
    bridge.scene()->environment = studioEnvironment();
    bridge.bindLayerTexture(1, cutoutTexture);
    bridge.bindLayerTexture(20, backdropTexture(1024, 576));
    bridge.evaluateAndSync(motion, firstFrame);

    std::shared_ptr<DirectionalLight> shadowLight;
    bridge.scene()->traverse([&](Object3D& object) {
        if (auto* directional = dynamic_cast<DirectionalLight*>(&object)) {
            if (directional->castShadow) {
                shadowLight = std::shared_ptr<DirectionalLight>(directional, [](DirectionalLight*) {});
            }
        }
    });
    if (!shadowLight || !shadowLight->shadow) {
        throw std::runtime_error("bridge did not expose the shadow-casting key light");
    }
    shadowLight->shadow->mapSize.set(2048.f, 2048.f);

    auto ring = Mesh::create(TorusGeometry::create(2.22f, 0.030f, 12, 160), neonMaterial(0x35d7ff));
    ring->position.set(0.65f, 2.05f, -5.85f);
    bridge.scene()->add(ring);
    auto ringHalo = Mesh::create(TorusGeometry::create(2.22f, 0.10f, 12, 160), neonMaterial(0x2a6bff, 0.12f));
    ringHalo->position.set(0.65f, 2.05f, -5.87f);
    bridge.scene()->add(ringHalo);

    auto updateCamera = [&](int frame) {
        const float t = float(frame) / float(FPS);
        const float u = t / kDuration;
        const float sweep = -0.38f * std::cos(2.f * kPi * u);
        const float z = 7.90f - 0.50f * std::sin(kPi * u);
        bridge.camera()->position.set(sweep, 2.22f + 0.03f * std::sin(2.f * kPi * u), z);
        bridge.camera()->lookAt(0.02f, 1.15f, 0.f);
    };

    const auto started = std::chrono::steady_clock::now();
    std::size_t bytesWritten = 0;
    for (int i = 0; i < frameCount; ++i) {
        const int frame = firstFrame + i;
        bridge.evaluateAndSync(motion, frame);
        const float u = std::min(1.f, (float(frame) / float(FPS)) / kDuration);
        ring->rotation.z = 0.10f * std::sin(2.f * kPi * u);
        ringHalo->rotation.z = ring->rotation.z;
        updateCamera(frame);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != std::size_t(W) * H * 3) {
            throw std::runtime_error("invalid RGB readback size at frame " + std::to_string(frame));
        }
        const fs::path path = framePath(framesDir, frame);
        writePng(path, pixels);
        bytesWritten += fs::file_size(path);
        if (i == 0 || i == frameCount / 2 || i == frameCount - 1) {
            std::cout << "rendered frame " << frame << " -> " << path << "\n";
        }
    }
    const auto finished = std::chrono::steady_clock::now();

    std::ofstream sceneFile(out / "scene.json");
    sceneFile << "{\n"
              << "  \"fixture\": \"three-layer YouTube cutout/3D depth and lighting demo\",\n"
              << "  \"output\": \"1920x1080 30fps " << frameCount << " frames\",\n"
              << "  \"cutout\": \"" << cutout.filename().string() << "\",\n"
              << "  \"cutout_pixels\": [" << imageWidth << "," << imageHeight << "],\n"
              << "  \"layers\": [\n"
              << "    {\"name\":\"foreground_creator_cutout\",\"depth_z\":2.65,\"type\":\"2.5D RGBA cutout with bottom alpha fade\"},\n"
              << "    {\"name\":\"foreground_desk\",\"depth_z\":3.2,\"material\":\"unlit dark foreground desk\"},\n"
              << "    {\"name\":\"midground_cube\",\"depth_z\":0.0,\"material\":\"PBR orange metal/rough\",\"motion\":\"Y rotation 360deg over 10s\"},\n"
              << "    {\"name\":\"background_sphere\",\"depth_z\":-3.8,\"material\":\"PBR cyan metal\",\"motion\":\"7cm vertical float\"},\n"
              << "    {\"name\":\"generated_backdrop\",\"depth_z\":-6.9,\"material\":\"unlit procedural gradient\"}\n"
              << "  ],\n"
              << "  \"lights\": [\"cool ambient\",\"warm directional key with 2048 shadow map\",\"cool directional rim\",\"warm point fill\",\"cyan point practical\"],\n"
              << "  \"camera\": \"lateral dolly -0.38..+0.38, push-in 7.90..7.40, loop-safe\"\n"
              << "}\n";

    std::ofstream metadata(out / "metadata.json");
    metadata << std::fixed << std::setprecision(4)
             << "{\n  \"width\": " << W << ",\n  \"height\": " << H
             << ",\n  \"fps\": " << FPS << ",\n  \"frames_rendered\": " << frameCount
             << ",\n  \"first_frame\": " << firstFrame
             << ",\n  \"duration_seconds\": " << double(frameCount) / FPS
             << ",\n  \"cutout\": \"" << cutout.filename().string() << "\""
             << ",\n  \"cutout_width\": " << imageWidth
             << ",\n  \"cutout_height\": " << imageHeight
             << ",\n  \"render_seconds\": "
             << std::chrono::duration<double>(finished - started).count()
             << ",\n  \"png_bytes\": " << bytesWritten
             << ",\n  \"gl_renderer\": \"" << glRenderer << "\""
             << ",\n  \"gl_version\": \"" << (glVersion ? glVersion : "unknown") << "\""
             << ",\n  \"video_encoder\": \"libx264 (external ffmpeg)\"\n}\n";

    renderer.dispose();
    std::cout << "LAYERED_YOUTUBE PASS frames=" << frameCount << " gl=" << glRenderer
              << " cutout=" << cutout.filename().string() << " png_bytes=" << bytesWritten << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "LAYERED_YOUTUBE FAIL: " << error.what() << '\n';
    return 1;
}
