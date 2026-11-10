// chrononmotion_character_plan — production command for the 3D character path.
//
// Input:  a motion-only rigged glTF/GLB (skins/morphs, no materials) plus an
//         optional static base mesh that supplies UVs and material identity.
// Output: one `chronon.render-plan.v3` JSON whose single Mesh layer carries the
//         full baked `inline_mesh.animation` track, plus a deterministic
//         manifest naming every input digest, the clip, the frame range and the
//         bake signature.
//
// This is the missing production entry point: before it, `applyRigGeometryBake`
// was reachable only from the authoring test. It does not add a second
// evaluator, a renderer or a dependency: the Motion readers, RigProgram
// evaluator, geometry bake and RenderPlan emitter are all existing authorities.

#include "chrononmotion/assets/GltfReader.hpp"
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace rp = chronon3d::render_plan;
using chrononmotion::GltfAsset;
using chrononmotion::GltfMotionAsset;
using chrononmotion::motion::BakedRigProgram;
using chrononmotion::motion::RigProgram;
using chrononmotion::motion::RigProgramEvaluator;
using chrononmotion::motion::RigProgramFrame;

namespace {

constexpr const char* kManifestSchema = "chrononmotion.character-plan.manifest.v1";
constexpr int kDefaultWidth = 1080;
constexpr int kDefaultHeight = 1920;
constexpr int kDefaultFrames = 150;
constexpr int kDefaultFps = 30;

struct Options {
    std::filesystem::path rig;
    std::filesystem::path mesh;
    std::filesystem::path out;
    std::filesystem::path manifest;
    std::string clip;
    std::string texture;
    std::string metallicRoughness;
    int width{kDefaultWidth};
    int height{kDefaultHeight};
    int frames{kDefaultFrames};
    int fps{kDefaultFps};
    std::array<float, 3> cameraPosition{0.0f, 0.0f, -700.0f};
    std::array<float, 3> cameraRotation{0.0f, 0.0f, 0.0f};
    float fov{50.0f};
    float nearPlane{0.1f};
    float farPlane{2000.0f};
    std::optional<std::array<float, 4>> baseColor;
    float diffuse{1.0f};
    float specular{0.25f};
    float shininess{32.0f};
    std::optional<float> metallic;
    std::optional<float> roughness;
    bool doubleSided{false};
    std::optional<float> alphaCutoff;
    bool stripRigMaterials{false};
};

[[noreturn]] void usage(const std::string& problem) {
    throw std::invalid_argument(
        problem + "\n"
        "usage: chrononmotion_character_plan --rig <rigged.glb|rigged.gltf> "
        "[--mesh <static-base.glb>] --out <plan.json> [options]\n"
        "  --manifest <path>            manifest output (default: <plan>.manifest.json)\n"
        "  --clip <name>                clip name (default: the only/first clip)\n"
        "  --frames <n> --fps <n>       baked range and rate (default 150 @ 30)\n"
        "  --width <n> --height <n>     canvas (default 1080x1920)\n"
        "  --camera x,y,z --rotation x,y,z --fov <deg>\n"
        "  --texture <asset>            base-color asset identity\n"
        "  --metallic-roughness <asset> glTF MR asset (G roughness, B metallic)\n"
        "  --base-color r,g,b,a --metallic <f> --roughness <f>\n"
        "  --diffuse <f> --specular <f> --shininess <f> --alpha-cutoff <f>\n"
        "  --double-sided\n"
        "  --strip-rig-materials        drop material/texture/image blocks from the\n"
        "                               rig document before the motion-only reader\n");
}

std::vector<std::byte> readBytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) throw std::runtime_error("cannot open " + path.string());
    const auto size = file.tellg();
    if (size < 0) throw std::runtime_error("cannot size " + path.string());
    std::vector<std::byte> bytes(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!bytes.empty() &&
        !file.read(reinterpret_cast<char*>(bytes.data()),
                   static_cast<std::streamsize>(bytes.size())))
        throw std::runtime_error("cannot read " + path.string());
    return bytes;
}

std::string readText(const std::filesystem::path& path) {
    const auto bytes = readBytes(path);
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::uint64_t fnv1a64(std::span<const std::byte> bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::byte byte : bytes) {
        hash ^= static_cast<std::uint64_t>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::string hexDigest(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

std::array<float, 3> parseVec3(const std::string& text, const char* flag) {
    std::array<float, 3> out{};
    std::size_t start = 0;
    for (std::size_t i = 0; i < 3; ++i) {
        const auto comma = text.find(',', start);
        const auto piece = text.substr(start, comma == std::string::npos
                                                  ? std::string::npos
                                                  : comma - start);
        try {
            out[i] = std::stof(piece);
        } catch (const std::exception&) {
            usage(std::string(flag) + " expects x,y,z");
        }
        if (i < 2 && comma == std::string::npos)
            usage(std::string(flag) + " expects x,y,z");
        start = comma == std::string::npos ? text.size() : comma + 1;
    }
    return out;
}

std::array<float, 4> parseVec4(const std::string& text, const char* flag) {
    std::array<float, 4> out{};
    std::size_t start = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        const auto comma = text.find(',', start);
        const auto piece = text.substr(start, comma == std::string::npos
                                                  ? std::string::npos
                                                  : comma - start);
        try {
            out[i] = std::stof(piece);
        } catch (const std::exception&) {
            usage(std::string(flag) + " expects r,g,b,a");
        }
        if (i < 3 && comma == std::string::npos)
            usage(std::string(flag) + " expects r,g,b,a");
        start = comma == std::string::npos ? text.size() : comma + 1;
    }
    return out;
}

int parseInt(const std::string& text, const char* flag) {
    try {
        return std::stoi(text);
    } catch (const std::exception&) {
        usage(std::string(flag) + " expects an integer");
    }
}

float parseFloat(const std::string& text, const char* flag) {
    try {
        return std::stof(text);
    } catch (const std::exception&) {
        usage(std::string(flag) + " expects a number");
    }
}

Options parseOptions(int argc, char** argv) {
    Options options;
    const auto value = [&](int& index, const char* flag) -> std::string {
        if (index + 1 >= argc) usage(std::string(flag) + " requires a value");
        return argv[++index];
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag = argv[i];
        if (flag == "--rig") options.rig = value(i, "--rig");
        else if (flag == "--mesh") options.mesh = value(i, "--mesh");
        else if (flag == "--out") options.out = value(i, "--out");
        else if (flag == "--manifest") options.manifest = value(i, "--manifest");
        else if (flag == "--clip") options.clip = value(i, "--clip");
        else if (flag == "--texture") options.texture = value(i, "--texture");
        else if (flag == "--metallic-roughness")
            options.metallicRoughness = value(i, "--metallic-roughness");
        else if (flag == "--width") options.width = parseInt(value(i, "--width"), "--width");
        else if (flag == "--height") options.height = parseInt(value(i, "--height"), "--height");
        else if (flag == "--frames") options.frames = parseInt(value(i, "--frames"), "--frames");
        else if (flag == "--fps") options.fps = parseInt(value(i, "--fps"), "--fps");
        else if (flag == "--camera")
            options.cameraPosition = parseVec3(value(i, "--camera"), "--camera");
        else if (flag == "--rotation")
            options.cameraRotation = parseVec3(value(i, "--rotation"), "--rotation");
        else if (flag == "--fov") options.fov = parseFloat(value(i, "--fov"), "--fov");
        else if (flag == "--base-color")
            options.baseColor = parseVec4(value(i, "--base-color"), "--base-color");
        else if (flag == "--metallic")
            options.metallic = parseFloat(value(i, "--metallic"), "--metallic");
        else if (flag == "--roughness")
            options.roughness = parseFloat(value(i, "--roughness"), "--roughness");
        else if (flag == "--diffuse")
            options.diffuse = parseFloat(value(i, "--diffuse"), "--diffuse");
        else if (flag == "--specular")
            options.specular = parseFloat(value(i, "--specular"), "--specular");
        else if (flag == "--shininess")
            options.shininess = parseFloat(value(i, "--shininess"), "--shininess");
        else if (flag == "--alpha-cutoff")
            options.alphaCutoff = parseFloat(value(i, "--alpha-cutoff"), "--alpha-cutoff");
        else if (flag == "--double-sided") options.doubleSided = true;
        else if (flag == "--strip-rig-materials") options.stripRigMaterials = true;
        else usage("unknown flag: " + std::string(flag));
    }
    if (options.rig.empty()) usage("--rig is required");
    if (options.out.empty()) usage("--out is required");
    if (options.frames < 1 || options.frames > 4096)
        usage("--frames must be in [1, 4096] (the inline-mesh animation bound)");
    if (options.fps < 1) usage("--fps must be positive");
    if (options.width < 1 || options.height < 1) usage("canvas must be positive");
    if (options.manifest.empty())
        options.manifest = options.out.string() + ".manifest.json";
    return options;
}

std::string glbJsonDocument(const std::vector<std::byte>& bytes,
                            const std::filesystem::path& path) {
    if (bytes.size() < 20) throw std::runtime_error("GLB header is truncated: " + path.string());
    const auto* raw = reinterpret_cast<const std::uint8_t*>(bytes.data());
    const auto u32 = [&](std::size_t offset) {
        return static_cast<std::uint32_t>(raw[offset]) |
               (static_cast<std::uint32_t>(raw[offset + 1]) << 8) |
               (static_cast<std::uint32_t>(raw[offset + 2]) << 16) |
               (static_cast<std::uint32_t>(raw[offset + 3]) << 24);
    };
    if (u32(0) != 0x46546C67U) throw std::runtime_error("not a GLB container: " + path.string());
    std::size_t offset = 12;
    std::string json;
    std::vector<std::byte> binary;
    while (offset + 8 <= bytes.size()) {
        const std::uint32_t length = u32(offset);
        const std::uint32_t kind = u32(offset + 4);
        offset += 8;
        if (offset + length > bytes.size())
            throw std::runtime_error("GLB chunk is truncated: " + path.string());
        if (kind == 0x4E4F534AU)  // JSON
            json.assign(reinterpret_cast<const char*>(raw + offset), length);
        else if (kind == 0x004E4942U)  // BIN
            binary.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
        offset += length;
    }
    if (json.empty()) throw std::runtime_error("GLB has no JSON chunk: " + path.string());

    nlohmann::json document = nlohmann::json::parse(json);
    if (!binary.empty()) {
        auto& buffers = document["buffers"];
        if (buffers.is_array() && !buffers.empty() && !buffers[0].contains("uri")) {
            static constexpr char kAlphabet[] =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            std::string encoded;
            encoded.reserve((binary.size() + 2) / 3 * 4);
            std::size_t i = 0;
            while (i + 2 < binary.size()) {
                const auto a = static_cast<std::uint32_t>(binary[i]);
                const auto b = static_cast<std::uint32_t>(binary[i + 1]);
                const auto c = static_cast<std::uint32_t>(binary[i + 2]);
                const std::uint32_t triple = (a << 16) | (b << 8) | c;
                encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
                encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
                encoded.push_back(kAlphabet[(triple >> 6) & 0x3F]);
                encoded.push_back(kAlphabet[triple & 0x3F]);
                i += 3;
            }
            if (i < binary.size()) {
                const auto a = static_cast<std::uint32_t>(binary[i]);
                const auto b = i + 1 < binary.size()
                    ? static_cast<std::uint32_t>(binary[i + 1]) : 0U;
                const std::uint32_t triple = (a << 16) | (b << 8);
                encoded.push_back(kAlphabet[(triple >> 18) & 0x3F]);
                encoded.push_back(kAlphabet[(triple >> 12) & 0x3F]);
                encoded.push_back(i + 1 < binary.size()
                                      ? kAlphabet[(triple >> 6) & 0x3F] : '=');
                encoded.push_back('=');
            }
            buffers[0]["uri"] = "data:application/octet-stream;base64," + encoded;
        }
    }
    return document.dump();
}

// The motion reader accepts translation/rotation/scale on nodes, not the
// equivalent 4x4 `matrix` that many exporters emit. Decompose it here so a
// standard rigged GLB is readable without rewriting the file on disk. Shear or
// a mirror cannot be represented as TRS: fail closed instead of baking a
// silently wrong pose.
void decomposeNodeMatrices(nlohmann::json& document) {
    if (!document.contains("nodes") || !document["nodes"].is_array()) return;
    for (auto& node : document["nodes"]) {
        if (!node.contains("matrix")) continue;
        const auto& values = node["matrix"];
        if (!values.is_array() || values.size() != 16)
            throw std::runtime_error("glTF node matrix must have 16 components");
        std::array<double, 16> m{};
        for (std::size_t i = 0; i < 16; ++i) m[i] = values.at(i).get<double>();
        const std::array<double, 3> c0{m[0], m[1], m[2]};
        const std::array<double, 3> c1{m[4], m[5], m[6]};
        const std::array<double, 3> c2{m[8], m[9], m[10]};
        const auto length = [](const std::array<double, 3>& v) {
            return std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        };
        const double sx = length(c0), sy = length(c1), sz = length(c2);
        if (sx < 1e-12 || sy < 1e-12 || sz < 1e-12)
            throw std::runtime_error("glTF node matrix has a zero scale axis");
        const double r[3][3] = {
            {c0[0] / sx, c1[0] / sy, c2[0] / sz},
            {c0[1] / sx, c1[1] / sy, c2[1] / sz},
            {c0[2] / sx, c1[2] / sy, c2[2] / sz},
        };
        const auto dot = [](const double a[3], const double b[3]) {
            return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
        };
        const double det =
            r[0][0] * (r[1][1] * r[2][2] - r[1][2] * r[2][1]) -
            r[0][1] * (r[1][0] * r[2][2] - r[1][2] * r[2][0]) +
            r[0][2] * (r[1][0] * r[2][1] - r[1][1] * r[2][0]);
        if (std::abs(dot(r[0], r[1])) > 1e-4 ||
            std::abs(dot(r[0], r[2])) > 1e-4 ||
            std::abs(dot(r[1], r[2])) > 1e-4 ||
            std::abs(det - 1.0) > 1e-4)
            throw std::runtime_error(
                "glTF node matrix is not a TRS transform (shear or mirror is not supported)");
        double x = 0.0, y = 0.0, z = 0.0, w = 1.0;
        const double trace = r[0][0] + r[1][1] + r[2][2];
        if (trace > 0.0) {
            const double s = std::sqrt(trace + 1.0) * 2.0;
            w = 0.25 * s;
            x = (r[2][1] - r[1][2]) / s;
            y = (r[0][2] - r[2][0]) / s;
            z = (r[1][0] - r[0][1]) / s;
        } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
            const double s = std::sqrt(1.0 + r[0][0] - r[1][1] - r[2][2]) * 2.0;
            w = (r[2][1] - r[1][2]) / s;
            x = 0.25 * s;
            y = (r[0][1] + r[1][0]) / s;
            z = (r[0][2] + r[2][0]) / s;
        } else if (r[1][1] > r[2][2]) {
            const double s = std::sqrt(1.0 + r[1][1] - r[0][0] - r[2][2]) * 2.0;
            w = (r[0][2] - r[2][0]) / s;
            x = (r[0][1] + r[1][0]) / s;
            y = 0.25 * s;
            z = (r[1][2] + r[2][1]) / s;
        } else {
            const double s = std::sqrt(1.0 + r[2][2] - r[0][0] - r[1][1]) * 2.0;
            w = (r[1][0] - r[0][1]) / s;
            x = (r[0][2] + r[2][0]) / s;
            y = (r[1][2] + r[2][1]) / s;
            z = 0.25 * s;
        }
        const double qn = std::sqrt(x * x + y * y + z * z + w * w);
        node.erase("matrix");
        node["translation"] = {m[12], m[13], m[14]};
        node["rotation"] = {x / qn, y / qn, z / qn, w / qn};
        node["scale"] = {sx, sy, sz};
    }
}

// `extras` is exporter metadata (Sketchfab author/title, DCC settings). The
// motion reader rejects it, and it carries no animation data, so drop it
// everywhere instead of forcing every source asset to be pre-cleaned.
void eraseExtras(nlohmann::json& value) {
    if (value.is_object()) {
        value.erase("extras");
        for (auto& child : value.items()) eraseExtras(child.value());
    } else if (value.is_array()) {
        for (auto& child : value) eraseExtras(child);
    }
}

const chrononmotion::GltfMotionClip& selectClip(
    const GltfMotionAsset& asset, const std::string& requested) {
    if (asset.clips.empty())
        throw std::runtime_error("the rig document carries no animation clip");
    if (requested.empty()) return asset.clips.front();
    for (const auto& clip : asset.clips)
        if (clip.name == requested) return clip;
    std::string names;
    for (const auto& clip : asset.clips) names += " " + clip.name;
    throw std::runtime_error("clip '" + requested + "' is not in the rig document; clips:" + names);
}

struct BaseMesh {
    std::vector<rp::InlineMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

BaseMesh staticBaseMesh(const std::filesystem::path& path) {
    GltfAsset asset;
    const auto extension = path.extension().string();
    if (extension == ".glb" || extension == ".GLB") {
        asset = chrononmotion::loadGlb(readBytes(path));
    } else {
        asset = chrononmotion::loadGltf(readText(path));
    }
    if (asset.meshes.empty()) throw std::runtime_error("base mesh has no mesh: " + path.string());
    const auto& mesh = *asset.meshes.front();
    const auto geometry = mesh.geometry();
    if (!geometry) throw std::runtime_error("base mesh has no geometry: " + path.string());
    const auto* position = geometry->getAttribute<float>("position");
    const auto* normal = geometry->getAttribute<float>("normal");
    const auto* uv = geometry->getAttribute<float>("uv");
    if (!position) throw std::runtime_error("base mesh has no position attribute: " + path.string());
    BaseMesh base;
    base.vertices.reserve(static_cast<std::size_t>(position->count()));
    for (int i = 0; i < position->count(); ++i) {
        const auto at = static_cast<std::size_t>(i);
        base.vertices.push_back(rp::InlineMeshVertex{
            {position->getX(at), position->getY(at), position->getZ(at)},
            {normal ? normal->getX(at) : 0.0f,
             normal ? normal->getY(at) : 0.0f,
             normal ? normal->getZ(at) : 1.0f},
            {uv ? uv->getX(at) : 0.0f, uv ? uv->getY(at) : 0.0f}});
    }
    if (const auto* index = geometry->getIndex())
        for (int i = 0; i < index->count(); ++i)
            base.indices.push_back(static_cast<std::uint32_t>(
                index->getX(static_cast<std::size_t>(i))));
    if (base.indices.empty()) {
        base.indices.resize(base.vertices.size());
        for (std::size_t i = 0; i < base.indices.size(); ++i)
            base.indices[i] = static_cast<std::uint32_t>(i);
    }
    return base;
}

void writeFile(const std::filesystem::path& path, const std::string& text) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || !(file << text)) throw std::runtime_error("cannot write " + path.string());
}

} // namespace

int main(int argc, char** argv) {
    try {
        const Options options = parseOptions(argc, argv);

        // ── Rig document ──────────────────────────────────────────────────
        std::string rigJson;
        if (options.rig.extension() == ".glb" || options.rig.extension() == ".GLB")
            rigJson = glbJsonDocument(readBytes(options.rig), options.rig);
        else
            rigJson = readText(options.rig);
        {
            auto document = nlohmann::json::parse(rigJson);
            if (options.stripRigMaterials) {
                document.erase("materials");
                document.erase("textures");
                document.erase("images");
                // Texture sampler state is not motion data; the reader rejects
                // the whole root block, so a textured GLB would fail even with
                // its materials stripped.
                document.erase("samplers");
                for (auto& mesh : document["meshes"])
                    for (auto& primitive : mesh["primitives"]) primitive.erase("material");
            }
            // The motion reader is motion-only by contract: UV, colour and
            // tangent attributes belong to the static base mesh, so they are
            // dropped here instead of making every rigged GLB unreadable.
            // POSITION/NORMAL/JOINTS_n/WEIGHTS_n are the accepted set.
            if (document.contains("meshes") && document["meshes"].is_array()) {
                for (auto& mesh : document["meshes"]) {
                    if (!mesh.contains("primitives")) continue;
                    for (auto& primitive : mesh["primitives"]) {
                        if (!primitive.contains("attributes")) continue;
                        auto& attributes = primitive["attributes"];
                        std::vector<std::string> dropped;
                        for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                            const std::string& key = it.key();
                            const bool motion =
                                key == "POSITION" || key == "NORMAL" ||
                                key.rfind("JOINTS_", 0) == 0 || key.rfind("WEIGHTS_", 0) == 0;
                            if (!motion) dropped.push_back(key);
                        }
                        for (const auto& key : dropped) attributes.erase(key);
                    }
                }
            }
            eraseExtras(document);
            decomposeNodeMatrices(document);
            rigJson = document.dump();
        }
        const auto motionAsset = chrononmotion::loadGltfMotion(rigJson);
        const auto& clip = selectClip(motionAsset, options.clip);

        RigProgram program;
        program.graph = motionAsset.graph;
        program.animation = clip.animation;
        program.geometry = motionAsset.geometry;
        for (const auto& channel : clip.morphs) {
            if (channel.geometryId == 0 || channel.geometryId > program.geometry.size())
                throw std::runtime_error("clip names an out-of-range morph geometry");
            auto& geometry = program.geometry[channel.geometryId - 1];
            if (geometry.id != channel.geometryId ||
                channel.weights.size() != geometry.morphs.size())
                throw std::runtime_error("clip morph channel does not match the rig geometry");
            geometry.morphTracks = channel.weights;
        }
        const RigProgramEvaluator evaluator(program);
        const BakedRigProgram bake = evaluator.bake(0, options.frames - 1,
                                                    static_cast<float>(options.fps));
        if (bake.frames.size() != static_cast<std::size_t>(options.frames))
            throw std::runtime_error("bake returned an unexpected frame count");

        const RigProgramFrame& first = bake.frames.front();
        const chrononmotion::motion::RigFrameGeometry* deformed = nullptr;
        for (const auto& geometry : first.geometry)
            if (!geometry.positions.empty()) { deformed = &geometry; break; }
        if (!deformed) throw std::runtime_error("the clip bakes no geometry for its first frame");
        if (deformed->positions.size() > rp::kInlineMeshMaxVertices)
            throw std::runtime_error("baked geometry exceeds the inline-mesh vertex ceiling");

        // ── Base mesh (UV/material identity) or the baked topology ────────
        BaseMesh base;
        if (!options.mesh.empty()) {
            base = staticBaseMesh(options.mesh);
            if (base.vertices.size() != deformed->positions.size())
                throw std::runtime_error(
                    "base mesh vertex count does not match the rig geometry (" +
                    std::to_string(base.vertices.size()) + " vs " +
                    std::to_string(deformed->positions.size()) + ")");
        } else {
            const auto binding = std::find_if(
                program.geometry.begin(), program.geometry.end(),
                [&](const auto& geometry) { return geometry.id == deformed->id; });
            if (binding == program.geometry.end() || binding->indices.empty())
                throw std::runtime_error(
                    "no --mesh supplied and the rig geometry has no triangle topology");
            base.indices = binding->indices;
            base.vertices.reserve(deformed->positions.size());
            for (std::size_t i = 0; i < deformed->positions.size(); ++i) {
                const auto& position = deformed->positions[i];
                const auto normal = i < deformed->normals.size()
                    ? deformed->normals[i] : chrononmotion::Vector3{0.0f, 0.0f, 1.0f};
                base.vertices.push_back(rp::InlineMeshVertex{
                    {position.x, position.y, position.z},
                    {normal.x, normal.y, normal.z},
                    {0.0f, 0.0f}});
            }
        }
        if (base.vertices.empty() || base.indices.size() < 3 ||
            base.indices.size() % 3 != 0)
            throw std::runtime_error("base mesh is not a non-empty triangle list");
        if (base.indices.size() > rp::kInlineMeshMaxIndices)
            throw std::runtime_error("base mesh exceeds the inline-mesh index ceiling");

        // ── RenderPlan ────────────────────────────────────────────────────
        rp::RenderPlan plan;
        plan.schema = rp::kRenderPlanSchemaV3;
        plan.job_id = options.out.stem().string();
        plan.canvas.width = options.width;
        plan.canvas.height = options.height;
        plan.canvas.fps = chronon3d::FrameRate{options.fps, 1};
        plan.canvas.duration = chronon3d::Frame{options.frames};
        plan.output.path = options.out.string();

        rp::CameraPlan camera;
        camera.position = options.cameraPosition;
        camera.rotation_deg = options.cameraRotation;
        camera.fov_deg = options.fov;
        camera.near = options.nearPlane;
        camera.far = options.farPlane;
        plan.camera = camera;

        rp::InlineMeshPlan meshPlan;
        meshPlan.vertices = std::move(base.vertices);
        meshPlan.indices = std::move(base.indices);
        meshPlan.metallic_roughness_texture_asset = options.metallicRoughness;
        rp::InlineMeshMaterialPlan material;
        material.diffuse = options.diffuse;
        material.specular = options.specular;
        material.shininess = options.shininess;
        material.base_color = options.baseColor;
        material.metallic = options.metallic;
        material.roughness = options.roughness;
        material.double_sided = options.doubleSided;
        material.alpha_cutoff = options.alphaCutoff;
        meshPlan.material = material;

        rp::LayerPlan layer;
        layer.id = "character";
        layer.type = rp::LayerType::Mesh;
        layer.duration_frames = chronon3d::Frame{options.frames};
        layer.enable_3d = true;
        layer.position_dimensions = 3;
        layer.asset = options.texture;
        if (!options.texture.empty()) layer.input_color_space = "srgb";
        layer.inline_mesh = std::move(meshPlan);
        plan.layers.push_back(std::move(layer));

        chrononmotion::motion::applyRigGeometryBake(
            plan.layers.front(), bake, deformed->id,
            std::span<const std::uint32_t>(plan.layers.front().inline_mesh->indices), 0);

        if (const auto problem = chrononmotion::motion::renderPlanEmissionProblem(plan);
            !problem.empty())
            throw std::runtime_error("the produced plan is not expressible: " + problem);
        const std::string planJson = chrononmotion::motion::renderPlanToJson(plan);
        writeFile(options.out, planJson);

        // ── Manifest ──────────────────────────────────────────────────────
        const auto rigBytes = readBytes(options.rig);
        nlohmann::json manifest;
        manifest["schema"] = kManifestSchema;
        manifest["tool"] = "chrononmotion_character_plan";
        manifest["clip"] = clip.name.empty() ? "clip_0" : clip.name;
        manifest["fps"] = options.fps;
        manifest["frames"] = options.frames;
        manifest["canvas"] = {options.width, options.height};
        manifest["geometry_id"] = deformed->id;
        manifest["vertices"] = plan.layers.front().inline_mesh->vertices.size();
        manifest["indices"] = plan.layers.front().inline_mesh->indices.size();
        manifest["animation_samples"] = plan.layers.front().inline_mesh->animation.size();
        manifest["bake_signature"] = bake.signature;
        manifest["rig"] = {
            {"path", options.rig.string()},
            {"bytes", rigBytes.size()},
            {"fnv1a64", hexDigest(fnv1a64(rigBytes))},
            {"stripped_materials", options.stripRigMaterials},
            {"motion_attributes_only", true},
        };
        if (!options.mesh.empty()) {
            const auto meshBytes = readBytes(options.mesh);
            manifest["mesh"] = {
                {"path", options.mesh.string()},
                {"bytes", meshBytes.size()},
                {"fnv1a64", hexDigest(fnv1a64(meshBytes))},
            };
        }
        manifest["plan"] = {
            {"path", options.out.string()},
            {"bytes", planJson.size()},
            {"fnv1a64", hexDigest(fnv1a64(std::span<const std::byte>(
                 reinterpret_cast<const std::byte*>(planJson.data()), planJson.size())))},
        };
        writeFile(options.manifest, manifest.dump(2) + "\n");

        std::cout << options.out.string() << '\n' << options.manifest.string() << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "chrononmotion_character_plan: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
