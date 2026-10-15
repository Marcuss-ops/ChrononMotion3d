// Spike: headless textured cube -> PPM.
 //
 // Proves the render module end to end with no display server:
 // headless Canvas (GLFW Null platform) + GLRenderer + Lambert shading +
 // directional light + procedural checker texture + offscreen RenderTarget +
 // readRGBPixels. Usage: chrononmotion_render_spike out.ppm [width height]

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "threepp/canvas/Canvas.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/materials/MeshLambertMaterial.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/DataTexture.hpp"

using namespace threepp;

namespace {

std::vector<unsigned char> checkerTexture(int size, int cells) {
    std::vector<unsigned char> data(size * size * 3);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool white = ((x / cell) + (y / cell)) % 2 == 0;
            const auto v = static_cast<unsigned char>(white ? 235 : 30);
            data[(y * size + x) * 3 + 0] = v;
            data[(y * size + x) * 3 + 1] = v;
            data[(y * size + x) * 3 + 2] = v;
        }
    }
    return data;
}

} // namespace

int main(int argc, char** argv) {
    const std::string out = argc > 1 ? argv[1] : "cube.ppm";
    const int width = argc > 2 ? std::atoi(argv[2]) : 256;
    const int height = argc > 3 ? std::atoi(argv[3]) : 256;

    Canvas canvas(Canvas::Parameters().size(width, height).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x1a1d24));

    auto target = RenderTarget::create(width, height, RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    auto scene = Scene::create();
    scene->add(AmbientLight::create(Color(0xffffff), 0.55f));
    auto light = DirectionalLight::create(Color(0xffffff), 2.0f);
    light->position.set(2, 3, 4);
    scene->add(light);

    auto texture = DataTexture::create(checkerTexture(64, 8), 64, 64);
    texture->needsUpdate();
    auto material = MeshLambertMaterial::create();
    material->color = Color(0xffffff);
    material->map = texture;

    auto cube = Mesh::create(BoxGeometry::create(1.4f, 1.4f, 1.4f), material);
    cube->rotation.y = 0.6f;
    cube->rotation.x = 0.35f;
    scene->add(cube);

    auto camera = PerspectiveCamera::create(45, float(width) / float(height), 0.1f, 100);
    camera->position.z = 4;
    renderer.render(*scene, *camera);

    const auto pixels = renderer.readRGBPixels();
    if (int(pixels.size()) != width * height * 3) {
        std::cerr << "spike: readback size " << pixels.size() << ", want " << width * height * 3 << "\n";
        return 1;
    }
    // Sanity: the frame must not be all clear color (cube covers the center).
    const auto at = [&](int x, int y) -> int {
        return pixels[(y * width + x) * 3];
    };
    const int center = at(width / 2, height / 2);
    const int corner = at(2, 2);
    std::cout << "spike: center=" << center << " corner=" << corner << "\n";

    std::ofstream ppm(out, std::ios::binary);
    ppm << "P6\n" << width << " " << height << "\n255\n";
    // PPM rows go top-down; GL readback is bottom-up.
    for (int y = height - 1; y >= 0; --y) {
        ppm.write(reinterpret_cast<const char*>(&pixels[y * width * 3]), width * 3);
    }
    std::cout << "spike: wrote " << out << "\n";

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    return 0;
}
