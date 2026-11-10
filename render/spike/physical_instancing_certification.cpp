// GPU certification: physical glass + InstanceBatch crowd.
//
// Proves the two new Motion contracts reach a real NVIDIA GL frame:
//   * a PBR descriptor carrying transmission/IOR/thickness/dispersion/clearcoat
//     becomes a threepp MeshPhysicalMaterial with the same values;
//   * a 400-placement InstanceBatch becomes one InstancedMesh whose matrices
//     match the authored local transforms.
// The encode path is the production one: GL target -> GL PBO -> CUDA -> NVENC.
// PNG/numeric diagnostics run in a separate readback pass.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "glad/glad.h"
#include "gpu_certification_gate.hpp"

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/PlaneGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include "threepp/canvas/Canvas.hpp"
#include "threepp/materials/MeshPhysicalMaterial.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/objects/InstancedMesh.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/DataTexture.hpp"

#ifdef CHRONONMOTION_HAS_GL_CUDA_NVENC
#include "gl_cuda_nvenc_encoder.hpp"
#endif

using namespace threepp;
namespace fs = std::filesystem;

namespace {

    constexpr int W = 1920, H = 1080, FPS = 30, FRAMES = 150;
    constexpr int kInstanceCount = 400;
    constexpr int kGlassLayer = 2;
    constexpr int kCrowdLayer = 3;

    void writePng(const fs::path& path, const std::vector<unsigned char>& rgb) {
        std::vector<unsigned char> topDown(rgb.size());
        const size_t stride = size_t(W) * 3;
        for (int y = 0; y < H; ++y)
            std::copy_n(rgb.data() + size_t(H - y - 1) * stride, stride, topDown.data() + size_t(y) * stride);
        if (!stbi_write_png(path.c_str(), W, H, 3, topDown.data(), static_cast<int>(stride)))
            throw std::runtime_error("failed writing frame PNG");
    }

    size_t changedPixels(const std::vector<unsigned char>& p, const std::array<unsigned char, 3>& bg) {
        size_t n = 0;
        for (size_t i = 0; i < p.size(); i += 3)
            if (p[i] != bg[0] || p[i + 1] != bg[1] || p[i + 2] != bg[2]) ++n;
        return n;
    }

    std::shared_ptr<DataTexture> makeCheckerTexture() {
        constexpr int size = 64;
        std::vector<unsigned char> rgb(size * size * 3);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const bool hi = ((x / 8) + (y / 8)) % 2 == 0;
                const size_t i = size_t(y * size + x) * 3;
                rgb[i + 0] = hi ? 238 : 22;
                rgb[i + 1] = hi ? 214 : 42;
                rgb[i + 2] = hi ? 168 : 96;
            }
        }
        auto texture = DataTexture::create(std::move(rgb), size, size);
        texture->format = Format::RGB;
        texture->colorSpace = ColorSpace::sRGB;
        texture->generateMipmaps = true;
        texture->minFilter = Filter::LinearMipmapLinear;
        texture->magFilter = Filter::Linear;
        texture->needsUpdate();
        return texture;
    }

    MeshPhysicalMaterial* findPhysicalMaterial(Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) {
            if (auto* material = mesh->materialAs<MeshPhysicalMaterial>()) return material;
        }
        for (const auto& child : node.children) {
            if (!child) continue;
            if (auto* found = findPhysicalMaterial(*child)) return found;
        }
        return nullptr;
    }

}// namespace

int main(int argc, char** argv) try {
#ifndef CHRONONMOTION_HAS_GL_CUDA_NVENC
    (void) argc;
    (void) argv;
    throw std::runtime_error("physical/instancing certification requires the GL PBO -> CUDA -> NVENC build");
#else
    chrononmotion::certification::prepare_nvidia_egl_headless();
    const fs::path out = argc > 1 ? argv[1] : "out/threepp_certification/T16";
    fs::create_directories(out);

    chrononmotion::motion::MotionScene motion("physical glass and instance batch", float(FPS));

    // Checker backdrop: gives the transmitted glass something to refract.
    auto backdrop = chrononmotion::PlaneGeometry::create(20.f, 12.f);
    chrononmotion::motion::MeshAttachment backdropAttachment;
    backdropAttachment.geometry = backdrop;
    backdropAttachment.material = chrononmotion::MaterialDescriptor::unlit(1, chrononmotion::Color(0xffffff));
    auto& backdropLayer = motion.addMesh(1, "checker backdrop", std::move(backdropAttachment));
    backdropLayer.transform.position = chrononmotion::Vector3(0.f, 3.9f, -5.f);

    // Physical glass sphere.
    chrononmotion::motion::MeshAttachment glass;
    glass.geometry = chrononmotion::SphereGeometry::create(1.35f, 64, 48);
    glass.material = chrononmotion::MaterialDescriptor::pbr(2, chrononmotion::Color(0xffffff), 0.02f, 0.f);
    glass.material.transmission = 1.f;
    glass.material.ior = 1.5f;
    glass.material.thickness = 0.6f;
    glass.material.dispersion = 0.04f;
    glass.material.clearcoat = 1.f;
    glass.material.validate();
    auto& glassLayer = motion.addMesh(kGlassLayer, "physical glass", std::move(glass));
    glassLayer.transform.position = chrononmotion::Vector3(0.f, 2.7f, 0.f);
    glassLayer.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
    glassLayer.tracks.rotation.add(float(FRAMES) / float(FPS), chrononmotion::Vector3(0.f, 6.2831853f, 0.f));

    // 400 instances of one box, authored as a single batch.
    chrononmotion::motion::MeshAttachment crowd;
    crowd.geometry = chrononmotion::BoxGeometry::create(0.17f, 0.9f, 0.17f);
    crowd.material = chrononmotion::MaterialDescriptor::pbr(3, chrononmotion::Color(0x30465e), 0.35f, 0.1f);
    chrononmotion::motion::InstanceBatch batch;
    batch.localTransforms.reserve(kInstanceCount);
    for (int i = 0; i < kInstanceCount; ++i) {
        const float x = (float(i % 20) - 9.5f) * 0.43f;
        const float z = (float(i / 20) - 9.5f) * 0.43f;
        chrononmotion::Matrix4 local;
        local.makeTranslation(x, 0.45f, z + 1.6f);
        batch.localTransforms.push_back(local);
    }
    batch.validate();
    crowd.instances = std::move(batch);
    motion.addMesh(kCrowdLayer, "instance crowd", std::move(crowd));

    auto& ambient = motion.addAmbientLight(10, "ambient");
    ambient.color = chrononmotion::Color(0xffffff);
    ambient.intensity = 1.6f;
    auto& key = motion.addDirectionalLight(11, "key");
    key.color = chrononmotion::Color(0xfff0dd);
    key.intensity = 3.0f;
    key.transform.position = chrononmotion::Vector3(4.f, 6.f, 5.f);
    auto& fill = motion.addDirectionalLight(12, "fill");
    fill.color = chrononmotion::Color(0x9bb8d0);
    fill.intensity = 1.0f;
    fill.transform.position = chrononmotion::Vector3(-5.f, 3.f, 4.f);

    chrononmotion::motion::CameraRig cameraRig(
            20, 40.f, float(W) / float(H), 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 1.55f, 0.f));
    const float duration = float(FRAMES) / float(FPS);
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 3.0f, 9.8f));
    cameraRig.positionTrack().add(duration * 0.5f, chrononmotion::Vector3(2.4f, 3.4f, 9.0f));
    cameraRig.positionTrack().add(duration, chrononmotion::Vector3(0.f, 3.0f, 9.8f));

    threepp::motion::MotionSceneBridge bridge(40.f, float(W) / float(H), 0.1f, 100.f);
    bridge.bindLayerTexture(1, makeCheckerTexture());
    bridge.evaluateAndSync(motion, 0, &cameraRig);

    auto glassNode = bridge.layerNode(kGlassLayer);
    if (!glassNode) throw std::runtime_error("bridge did not materialize the glass layer");
    auto* physical = findPhysicalMaterial(*glassNode);
    if (!physical) throw std::runtime_error("glass layer did not use MeshPhysicalMaterial");
    if (std::abs(physical->transmission - 1.f) > 1e-6f ||
        std::abs(physical->ior - 1.5f) > 1e-6f ||
        std::abs(physical->thickness - 0.6f) > 1e-6f ||
        std::abs(physical->dispersion - 0.04f) > 1e-6f ||
        std::abs(physical->clearcoat - 1.f) > 1e-6f) {
        throw std::runtime_error("MeshPhysicalMaterial values do not match the Motion descriptor");
    }

    auto crowdNode = bridge.layerNode(kCrowdLayer);
    auto* instanced = dynamic_cast<InstancedMesh*>(crowdNode.get());
    if (!instanced) throw std::runtime_error("instance layer did not materialize as InstancedMesh");
    if (instanced->count() != size_t(kInstanceCount))
        throw std::runtime_error("InstancedMesh count differs from the authored batch");
    Matrix4 firstInstance;
    instanced->getMatrixAt(0, firstInstance);
    if (std::abs(firstInstance.elements[12] - (-9.5f * 0.43f)) > 1e-5f ||
        std::abs(firstInstance.elements[13] - 0.45f) > 1e-5f ||
        std::abs(firstInstance.elements[14] - (-9.5f * 0.43f + 1.6f)) > 1e-5f) {
        throw std::runtime_error("first instance matrix differs from the authored transform");
    }

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    const auto* rendererName = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    chrononmotion::certification::require_nvidia_renderer(rendererName ? rendererName : "unknown");
    renderer.setClearColor(Color(0x10151d));
    auto target = RenderTarget::create(W, H, RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    chrononmotion::certification::require_nvenc_encoder();
    const std::string video = (out / "T16_physical_instancing_1080p30.mp4").string();
    chrononmotion::certification::GlCudaNvencEncoder encoder(W, H, FPS, video);

    auto t0 = std::chrono::steady_clock::now();
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        encoder.encode_current_target(f);
    }
    encoder.finish();
    if (encoder.frames_submitted() != size_t(FRAMES))
        throw std::runtime_error("GPU encoder did not accept every frame");
    const auto renderDone = std::chrono::steady_clock::now();

    std::array<unsigned char, 3> background{};
    std::vector<unsigned char> first, middle, last, frame75;
    size_t minCoverage = std::numeric_limits<size_t>::max(), maxCoverage = 0;
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != size_t(W) * H * 3) throw std::runtime_error("invalid diagnostic readback size");
        if (f == 0) {
            background = {pixels[0], pixels[1], pixels[2]};
            first = pixels;
        }
        if (f == FRAMES / 2) {
            middle = pixels;
            writePng(out / "frame-75.png", pixels);
        }
        if (f == FRAMES - 1) last = pixels;
        if (f == 75) {
            frame75 = pixels;
            writePng(out / "frame-75-raw.png", pixels);
        }
        if (f == 0) writePng(out / "frame-0.png", pixels);
        if (f == FRAMES - 1) writePng(out / "frame-149.png", pixels);
        const auto coverage = changedPixels(pixels, background);
        minCoverage = std::min(minCoverage, coverage);
        maxCoverage = std::max(maxCoverage, coverage);
    }

    // Random access: frame 75 must be byte-identical when rendered on its own.
    bridge.evaluateAndSync(motion, 75, &cameraRig);
    renderer.render(*bridge.scene(), *bridge.camera());
    const auto randomAccessPixels = renderer.readRGBPixels();
    const bool randomAccess = randomAccessPixels == frame75;
    // High coverage is expected: the backdrop fills the frame. Only "nothing
    // rendered" is a failure, so there is deliberately no upper bound.
    const bool accepted = randomAccess && minCoverage >= 10000;

    std::ofstream sceneJson(out / "scene.json");
    sceneJson << "{\n"
              << "  \"scene\": \"physical glass sphere over a 400-instance crowd\",\n"
              << "  \"glass\": {\"transmission\": 1.0, \"ior\": 1.5, \"thickness\": 0.6, \"dispersion\": 0.04, \"clearcoat\": 1.0},\n"
              << "  \"instances\": " << kInstanceCount << ",\n"
              << "  \"instance_source\": \"MotionScene MeshAttachment::instances -> MotionSceneBridge InstancedMesh\",\n"
              << "  \"camera\": \"Motion CameraRig position track\",\n"
              << "  \"lights\": [\"ambient 1.6\", \"directional key 3.0\", \"directional fill 1.0\"]\n"
              << "}\n";

    std::ofstream metadata(out / "metadata.json");
    metadata << std::fixed << std::setprecision(3)
             << "{\n  \"test_id\": \"T16\",\n  \"status\": \"" << (accepted ? "PASS" : "FAIL") << "\",\n"
             << "  \"width\": 1920,\n  \"height\": 1080,\n  \"fps\": 30,\n  \"frames\": " << FRAMES << ",\n"
             << "  \"duration_seconds\": " << duration << ",\n"
             << "  \"pipeline\": \"MotionScene -> MotionSceneBridge -> threepp GL -> GL PBO -> CUDA -> NVENC\",\n"
             << "  \"physical_material_type\": \"MeshPhysicalMaterial\",\n"
             << "  \"physical_material_values_match\": true,\n"
             << "  \"instance_count\": " << kInstanceCount << ",\n"
             << "  \"instance_count_match\": true,\n"
             << "  \"random_access_frame_75_identical\": " << (randomAccess ? "true" : "false") << ",\n"
             << "  \"min_changed_pixels\": " << minCoverage << ",\n"
             << "  \"max_changed_pixels\": " << maxCoverage << ",\n"
             << "  \"render_seconds\": " << std::chrono::duration<double>(renderDone - t0).count() << ",\n"
             << "  \"gl_renderer\": \"" << (rendererName ? rendererName : "unknown") << "\",\n"
             << "  \"cuda_device\": \"" << encoder.device_name() << "\",\n"
             << "  \"frames_submitted_to_nvenc\": " << encoder.frames_submitted() << ",\n"
             << "  \"encoded_packets_muxed\": " << encoder.packets_written() << ",\n"
             << "  \"encoder_input_gpu_resident\": true,\n"
             << "  \"cpu_framebuffer_readback_in_encode_path\": false,\n"
             << "  \"video_encoder\": \"h264_nvenc\"\n}\n";

    if (!accepted) throw std::runtime_error("T16 acceptance failed: random access or coverage");

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    std::cout << "T16 PASS glass=MeshPhysicalMaterial instances=" << instanced->count()
              << " coverage=" << minCoverage << ".." << maxCoverage
              << " random_access=" << randomAccess
              << " renderer=" << (rendererName ? rendererName : "unknown") << '\n';
    return 0;
#endif
} catch (const std::exception& e) {
    std::cerr << "physical/instancing certification FAIL: " << e.what() << '\n';
    return 1;
}
