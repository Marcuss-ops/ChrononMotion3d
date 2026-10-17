// Clip sequence demo: Fox.glb walk -> survey -> run through
// MotionSceneBridge::bindLayerClipSequence with linear crossfades.
// Usage: chrononmotion_render_clip_sequence_demo out.mp4 [asset.glb]
// Writes a 1280x720 30fps H.264 MP4 and prints per-frame change stats.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "threepp/canvas/Canvas.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/math/Box3.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/renderers/GLRenderer.hpp"

using namespace threepp;
namespace fs = std::filesystem;

namespace {
    constexpr int W = 1280, H = 720, FPS = 30;
    constexpr int FRAMES = 240;// 8 seconds

    std::shared_ptr<AnimationClip> findClip(const std::vector<std::shared_ptr<AnimationClip>>& clips, const std::string& name) {
        for (const auto& c : clips) {
            if (c && c->name() == name) return c;
        }
        throw std::runtime_error("GLB has no animation clip named " + name);
    }
}// namespace

int main(int argc, char** argv) try {
    if (argc < 2) throw std::invalid_argument("usage: clip_sequence_demo out.mp4 [asset.glb]");
    const fs::path out = argv[1];
    const fs::path asset = argc > 2 ? fs::path(argv[2]) : fs::path("render/spike/assets/threepp-certification/Fox.glb");

    GLTFLoader loader;
    auto result = loader.load(asset);
    if (!result || !result->scene) throw std::runtime_error("GLTFLoader failed: " + asset.string());

    chrononmotion::motion::MotionScene motion("clip_sequence_demo", float(FPS));
    chrononmotion::motion::MeshAttachment attachment;
    attachment.assetId = "fox";
    motion.addMesh(1, "fox", std::move(attachment));

    threepp::motion::MotionSceneBridge bridge(40.f, float(W) / H, 0.05f, 1000.f);
    bridge.bindAsset("fox", result->scene);
    bridge.evaluateAndSync(motion, 0);

    using Segment = threepp::motion::MotionSceneBridge::ClipSegment;
    // Walk 0-2.5s, survey (talk stand-in) crossfades in at 2.5s, run crossfades in at 5.0s.
    bridge.bindLayerClipSequence(1, {
                                            Segment{findClip(result->animations, "Walk"), 0.f, 0.f, 0.f},
                                            Segment{findClip(result->animations, "Survey"), 2.5f, 0.f, 0.5f},
                                            Segment{findClip(result->animations, "Run"), 5.0f, 8.f, 0.5f},
                                        });

    auto node = bridge.layerNode(1);
    if (!node) throw std::runtime_error("bridge did not materialize the GLB subtree");
    Box3 box;
    box.setFromObject(*node, true);
    Vector3 center, size;
    box.getCenter(center);
    box.getSize(size);
    const float span = std::max({size.x, size.y, size.z, 0.25f});
    bridge.camera()->position.set(center.x + span * 0.72f, center.y + span * 0.4f, center.z + span * 2.15f);
    bridge.camera()->lookAt(center);
    bridge.scene()->add(AmbientLight::create(Color(0xffffff), 0.5f));
    auto key = DirectionalLight::create(Color(0xffffff), 3.2f);
    key->position.set(-3.f, 5.f, 7.f);
    bridge.scene()->add(key);

    Canvas canvas(Canvas::Parameters().size(W, H).antialiasing(4).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x151922));

    fs::create_directories(out.parent_path().empty() ? fs::path(".") : out.parent_path());
    const std::string cmd = "ffmpeg -y -loglevel error -f rawvideo -pixel_format rgb24 -video_size " +
                            std::to_string(W) + "x" + std::to_string(H) + " -framerate " + std::to_string(FPS) +
                            " -i - -an -c:v libx264 -pix_fmt yuv420p '" + out.string() + "'";
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) throw std::runtime_error("failed to open ffmpeg pipe");

    std::vector<unsigned char> prev;
    const std::size_t row = std::size_t(W) * 3;
    std::vector<unsigned char> flipped(row * H);
    for (int f = 0; f < FRAMES; ++f) {
        bridge.evaluateAndSync(motion, f);
        renderer.render(*bridge.scene(), *bridge.camera());
        auto pixels = renderer.readRGBPixels();
        if (pixels.size() != row * H) throw std::runtime_error("invalid readback size");

        for (int y = 0; y < H; ++y) {
            std::copy_n(pixels.data() + std::size_t(H - y - 1) * row, row, flipped.data() + std::size_t(y) * row);
        }
        if (std::fwrite(flipped.data(), 1, flipped.size(), pipe) != flipped.size()) {
            pclose(pipe);
            throw std::runtime_error("ffmpeg pipe write failed");
        }

        // Frame-to-frame mean absolute change: a crossfade should have no spike.
        if (!prev.empty()) {
            double sum = 0;
            for (std::size_t i = 0; i < pixels.size(); ++i) sum += std::abs(int(pixels[i]) - int(prev[i]));
            const double mean = sum / double(pixels.size());
            if (f % 30 == 0 || (f >= 74 && f <= 80) || (f >= 149 && f <= 155))
                std::cout << "frame " << f << " mean_delta=" << mean << "\n";
        }
        prev = std::move(pixels);
    }
    const int rc = pclose(pipe);
    if (rc != 0) throw std::runtime_error("ffmpeg exited with status " + std::to_string(rc));
    std::cout << "wrote " << out << " (" << FRAMES << " frames)\n";
    return 0;
} catch (const std::exception& e) {
    std::cerr << "clip_sequence_demo: " << e.what() << "\n";
    return 1;
}
