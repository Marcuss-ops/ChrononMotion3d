// CAM-04: pixel renderer for the exact serialized C3D RenderPlan.
// This intentionally implements only the shared image-plane/perspective subset
// used by T16; it consumes the plan itself and fails closed on other content.
#include "threepp/canvas/Canvas.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/loaders/ImageLoader.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/Texture.hpp"

#include <nlohmann/json.hpp>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {
struct Plane {
    std::shared_ptr<threepp::Mesh> mesh;
    std::array<float, 3> position;
};

std::array<float, 3> vec3(const json& value) {
    if (!value.is_array() || value.size() < 3)
        throw std::runtime_error("CAM-04 expected a three-component vector");
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>()};
}

std::vector<unsigned char> render_frame(
    threepp::GLRenderer& renderer,
    threepp::Scene& scene,
    threepp::PerspectiveCamera& camera,
    const std::vector<Plane>& planes,
    const json& sample) {
    const auto& c = sample.at("camera");
    const auto p = vec3(c.at("position"));
    const auto r = vec3(c.at("rotation_deg"));
    camera.position.set(p[0], p[1], -p[2]); // C3D +Z-forward to threepp -Z-forward
    camera.rotation.set(-r[0] * 0.017453292519943295f,
                         -r[1] * 0.017453292519943295f,
                          r[2] * 0.017453292519943295f);
    camera.fov = c.at("fov_deg").get<float>();
    camera.nearPlane = c.at("near").get<float>();
    camera.farPlane = c.at("far").get<float>();
    camera.zoom = c.at("zoom").get<float>();
    camera.updateMatrixWorld(true);
    camera.updateProjectionMatrix();

    for (std::size_t i = 0; i < planes.size(); ++i) {
        const auto& pos = planes[i].position;
        planes[i].mesh->position.set(pos[0], pos[1], -pos[2]);
    }
    renderer.render(scene, camera);
    auto pixels = renderer.readRGBPixels();
    if (pixels.size() != 1920u * 1080u * 3u)
        throw std::runtime_error("CAM-04 threepp framebuffer has an invalid size");
    return pixels;
}
}

int main(int argc, char** argv) try {
    if (argc != 3) throw std::runtime_error("usage: cam04_renderplan_pixels PLAN OUTPUT_DIR");
    const fs::path planPath = fs::absolute(argv[1]);
    const fs::path output = fs::absolute(argv[2]);
    std::ifstream input(planPath);
    if (!input) throw std::runtime_error("cannot open RenderPlan: " + planPath.string());
    const json plan = json::parse(input);
    const auto& canvas = plan.at("canvas");
    if (canvas.at("width") != 1920 || canvas.at("height") != 1080)
        throw std::runtime_error("CAM-04 currently certifies only the T16 1920x1080 plan");
    if (!plan.contains("camera_samples") || plan.at("camera_samples").size() != 150)
        throw std::runtime_error("CAM-04 expected all 150 resolved camera samples");
    const fs::path assets = planPath.parent_path();
    fs::create_directories(output);

    threepp::Canvas canvasWindow(threepp::Canvas::Parameters().size(1920, 1080).headless(true));
    threepp::GLRenderer renderer(canvasWindow);
    renderer.setClearColor(threepp::Color(0x000000));
    auto scene = threepp::Scene::create();
    auto camera = threepp::PerspectiveCamera::create(50.f, 1920.f / 1080.f, 1.f, 10000.f);
    auto target = threepp::RenderTarget::create(1920, 1080, {});
    renderer.setRenderTarget(target.get());

    threepp::ImageLoader loader;
    std::vector<Plane> planes;
    for (const auto& layer : plan.at("layers")) {
        if (layer.at("type") != "image" || !layer.value("enable_3d", false))
            throw std::runtime_error("CAM-04 subset supports only 3D image layers");
        const fs::path asset = assets / layer.at("asset").get<std::string>();
        const auto size = vec3(json::array({layer.at("size")[0], layer.at("size")[1], 0}));
        const auto pos = layer.contains("position") ? vec3(layer.at("position")) : std::array<float,3>{0,0,0};
        const auto scale = layer.contains("scale") ? vec3(layer.at("scale")) : std::array<float,3>{1,1,1};
        auto decoded = loader.load(asset, 4, true);
        if (!decoded || decoded->width() == 0 || decoded->height() == 0)
            throw std::runtime_error("CAM-04 cannot decode asset: " + asset.string());
        auto texture = threepp::Texture::create(std::move(*decoded));
        texture->needsUpdate();
        auto material = threepp::MeshBasicMaterial::create(
            threepp::MeshBasicMaterial::Params{}.map(texture));
        material->transparent = true;
        material->depthWrite = true;
        const float width = size[0] * scale[0];
        const float height = size[1] * scale[1];
        auto mesh = threepp::Mesh::create(threepp::PlaneGeometry::create(width, height), material);
        mesh->position.set(pos[0], pos[1], -pos[2]);
        scene->add(mesh);
        planes.push_back({mesh, pos});
    }

    const std::array<int, 5> frames{0, 30, 75, 120, 149};
    std::ofstream raw(output / "threepp_samples.rgb", std::ios::binary);
    if (!raw) throw std::runtime_error("cannot create CAM-04 raw output");
    for (const int frame : frames) {
        const auto pixels = render_frame(renderer, *scene, *camera, planes,
                                         plan.at("camera_samples").at(frame));
        raw.write(reinterpret_cast<const char*>(pixels.data()),
                  static_cast<std::streamsize>(pixels.size()));
        std::ofstream sample(output / ("frame_" + std::to_string(frame) + ".rgb"), std::ios::binary);
        sample.write(reinterpret_cast<const char*>(pixels.data()),
                     static_cast<std::streamsize>(pixels.size()));
        if (!raw || !sample) throw std::runtime_error("CAM-04 failed writing pixel evidence");
    }
    std::cout << "CAM-04 rendered the five requested frames from " << planPath << '\n';
    return 0;
} catch (const std::exception& e) {
    std::cerr << "CAM-04 FAIL: " << e.what() << '\n';
    return 1;
}
