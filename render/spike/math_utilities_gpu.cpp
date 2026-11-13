// GPU raster demo for the restored math primitives.
//
// The core library stays renderer-free: this spike lives in the opt-in render
// sibling. It builds threepp OpenGL meshes from the same data the CPU demo
// uses — SH vertex colours, an ImprovedNoise DataTexture, Rng-driven
// InstancedMesh placements and the InstanceBatch::bounds AABB — renders
// offscreen through a headless EGL context, and pipes RGB24 frames straight
// into ffmpeg's NVENC H.264 encoder.

#include "chrononmotion/chrononmotion.hpp"
#include "chrononmotion/motion/Layer.hpp"

#include "threepp/canvas/Canvas.hpp"
#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/core/BufferAttribute.hpp"
#include "threepp/core/BufferGeometry.hpp"
#include "threepp/geometries/BoxGeometry.hpp"
#include "threepp/geometries/PlaneGeometry.hpp"
#include "threepp/geometries/SphereGeometry.hpp"
#include "threepp/materials/LineBasicMaterial.hpp"
#include "threepp/materials/MeshBasicMaterial.hpp"
#include "threepp/objects/InstancedMesh.hpp"
#include "threepp/objects/LineSegments.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "threepp/scenes/Scene.hpp"
#include "threepp/textures/DataTexture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace cm = chrononmotion;
namespace cmm = chrononmotion::motion;

namespace {

    constexpr int W = 1920;
    constexpr int H = 1080;
    constexpr int FPS = 30;
    constexpr int FRAMES = 180;
    constexpr int NOISE_W = 512;
    constexpr int NOISE_H = 256;
    constexpr int INSTANCE_COUNT = 56;

    float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }

    struct Rgb {
        float r, g, b;
    };

    Rgb mix(Rgb a, Rgb b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }

    Rgb noisePalette(float v) {
        v = clamp01(v);
        if (v < 0.36f) return mix({0.015f, 0.035f, 0.12f}, {0.0f, 0.42f, 0.62f}, v / 0.36f);
        if (v < 0.66f) return mix({0.0f, 0.42f, 0.62f}, {0.92f, 0.70f, 0.14f}, (v - 0.36f) / 0.30f);
        return mix({0.92f, 0.70f, 0.14f}, {1.0f, 0.98f, 0.86f}, (v - 0.66f) / 0.34f);
    }

    Rgb hsv(float h, float s, float v) {
        h = h - std::floor(h);
        const float i = std::floor(h * 6.f);
        const float f = h * 6.f - i;
        const float p = v * (1.f - s);
        const float q = v * (1.f - f * s);
        const float t = v * (1.f - (1.f - f) * s);
        switch (static_cast<int>(i) % 6) {
            case 0: return {v, t, p};
            case 1: return {q, v, p};
            case 2: return {p, v, t};
            case 3: return {p, q, v};
            case 4: return {t, p, v};
            default: return {v, p, q};
        }
    }

    cm::SphericalHarmonics3 warmEnvironment() {
        cm::SphericalHarmonics3::Coefficients c{};
        c[0] = cm::Vector3(0.62f, 0.70f, 0.86f);
        c[1] = cm::Vector3(0.34f, 0.46f, 0.64f);
        c[2] = cm::Vector3(0.10f, 0.15f, 0.24f);
        c[3] = cm::Vector3(0.62f, 0.42f, 0.24f);
        c[4] = cm::Vector3(0.10f, 0.08f, 0.06f);
        c[5] = cm::Vector3(0.05f, 0.05f, 0.06f);
        c[6] = cm::Vector3(0.15f, 0.18f, 0.22f);
        c[7] = cm::Vector3(0.09f, 0.07f, 0.05f);
        c[8] = cm::Vector3(0.11f, 0.13f, 0.17f);
        return cm::SphericalHarmonics3(c);
    }

    cm::SphericalHarmonics3 coolEnvironment() {
        cm::SphericalHarmonics3::Coefficients c{};
        c[0] = cm::Vector3(0.34f, 0.42f, 0.62f);
        c[1] = cm::Vector3(0.22f, 0.32f, 0.56f);
        c[2] = cm::Vector3(0.42f, 0.30f, 0.22f);
        c[3] = cm::Vector3(0.12f, 0.20f, 0.34f);
        c[4] = cm::Vector3(0.06f, 0.08f, 0.12f);
        c[5] = cm::Vector3(0.08f, 0.06f, 0.05f);
        c[6] = cm::Vector3(0.12f, 0.16f, 0.24f);
        c[7] = cm::Vector3(0.05f, 0.08f, 0.12f);
        c[8] = cm::Vector3(0.14f, 0.12f, 0.18f);
        return cm::SphericalHarmonics3(c);
    }

    void updateSphereColors(threepp::Mesh& sphere, const cm::SphericalHarmonics3& sh, float time) {
        const auto geometry = sphere.geometry();
        auto normals = geometry->getAttribute<float>("normal");
        auto colors = geometry->getAttribute<float>("color");
        if (!normals || !colors) return;

        const float angle = time * 0.55f;
        const float ca = std::cos(angle), sa = std::sin(angle);
        cm::Vector3 light(std::cos(time * 0.7f), 0.65f, std::sin(time * 0.7f));
        light.normalize();

        for (int i = 0; i < normals->count(); ++i) {
            const cm::Vector3 n(normals->getX(i), normals->getY(i), normals->getZ(i));
            const cm::Vector3 rotated(n.x * ca + n.z * sa, n.y, -n.x * sa + n.z * ca);
            cm::Vector3 irradiance;
            sh.getIrradianceAt(rotated, irradiance);
            const float specular = std::pow(std::max(0.f, rotated.dot(light)), 48.f) * 0.55f;
            irradiance += cm::Vector3(1.0f, 0.92f, 0.78f) * specular;
            irradiance *= 0.82f;
            colors->setXYZ(i, clamp01(irradiance.x), clamp01(irradiance.y), clamp01(irradiance.z));
        }
        colors->needsUpdate();
    }

    void fillNoiseTexture(std::vector<unsigned char>& data, float time, const cm::ImprovedNoise& noise) {
        for (int y = 0; y < NOISE_H; ++y) {
            const float v = static_cast<float>(y) / NOISE_H * 5.2f;
            for (int x = 0; x < NOISE_W; ++x) {
                const float u = static_cast<float>(x) / NOISE_W * 10.4f;
                const float n = noise.noise(u, v, time * 0.55f) * 0.65f +
                                noise.noise(u * 2.3f + 7.1f, v * 2.3f - 3.7f, time * 0.85f) * 0.35f;
                const float normalized = n * 0.5f + 0.5f;
                Rgb color = noisePalette(normalized);
                const float band = normalized * 9.f;
                if (band - std::floor(band) < 0.065f) color = mix(color, {0.92f, 0.97f, 1.0f}, 0.30f);
                const float vignette = 1.f - 0.28f * std::pow((x - NOISE_W * 0.5f) / (NOISE_W * 0.5f), 2.f);
                const std::size_t index = (static_cast<std::size_t>(y) * NOISE_W + x) * 3;
                data[index] = static_cast<unsigned char>(clamp01(color.r * vignette) * 255.f);
                data[index + 1] = static_cast<unsigned char>(clamp01(color.g * vignette) * 255.f);
                data[index + 2] = static_cast<unsigned char>(clamp01(color.b * vignette) * 255.f);
            }
        }
    }

    struct Placement {
        float size;
        float hue;
        cm::Matrix4 local;
    };

    void updateAabb(threepp::LineSegments& aabb, const cm::Box3& box) {
        auto positions = aabb.geometry()->getAttribute<float>("position");
        if (!positions) return;

        const float x0 = box.min().x, y0 = box.min().y, z0 = box.min().z;
        const float x1 = box.max().x, y1 = box.max().y, z1 = box.max().z;
        const std::array<std::array<float, 3>, 24> vertices = {{
                {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z0}, {x1, y1, z0},
                {x1, y1, z0}, {x0, y1, z0}, {x0, y1, z0}, {x0, y0, z0},
                {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z1}, {x1, y1, z1},
                {x1, y1, z1}, {x0, y1, z1}, {x0, y1, z1}, {x0, y0, z1},
                {x0, y0, z0}, {x0, y0, z1}, {x1, y0, z0}, {x1, y0, z1},
                {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z0}, {x0, y1, z1},
        }};
        for (int i = 0; i < 24; ++i)
            positions->setXYZ(i, vertices[i][0], vertices[i][1], vertices[i][2]);
        positions->needsUpdate();
    }

    void updateInstances(threepp::InstancedMesh& instances, threepp::LineSegments& aabb, int frame) {
        cm::Rng frameRng(0x9E3779B97F4A7C15ull ^ (static_cast<std::uint64_t>(frame + 1) * 0xBF58476D1CE4E5B9ull));
        std::vector<Placement> placements;
        placements.reserve(INSTANCE_COUNT);

        for (int i = 0; i < INSTANCE_COUNT; ++i) {
            cm::Rng rng = frameRng.fork(static_cast<std::uint64_t>(i));
            Placement placement;
            const float x = -1.45f + rng.nextFloat() * 2.9f;
            const float y = -0.60f + rng.nextFloat() * 1.2f;
            const float z = -0.35f + rng.nextFloat() * 0.7f;
            placement.size = 0.06f + rng.nextFloat() * 0.14f;
            placement.hue = rng.nextFloat();

            cm::Matrix4 translation, scale;
            translation.makeTranslation(x, y, z);
            scale.makeScale(placement.size, placement.size, placement.size);
            placement.local.multiplyMatrices(translation, scale);
            placements.push_back(placement);
        }

        std::sort(placements.begin(), placements.end(),
                  [](const Placement& a, const Placement& b) { return a.size > b.size; });

        cmm::InstanceBatch batch;
        batch.localTransforms.reserve(placements.size());
        for (std::size_t i = 0; i < placements.size(); ++i) {
            const Placement& placement = placements[i];
            batch.localTransforms.push_back(placement.local);

            threepp::Matrix4 matrix;
            matrix.fromArray(placement.local.elements, 0);
            instances.setMatrixAt(i, matrix);

            const Rgb color = hsv(placement.hue, 0.62f, 0.95f);
            instances.setColorAt(i, threepp::Color(color.r, color.g, color.b));
        }
        instances.instanceMatrix()->needsUpdate();
        if (instances.instanceColor()) instances.instanceColor()->needsUpdate();

        batch.validate();
        const cm::Box3 unit(cm::Vector3(-0.5f, -0.5f, -0.5f), cm::Vector3(0.5f, 0.5f, 0.5f));
        updateAabb(aabb, batch.bounds(unit));
    }

    std::shared_ptr<threepp::LineSegments> makeGrid() {
        std::vector<float> points;
        for (int i = -12; i <= 12; ++i) {
            const float x = static_cast<float>(i) * 0.42f;
            points.insert(points.end(), {x, -1.82f, -1.8f, x, -1.82f, 0.8f});
        }
        for (int j = -4; j <= 2; ++j) {
            const float z = static_cast<float>(j) * 0.42f;
            points.insert(points.end(), {-4.9f, -1.82f, z, 4.9f, -1.82f, z});
        }

        auto geometry = threepp::BufferGeometry::create();
        geometry->setAttribute("position", threepp::FloatBufferAttribute::create(std::move(points), 3));
        auto material = threepp::LineBasicMaterial::create();
        material->color = threepp::Color(0x16304d);
        auto grid = threepp::LineSegments::create(geometry, material);
        grid->frustumCulled = false;
        return grid;
    }

    std::shared_ptr<threepp::LineSegments> makeAabb() {
        auto geometry = threepp::BufferGeometry::create();
        geometry->setAttribute("position", threepp::FloatBufferAttribute::create(std::vector<float>(24 * 3, 0.f), 3));
        auto material = threepp::LineBasicMaterial::create();
        material->color = threepp::Color(0x66ff99);
        auto aabb = threepp::LineSegments::create(geometry, material);
        aabb->frustumCulled = false;
        return aabb;
    }

    bool renderMovie(const std::string& path, bool useNvenc) {
        const std::string ffmpeg = std::filesystem::exists("/usr/bin/ffmpeg") ? "/usr/bin/ffmpeg" : "ffmpeg";
        const std::string codec = useNvenc
                ? "-c:v h264_nvenc -preset p5 -rc vbr -cq 19 -b:v 0"
                : "-c:v libx264 -preset medium -crf 18";
        const std::string filter =
                "vflip,"
                "drawtext=text='CHRONONMOTION3D / GPU RASTER':fontsize=34:fontcolor=white:"
                "box=1:boxcolor=black@0.55:boxborderw=10:x=40:y=30,"
                "drawtext=text='FRAME %{n}':fontsize=24:fontcolor=0x9fe8ff:"
                "box=1:boxcolor=black@0.55:boxborderw=8:x=w-text_w-40:y=36,"
                "drawtext=text='SH SPHERE  |  IMPROVEDNOISE FIELD  |  RNG INSTANCES  |  INSTANCEBATCH BOUNDS':"
                "fontsize=22:fontcolor=0xbfe8ff:box=1:boxcolor=black@0.55:boxborderw=8:x=40:y=h-56";
        const std::string command =
                ffmpeg + " -y -hide_banner -loglevel error -f rawvideo -pix_fmt rgb24 -s " +
                std::to_string(W) + "x" + std::to_string(H) + " -r " + std::to_string(FPS) +
                " -i - -vf \"" + filter + "\" -an " + codec + " -pix_fmt yuv420p "
                "-movflags +faststart \"" + path + "\"";

        FILE* pipe = popen(command.c_str(), "w");
        if (!pipe) {
            std::fprintf(stderr, "could not start ffmpeg\n");
            return false;
        }

        threepp::Canvas canvas(threepp::Canvas::Parameters().size(W, H).headless(true));
        threepp::GLRenderer renderer(canvas);
        renderer.setClearColor(threepp::Color(0x04060c));

        threepp::RenderTarget::Options options;
        options.samples = 4;
        auto target = threepp::RenderTarget::create(W, H, options);
        renderer.setRenderTarget(target.get());

        auto scene = threepp::Scene::create();
        scene->background = threepp::Background(threepp::Color(0x04060c));

        // --- SH sphere -----------------------------------------------------
        auto sphereGeometry = threepp::SphereGeometry::create(1.28f, 96, 64);
        const int sphereVertices = sphereGeometry->getAttribute<float>("position")->count();
        sphereGeometry->setAttribute("color",
                                     threepp::FloatBufferAttribute::create(
                                             std::vector<float>(static_cast<std::size_t>(sphereVertices) * 3, 1.f), 3));
        auto sphereMaterial = threepp::MeshBasicMaterial::create();
        sphereMaterial->vertexColors = true;
        auto sphere = threepp::Mesh::create(sphereGeometry, sphereMaterial);
        sphere->position.set(-2.05f, 0.10f, 0.f);
        sphere->frustumCulled = false;
        scene->add(sphere);

        // --- noise plane ---------------------------------------------------
        std::vector<unsigned char> noiseData(static_cast<std::size_t>(NOISE_W) * NOISE_H * 3, 0);
        auto noiseTexture = threepp::DataTexture::create(noiseData, NOISE_W, NOISE_H);
        noiseTexture->format = threepp::Format::RGB;
        noiseTexture->colorSpace = threepp::ColorSpace::sRGB;
        noiseTexture->magFilter = threepp::Filter::Linear;
        noiseTexture->minFilter = threepp::Filter::Linear;
        auto noiseMaterial = threepp::MeshBasicMaterial::create();
        noiseMaterial->map = noiseTexture;
        auto noisePlane = threepp::Mesh::create(threepp::PlaneGeometry::create(3.5f, 1.75f), noiseMaterial);
        noisePlane->position.set(1.55f, 1.18f, -0.35f);
        noisePlane->rotation.y = -0.10f;
        noisePlane->rotation.x = 0.04f;
        noisePlane->frustumCulled = false;
        scene->add(noisePlane);

        // --- RNG instances + AABB -----------------------------------------
        auto boxGeometry = threepp::BoxGeometry::create(1.f, 1.f, 1.f);
        auto instanceMaterial = threepp::MeshBasicMaterial::create();
        instanceMaterial->color = threepp::Color(0xffffff);
        auto instances = threepp::InstancedMesh::create(boxGeometry, instanceMaterial, INSTANCE_COUNT);
        instances->position.set(1.55f, -0.95f, 0.f);
        instances->frustumCulled = false;
        scene->add(instances);

        auto aabb = makeAabb();
        aabb->position = instances->position;
        scene->add(aabb);
        scene->add(makeGrid());

        auto camera = threepp::PerspectiveCamera::create(38.f, static_cast<float>(W) / H, 0.1f, 100.f);
        camera->position.set(0.f, 0.15f, 7.4f);
        camera->lookAt(0.f, 0.f, 0.f);

        const cm::SphericalHarmonics3 warm = warmEnvironment();
        const cm::SphericalHarmonics3 cool = coolEnvironment();
        const cm::ImprovedNoise noise;

        for (int frame = 0; frame < FRAMES; ++frame) {
            const float time = static_cast<float>(frame) / FPS;
            const float blend = 0.5f + 0.5f * std::sin(time * 0.62f);
            cm::SphericalHarmonics3 sh = warm;
            sh.lerp(cool, blend);
            sh.scale(0.98f + 0.04f * std::sin(time * 0.9f));

            updateSphereColors(*sphere, sh, time);

            fillNoiseTexture(noiseData, time, noise);
            noiseTexture->setData(noiseData);
            noiseTexture->needsUpdate();

            updateInstances(*instances, *aabb, frame);

            camera->position.x = std::sin(time * 0.18f) * 0.22f;
            camera->position.y = 0.15f + std::sin(time * 0.25f) * 0.06f;
            camera->lookAt(0.f, 0.f, 0.f);

            renderer.render(*scene, *camera);
            const auto pixels = renderer.readRGBPixels();
            if (pixels.size() != static_cast<std::size_t>(W) * H * 3) {
                std::fprintf(stderr, "readback size %zu, want %d\n", pixels.size(), W * H * 3);
                pclose(pipe);
                return false;
            }
            if (std::fwrite(pixels.data(), 1, pixels.size(), pipe) != pixels.size()) {
                std::fprintf(stderr, "ffmpeg pipe closed at frame %d\n", frame);
                pclose(pipe);
                return false;
            }
            if ((frame + 1) % 30 == 0)
                std::fprintf(stderr, "rendered %d/%d frames\n", frame + 1, FRAMES);
        }

        const int status = pclose(pipe);
        renderer.setRenderTarget(nullptr);
        renderer.dispose();
        if (status != 0) {
            std::fprintf(stderr, "ffmpeg exited with status %d\n", status);
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            return false;
        }
        return true;
    }

}// namespace

int main() {
    std::filesystem::create_directories("out/gpu_math_demo");
    const std::string nvencOutput = "out/gpu_math_demo/math_utilities_gpu_nvenc.mp4";
    if (renderMovie(nvencOutput, true)) {
        std::printf("wrote %s (NVENC)\n", nvencOutput.c_str());
        return 0;
    }

    std::fprintf(stderr, "NVENC unavailable, falling back to libx264\n");
    const std::string cpuOutput = "out/gpu_math_demo/math_utilities_gpu.mp4";
    if (!renderMovie(cpuOutput, false)) return 1;
    std::printf("wrote %s (libx264)\n", cpuOutput.c_str());
    return 0;
}
