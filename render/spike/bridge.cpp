// Proof: MotionScene + CameraRig -> MotionSceneBridge -> headless GLRenderer -> PPM.
//
// Proves Milestone 3 (Motion -> threepp bridge):
//   - Hierarchical MotionScene (parent Null rig + child BoxGeometry mesh +
//     child SphereGeometry satellite)
//   - Keyframed transforms (position, rotation, scale) with Motion easing
//   - Motion-authored AmbientLight + DirectionalLight
//   - Animated CameraRig with look-at target
//   - Multi-frame evaluation & offscreen GL rendering (frames 0, 15, 30)
//   - Asserts distinct frame pixels across time and bit-exact determinism when
//     re-evaluating frame 0 after frame 30.
//
// Usage: chrononmotion_render_bridge [out.ppm [w h]]

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "chrononmotion/geometries/BoxGeometry.hpp"
#include "chrononmotion/geometries/SphereGeometry.hpp"
#include "chrononmotion/materials/Descriptors.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "chrononmotion/motion/Easing.hpp"
#include "chrononmotion/motion/Lighting.hpp"
#include "chrononmotion/motion/MotionScene.hpp"

#include "threepp/canvas/Canvas.hpp"
#include "threepp/motion/MotionBridge.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/textures/DataTexture.hpp"

namespace {

std::vector<unsigned char> checkerTexture(int size, int cells) {
    std::vector<unsigned char> data(size * size * 3);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool hi = ((x / cell) + (y / cell)) % 2 == 0;
            const size_t idx = static_cast<size_t>(y * size + x) * 3;
            data[idx + 0] = static_cast<unsigned char>(hi ? 240 : 45);
            data[idx + 1] = static_cast<unsigned char>(hi ? 200 : 105);
            data[idx + 2] = static_cast<unsigned char>(hi ? 110 : 225);
        }
    }
    return data;
}

std::size_t countNonBackgroundPixels(
    const std::vector<unsigned char>& pixels,
    int width,
    int height) {
    const unsigned char bgR = pixels[0];
    const unsigned char bgG = pixels[1];
    const unsigned char bgB = pixels[2];
    std::size_t covered = 0;
    for (int i = 0; i < width * height; ++i) {
        const std::size_t idx = static_cast<std::size_t>(i) * 3;
        if (pixels[idx] != bgR || pixels[idx + 1] != bgG || pixels[idx + 2] != bgB) {
            ++covered;
        }
    }
    return covered;
}

} // namespace

int main(int argc, char** argv) {
    const std::string outPpm = argc > 1 ? argv[1] : "bridge.ppm";
    const int width = argc > 2 ? std::atoi(argv[2]) : 256;
    const int height = argc > 3 ? std::atoi(argv[3]) : 256;

    // 1. Author a hierarchical MotionScene (space and time authority)
    chrononmotion::motion::MotionScene motionScene("bridge_proof", 30.f);

    auto& rigNull = motionScene.addNull(1, "rig_root");
    rigNull.tracks.position.add(0.f, chrononmotion::Vector3(-0.45f, 0.f, 0.f), chrononmotion::motion::Easing::easeInOut());
    rigNull.tracks.position.add(0.5f, chrononmotion::Vector3(0.f, 0.25f, 0.f), chrononmotion::motion::Easing::easeInOut());
    rigNull.tracks.position.add(1.0f, chrononmotion::Vector3(0.55f, -0.15f, 0.f));
    rigNull.tracks.rotation.add(0.f, chrononmotion::Vector3(0.25f, 0.35f, 0.f), chrononmotion::motion::Easing::easeInOut());
    rigNull.tracks.rotation.add(0.5f, chrononmotion::Vector3(0.55f, 1.15f, 0.2f), chrononmotion::motion::Easing::easeInOut());
    rigNull.tracks.rotation.add(1.0f, chrononmotion::Vector3(0.85f, 2.10f, 0.4f));

    chrononmotion::motion::MeshAttachment cubeAttachment;
    cubeAttachment.geometry = chrononmotion::BoxGeometry::create(1.25f, 1.25f, 1.25f);
    cubeAttachment.material = chrononmotion::MaterialDescriptor::lambert(10, chrononmotion::Color(0xffffff));
    auto& cubeLayer = motionScene.addMesh(2, "hero_cube", std::move(cubeAttachment), 1);
    cubeLayer.tracks.scale.add(0.f, chrononmotion::Vector3(1.f, 1.f, 1.f));
    cubeLayer.tracks.scale.add(0.5f, chrononmotion::Vector3(1.18f, 1.18f, 1.18f));
    cubeLayer.tracks.scale.add(1.0f, chrononmotion::Vector3(0.95f, 0.95f, 0.95f));

    chrononmotion::motion::MeshAttachment sphereAttachment;
    sphereAttachment.geometry = chrononmotion::SphereGeometry::create(0.38f, 20, 16);
    sphereAttachment.material = chrononmotion::MaterialDescriptor::emissive(
        20, chrononmotion::Color(0xff6b35), 0.35f);
    auto& sphereLayer = motionScene.addMesh(3, "orbit_sphere", std::move(sphereAttachment), 1);
    sphereLayer.tracks.position.add(0.f, chrononmotion::Vector3(1.35f, 0.3f, 0.4f));
    sphereLayer.tracks.position.add(0.5f, chrononmotion::Vector3(-1.25f, 0.55f, 0.6f));
    sphereLayer.tracks.position.add(1.0f, chrononmotion::Vector3(0.15f, -1.25f, 0.8f));

    auto& ambient = motionScene.addAmbientLight(100, "ambient");
    ambient.color = chrononmotion::Color(0xffffff);
    ambient.intensity = 0.55f;

    auto& keyLight = motionScene.addDirectionalLight(101, "key_light");
    keyLight.color = chrononmotion::Color(0xffffff);
    keyLight.intensity = 2.0f;
    keyLight.transform.position = chrononmotion::Vector3(2.5f, 3.5f, 4.0f);
    keyLight.directionTrack.add(0.f, chrononmotion::Vector3(-0.5f, -0.7f, -1.0f));
    keyLight.directionTrack.add(1.0f, chrononmotion::Vector3(0.4f, -0.6f, -1.0f));

    chrononmotion::motion::CameraRig cameraRig(
        200, 45.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
    cameraRig.setTarget(chrononmotion::Vector3(0.f, 0.f, 0.f));
    cameraRig.positionTrack().add(0.f, chrononmotion::Vector3(0.f, 0.3f, 4.2f));
    cameraRig.positionTrack().add(0.5f, chrononmotion::Vector3(0.8f, 0.6f, 3.8f));
    cameraRig.positionTrack().add(1.0f, chrononmotion::Vector3(-0.7f, 0.2f, 4.0f));

    // 2. Set up headless GLRenderer + MotionSceneBridge (pixel sibling)
    threepp::Canvas canvas(threepp::Canvas::Parameters().size(width, height).headless(true));
    threepp::GLRenderer renderer(canvas);
    renderer.setClearColor(threepp::Color(0x1a1d24));

    auto target = threepp::RenderTarget::create(width, height, threepp::RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    threepp::motion::MotionSceneBridge bridge(
        45.f, static_cast<float>(width) / static_cast<float>(height), 0.1f, 100.f);
    auto tex = threepp::DataTexture::create(checkerTexture(64, 8), 64, 64);
    tex->needsUpdate();
    bridge.bindMaterialTexture(10, tex);

    auto renderFrame = [&](int frame) {
        bridge.evaluateAndSync(motionScene, frame, &cameraRig);
        renderer.render(*bridge.scene(), *bridge.camera());
        return renderer.readRGBPixels();
    };

    const auto f0 = renderFrame(0);
    const auto f15 = renderFrame(15);
    const auto f30 = renderFrame(30);
    const auto f0Repeat = renderFrame(0);

    const std::size_t cov0 = countNonBackgroundPixels(f0, width, height);
    const std::size_t cov15 = countNonBackgroundPixels(f15, width, height);
    const std::size_t cov30 = countNonBackgroundPixels(f30, width, height);

    std::cout << "bridge proof: coverage f0=" << cov0 << " f15=" << cov15 << " f30=" << cov30 << "\n";

    if (cov0 == 0 || cov15 == 0 || cov30 == 0) {
        std::cerr << "bridge proof: expected non-zero coverage on all rendered frames\n";
        return 1;
    }
    if (f0 == f15 || f15 == f30 || f0 == f30) {
        std::cerr << "bridge proof: expected animated frames 0, 15, 30 to differ\n";
        return 1;
    }
    if (f0 != f0Repeat) {
        std::cerr << "bridge proof: out-of-order re-evaluation of frame 0 was not deterministic\n";
        return 1;
    }

    // Render frame 15 as the representative output PPM
    std::ofstream ppm(outPpm, std::ios::binary);
    ppm << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        ppm.write(reinterpret_cast<const char*>(&f15[y * width * 3]), width * 3);
    }
    std::cout << "bridge proof: wrote " << outPpm << " (deterministic check PASS)\n";

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    return 0;
}
