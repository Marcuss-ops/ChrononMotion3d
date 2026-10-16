// Proof: GLB import -> headless render -> PPM.
//
// Modes:
//   1. Self-contained proof (default):
//        chrononmotion_render_glb [out.ppm [w h]]
//      Synthesizes a deterministic binary GLB 2.0 (24-vertex indexed cube with
//      POSITION, NORMAL, TEXCOORD_0, node rotation, and an embedded 64x64 PNG
//      checkerboard texture in the GLB BIN chunk), loads it via GLTFLoader,
//      renders it offscreen with Lambert lighting, verifies pixel coverage and
//      texture variation, and writes out.ppm.
//   2. External GLB file:
//        chrononmotion_render_glb <model.glb> <out.ppm> [w h]

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "threepp/cameras/PerspectiveCamera.hpp"
#include "threepp/canvas/Canvas.hpp"
#include "threepp/lights/AmbientLight.hpp"
#include "threepp/lights/DirectionalLight.hpp"
#include "threepp/loaders/GLTFLoader.hpp"
#include "threepp/materials/MeshLambertMaterial.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"

using namespace threepp;

namespace {

void appendU16(std::vector<uint8_t>& buf, uint16_t v) {
    buf.push_back(static_cast<uint8_t>(v & 0xffu));
    buf.push_back(static_cast<uint8_t>((v >> 8) & 0xffu));
}

void appendU32(std::vector<uint8_t>& buf, uint32_t v) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        buf.push_back(static_cast<uint8_t>((v >> shift) & 0xffu));
    }
}

void appendFloat(std::vector<uint8_t>& buf, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    appendU32(buf, bits);
}

std::vector<uint8_t> encodeCheckerPng(int size, int cells) {
    std::vector<uint8_t> rgba(size * size * 4);
    const int cell = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const bool hi = ((x / cell) + (y / cell)) % 2 == 0;
            const size_t idx = static_cast<size_t>(y * size + x) * 4;
            rgba[idx + 0] = hi ? 240 : 40;
            rgba[idx + 1] = hi ? 210 : 95;
            rgba[idx + 2] = hi ? 120 : 215;
            rgba[idx + 3] = 255;
        }
    }
    std::vector<uint8_t> png;
    auto writeFunc = [](void* context, void* data, int len) {
        auto* out = static_cast<std::vector<uint8_t>*>(context);
        const auto* bytes = static_cast<const uint8_t*>(data);
        out->insert(out->end(), bytes, bytes + len);
    };
    stbi_write_png_to_func(writeFunc, &png, size, size, 4, rgba.data(), size * 4);
    return png;
}

void writeSyntheticTexturedCubeGlb(const std::filesystem::path& path) {
    struct Face {
        float n[3];
        float v[4][3];
    };
    constexpr float h = 0.7f;
    const Face faces[6] = {
        {{0, 0, 1}, {{-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}}},
        {{0, 0, -1}, {{h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}}},
        {{0, 1, 0}, {{-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}}},
        {{0, -1, 0}, {{-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}}},
        {{1, 0, 0}, {{h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}}},
        {{-1, 0, 0}, {{-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}}},
    };
    const float uvs[4][2] = {{0.f, 0.f}, {1.f, 0.f}, {1.f, 1.f}, {0.f, 1.f}};

    std::vector<uint8_t> bin;
    // View 0: POSITION (24 * 3 floats = 288 bytes)
    for (const auto& f : faces) {
        for (int i = 0; i < 4; ++i) {
            appendFloat(bin, f.v[i][0]);
            appendFloat(bin, f.v[i][1]);
            appendFloat(bin, f.v[i][2]);
        }
    }
    const size_t posLen = bin.size(); // 288

    // View 1: NORMAL (24 * 3 floats = 288 bytes)
    for (const auto& f : faces) {
        for (int i = 0; i < 4; ++i) {
            appendFloat(bin, f.n[0]);
            appendFloat(bin, f.n[1]);
            appendFloat(bin, f.n[2]);
        }
    }
    const size_t normOffset = posLen;
    const size_t normLen = bin.size() - normOffset; // 288

    // View 2: TEXCOORD_0 (24 * 2 floats = 192 bytes)
    const size_t uvOffset = bin.size();
    for (int f = 0; f < 6; ++f) {
        for (int i = 0; i < 4; ++i) {
            appendFloat(bin, uvs[i][0]);
            appendFloat(bin, uvs[i][1]);
        }
    }
    const size_t uvLen = bin.size() - uvOffset; // 192

    // View 3: INDICES (36 * uint16 = 72 bytes)
    const size_t idxOffset = bin.size();
    for (uint16_t f = 0; f < 6; ++f) {
        const uint16_t base = f * 4;
        appendU16(bin, base + 0);
        appendU16(bin, base + 1);
        appendU16(bin, base + 2);
        appendU16(bin, base + 0);
        appendU16(bin, base + 2);
        appendU16(bin, base + 3);
    }
    const size_t idxLen = bin.size() - idxOffset; // 72

    // View 4: embedded PNG image
    while (bin.size() % 4 != 0) bin.push_back(0);
    const size_t imgOffset = bin.size();
    const std::vector<uint8_t> png = encodeCheckerPng(64, 8);
    bin.insert(bin.end(), png.begin(), png.end());
    const size_t imgLen = png.size();
    while (bin.size() % 4 != 0) bin.push_back(0);

    const std::string json =
        std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],)") +
        R"("nodes":[{"name":"textured_cube","rotation":[0.1675188,0.2845147,-0.0505642,0.9425056],"mesh":0}],)" +
        R"("buffers":[{"byteLength":)" + std::to_string(bin.size()) + R"(}],)" +
        R"("bufferViews":[)" +
        R"({"buffer":0,"byteOffset":0,"byteLength":)" + std::to_string(posLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(normOffset) + R"(,"byteLength":)" + std::to_string(normLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(uvOffset) + R"(,"byteLength":)" + std::to_string(uvLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(idxOffset) + R"(,"byteLength":)" + std::to_string(idxLen) + R"(},)" +
        R"({"buffer":0,"byteOffset":)" + std::to_string(imgOffset) + R"(,"byteLength":)" + std::to_string(imgLen) + R"(}],)" +
        R"("accessors":[)" +
        R"({"bufferView":0,"componentType":5126,"count":24,"type":"VEC3","min":[-0.7,-0.7,-0.7],"max":[0.7,0.7,0.7]},)" +
        R"({"bufferView":1,"componentType":5126,"count":24,"type":"VEC3"},)" +
        R"({"bufferView":2,"componentType":5126,"count":24,"type":"VEC2"},)" +
        R"({"bufferView":3,"componentType":5123,"count":36,"type":"SCALAR"}],)" +
        R"("images":[{"bufferView":4,"mimeType":"image/png"}],)" +
        R"("samplers":[{"magFilter":9728,"minFilter":9728,"wrapS":10497,"wrapT":10497}],)" +
        R"("textures":[{"sampler":0,"source":0}],)" +
        R"("materials":[{"name":"checker_pbr","pbrMetallicRoughness":{"baseColorFactor":[1,1,1,1],"baseColorTexture":{"index":0},"metallicFactor":0.1,"roughnessFactor":0.8}}],)" +
        R"("meshes":[{"name":"cube_mesh","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3,"material":0}]}]})";

    std::vector<uint8_t> paddedJson(json.begin(), json.end());
    while (paddedJson.size() % 4 != 0) paddedJson.push_back(static_cast<uint8_t>(' '));

    std::vector<uint8_t> glb;
    const uint32_t totalLen = static_cast<uint32_t>(12 + 8 + paddedJson.size() + 8 + bin.size());
    appendU32(glb, 0x46546C67u); // "glTF"
    appendU32(glb, 2u);
    appendU32(glb, totalLen);
    appendU32(glb, static_cast<uint32_t>(paddedJson.size()));
    appendU32(glb, 0x4E4F534Au); // "JSON"
    glb.insert(glb.end(), paddedJson.begin(), paddedJson.end());
    appendU32(glb, static_cast<uint32_t>(bin.size()));
    appendU32(glb, 0x004E4942u); // "BIN\0"
    glb.insert(glb.end(), bin.begin(), bin.end());

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(glb.data()), static_cast<std::streamsize>(glb.size()));
}

bool endsWithGlbOrGltf(const std::string& s) {
    auto lower = s;
    for (char& c : lower) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return (lower.size() >= 4 && lower.substr(lower.size() - 4) == ".glb") ||
           (lower.size() >= 5 && lower.substr(lower.size() - 5) == ".gltf");
}

} // namespace

int main(int argc, char** argv) {
    std::filesystem::path glbPath;
    std::string outPpm = "glb.ppm";
    int width = 256;
    int height = 256;
    bool cleanupGlb = false;

    if (argc >= 3 && endsWithGlbOrGltf(argv[1])) {
        glbPath = argv[1];
        outPpm = argv[2];
        if (argc > 3) width = std::atoi(argv[3]);
        if (argc > 4) height = std::atoi(argv[4]);
    } else {
        if (argc > 1) outPpm = argv[1];
        if (argc > 2) width = std::atoi(argv[2]);
        if (argc > 3) height = std::atoi(argv[3]);
        glbPath = std::filesystem::path(outPpm + ".synthetic.glb");
        writeSyntheticTexturedCubeGlb(glbPath);
        cleanupGlb = true;
    }

    GLTFLoader loader;
    auto result = loader.load(glbPath);
    if (cleanupGlb) {
        std::error_code ec;
        std::filesystem::remove(glbPath, ec);
    }
    if (!result || !result->scene) {
        std::cerr << "glb proof: failed to load " << glbPath << "\n";
        return 1;
    }
    std::cout << "glb proof: loaded scene with " << result->scene->children.size() << " root children\n";

    Canvas canvas(Canvas::Parameters().size(width, height).headless(true));
    GLRenderer renderer(canvas);
    renderer.setClearColor(Color(0x1a1d24));

    auto target = RenderTarget::create(width, height, RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    auto scene = result->scene;
    // glTF defaults to metallic PBR, which renders dark without an environment
    // map. Swap lit PBR for Lambert keeping the loaded baseColor + texture map,
    // so the proof verifies geometry + UVs + embedded texture + lights.
    int meshCount = 0;
    int swapped = 0;
    int textured = 0;
    scene->traverse([&](Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) {
            ++meshCount;
            if (auto stdMat = mesh->materialAs<MeshStandardMaterial>()) {
                auto lambert = MeshLambertMaterial::create();
                lambert->color = stdMat->color;
                lambert->map = stdMat->map;
                lambert->opacity = stdMat->opacity;
                lambert->transparent = stdMat->transparent;
                if (lambert->map) ++textured;
                mesh->setMaterial(lambert);
                ++swapped;
            }
        }
    });
    std::cout << "glb proof: meshes=" << meshCount << " swapped=" << swapped << " textured=" << textured << "\n";
    if (meshCount == 0) {
        std::cerr << "glb proof: scene contained no meshes\n";
        return 1;
    }

    scene->add(AmbientLight::create(Color(0xffffff), 0.6f));
    auto light = DirectionalLight::create(Color(0xffffff), 2.0f);
    light->position.set(2, 3, 4);
    scene->add(light);

    auto camera = PerspectiveCamera::create(45, float(width) / float(height), 0.1f, 100);
    camera->position.z = 4;
    renderer.render(*scene, *camera);

    const auto pixels = renderer.readRGBPixels();
    if (int(pixels.size()) != width * height * 3) {
        std::cerr << "glb proof: readback size " << pixels.size() << "\n";
        return 1;
    }

    const auto pixelAt = [&](int x, int y) -> uint32_t {
        const size_t i = static_cast<size_t>(y * width + x) * 3;
        return (uint32_t(pixels[i]) << 16) | (uint32_t(pixels[i + 1]) << 8) | uint32_t(pixels[i + 2]);
    };
    const uint32_t corner = pixelAt(2, 2);
    const uint32_t center = pixelAt(width / 2, height / 2);
    std::set<uint32_t> centerPatchColors;
    for (int y = height / 3; y < (2 * height) / 3; ++y) {
        for (int x = width / 3; x < (2 * width) / 3; ++x) {
            const uint32_t c = pixelAt(x, y);
            if (c != corner) centerPatchColors.insert(c);
        }
    }
    std::cout << "glb proof: center=0x" << std::hex << center << " corner=0x" << corner << std::dec
              << " distinct_lit_colors=" << centerPatchColors.size() << "\n";
    if (center == corner || centerPatchColors.size() < 2) {
        std::cerr << "glb proof: expected non-background textured pixels in center region\n";
        return 1;
    }

    std::ofstream ppm(outPpm, std::ios::binary);
    ppm << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        ppm.write(reinterpret_cast<const char*>(&pixels[y * width * 3]), width * 3);
    }
    std::cout << "glb proof: wrote " << outPpm << "\n";

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    return 0;
}
