#include "chrononmotion/assets/GltfReader.hpp"
#include "chrononmotion/materials/PbrMaterial.hpp"
#include "chrononmotion/assets/GltfMotionBridge.hpp"
#include "chrononmotion/motion/MotionScene.hpp"
#include "chrononmotion/motion/AuthoringPolicy.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

using namespace chrononmotion;

namespace {
int failures = 0;
int checks = 0;

void check(bool condition, const char* label) {
    ++checks;
    if (!condition) {
        ++failures;
        std::printf("  FAIL  %s\n", label);
    }
}

template<class Function>
void rejects(Function&& function, const char* label) {
    bool rejected = false;
    try { function(); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, label);
}

void appendU16(std::vector<std::byte>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::byte>(value & 0xff));
    bytes.push_back(static_cast<std::byte>((value >> 8) & 0xff));
}
void appendU32(std::vector<std::byte>& bytes, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<std::byte>((value >> shift) & 0xff));
}
void appendFloat(std::vector<std::byte>& bytes, float value) {
    std::uint32_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(bytes, bits);
}
std::string base64(const std::vector<std::byte>& bytes) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 3) {
        const std::uint32_t a = std::to_integer<std::uint8_t>(bytes[offset]);
        const std::uint32_t b = offset + 1 < bytes.size() ? std::to_integer<std::uint8_t>(bytes[offset + 1]) : 0;
        const std::uint32_t c = offset + 2 < bytes.size() ? std::to_integer<std::uint8_t>(bytes[offset + 2]) : 0;
        const std::uint32_t value = (a << 16) | (b << 8) | c;
        output.push_back(alphabet[(value >> 18) & 63]);
        output.push_back(alphabet[(value >> 12) & 63]);
        output.push_back(offset + 1 < bytes.size() ? alphabet[(value >> 6) & 63] : '=');
        output.push_back(offset + 2 < bytes.size() ? alphabet[value & 63] : '=');
    }
    return output;
}

std::vector<std::byte> triangleBytes() {
    std::vector<std::byte> bytes;
    for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) appendFloat(bytes, value);
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f}) appendFloat(bytes, value);
    for (float value : {0.f, 0.f, 1.f, 0.f, 0.f, 1.f}) appendFloat(bytes, value);
    appendU16(bytes, 0); appendU16(bytes, 1); appendU16(bytes, 2);
    return bytes;
}

std::string dataUriDocument(const std::vector<std::byte>& bytes) {
    return std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"name":"triangle-node","translation":[2,3,4],"mesh":0}],"buffers":[{"byteLength":102,"uri":"data:application/octet-stream;base64,)") +
        base64(bytes) + R"("}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"name":"tri","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]})";
}

std::vector<std::byte> glb(const std::string& json, const std::vector<std::byte>& binary) {
    std::vector<std::byte> paddedJson;
    for (unsigned char value : json) paddedJson.push_back(static_cast<std::byte>(value));
    while (paddedJson.size() % 4) paddedJson.push_back(std::byte{' '});
    std::vector<std::byte> paddedBinary = binary;
    while (paddedBinary.size() % 4) paddedBinary.push_back(std::byte{0});
    const std::uint32_t total = static_cast<std::uint32_t>(12 + 8 + paddedJson.size() + 8 + paddedBinary.size());
    std::vector<std::byte> result;
    appendU32(result, 0x46546c67u); appendU32(result, 2); appendU32(result, total);
    appendU32(result, static_cast<std::uint32_t>(paddedJson.size())); appendU32(result, 0x4e4f534au);
    result.insert(result.end(), paddedJson.begin(), paddedJson.end());
    appendU32(result, static_cast<std::uint32_t>(paddedBinary.size())); appendU32(result, 0x004e4942u);
    result.insert(result.end(), paddedBinary.begin(), paddedBinary.end());
    return result;
}

void validJsonImport() {
    const auto bytes = triangleBytes();
    const auto json = dataUriDocument(bytes);
    const auto asset = loadGltf(json);
    check(asset.scene != nullptr && asset.meshes.size() == 1, "JSON import returns a scene and one primitive mesh");
    check(asset.scene->children.size() == 1 && asset.scene->children.front()->children.size() == 1 &&
          asset.scene->children.front()->children.front() == asset.meshes.front().get(),
          "returned mesh has one deterministic strong owner in its selected scene hierarchy");
    check(asset.meshes.front()->name == "tri_primitive_0", "mesh identity is deterministic from source name and primitive index");
    auto* geometry = asset.meshes.front()->geometry().get();
    check(geometry && geometry->hasIndex(), "indexed primitive becomes BufferGeometry");
    check(geometry && geometry->getAttribute("position") && geometry->getAttribute("normal") && geometry->getAttribute("uv"),
          "POSITION, NORMAL, and TEXCOORD_0 map to standard geometry attributes");
    if (geometry) {
        const auto* position = geometry->getAttribute<float>("position");
        const auto* uv = geometry->getAttribute<float>("uv");
        check(position && position->count() == 3 && position->getX(1) == 1.f && position->getY(2) == 1.f,
              "float VEC3 accessor values are decoded exactly");
        check(uv && uv->count() == 3 && uv->getX(2) == 0.f && uv->getY(2) == 1.f,
              "float VEC2 accessor values are decoded exactly");
        check(geometry->getIndex() && geometry->getIndex()->count() == 3 && static_cast<unsigned int>(geometry->getIndex()->getX(2)) == 2u,
              "unsigned-short scalar indices map to BufferGeometry index");
        check(geometry->boundingBox && geometry->boundingBox->max().x == 1.f && geometry->boundingSphere,
              "import computes deterministic geometry bounds");
    }
    asset.scene->updateMatrixWorld(true);
    Vector3 world{};
    asset.meshes.front()->getWorldPosition(world);
    check(world == Vector3(2.f, 3.f, 4.f), "node translation is retained in the Object3D hierarchy");

    const auto again = loadGltf(json);
    check(again.meshes.size() == asset.meshes.size() &&
          again.meshes.front()->geometry()->getAttribute<float>("position")->getX(1) ==
          asset.meshes.front()->geometry()->getAttribute<float>("position")->getX(1),
          "repeated parse yields stable primitive order and values");
}

void pbrMaterialImport() {
    auto replaceOnce = [](std::string source, const std::string& from, const std::string& to) {
        const auto position = source.find(from);
        if (position != std::string::npos) source.replace(position, from.size(), to);
        return source;
    };
    auto json = dataUriDocument(triangleBytes());
    json = replaceOnce(std::move(json), "\"asset\":{\"version\":\"2.0\"}",
        "\"asset\":{\"version\":\"2.0\"},\"materials\":[{\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0.2,0.4,0.6,0.5],\"metallicFactor\":0.7,\"roughnessFactor\":0.3},\"emissiveFactor\":[0.1,0.2,0.3],\"alphaMode\":\"MASK\",\"alphaCutoff\":0.25,\"doubleSided\":true}]");
    json = replaceOnce(std::move(json), "\"indices\":3", "\"indices\":3,\"material\":0");
    const auto asset = loadGltf(json);
    const auto material = asset.meshes.front()->material();
    check(material && material->is<PbrMaterial>(), "glTF metallic-roughness material imports as PbrMaterial");
    if (material) {
        const auto* pbr = material->as<PbrMaterial>();
        check(pbr && pbr->baseColor.r == 0.2f && pbr->baseColor.g == 0.4f &&
              pbr->baseColor.b == 0.6f && pbr->roughness == 0.3f && pbr->metalness == 0.7f,
              "glTF PBR factors retain base color, roughness, and metalness");
        check(pbr && pbr->opacity == 0.5f && pbr->alphaTest == 0.25f && pbr->doubleSided &&
              pbr->side == Side::Double && pbr->emissiveColor.b == 0.3f,
              "glTF alpha, emissive, and double-sided properties are imported");
    }
    auto attachment = motion::meshAttachmentFromGltf(*asset.meshes.front(), 47);
    check(attachment.geometry == asset.meshes.front()->geometry() &&
          attachment.material.kind == MaterialKind::PBR &&
          attachment.material.metalness == 0.7f && attachment.material.roughness == 0.3f &&
          attachment.material.alphaCutoff == 0.25f && attachment.material.doubleSided,
          "explicit glTF-to-MotionScene bridge preserves PBR, alpha cutoff, and geometry");
    auto invalid = replaceOnce(json, "\"metallicFactor\":0.7", "\"metallicFactor\":1.2");
    bool rejected = false;
    try { (void)loadGltf(invalid); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "out-of-range glTF metallic factor fails closed");
    auto textured = replaceOnce(json, "\"roughnessFactor\":0.3", "\"roughnessFactor\":0.3,\"baseColorTexture\":{\"index\":0},\"metallicRoughnessTexture\":{\"index\":0}");
    textured = replaceOnce(std::move(textured), "\"materials\":[",
        "\"images\":[{\"uri\":\"textures/albedo.png\"}],\"samplers\":[{\"magFilter\":9729,\"minFilter\":9729,\"wrapS\":33071,\"wrapT\":33071}],\"textures\":[{\"source\":0,\"sampler\":0}],\"materials\":[");
    const auto texturedAsset = loadGltf(textured);
    const auto* texturedMaterial = texturedAsset.meshes.front()->material()->as<PbrMaterial>();
    check(texturedMaterial && texturedMaterial->baseColorTextureUri == "textures/albedo.png",
          "glTF base-color texture retains its mounted-root-relative image identity");
    check(texturedMaterial && texturedMaterial->metallicRoughnessTextureUri == "textures/albedo.png",
          "glTF metallic-roughness texture retains its mounted-root-relative image identity");
    auto texturedAttachment = motion::meshAttachmentFromGltf(*texturedAsset.meshes.front(), 48);
    check(texturedAttachment.baseColorTextureAsset == "textures/albedo.png",
          "glTF texture identity crosses the explicit MotionScene bridge");
    check(texturedAttachment.metallicRoughnessTextureAsset == "textures/albedo.png",
          "glTF metallic-roughness identity crosses the explicit MotionScene bridge");
    auto unsafeTexture = replaceOnce(textured, "textures/albedo.png", "../albedo.png");
    rejected = false;
    try { (void)loadGltf(unsafeTexture); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "glTF base-color texture rejects mounted-root traversal");
}

void validGlbImport() {
    auto bytes = triangleBytes();
    std::string json = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"buffers":[{"byteLength":102}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":36},{"buffer":0,"byteOffset":72,"byteLength":24},{"buffer":0,"byteOffset":96,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":3,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]})";
    const auto container = glb(json, bytes);
    const auto asset = loadGlb(container);
    check(asset.meshes.size() == 1 && asset.meshes.front()->geometry()->hasIndex(), "GLB 2.0 JSON/BIN chunks produce indexed geometry");
    const auto noBinContainer = glb(json, {});
    rejects([&] { (void)loadGlb(noBinContainer); }, "GLB rejects a BIN chunk shorter than the declared buffer");
    auto truncated = container;
    truncated.pop_back();
    rejects([&] { (void)loadGlb(truncated); }, "GLB rejects declared length mismatch and truncated bytes");
}

void unindexedAndExternalBuffer() {
    std::vector<std::byte> bytes;
    for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) appendFloat(bytes, value);
    const std::string json = std::string(R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],"buffers":[{"byteLength":36,"uri":"triangle.bin"}],"bufferViews":[{"buffer":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"meshes":[{"primitives":[{"attributes":{"POSITION":0}}]}]})");
    rejects([&] { (void)loadGltf(json); }, "external buffer URIs require explicit caller resolution");
    int resolutions = 0;
    const auto resolver = [&](std::string_view uri) {
        ++resolutions;
        if (uri != "triangle.bin") throw std::invalid_argument("unexpected URI");
        return bytes;
    };
    const auto asset = loadGltf(json, resolver);
    check(resolutions == 1 && asset.meshes.size() == 1, "explicit resolver supplies external bytes without implicit filesystem access");
    check(!asset.meshes.front()->geometry()->hasIndex() &&
          asset.meshes.front()->geometry()->getAttribute<float>("position")->count() == 3,
          "non-indexed triangle primitive imports with its vertex array");
}

void rejectsUnsupportedAndMalformed() {
    const auto base = dataUriDocument(triangleBytes());
    rejects([&] { (void)loadGltf("{"); }, "malformed JSON is rejected");
    rejects([&] { (void)loadGltf(base.substr(0, base.size() - 4)); }, "truncated JSON is rejected");
    auto replaceOnce = [](std::string source, const std::string& from, const std::string& to) {
        const auto position = source.find(from);
        if (position != std::string::npos) source.replace(position, from.size(), to);
        return source;
    };
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"version\":\"2.0\"", "\"version\":\"1.0\"")); },
            "non-2.0 glTF version is rejected");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"indices\":3", "\"indices\":99")); },
            "out-of-range index accessor reference is rejected");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"indices\":3", "\"indices\":3,\"mode\":1")); },
            "non-triangle primitive mode is rejected");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"indices\":3", "\"indices\":3,\"targets\":[]")); },
            "morph target primitives are explicitly rejected");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"asset\":{\"version\":\"2.0\"}", "\"asset\":{\"version\":\"2.0\"},\"extensionsUsed\":[\"KHR_draco_mesh_compression\"]")); },
            "declared compression extensions are rejected instead of ignored");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"mesh\":0}", "\"mesh\":0,\"renderMode\":\"transparent\"}")); },
            "unknown render-affecting node data is rejected instead of ignored");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"nodes\":[{\"name\":\"triangle-node\",\"translation\":[2,3,4],\"mesh\":0}]",
                "\"nodes\":[{\"name\":\"triangle-node\",\"translation\":[2,3,4],\"mesh\":0,\"children\":[0]}]")); },
            "cyclic node hierarchy is rejected");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"byteLength\":102", "\"byteLength\":1000000000")); },
            "oversized declared buffer is rejected before allocation");
    rejects([&] { (void)loadGlb(std::span<const std::byte>{}); }, "empty GLB container is rejected");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"max\":[1,1,0]", "\"max\":[2,1,0]")); },
            "POSITION accessor min/max must agree with the decoded vertices");
    rejects([&] { (void)loadGltf(replaceOnce(base, "\"max\":[1,1,0]", "\"max\":[1,1,0],\"unknownAccessorFlag\":true")); },
            "unknown accessor fields are rejected");

    GltfReadPolicy disabled;
    disabled.allowStaticGltfImport = false;
    rejects([&] { (void)loadGltf(base, {}, disabled); }, "authoring policy can disable static glTF import");
    GltfReadPolicy tinyBudget;
    tinyBudget.maxVertices = 2;
    rejects([&] { (void)loadGltf(base, {}, tinyBudget); }, "authoring policy enforces glTF vertex budget");
    bool sceneImportDenied = false;
    motion::MotionScene importScene("import policy");
    motion::AuthoringPolicy scenePolicy;
    scenePolicy.allowStaticGltfImport = false;
    importScene.setAuthoringPolicy(scenePolicy);
    const auto sceneImportPolicy = importScene.gltfReadPolicy();
    try { (void)loadGltf(base, {}, sceneImportPolicy); }
    catch (const std::invalid_argument&) { sceneImportDenied = true; }
    check(sceneImportDenied && importScene.auditLog().size() == 1,
          "scene-derived glTF policy denies imports before recording an accepted audit event");
    bool auditedImport = false;
    GltfReadPolicy observed;
    observed.audit = [&](bool motion, std::string_view format) {
        auditedImport = !motion && format == "gltf-json-static";
    };
    (void)loadGltf(base, {}, observed);
    check(auditedImport, "glTF reader reports accepted imports to the policy audit hook");

}

} // namespace

int main() {
    validJsonImport();
    pbrMaterialImport();
    validGlbImport();
    unindexedAndExternalBuffer();
    rejectsUnsupportedAndMalformed();
    std::printf("\n%d glTF checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
