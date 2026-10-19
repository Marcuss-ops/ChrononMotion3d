#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/core/Object3D.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/scenes/Scene.hpp"

#include "chrononmotion/cameras/PerspectiveCamera.hpp"
#include "chrononmotion/motion/MotionPlane.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
    constexpr int kWidth = 320;
    constexpr int kHeight = 180;

    void require(bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    }

    std::size_t redPixels(const std::vector<unsigned char>& rgb) {
        std::size_t count = 0;
        for (std::size_t i = 0; i + 2 < rgb.size(); i += 3)
            count += rgb[i] > 240 && rgb[i + 1] < 16 && rgb[i + 2] < 16;
        return count;
    }

    std::pair<double, double> whiteCentroid(const std::vector<unsigned char>& rgb) {
        double xSum = 0.0;
        double ySum = 0.0;
        std::size_t count = 0;
        for (int yBottom = 0; yBottom < kHeight; ++yBottom) {
            const int yTop = kHeight - 1 - yBottom;
            for (int x = 0; x < kWidth; ++x) {
                const std::size_t i = (static_cast<std::size_t>(yBottom) * kWidth + x) * 3;
                if (rgb[i] > 240 && rgb[i + 1] > 240 && rgb[i + 2] > 240) {
                    xSum += x + 0.5;
                    ySum += yTop + 0.5;
                    ++count;
                }
            }
        }
        require(count != 0, "projected white marker produced no white pixels");
        return {xSum / count, ySum / count};
    }
}

int main() try {
    using namespace threepp;

    Canvas canvas(Canvas::Parameters().size(kWidth, kHeight).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x000000));
    auto target = RenderTarget::create(kWidth, kHeight, RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    Scene scene;
    PerspectiveCamera camera(35.f, static_cast<float>(kWidth) / kHeight, 0.1f, 100.f);
    camera.position.set(0.f, 0.f, 5.f);
    camera.lookAt(Vector3(0.f, 0.f, 0.f));
    camera.updateMatrixWorld(true);

    auto red = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(0xff0000));
    auto plane = Mesh::create(PlaneGeometry::create(1.f, 1.f), red);
    scene.add(plane);

    // Perspective size invariant: moving the plane from distance 5 to 15 and
    // scaling it by 3 must preserve its pixel coverage (same projection).
    renderer.render(scene, camera);
    const auto nearFrame = renderer.readRGBPixels();
    const std::size_t nearCount = redPixels(nearFrame);
    require(nearCount > 0, "near red plane produced no pixels");
    plane->position.z = -10.f;
    plane->scale.set(3.f, 3.f, 1.f);
    renderer.render(scene, camera);
    const auto compensatedFrame = renderer.readRGBPixels();
    const std::size_t farCount = redPixels(compensatedFrame);
    const double coverageError = std::abs(static_cast<double>(nearCount) - farCount) / nearCount;
    require(coverageError < 0.002, "3x perspective compensation changed plane coverage by >= 0.2%");

    // Out-of-order GL frame repeat: after other transforms, restoring an
    // absolute pose must return byte-identical pixels on this same device.
    plane->position.set(0.35f, -0.2f, 0.f);
    plane->scale.set(1.f, 1.f, 1.f);
    renderer.render(scene, camera);
    const auto reference = renderer.readRGBPixels();
    for (int frame = 0; frame < 120; ++frame) {
        plane->position.x = -1.f + 2.f * frame / 119.f;
        renderer.render(scene, camera);
        (void)renderer.readRGBPixels();
    }
    plane->position.set(0.35f, -0.2f, 0.f);
    renderer.render(scene, camera);
    require(renderer.readRGBPixels() == reference, "random-access GL frame differs from its reference pixels");

    // 3D -> 2D anchor check. ChrononMotion's projection reports top-left
    // viewport pixels; compare it with the centroid measured from GL readback.
    auto white = MeshBasicMaterial::create(MeshBasicMaterial::Params{}.color(0xffffff));
    auto marker = Mesh::create(SphereGeometry::create(0.02f, 24, 16), white);
    scene.add(marker);
    chrononmotion::PerspectiveCamera motionCamera(35.f, static_cast<float>(kWidth) / kHeight, 0.1f, 100.f);
    motionCamera.position.set(0.f, 0.f, 5.f);
    motionCamera.lookAt(chrononmotion::Vector3(0.f, 0.f, 0.f));
    motionCamera.updateMatrixWorld(true);
    motionCamera.updateProjectionMatrix();
    chrononmotion::motion::PerspectiveView view(motionCamera, {kWidth, kHeight});
    for (const float x : {-0.8f, 0.f, 0.8f}) {
        marker->position.set(x, 0.25f, 0.f);
        renderer.render(scene, camera);
        const auto pixels = renderer.readRGBPixels();
        const auto centroid = whiteCentroid(pixels);
        chrononmotion::Vector2 projected;
        chrononmotion::Vector3 ndc;
        require(view.project({x, 0.25f, 0.f}, projected, ndc), "visible 3D marker failed projection");
        require(std::hypot(centroid.first - projected.x, centroid.second - projected.y) <= 0.5,
                "3D-to-2D projected anchor differs from white pixel centroid by > 0.5 px");
    }

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    std::cout << "OpenGL deterministic pixel certification passed (coverage error="
              << coverageError << ")\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "OpenGL pixel certification failed: " << error.what() << '\n';
    return 1;
}
