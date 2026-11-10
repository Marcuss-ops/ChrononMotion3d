// Layered YouTube demo: three real depth planes in one OpenGL render.
//
//   foreground  UAL1 humanoid (CC0), animated idle, warm key + cool rim
//   midground   glossy orange cube on a pedestal, rotating
//   background  metallic cyan sphere on a pedestal + neon ring
//
// The camera performs a slow lateral dolly with a gentle push-in, so the
// foreground/background parallax is visible. Frames are written as PNGs and
// encoded separately with libx264 for a clean YouTube delivery.
//
// Usage:
//   chrononmotion_render_layered_youtube [output_dir] [frame_count] [first_frame]
//
// Default: out/layered_youtube_20261107 300 0
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
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
#include "threepp/animation/AnimationClip.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/core/Object3D.hpp"
#include "threepp/geometries/TorusGeometry.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/objects/SkinnedMesh.hpp"
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
    if (!stbi_write_png(path.string().c_str(), W, H, 3, top.data(), int(stride))) {
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
        const float v = (float(y) + 0.5f) / float(height); // 0 bottom, 1 top
        for (int x = 0; x < width; ++x) {
            const float u = (float(x) + 0.5f) / float(width);
            // Deep blue-black studio gradient with a soft cyan/magenta glow
            // behind the background layer. Kept subtle so the neon ring reads.
            const float glowCyan = std::exp(-((u - 0.62f) * (u - 0.62f) * 18.f +
                                              (v - 0.48f) * (v - 0.48f) * 32.f));
            const float glowMagenta = std::exp(-((u - 0.30f) * (u - 0.30f) * 26.f +
                                                 (v - 0.55f) * (v - 0.55f) * 40.f));
            const float vignette = 0.55f + 0.45f * (1.f - std::abs(u - 0.5f) * 1.6f);
            const float top = 5.f, bottom = 2.f;
            float r = (bottom + (top - bottom) * v) * vignette + 4.5f * glowMagenta + 0.8f * glowCyan;
            float g = (bottom + 4.f * (top - bottom) * v) * vignette + 1.8f * glowMagenta + 8.f * glowCyan;
            float b = (bottom + 12.f * (top - bottom) * v) * vignette + 8.f * glowMagenta + 12.f * glowCyan;
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
#ifndef _WIN32
        setenv("THREEPP_GLFW_PLATFORM", "egl", 0);
#endif
    }

    const fs::path out = argc > 1 ? fs::path(argv[1]) : fs::path("out/layered_youtube_20261107");
    const int frameCount = argc > 2 ? std::max(1, std::atoi(argv[2])) : 300;
    const int firstFrame = argc > 3 ? std::max(0, std::atoi(argv[3])) : 0;
    const fs::path framesDir = out / "frames";
    fs::create_directories(out);
    fs::remove_all(framesDir);
    fs::create_directories(framesDir);

    const fs::path asset = argc > 4
            ? fs::path(argv[4])
            : fs::path(CHRONONMOTION_SPIKE_ASSET_DIR) / "scene03" / "UAL1_Standard.glb";
    const std::string requestedClip = argc > 5 ? argv[5] : "Idle_Loop";
    GLTFLoader loader;
    auto result = loader.load(asset);
    if (!result || !result->scene) throw std::runtime_error("GLTFLoader failed: " + asset.string());
    if (result->animations.empty()) throw std::runtime_error(asset.filename().string() + " has no animation clips");

    std::shared_ptr<AnimationClip> idle;
    for (const auto& candidate : result->animations) {
        if (candidate && candidate->name() == requestedClip) idle = candidate;
    }
    if (!idle) idle = result->animations.front();

    int meshCount = 0;
    int skinnedCount = 0;
    result->scene->traverse([&](Object3D& object) {
        if (auto* mesh = dynamic_cast<Mesh*>(&object)) {
            ++meshCount;
            if (dynamic_cast<SkinnedMesh*>(mesh)) ++skinnedCount;
        }
    });

    chrononmotion::motion::MotionScene motion("layered_youtube_20261107", float(FPS));

    // ---- Layer 1: foreground creator -------------------------------------
    chrononmotion::motion::MeshAttachment creatorAttachment;
    creatorAttachment.assetId = "creator_asset";
    auto& creator = motion.addMesh(1, "foreground_creator", std::move(creatorAttachment));
    creator.transform.position = {-0.35f, 0.f, 2.55f};
    creator.castsShadow = true;
    creator.acceptsShadow = true;

    // ---- Floor: shadow receiver and colour bounce ------------------------
    chrononmotion::motion::MeshAttachment floorAttachment;
    floorAttachment.geometry = chrononmotion::BoxGeometry::create(20.f, 0.12f, 20.f);
    floorAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            10, chrononmotion::Color(0x070b12), 0.40f, 0.28f);
    auto& floor = motion.addMesh(10, "studio_floor", std::move(floorAttachment));
    floor.transform.position = {0.f, -0.06f, 0.f};
    floor.castsShadow = false;
    floor.acceptsShadow = true;

    // ---- Layer 2: midground cube and pedestal ----------------------------
    chrononmotion::motion::MeshAttachment midPedestalAttachment;
    midPedestalAttachment.geometry = chrononmotion::BoxGeometry::create(1.00f, 0.45f, 1.00f);
    midPedestalAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            11, chrononmotion::Color(0x1a2333), 0.55f, 0.25f);
    auto& midPedestal = motion.addMesh(11, "mid_pedestal", std::move(midPedestalAttachment));
    midPedestal.transform.position = {0.30f, 0.225f, 0.0f};
    midPedestal.castsShadow = true;
    midPedestal.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment cubeAttachment;
    cubeAttachment.geometry = chrononmotion::BoxGeometry::create(0.90f, 0.90f, 0.90f);
    cubeAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            12, chrononmotion::Color(0xff5e3d), 0.16f, 0.48f);
    auto& cube = motion.addMesh(12, "midground_cube", std::move(cubeAttachment));
    cube.transform.position = {0.30f, 0.90f, 0.0f};
    cube.castsShadow = true;
    cube.acceptsShadow = true;
    cube.tracks.rotation.add(0.f, {0.18f, 0.35f, 0.06f});
    cube.tracks.rotation.add(kDuration, {0.18f, 0.35f + 2.f * kPi, 0.06f});

    // ---- Layer 3: background metallic sphere and pedestal ----------------
    chrononmotion::motion::MeshAttachment backPedestalAttachment;
    backPedestalAttachment.geometry = chrononmotion::BoxGeometry::create(1.15f, 0.65f, 1.15f);
    backPedestalAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            13, chrononmotion::Color(0x161f2e), 0.52f, 0.22f);
    auto& backPedestal = motion.addMesh(13, "back_pedestal", std::move(backPedestalAttachment));
    backPedestal.transform.position = {1.05f, 0.325f, -3.80f};
    backPedestal.castsShadow = true;
    backPedestal.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment sphereAttachment;
    sphereAttachment.geometry = chrononmotion::SphereGeometry::create(0.82f, 64, 48);
    sphereAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            14, chrononmotion::Color(0x24d7cd), 0.08f, 0.88f);
    auto& sphere = motion.addMesh(14, "background_sphere", std::move(sphereAttachment));
    sphere.transform.position = {1.05f, 1.47f, -3.80f};
    sphere.castsShadow = true;
    sphere.acceptsShadow = true;
    sphere.tracks.position.add(0.f, {1.05f, 1.47f, -3.80f});
    sphere.tracks.position.add(kDuration * 0.5f, {1.05f, 1.54f, -3.80f});
    sphere.tracks.position.add(kDuration, {1.05f, 1.47f, -3.80f});

    // ---- Layer 4: backdrop card (rendered behind everything) -------------
    chrononmotion::motion::MeshAttachment backdropAttachment;
    backdropAttachment.geometry = chrononmotion::PlaneGeometry::create(24.f, 14.f, 1, 1);
    backdropAttachment.material = chrononmotion::MaterialDescriptor::unlit(20, chrononmotion::Color(0xffffff));
    auto& backdrop = motion.addMesh(20, "generated_backdrop", std::move(backdropAttachment));
    backdrop.transform.position = {0.f, 2.0f, -6.90f};
    backdrop.castsShadow = false;
    backdrop.acceptsShadow = false;

    // ---- Independent lighting -------------------------------------------
    auto& ambient = motion.addAmbientLight(100, "cool_ambient");
    ambient.color = chrononmotion::Color(0x9fb4e8);
    ambient.intensity = 0.35f;

    auto& key = motion.addDirectionalLight(101, "warm_key");
    key.color = chrononmotion::Color(0xffefd6);
    key.intensity = 2.7f;
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
    warmFill.intensity = 10.f;
    warmFill.range = 8.f;
    warmFill.decay = 2.f;
    warmFill.positionTrack.add(0.f, {-2.6f, 2.2f, 3.0f});

    auto& cyanPractical = motion.addPointLight(104, "cyan_practical");
    cyanPractical.color = chrononmotion::Color(0x3fe7ff);
    cyanPractical.intensity = 16.f;
    cyanPractical.range = 8.f;
    cyanPractical.decay = 2.f;
    cyanPractical.positionTrack.add(0.f, {0.35f, 2.4f, -4.6f});

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    const std::string glRenderer = rendererName ? rendererName : "unknown";
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
    bridge.bindAsset("creator_asset", result->scene);
    bridge.bindLayerTexture(20, backdropTexture(1024, 576));
    bridge.evaluateAndSync(motion, firstFrame);
    bridge.bindLayerAnimation(1, idle);
    bridge.evaluateAndSync(motion, firstFrame);

    auto creatorNode = bridge.layerNode(1);
    if (!creatorNode) throw std::runtime_error("creator layer was not materialized");
    Box3 creatorBox;
    creatorBox.setFromObject(*creatorNode, true);
    Vector3 creatorCenter, creatorSize;
    creatorBox.getCenter(creatorCenter);
    creatorBox.getSize(creatorSize);
    if (creatorSize.y > 1e-4f) {
        const float uniformScale = 1.82f / creatorSize.y;
        if (std::abs(uniformScale - 1.f) > 0.02f) {
            creator.transform.scale = {uniformScale, uniformScale, uniformScale};
            bridge.evaluateAndSync(motion, firstFrame);
            creatorBox.setFromObject(*creatorNode, true);
            creatorBox.getSize(creatorSize);
        }
    }
    if (std::abs(creatorBox.min().y) > 0.01f) {
        creator.transform.position.y -= creatorBox.min().y;
        bridge.evaluateAndSync(motion, firstFrame);
        creatorBox.setFromObject(*creatorNode, true);
        creatorBox.getSize(creatorSize);
    }
    std::cout << "creator asset=" << asset.filename().string() << " clip=" << idle->name()
              << " bbox minY=" << creatorBox.min().y << " size="
              << creatorSize.x << "x" << creatorSize.y << "x" << creatorSize.z << "\n";

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

    // Neon ring set dressing is deliberately a direct scene object: it is not a
    // content layer and must stay behind the bridge-managed layer stack.
    auto ring = Mesh::create(TorusGeometry::create(2.22f, 0.030f, 12, 160), neonMaterial(0x35d7ff));
    ring->position.set(0.45f, 2.05f, -5.85f);
    bridge.scene()->add(ring);
    auto halo = Mesh::create(TorusGeometry::create(2.22f, 0.10f, 12, 160), neonMaterial(0x2a6bff, 0.12f));
    halo->position.set(0.45f, 2.05f, -5.87f);
    bridge.scene()->add(halo);

    auto updateCamera = [&](int frame) {
        const float t = float(frame) / float(FPS);
        const float u = t / kDuration;
        const float sweep = -0.45f * std::cos(2.f * kPi * u);
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
        halo->rotation.z = ring->rotation.z;
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
              << "  \"fixture\": \"three-layer YouTube depth/lighting demo\",\n"
              << "  \"output\": \"1920x1080 30fps " << frameCount << " frames\",\n"
              << "  \"layers\": [\n"
              << "    {\"name\":\"foreground_creator\",\"depth_z\":2.55,\"source\":\"" << asset.filename().string()
              << "\",\"clip\":\"" << idle->name() << "\",\"license\":\""
              << (asset.filename().string().find("low-poly") != std::string::npos
                              ? "CC BY 4.0 (Razvan Savescu / SingleWayArt)"
                              : "CC0 1.0 (Quaternius Universal Animation Library)")
              << "\"},\n"
              << "    {\"name\":\"midground_cube\",\"depth_z\":0.0,\"material\":\"PBR orange metal/rough\",\"motion\":\"Y rotation 360deg over 10s\"},\n"
              << "    {\"name\":\"background_sphere\",\"depth_z\":-3.8,\"material\":\"PBR cyan metal\",\"motion\":\"7cm vertical float\"},\n"
              << "    {\"name\":\"generated_backdrop\",\"depth_z\":-6.9,\"material\":\"unlit procedural gradient\"}\n"
              << "  ],\n"
              << "  \"lights\": [\"cool ambient\",\"warm directional key with 2048 shadow map\",\"cool directional rim\",\"warm point fill\",\"cyan point practical\"],\n"
              << "  \"camera\": \"lateral dolly -0.55..+0.55, push-in 8.10..7.50, loop-safe\"\n"
              << "}\n";

    std::ofstream metadata(out / "metadata.json");
    metadata << std::fixed << std::setprecision(4)
             << "{\n  \"width\": " << W << ",\n  \"height\": " << H
             << ",\n  \"fps\": " << FPS << ",\n  \"frames_rendered\": " << frameCount
             << ",\n  \"first_frame\": " << firstFrame
             << ",\n  \"duration_seconds\": " << double(frameCount) / FPS
             << ",\n  \"mesh_count\": " << meshCount
             << ",\n  \"skinned_mesh_count\": " << skinnedCount
             << ",\n  \"asset\": \"" << asset.filename().string() << "\""
             << ",\n  \"clip\": \"" << idle->name() << "\""
             << ",\n  \"clip_duration_seconds\": " << idle->getDuration()
             << ",\n  \"creator_bbox_min_y\": " << creatorBox.min().y
             << ",\n  \"creator_bbox_height\": " << creatorSize.y
             << ",\n  \"render_seconds\": "
             << std::chrono::duration<double>(finished - started).count()
             << ",\n  \"png_bytes\": " << bytesWritten
             << ",\n  \"gl_renderer\": \"" << glRenderer << "\""
             << ",\n  \"gl_version\": \"" << (reinterpret_cast<const char*>(glGetString(GL_VERSION)) ?: "unknown") << "\""
             << ",\n  \"video_encoder\": \"libx264 (external ffmpeg)\"\n}\n";

    renderer.dispose();
    std::cout << "LAYERED_YOUTUBE PASS frames=" << frameCount << " gl=" << glRenderer
              << " clip=" << idle->name() << " png_bytes=" << bytesWritten << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "LAYERED_YOUTUBE FAIL: " << error.what() << '\n';
    return 1;
}
