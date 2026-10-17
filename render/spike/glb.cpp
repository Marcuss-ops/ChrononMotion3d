// Proof: GLB import -> headless render -> PPM.
//
// Modes:
//   1. Self-contained proof (default):
//        chrononmotion_render_glb [out.ppm [w h]]
//      Synthesizes a deterministic binary GLB 2.0 (24-vertex indexed cube with
//      POSITION, NORMAL, TEXCOORD_0, node rotation, and an embedded 64x64 PNG
//      checkerboard texture in the GLB BIN chunk), loads it via GLTFLoader,
//      renders it offscreen with the imported PBR material plus direct lights, verifies pixel coverage and
//      texture variation, and writes out.ppm.
//   2. External GLB file:
//        chrononmotion_render_glb <model.glb> <out.ppm> [w h]

#include <algorithm>
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
#include "threepp/math/Box3.hpp"
#include "threepp/materials/MeshStandardMaterial.hpp"
#include "threepp/objects/Group.hpp"
#include "threepp/objects/Mesh.hpp"
#include "threepp/renderers/GLRenderer.hpp"
#include "threepp/renderers/RenderTarget.hpp"
#include "chrononmotion/motion/CameraRig.hpp"
#include "threepp/motion/MotionBridge.hpp"

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
    int orbitFrames = 0;
    std::filesystem::path orbitDirectory;
    bool cleanupGlb = false;

    if (argc >= 6 && std::string(argv[1]) == "--orbit") {
        glbPath = argv[2];
        orbitDirectory = argv[3];
        orbitFrames = std::atoi(argv[4]);
        width = std::atoi(argv[5]);
        height = argc > 6 ? std::atoi(argv[6]) : width * 9 / 16;
        if (orbitFrames < 2 || width < 1 || height < 1) {
            std::cerr << "usage: chrononmotion_render_glb --orbit model.glb frame_dir frames width [height]\n";
            return 2;
        }
        std::filesystem::create_directories(orbitDirectory);
    } else if (argc >= 3 && endsWithGlbOrGltf(argv[1])) {
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
    renderer.setClearColor(Color(0x393633));
    auto scene = result->scene;

    // Compose the proof around the imported object's actual bounds rather than
    // assuming that its origin is its visual center. This keeps ordinary GLBs
    // inside frame and gives the preview an elevated three-quarter angle.
    scene->updateMatrixWorld(true);
    const Box3 bounds = Box3().setFromObject(*scene, true);
    const Vector3 objectCenter = bounds.getCenter();
    const Vector3 size = bounds.getSize();
    const float span = std::max({size.x, size.y, size.z});

    auto target = RenderTarget::create(width, height, RenderTarget::Options{});
    renderer.setRenderTarget(target.get());

    // Preserve GLTFLoader's imported materials and textures. This proof is meant
    // to exercise the actual imported PBR path rather than replacing it with a
    // simpler material for a brighter screenshot.
    int meshCount = 0;
    int standardMaterials = 0;
    int textured = 0;
    scene->traverse([&](Object3D& node) {
        if (auto* mesh = dynamic_cast<Mesh*>(&node)) {
            ++meshCount;
            if (auto stdMat = mesh->materialAs<MeshStandardMaterial>()) {
                ++standardMaterials;
                if (stdMat->map) ++textured;
            }
        }
    });
    std::cout << "glb proof: meshes=" << meshCount
              << " standard_materials=" << standardMaterials
              << " textured_standard_materials=" << textured << "\n";
    if (meshCount == 0) {
        std::cerr << "glb proof: scene contained no meshes\n";
        return 1;
    }

    scene->add(AmbientLight::create(Color(0xffffff), 8.0f));
    auto light = DirectionalLight::create(Color(0xffe4c4), 24.0f);
    light->position.set(objectCenter.x - span * 1.4f,
                        objectCenter.y + span * 1.8f,
                        objectCenter.z + span * 1.1f);
    auto lightTarget = Group::create();
    lightTarget->position.copy(objectCenter);
    scene->add(lightTarget);
    light->setTarget(*lightTarget);
    scene->add(light);

    const float distance = std::max(span * 2.7f, 1.f);
    auto camera = PerspectiveCamera::create(30, float(width) / float(height), 0.1f, 1000);
    chrononmotion::motion::CameraRig rig(301, 30.f, float(width) / float(height), 0.1f, 1000.f);
    chrononmotion::motion::CameraPose pose;
    pose.position = chrononmotion::Vector3(objectCenter.x + distance * 0.58f,
                                           objectCenter.y + distance * 0.48f,
                                           objectCenter.z + distance * 0.66f);
    chrononmotion::motion::CameraRig::lookAtPose(pose, chrononmotion::Vector3(objectCenter.x, objectCenter.y, objectCenter.z));
    renderer.toneMapping = ToneMapping::ACESFilmic;
    renderer.toneMappingExposure = 2.0f;
    const int renderCount = orbitFrames > 0 ? orbitFrames : 1;
    for (int frame = 0; frame < renderCount; ++frame) {
        if (orbitFrames > 0) {
            const float yaw = (float(frame) / float(orbitFrames - 1)) * 6.28318530718f;
            chrononmotion::motion::CameraPose framePose = pose;
            chrononmotion::motion::CameraRig::orbitAround(framePose,
                    chrononmotion::Vector3(objectCenter.x, objectCenter.y, objectCenter.z), yaw, 0.f);
            threepp::motion::applyCameraPose(framePose, *camera);
        } else {
            threepp::motion::applyCameraPose(pose, *camera);
        }
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
    const uint32_t centerPixel = pixelAt(width / 2, height / 2);
    std::set<uint32_t> centerPatchColors;
    for (int y = height / 3; y < (2 * height) / 3; ++y) {
        for (int x = width / 3; x < (2 * width) / 3; ++x) {
            const uint32_t c = pixelAt(x, y);
            if (c != corner) centerPatchColors.insert(c);
        }
    }
    std::cout << "glb proof: center=0x" << std::hex << centerPixel << " corner=0x" << corner << std::dec
              << " distinct_lit_colors=" << centerPatchColors.size() << "\n";
    if (centerPixel == corner || centerPatchColors.size() < 2) {
        std::cerr << "glb proof: expected non-background textured pixels in center region\n";
        return 1;
    }

    const auto output = orbitFrames > 0
            ? orbitDirectory / ("frame_" + std::string(6 - std::min(6, int(std::to_string(frame).size())), '0') + std::to_string(frame) + ".ppm")
            : std::filesystem::path(outPpm);
    std::ofstream ppm(output, std::ios::binary);
    ppm << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
        ppm.write(reinterpret_cast<const char*>(&pixels[y * width * 3]), width * 3);
    }
    std::cout << "glb proof: wrote " << output << "\n";
    }

    renderer.setRenderTarget(nullptr);
    renderer.dispose();
    return 0;
}
