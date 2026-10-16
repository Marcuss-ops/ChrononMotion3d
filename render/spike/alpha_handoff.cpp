// T13 alpha handoff: threepp GLB RGBA readback -> alpha-bearing MOV.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "glad/glad.h"
#include "stb_image_write.h"

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {
constexpr int W = 1920;
constexpr int H = 1080;
constexpr int FPS = 30;
constexpr int FRAMES = 150;

std::string shell_quote(const fs::path& path) {
    std::string result{"'"};
    for (const char c : path.string()) {
        if (c == '\'') result += "'\\''";
        else result += c;
    }
    return result + "'";
}

std::vector<unsigned char> top_down_rgba(const std::vector<unsigned char>& bottom_up) {
    const size_t stride = static_cast<size_t>(W) * 4;
    std::vector<unsigned char> result(bottom_up.size());
    for (int y = 0; y < H; ++y) {
        std::copy_n(bottom_up.data() + static_cast<size_t>(H - y - 1) * stride,
                    stride, result.data() + static_cast<size_t>(y) * stride);
    }
    return result;
}
} // namespace

int main(int argc, char** argv) try {
    const fs::path asset = argc > 1 ? argv[1] : "render/spike/assets/threepp-certification/CesiumMilkTruck.glb";
    const fs::path output_dir = argc > 2 ? argv[2] : "out/threepp_certification/T13-alpha";
    fs::create_directories(output_dir);

    GLTFLoader loader;
    auto loaded = loader.load(asset);
    if (!loaded || !loaded->scene) throw std::runtime_error("GLTFLoader failed to load certification asset");

    Box3 bounds;
    bounds.setFromObject(*loaded->scene, true);
    Vector3 center, size;
    bounds.getCenter(center);
    bounds.getSize(size);
    const float span = std::max({size.x, size.y, size.z});
    if (!(span > 1e-5f) || !std::isfinite(span)) throw std::runtime_error("invalid GLB bounds");
    constexpr float target_span = 1.8f;
    const float scale = target_span / span;

    chrononmotion::motion::MotionScene motion("T13_alpha_handoff", static_cast<float>(FPS));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.assetId = "alpha_glb";
    auto& model = motion.addMesh(1, "threepp_glb", std::move(attachment));
    model.transform.position = chrononmotion::Vector3(-center.x * scale, -center.y * scale, -center.z * scale);
    model.transform.scale = chrononmotion::Vector3(scale, scale, scale);
    model.tracks.rotation.add(0.f, chrononmotion::Vector3(0.f, 0.f, 0.f));
    model.tracks.rotation.add(static_cast<float>(FRAMES) / FPS,
                              chrononmotion::Vector3(0.f, 6.2831853f, 0.f));
    auto& ambient = motion.addAmbientLight(101, "ambient");
    ambient.intensity = 0.8f;
    auto& key = motion.addDirectionalLight(102, "key");
    key.intensity = 2.5f;
    key.transform.position = chrononmotion::Vector3(2.f, 3.f, 4.f);

    threepp::motion::MotionSceneBridge bridge(45.f, static_cast<float>(W) / H, 0.1f, 100.f);
    bridge.bindAsset("alpha_glb", loaded->scene);
    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x1a1d24), 0.f);
    const auto* gl_renderer_name = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    auto target = RenderTarget::create(W, H, RenderTarget::Options{});
    renderer.setRenderTarget(target.get());
    PerspectiveCamera camera(45.f, static_cast<float>(W) / H, 0.1f, 100.f);
    camera.position.set(0.f, 0.5f, 3.5f);
    camera.lookAt(Vector3(0.f, 0.f, 0.f));

    const fs::path video = output_dir / "T13_threepp_rgba_handoff_1080p30.mov";
    const std::string command = "ffmpeg -y -v error -f rawvideo -pixel_format rgba -video_size 1920x1080 -framerate 30 -i - -an -c:v qtrle -pix_fmt argb " + shell_quote(video);
    FILE* pipe = popen(command.c_str(), "w");
    if (!pipe) throw std::runtime_error("could not start alpha-capable FFmpeg encoder");

    std::vector<unsigned char> pixels(static_cast<size_t>(W) * H * 4);
    std::vector<unsigned char> sample;
    for (int frame = 0; frame < FRAMES; ++frame) {
        bridge.evaluateAndSync(motion, frame);
        renderer.render(*bridge.scene(), camera);
        renderer.readPixels({0, 0}, {W, H}, Format::RGBA, pixels.data());
        auto encoded_order = top_down_rgba(pixels);
        if (frame == 75) {
            sample = encoded_order;
            if (!stbi_write_png((output_dir / "frame-75-rgba.png").c_str(), W, H, 4,
                                sample.data(), W * 4)) {
                pclose(pipe);
                throw std::runtime_error("failed writing RGBA proof frame");
            }
        }
        if (std::fwrite(encoded_order.data(), 1, encoded_order.size(), pipe) != encoded_order.size()) {
            pclose(pipe);
            throw std::runtime_error("short write to alpha-capable FFmpeg encoder");
        }
    }
    const int encoder_status = pclose(pipe);
    if (encoder_status != 0) throw std::runtime_error("FFmpeg alpha video encoder failed");

    size_t transparent = 0, opaque = 0;
    unsigned int alpha_min = 255, alpha_max = 0;
    for (size_t i = 3; i < sample.size(); i += 4) {
        const unsigned int alpha = sample[i];
        alpha_min = std::min(alpha_min, alpha);
        alpha_max = std::max(alpha_max, alpha);
        transparent += alpha == 0;
        opaque += alpha >= 250;
    }
    if (transparent < static_cast<size_t>(W * H / 10) || opaque < 1000 || alpha_max < 250) {
        throw std::runtime_error("threepp RGBA render did not contain substantial transparency and opaque model pixels");
    }

    std::ofstream metadata(output_dir / "metadata.json");
    metadata << "{\n  \"test_id\": \"T13-alpha-source\",\n"
             << "  \"source_path\": \"GLTFLoader -> MotionSceneBridge -> GLRenderer RGBA readback\",\n"
             << "  \"gl_renderer\": \"" << (gl_renderer_name ? gl_renderer_name : "unknown") << "\",\n"
             << "  \"readback\": \"CPU RGBA framebuffer readPixels\",\n"
             << "  \"output\": \"" << video.filename().string() << "\",\n"
             << "  \"width\": " << W << ", \"height\": " << H
             << ", \"fps\": " << FPS << ", \"frames\": " << FRAMES << ",\n"
             << "  \"source_alpha\": {\"minimum\": " << alpha_min << ", \"maximum\": " << alpha_max
             << ", \"transparent_pixels_frame_75\": " << transparent
             << ", \"opaque_pixels_frame_75\": " << opaque << "},\n"
             << "  \"codec\": \"qtrle\", \"pixel_format\": \"argb\",\n"
             << "  \"encoder\": \"FFmpeg qtrle CPU (no GPU alpha-capable encoder selected)\",\n"
             << "  \"status\": \"SOURCE_ONLY\"\n}\n";
    metadata.close();
    std::cout << "threepp RGBA source written (not an end-to-end PASS): " << video << " ("
              << transparent << " transparent, " << opaque << " opaque pixels in frame 75)\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "alpha handoff certification failed: " << error.what() << '\n';
    return 1;
}
