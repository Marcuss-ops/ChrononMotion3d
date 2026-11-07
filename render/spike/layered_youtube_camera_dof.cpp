// Camera / DOF / occlusion diagnostic for the layered YouTube scene.
//
// The cutout foreground is transparent, so a single BokehPass depth prepass
// would treat its whole rectangle as a solid occluder and falsify focus behind
// it. Instead each depth layer is rendered on transparent black, defocused
// with the real BokehPass at the chosen focus distance, and composited back to
// front. That keeps both the blur and the layer occlusion honest.
//
// Output: 3 camera positions x 3 focus layers (person, cube, sphere), 1280x720.
#include <algorithm>
#include <array>
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
#include "threepp/postprocessing/BokehPass.hpp"
#include "threepp/postprocessing/EffectComposer.hpp"
#include "threepp/postprocessing/RenderPass.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/DataTexture.hpp"
#include "threepp/textures/Texture.hpp"

using namespace threepp;
namespace fs = std::filesystem;
namespace {
constexpr int W = 1280;
constexpr int H = 720;
constexpr int FPS = 30;
constexpr int kFrame = 45;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kAperture = 0.0052f;
constexpr float kMaxBlur = 0.020f;

#ifndef CHRONONMOTION_SPIKE_ASSET_DIR
#define CHRONONMOTION_SPIKE_ASSET_DIR "render/spike/assets"
#endif

enum : std::uint32_t {
    kPerson = 1,
    kDesk = 30,
    kFloor = 10,
    kMidPedestal = 11,
    kCube = 12,
    kBackPedestal = 13,
    kSphere = 14,
    kBackdrop = 20,
};

const std::array<std::uint32_t, 8> kAllLayers{
        kPerson, kDesk, kFloor, kMidPedestal, kCube, kBackPedestal, kSphere, kBackdrop};

void writePng(const fs::path& path, const std::vector<unsigned char>& rgb) {
    if (rgb.size() != std::size_t(W) * H * 3) {
        throw std::runtime_error("writePng: unexpected RGB size");
    }
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

void flipRgba(std::vector<unsigned char>& rgba) {
    const std::size_t stride = std::size_t(W) * 4;
    std::vector<unsigned char> row(stride);
    for (int y = 0; y < H / 2; ++y) {
        auto* a = rgba.data() + std::size_t(y) * stride;
        auto* b = rgba.data() + std::size_t(H - 1 - y) * stride;
        std::copy_n(a, stride, row.data());
        std::copy_n(b, stride, a);
        std::copy_n(row.data(), stride, b);
    }
}

void writeRgbaPng(const fs::path& path, std::vector<unsigned char> rgba) {
    if (rgba.size() != std::size_t(W) * H * 4) {
        throw std::runtime_error("writeRgbaPng: unexpected RGBA size");
    }
    flipRgba(rgba);
    if (!stbi_write_png(path.c_str(), W, H, 4, rgba.data(), W * 4)) {
        throw std::runtime_error("RGBA PNG write failed: " + path.string());
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

std::shared_ptr<MeshBasicMaterial> neonMaterial(std::uint32_t hex, float opacity = 1.f) {
    auto material = MeshBasicMaterial::create();
    material->color.setHex(hex);
    material->transparent = opacity < 1.f;
    material->opacity = opacity;
    material->toneMapped = false;
    material->depthWrite = opacity >= 1.f;
    return material;
}

struct CameraPreset {
    const char* name;
    Vector3 position;
    Vector3 target;
};

struct FocusPreset {
    const char* name;
    std::uint32_t layer;
    Vector3 anchor;
};

void setLayerVisibility(const threepp::motion::MotionSceneBridge& bridge,
                        const std::vector<std::uint32_t>& visible,
                        const std::shared_ptr<Object3D>& ring,
                        const std::shared_ptr<Object3D>& halo,
                        bool ringVisible) {
    for (const auto id : kAllLayers) {
        auto node = bridge.layerNode(id);
        if (!node) throw std::runtime_error("missing layer node " + std::to_string(id));
        node->visible = std::find(visible.begin(), visible.end(), id) != visible.end();
    }
    if (ring) ring->visible = ringVisible;
    if (halo) halo->visible = ringVisible;
}

float focusDistance(const Vector3& camera, const Vector3& target, const Vector3& anchor) {
    Vector3 forward = target - camera;
    forward.normalize();
    return std::max(0.1f, (anchor - camera).dot(forward));
}
}// namespace

int main(int argc, char** argv) try {
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY") &&
        !std::getenv("THREEPP_GLFW_PLATFORM")) {
        setenv("THREEPP_GLFW_PLATFORM", "egl", 0);
    }

    const fs::path out = argc > 1 ? fs::path(argv[1])
                                  : fs::path("out/camera_dof_layers_20261107");
    const fs::path cutout = argc > 2
            ? fs::path(argv[2])
            : fs::path(CHRONONMOTION_SPIKE_ASSET_DIR) / "creator_transparent.png";
    const fs::path framesDir = out / "plates";
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

    chrononmotion::motion::MotionScene motion("layered_camera_dof", float(FPS));

    // Foreground 2.5D creator and the desk that hides the lower seam.
    const float planeH = 1.90f;
    const float planeW = planeH * float(imageWidth) / float(imageHeight);
    chrononmotion::motion::MeshAttachment creatorAttachment;
    creatorAttachment.geometry = chrononmotion::PlaneGeometry::create(planeW, planeH, 2, 2);
    creatorAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    creatorAttachment.material.transparent = true;
    auto& creator = motion.addMesh(kPerson, "foreground_creator_cutout", std::move(creatorAttachment));
    creator.transform.position = {-0.80f, 1.00f, 2.65f};
    creator.castsShadow = false;
    creator.acceptsShadow = false;

    chrononmotion::motion::MeshAttachment deskAttachment;
    deskAttachment.geometry = chrononmotion::BoxGeometry::create(9.0f, 0.90f, 0.90f);
    deskAttachment.material = chrononmotion::MaterialDescriptor::unlit(30, chrononmotion::Color(0x02050b));
    auto& desk = motion.addMesh(kDesk, "foreground_desk", std::move(deskAttachment));
    desk.transform.position = {0.f, -0.33f, 3.20f};
    desk.castsShadow = true;
    desk.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment floorAttachment;
    floorAttachment.geometry = chrononmotion::BoxGeometry::create(20.f, 0.12f, 20.f);
    floorAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            10, chrononmotion::Color(0x05070c), 0.92f, 0.0f);
    auto& floor = motion.addMesh(kFloor, "studio_floor", std::move(floorAttachment));
    floor.transform.position = {0.f, -0.06f, 0.f};
    floor.castsShadow = false;
    floor.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment midPedestalAttachment;
    midPedestalAttachment.geometry = chrononmotion::BoxGeometry::create(1.00f, 0.45f, 1.00f);
    midPedestalAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            11, chrononmotion::Color(0x1a2333), 0.55f, 0.25f);
    auto& midPedestal = motion.addMesh(kMidPedestal, "mid_pedestal", std::move(midPedestalAttachment));
    midPedestal.transform.position = {0.78f, 0.225f, 0.0f};
    midPedestal.castsShadow = true;
    midPedestal.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment cubeAttachment;
    cubeAttachment.geometry = chrononmotion::BoxGeometry::create(0.90f, 0.90f, 0.90f);
    cubeAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            12, chrononmotion::Color(0xff5e3d), 0.32f, 0.22f);
    auto& cube = motion.addMesh(kCube, "midground_cube", std::move(cubeAttachment));
    cube.transform.position = {0.78f, 0.90f, 0.0f};
    cube.castsShadow = true;
    cube.acceptsShadow = true;
    cube.tracks.rotation.add(0.f, {0.18f, 0.35f, 0.06f});
    cube.tracks.rotation.add(10.f, {0.18f, 0.35f + 2.f * kPi, 0.06f});

    chrononmotion::motion::MeshAttachment backPedestalAttachment;
    backPedestalAttachment.geometry = chrononmotion::BoxGeometry::create(1.15f, 0.65f, 1.15f);
    backPedestalAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            13, chrononmotion::Color(0x161f2e), 0.52f, 0.22f);
    auto& backPedestal = motion.addMesh(kBackPedestal, "back_pedestal", std::move(backPedestalAttachment));
    backPedestal.transform.position = {1.62f, 0.325f, -3.80f};
    backPedestal.castsShadow = true;
    backPedestal.acceptsShadow = true;

    chrononmotion::motion::MeshAttachment sphereAttachment;
    sphereAttachment.geometry = chrononmotion::SphereGeometry::create(0.82f, 64, 48);
    sphereAttachment.material = chrononmotion::MaterialDescriptor::pbr(
            14, chrononmotion::Color(0x24d7cd), 0.08f, 0.88f);
    auto& sphere = motion.addMesh(kSphere, "background_sphere", std::move(sphereAttachment));
    sphere.transform.position = {1.62f, 1.47f, -3.80f};
    sphere.castsShadow = true;
    sphere.acceptsShadow = true;
    sphere.tracks.position.add(0.f, {1.62f, 1.47f, -3.80f});
    sphere.tracks.position.add(5.f, {1.62f, 1.54f, -3.80f});
    sphere.tracks.position.add(10.f, {1.62f, 1.47f, -3.80f});

    chrononmotion::motion::MeshAttachment backdropAttachment;
    backdropAttachment.geometry = chrononmotion::PlaneGeometry::create(24.f, 14.f, 1, 1);
    backdropAttachment.material = chrononmotion::MaterialDescriptor::unlit(20, chrononmotion::Color(0xffffff));
    auto& backdrop = motion.addMesh(kBackdrop, "generated_backdrop", std::move(backdropAttachment));
    backdrop.transform.position = {0.f, 2.0f, -6.90f};
    backdrop.castsShadow = false;
    backdrop.acceptsShadow = false;

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
    if (glRenderer.find("NVIDIA") == std::string::npos) {
        throw std::runtime_error("camera/DOF demo requires NVIDIA hardware GL; selected: " + glRenderer);
    }
    const char* glVersion = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 1.0f;
    renderer.shadowMap().enabled = true;
    renderer.shadowMap().type = ShadowMap::PFCSoft;

    threepp::motion::MotionSceneBridge bridge(38.f, float(W) / float(H), 0.05f, 100.f);
    bridge.scene()->environment = studioEnvironment();
    bridge.bindLayerTexture(kPerson, cutoutTexture);
    bridge.bindLayerTexture(kBackdrop, backdropTexture(1024, 576));
    bridge.evaluateAndSync(motion, kFrame);

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

    EffectComposer::Options options;
    options.samples = 4;
    EffectComposer composer(renderer, options);
    composer.renderToScreen = false;
    composer.setSize(W, H);
    composer.addPass(std::make_shared<RenderPass>(*bridge.scene(), *bridge.camera()));
    auto bokeh = std::make_shared<BokehPass>(*bridge.scene(), *bridge.camera(), 1.f, kAperture, kMaxBlur);
    bokeh->aspect = float(W) / float(H);
    bokeh->setDisk(true);
    composer.addPass(bokeh);

    const std::array<CameraPreset, 3> cameras{{
            {"left", {-2.20f, 2.05f, 8.00f}, {0.05f, 1.10f, 0.15f}},
            {"center", {0.0f, 2.10f, 7.80f}, {0.05f, 1.10f, 0.15f}},
            {"right", {2.20f, 2.20f, 8.00f}, {0.05f, 1.10f, 0.15f}},
    }};
    const std::array<FocusPreset, 3> focusSets{{
            {"person", kPerson, {-0.80f, 1.00f, 2.65f}},
            {"cube", kCube, {0.78f, 0.90f, 0.0f}},
            {"sphere", kSphere, {1.62f, 1.47f, -3.80f}},
    }};

    auto renderPlate = [&](bool withBokeh, float clearAlpha) {
        bokeh->enabled = withBokeh;
        renderer.setClearColor(Color(0x060a12), clearAlpha);
        composer.render(1.f / float(FPS));
        renderer.setRenderTarget(&composer.readBuffer());
        std::vector<unsigned char> rgba(std::size_t(W) * H * 4);
        renderer.readPixels({0, 0}, {W, H}, Format::RGBA, rgba.data());
        renderer.setRenderTarget(nullptr);
        return rgba;
    };

    auto compositeOver = [](std::vector<unsigned char>& dst,
                            const std::vector<unsigned char>& src) {
        for (std::size_t i = 0; i < dst.size(); i += 4) {
            const float a = float(src[i + 3]) / 255.f;
            const float inv = 1.f - a;
            // The GL blend path writes premultiplied RGB into a transparent
            // target; BokehPass filters RGBA together, so this stays valid.
            dst[i] = static_cast<unsigned char>(std::min(255.f, float(src[i]) + float(dst[i]) * inv));
            dst[i + 1] = static_cast<unsigned char>(std::min(255.f, float(src[i + 1]) + float(dst[i + 1]) * inv));
            dst[i + 2] = static_cast<unsigned char>(std::min(255.f, float(src[i + 2]) + float(dst[i + 2]) * inv));
            dst[i + 3] = 255;
        }
    };

    std::ofstream shots(out / "shots.json");
    shots << "{\n  \"renderer\": \"" << glRenderer << "\",\n"
          << "  \"gl_version\": \"" << (glVersion ? glVersion : "unknown") << "\",\n"
          << "  \"width\": " << W << ",\n  \"height\": " << H << ",\n"
          << "  \"frame\": " << kFrame << ",\n"
          << "  \"bokeh_pass\": {\"kernel\": \"disk_vogel_96\", \"aperture\": " << kAperture
          << ", \"max_blur_uv\": " << kMaxBlur << "},\n"
          << "  \"layer_order\": [\"stage\", \"sphere\", \"cube\", \"person\"],\n"
          << "  \"shots\": [\n";

    bool firstShot = true;
    for (std::size_t ci = 0; ci < cameras.size(); ++ci) {
        const auto& cam = cameras[ci];
        bridge.camera()->position = cam.position;
        bridge.camera()->lookAt(cam.target);
        bridge.camera()->updateMatrixWorld(true);

        // The stage is independent of focus and stays sharp.
        setLayerVisibility(bridge, {kFloor, kBackdrop}, ring, ringHalo, false);
        const auto stage = renderPlate(false, 1.f);

        for (const auto& focus : focusSets) {
            const float focusAt = focusDistance(cam.position, cam.target, focus.anchor);

            setLayerVisibility(bridge, {kBackPedestal, kSphere}, ring, ringHalo, true);
            bokeh->focus = focusAt;
            auto spherePlate = renderPlate(true, 0.f);

            setLayerVisibility(bridge, {kMidPedestal, kCube}, ring, ringHalo, false);
            bokeh->focus = focusAt;
            auto cubePlate = renderPlate(true, 0.f);

            setLayerVisibility(bridge, {kPerson, kDesk}, ring, ringHalo, false);
            bokeh->focus = focusAt;
            auto personPlate = renderPlate(true, 0.f);

            auto frame = stage;
            compositeOver(frame, spherePlate);
            compositeOver(frame, cubePlate);
            compositeOver(frame, personPlate);

            std::vector<unsigned char> rgb(std::size_t(W) * H * 3);
            for (std::size_t i = 0, j = 0; i < frame.size(); i += 4, j += 3) {
                rgb[j] = frame[i];
                rgb[j + 1] = frame[i + 1];
                rgb[j + 2] = frame[i + 2];
            }

            const std::string stem = std::string("camera_") + cam.name + "_focus_" + focus.name;
            writePng(out / (stem + ".png"), rgb);

            if (!firstShot) shots << ",\n";
            firstShot = false;
            shots << "    {\"file\": \"" << stem << ".png\", \"camera\": \"" << cam.name
                  << "\", \"focus_layer\": \"" << focus.name << "\", \"focus_distance\": "
                  << std::fixed << std::setprecision(4) << focusAt
                  << ", \"camera_position\": [" << cam.position.x << ", " << cam.position.y
                  << ", " << cam.position.z << "]}";
            std::cout << "rendered " << stem << " focus=" << focusAt << "\n";
        }
    }
    shots << "\n  ]\n}\n";
    shots.close();

    // Center-camera plates for the layer breakdown. Plates keep their alpha;
    // the Python sheet composites them over the sharp stage using the same
    // premultiplied convention as the C++ renderer.
    bridge.camera()->position = cameras[1].position;
    bridge.camera()->lookAt(cameras[1].target);
    bridge.camera()->updateMatrixWorld(true);
    setLayerVisibility(bridge, {kFloor, kBackdrop}, ring, ringHalo, false);
    writeRgbaPng(out / "plates" / "stage.png", renderPlate(false, 1.f));

    auto writePlate = [&](const char* filename,
                          const std::vector<std::uint32_t>& layers,
                          bool ringVisible,
                          const FocusPreset& focus) {
        setLayerVisibility(bridge, layers, ring, ringHalo, ringVisible);
        bokeh->focus = focusDistance(cameras[1].position, cameras[1].target, focus.anchor);
        writeRgbaPng(out / "plates" / filename, renderPlate(true, 0.f));
    };
    writePlate("person_plate.png", {kPerson, kDesk}, false, focusSets[0]);
    writePlate("cube_plate.png", {kMidPedestal, kCube}, false, focusSets[1]);
    writePlate("sphere_plate.png", {kBackPedestal, kSphere}, true, focusSets[2]);

    renderer.dispose();
    std::cout << "CAMERA_DOF_LAYERS PASS shots=9 out=" << out << " gl=" << glRenderer << "\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "CAMERA_DOF_LAYERS FAIL: " << error.what() << '\n';
    return 1;
}
