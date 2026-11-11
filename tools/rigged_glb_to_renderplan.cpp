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
#include "chrononmotion/motion/FaceRig.hpp"
#include "chrononmotion/motion/RenderPlanEmission.hpp"
#include "chrononmotion/motion/RenderPlanLowering.hpp"

#include <chronon3d/render_plan/render_plan.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
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
using chrononmotion::GltfMotionClip;
using chrononmotion::motion::BakedRigProgram;
using chrononmotion::motion::RigProgram;
using chrononmotion::motion::RigProgramEvaluator;
using chrononmotion::motion::RigProgramFrame;

namespace {

constexpr const char* kManifestSchema = "chrononmotion.character-plan.manifest.v2";
constexpr int kDefaultWidth = 1080;
constexpr int kDefaultHeight = 1920;
constexpr int kDefaultFrames = 150;
constexpr int kDefaultFps = 30;
constexpr float kDefaultCrossfadeSeconds = 0.25f;

struct Options {
    std::filesystem::path rig;
    std::filesystem::path mesh;
    std::filesystem::path out;
    std::filesystem::path manifest;
    std::string clip;
    std::vector<std::string> sequence{};
    bool framesSet{false};
    float crossfade{kDefaultCrossfadeSeconds};
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
    std::filesystem::path faceMap;
    std::filesystem::path faceCues;
};

[[noreturn]] void usage(const std::string& problem) {
    throw std::invalid_argument(
        problem + "\n"
        "usage: chrononmotion_character_plan --rig <rigged.glb|rigged.gltf> "
        "[--mesh <static-base.glb>] --out <plan.json> [options]\n"
        "  --manifest <path>            manifest output (default: <plan>.manifest.json)\n"
        "  --clip <name>                clip name (default: the only/first clip)\n"
        "  --sequence A,B[,...]         concatenate clips with a crossfade; the\n"
        "                               output length comes from the clip durations\n"
        "  --crossfade <seconds>        sequence crossfade (default 0.25)\n"
        "  --frames <n> --fps <n>       single-clip baked range and rate (default 150 @ 30)\n"
        "  --width <n> --height <n>     canvas (default 1080x1920)\n"
        "  --camera x,y,z --rotation x,y,z --fov <deg>\n"
        "  --texture <asset>            base-color asset identity\n"
        "  --metallic-roughness <asset> glTF MR asset (G roughness, B metallic)\n"
        "  --base-color r,g,b,a --metallic <f> --roughness <f>\n"
        "  --diffuse <f> --specular <f> --shininess <f> --alpha-cutoff <f>\n"
        "  --double-sided\n"
        "  --strip-rig-materials        drop material/texture/image blocks from the\n"
        "                               rig document before the motion-only reader\n"
        "  --face-map <json>            morph-index map for the optional face rig\n"
        "  --face-cues <json>           viseme/blink/gaze cues (single clip only)\n");
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

std::vector<std::byte> decodeBase64(std::string_view text) {
    constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::array<int, 256> table{};
    table.fill(-1);
    for (int i = 0; i < 64; ++i)
        table[static_cast<unsigned char>(alphabet[i])] = i;

    std::vector<std::byte> output;
    std::uint32_t accumulator = 0;
    int bits = -8;
    for (const char character : text) {
        if (character == '=') break;
        const int value = table[static_cast<unsigned char>(character)];
        if (value < 0) throw std::runtime_error("invalid base64 in embedded GLB buffer");
        accumulator = (accumulator << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<std::byte>((accumulator >> bits) & 0xFFU));
            bits -= 8;
        }
    }
    return output;
}

void writeBytes(const std::filesystem::path& path, std::span<const std::byte> bytes) {
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file || (!bytes.empty() && !file.write(
                      reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()))))
        throw std::runtime_error("cannot write " + path.string());
}

chrononmotion::motion::FaceRigMap loadFaceMap(const std::filesystem::path& path) {
    const nlohmann::json document = nlohmann::json::parse(readText(path));
    if (!document.is_object())
        throw std::runtime_error("face map must be a JSON object: " + path.string());
    chrononmotion::motion::FaceRigMap map;
    map.geometryId = document.at("geometry_id").get<std::uint32_t>();
    if (document.contains("visemes")) {
        const auto& visemes = document["visemes"];
        if (!visemes.is_object())
            throw std::runtime_error("face map visemes must be an object");
        for (auto it = visemes.begin(); it != visemes.end(); ++it) {
            const auto viseme = chrononmotion::motion::parseViseme(it.key());
            if (!viseme || *viseme == chrononmotion::motion::Viseme::Neutral)
                throw std::runtime_error("face map has an invalid or neutral viseme: " + it.key());
            if (!it.value().is_array())
                throw std::runtime_error("face map viseme entries must be arrays");
            for (const auto& item : it.value()) {
                chrononmotion::motion::FaceMorphBinding binding;
                binding.morph = item.at("morph").get<std::uint32_t>();
                binding.weight = item.value("weight", 1.f);
                map.visemes[static_cast<std::size_t>(*viseme)].push_back(binding);
            }
        }
    }
    if (document.contains("blink_morph"))
        map.blinkMorph = document["blink_morph"].get<std::uint32_t>();
    if (document.contains("gaze_left_morph"))
        map.gazeLeftMorph = document["gaze_left_morph"].get<std::uint32_t>();
    if (document.contains("gaze_right_morph"))
        map.gazeRightMorph = document["gaze_right_morph"].get<std::uint32_t>();
    return map;
}

chrononmotion::motion::FacePerformance loadFaceCues(const std::filesystem::path& path) {
    const nlohmann::json document = nlohmann::json::parse(readText(path));
    if (!document.is_object())
        throw std::runtime_error("face cues must be a JSON object: " + path.string());
    chrononmotion::motion::FacePerformance performance;
    performance.firstFrame = document.at("first_frame").get<int>();
    performance.lastFrame = document.at("last_frame").get<int>();
    if (document.contains("visemes")) {
        for (const auto& item : document["visemes"]) {
            const auto viseme = chrononmotion::motion::parseViseme(
                item.at("viseme").get<std::string>());
            if (!viseme)
                throw std::runtime_error("face cues contain an unknown viseme");
            chrononmotion::motion::VisemeCue cue;
            cue.frame = item.at("frame").get<int>();
            cue.viseme = *viseme;
            cue.weight = item.value("weight", 1.f);
            performance.visemes.push_back(cue);
        }
    }
    if (document.contains("blinks")) {
        for (const auto& item : document["blinks"]) {
            chrononmotion::motion::BlinkCue cue;
            cue.frame = item.at("frame").get<int>();
            cue.durationFrames = item.value("duration_frames", 4);
            cue.weight = item.value("weight", 1.f);
            performance.blinks.push_back(cue);
        }
    }
    if (document.contains("gaze")) {
        for (const auto& item : document["gaze"]) {
            chrononmotion::motion::GazeCue cue;
            cue.frame = item.at("frame").get<int>();
            cue.durationFrames = item.value("duration_frames", 8);
            cue.horizontal = item.value("horizontal", 0.f);
            cue.weight = item.value("weight", 1.f);
            performance.gaze.push_back(cue);
        }
    }
    return performance;
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

// Small, dependency-free SHA-256 for the manifest. The ticket requires SHA-256
// input/output digests; FNV-1a stays as the compact compatibility digest.
std::array<std::uint8_t, 32> sha256Digest(std::span<const std::byte> bytes) {
    static constexpr std::uint32_t kRound[64] = {
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
        0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
        0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
        0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
        0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
        0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
        0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
        0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
        0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
    };
    std::uint32_t state[8] = {
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    const auto rotr = [](std::uint32_t value, int count) {
        return (value >> count) | (value << (32 - count));
    };
    const auto process = [&](const std::uint8_t* block) {
        std::uint32_t words[64]{};
        for (int i = 0; i < 16; ++i) {
            words[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
                       (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                       (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                       static_cast<std::uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            const auto s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^
                            (words[i - 15] >> 3);
            const auto s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^
                            (words[i - 2] >> 10);
            words[i] = words[i - 16] + s0 + words[i - 7] + s1;
        }
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        std::uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (int i = 0; i < 64; ++i) {
            const auto sum1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const auto choose = (e & f) ^ (~e & g);
            const auto temp1 = h + sum1 + choose + kRound[i] + words[i];
            const auto sum0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const auto majority = (a & b) ^ (a & c) ^ (b & c);
            const auto temp2 = sum0 + majority;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    };
    std::size_t offset = 0;
    for (; offset + 64 <= bytes.size(); offset += 64)
        process(reinterpret_cast<const std::uint8_t*>(bytes.data()) + offset);
    std::uint8_t tail[128]{};
    const std::size_t remaining = bytes.size() - offset;
    if (remaining != 0)
        std::memcpy(tail, bytes.data() + offset, remaining);
    tail[remaining] = 0x80;
    const std::size_t padded = remaining < 56 ? 64 : 128;
    const std::uint64_t bitLength = static_cast<std::uint64_t>(bytes.size()) * 8u;
    for (int i = 0; i < 8; ++i)
        tail[padded - 1 - static_cast<std::size_t>(i)] =
            static_cast<std::uint8_t>((bitLength >> (8 * i)) & 0xffu);
    process(tail);
    if (padded == 128) process(tail + 64);
    std::array<std::uint8_t, 32> digest{};
    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = static_cast<std::uint8_t>(state[i] >> 24);
        digest[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
        digest[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
        digest[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
    }
    return digest;
}

std::string sha256Hex(std::span<const std::byte> bytes) {
    const auto digest = sha256Digest(bytes);
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const auto byte : digest) out << std::setw(2) << static_cast<unsigned>(byte);
    return out.str();
}

std::string sha256Hex(std::string_view text) {
    return sha256Hex(std::span<const std::byte>(
        reinterpret_cast<const std::byte*>(text.data()), text.size()));
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

std::vector<std::string> splitSequence(const std::string& text) {
    std::vector<std::string> clips;
    std::size_t start = 0;
    for (;;) {
        const auto comma = text.find(',', start);
        std::string piece = text.substr(
            start, comma == std::string::npos ? std::string::npos : comma - start);
        const auto first = piece.find_first_not_of(" \t\r\n");
        const auto last = piece.find_last_not_of(" \t\r\n");
        if (first == std::string::npos)
            usage("--sequence expects non-empty comma-separated clip names");
        clips.push_back(piece.substr(first, last - first + 1));
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return clips;
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
        else if (flag == "--sequence") options.sequence = splitSequence(value(i, "--sequence"));
        else if (flag == "--crossfade")
            options.crossfade = parseFloat(value(i, "--crossfade"), "--crossfade");
        else if (flag == "--texture") options.texture = value(i, "--texture");
        else if (flag == "--metallic-roughness")
            options.metallicRoughness = value(i, "--metallic-roughness");
        else if (flag == "--width") options.width = parseInt(value(i, "--width"), "--width");
        else if (flag == "--height") options.height = parseInt(value(i, "--height"), "--height");
        else if (flag == "--frames") {
            options.frames = parseInt(value(i, "--frames"), "--frames");
            options.framesSet = true;
        }
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
        else if (flag == "--face-map") options.faceMap = value(i, "--face-map");
        else if (flag == "--face-cues") options.faceCues = value(i, "--face-cues");
        else usage("unknown flag: " + std::string(flag));
    }
    if (options.rig.empty()) usage("--rig is required");
    if (options.out.empty()) usage("--out is required");
    if (!options.clip.empty() && !options.sequence.empty())
        usage("--clip and --sequence are mutually exclusive");
    if (!options.sequence.empty()) {
        if (options.sequence.size() < 2)
            usage("--sequence needs at least two clip names");
        if (options.framesSet)
            usage("--frames is only for a single clip; --sequence derives its length from clip durations");
    }
    if (options.faceMap.empty() != options.faceCues.empty())
        usage("--face-map and --face-cues must be used together");
    if (!options.faceMap.empty() && !options.sequence.empty())
        usage("face performance is single-clip only; --sequence is not supported");
    if (!std::isfinite(options.crossfade) || options.crossfade < 0.f)
        usage("--crossfade must be a finite non-negative number");
    if (options.frames < 1 || options.frames > 4096)
        usage("--frames must be in [1, 4096] (the inline-mesh animation bound)");
    if (options.fps < 1) usage("--fps must be positive");
    if (options.width < 1 || options.height < 1) usage("canvas must be positive");
    const auto finiteVec3 = [](const std::array<float, 3>& value) {
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
    };
    if (!finiteVec3(options.cameraPosition) || !finiteVec3(options.cameraRotation))
        usage("--camera and --rotation must be finite x,y,z values");
    if (!std::isfinite(options.fov) || options.fov <= 0.f || options.fov >= 180.f)
        usage("--fov must be finite and inside (0, 180)");
    if (!std::isfinite(options.nearPlane) || options.nearPlane <= 0.f)
        usage("--near must be finite and positive");
    if (!std::isfinite(options.farPlane) || options.farPlane <= options.nearPlane)
        usage("--far must be finite and greater than --near");
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

RigProgram makeProgram(const GltfMotionAsset& asset, const GltfMotionClip& clip) {
    RigProgram program;
    program.graph = asset.graph;
    program.animation = clip.animation;
    program.geometry = asset.geometry;
    for (const auto& channel : clip.morphs) {
        if (channel.geometryId == 0 || channel.geometryId > program.geometry.size())
            throw std::runtime_error("clip names an out-of-range morph geometry");
        auto& geometry = program.geometry[channel.geometryId - 1];
        if (geometry.id != channel.geometryId ||
            channel.weights.size() != geometry.morphs.size())
            throw std::runtime_error("clip morph channel does not match the rig geometry");
        geometry.morphTracks = channel.weights;
    }
    return program;
}

struct ClipEntry {
    std::string name;
    RigProgram program{};
    std::unique_ptr<RigProgramEvaluator> evaluator{};
    float duration{0.f};
    int frames{0};
};

float clipDuration(const GltfMotionClip& clip) {
    float duration = clip.duration;
    if (!(duration > 0.f) || !std::isfinite(duration)) {
        duration = 0.f;
        for (const auto& node : clip.animation)
            duration = std::max(duration, node.tracks.endTime());
    }
    if (!(duration > 0.f) || !std::isfinite(duration))
        throw std::runtime_error("clip has no finite positive duration");
    return duration;
}

BakedRigProgram bakeSequence(const std::vector<ClipEntry>& clips, float crossfade,
                             float fps, int& outputFrames) {
    if (clips.size() < 2) throw std::invalid_argument("sequence needs at least two clips");
    float shortest = clips.front().duration;
    for (const auto& clip : clips) shortest = std::min(shortest, clip.duration);
    if (crossfade >= shortest)
        throw std::invalid_argument("crossfade must be shorter than every clip");
    if (clips.size() > 2 && crossfade * 2.f >= shortest)
        throw std::invalid_argument("crossfade must be at most half of every clip in a multi-clip sequence");

    std::vector<float> starts(clips.size(), 0.f);
    double totalSeconds = clips.front().duration;
    for (std::size_t i = 1; i < clips.size(); ++i) {
        starts[i] = starts[i - 1] + clips[i - 1].duration - crossfade;
        totalSeconds += static_cast<double>(clips[i].duration) - crossfade;
    }
    const int frames = static_cast<int>(std::llround(totalSeconds * fps));
    if (frames < 1 || frames > 4096)
        throw std::invalid_argument("sequence exceeds the 4096-frame inline-mesh bound");
    outputFrames = frames;

    BakedRigProgram result;
    std::uint64_t signature = 1469598103934665603ULL;
    const auto mix = [&signature](std::uint64_t value) {
        signature ^= value;
        signature *= 1099511628211ULL;
    };
    mix(static_cast<std::uint64_t>(frames));
    mix(static_cast<std::uint64_t>(std::llround(fps * 1000.f)));

    for (int frameIndex = 0; frameIndex < frames; ++frameIndex) {
        const float seconds = static_cast<float>(frameIndex) / fps;
        std::vector<std::size_t> active;
        std::vector<float> weights;
        for (std::size_t i = 0; i < clips.size(); ++i) {
            float weight = 1.f;
            if (crossfade <= 0.f) {
                weight = (seconds >= starts[i] && seconds < starts[i] + clips[i].duration)
                             ? 1.f : 0.f;
            } else {
                if (i > 0)
                    weight *= std::clamp((seconds - starts[i]) / crossfade, 0.f, 1.f);
                if (i + 1 < clips.size())
                    weight *= std::clamp(
                        (starts[i] + clips[i].duration - seconds) / crossfade, 0.f, 1.f);
            }
            if (weight > 1e-6f) {
                active.push_back(i);
                weights.push_back(weight);
            }
        }
        if (active.empty())
            throw std::runtime_error("sequence has a gap with no active clip");
        float weightSum = 0.f;
        for (const auto weight : weights) weightSum += weight;
        if (!(weightSum > 0.f) || !std::isfinite(weightSum))
            throw std::runtime_error("sequence produced an invalid clip weight");
        for (auto& weight : weights) weight /= weightSum;

        std::vector<RigProgramFrame> evaluated;
        evaluated.reserve(active.size());
        for (const auto index : active) {
            const float localSeconds = std::clamp(
                seconds - starts[index], 0.f, clips[index].duration);
            int localFrame = static_cast<int>(std::llround(localSeconds * fps));
            localFrame = std::clamp(localFrame, 0, clips[index].frames - 1);
            evaluated.push_back(clips[index].evaluator->evaluate(localFrame, fps));
        }

        RigProgramFrame output;
        output.pose = evaluated.front().pose;
        for (const auto& reference : evaluated.front().geometry) {
            std::vector<const chrononmotion::motion::RigFrameGeometry*> sources;
            sources.reserve(evaluated.size());
            for (const auto& evaluatedFrame : evaluated) {
                const auto found = std::find_if(
                    evaluatedFrame.geometry.begin(), evaluatedFrame.geometry.end(),
                    [&reference](const auto& geometry) { return geometry.id == reference.id; });
                if (found == evaluatedFrame.geometry.end())
                    throw std::runtime_error("sequence clips do not share the same geometry id");
                if (found->positions.size() != reference.positions.size())
                    throw std::runtime_error("sequence clips do not share the same geometry topology");
                if (found->normals.empty() != reference.normals.empty())
                    throw std::runtime_error("sequence clips disagree on baked normals");
                sources.push_back(&*found);
            }
            chrononmotion::motion::RigFrameGeometry blended;
            blended.id = reference.id;
            blended.positions.resize(reference.positions.size());
            const bool hasNormals = !reference.normals.empty();
            if (hasNormals) blended.normals.resize(reference.positions.size());
            for (std::size_t vertex = 0; vertex < reference.positions.size(); ++vertex) {
                chrononmotion::Vector3 position{};
                chrononmotion::Vector3 normal{};
                for (std::size_t sourceIndex = 0; sourceIndex < sources.size(); ++sourceIndex) {
                    const auto& sourcePosition = sources[sourceIndex]->positions[vertex];
                    position.x += sourcePosition.x * weights[sourceIndex];
                    position.y += sourcePosition.y * weights[sourceIndex];
                    position.z += sourcePosition.z * weights[sourceIndex];
                    if (hasNormals) {
                        const auto& sourceNormal = sources[sourceIndex]->normals[vertex];
                        normal.x += sourceNormal.x * weights[sourceIndex];
                        normal.y += sourceNormal.y * weights[sourceIndex];
                        normal.z += sourceNormal.z * weights[sourceIndex];
                    }
                }
                blended.positions[vertex] = position;
                if (hasNormals) {
                    const float lengthSq = normal.lengthSq();
                    if (lengthSq > 1e-20f) normal /= std::sqrt(lengthSq);
                    else normal.set(0.f, 0.f, 1.f);
                    blended.normals[vertex] = normal;
                }
            }
            output.geometry.push_back(std::move(blended));
        }
        mix(chrononmotion::motion::rigFrameSignature(output));
        result.frames.push_back(std::move(output));
    }
    result.signature = signature;
    return result;
}

struct BaseMesh {
    std::vector<rp::InlineMeshVertex> vertices;
    std::vector<std::uint32_t> indices;
};

// The static reader rejects skin, animation, morph and material blocks by
// contract. A normal rigged GLB carries all of them, so strip a copy down to
// the geometry/material-identity subset the reader accepts. This is what makes
// the separate hand-built `lowpoly_base.glb` unnecessary.
nlohmann::json stripToStaticBase(const nlohmann::json& source) {
    auto document = source;
    for (const char* key : {"animations", "skins", "materials", "textures", "images",
                           "samplers", "cameras", "extensionsUsed", "extensionsRequired"})
        document.erase(key);
    if (document.contains("nodes") && document["nodes"].is_array()) {
        for (auto& node : document["nodes"]) {
            node.erase("skin");
            node.erase("weights");
            node.erase("camera");
            node.erase("extensions");
        }
    }
    if (document.contains("meshes") && document["meshes"].is_array()) {
        for (auto& mesh : document["meshes"]) {
            mesh.erase("weights");
            mesh.erase("extensions");
            if (!mesh.contains("primitives") || !mesh["primitives"].is_array()) continue;
            for (auto& primitive : mesh["primitives"]) {
                primitive.erase("material");
                primitive.erase("targets");
                primitive.erase("extensions");
                if (!primitive.contains("attributes")) continue;
                auto& attributes = primitive["attributes"];
                std::vector<std::string> dropped;
                for (auto it = attributes.begin(); it != attributes.end(); ++it) {
                    const std::string& key = it.key();
                    if (key != "POSITION" && key != "NORMAL" && key != "TEXCOORD_0")
                        dropped.push_back(key);
                }
                for (const auto& key : dropped) attributes.erase(key);
            }
        }
    }
    eraseExtras(document);
    return document;
}

BaseMesh meshToBase(const chrononmotion::Mesh& mesh) {
    const auto geometry = mesh.geometry();
    if (!geometry) throw std::runtime_error("base mesh has no geometry");
    const auto* position = geometry->getAttribute<float>("position");
    const auto* normal = geometry->getAttribute<float>("normal");
    const auto* uv = geometry->getAttribute<float>("uv");
    if (!position) throw std::runtime_error("base mesh has no position attribute");
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
    if (const auto* index = geometry->getIndex()) {
        for (int i = 0; i < index->count(); ++i)
            base.indices.push_back(static_cast<std::uint32_t>(
                index->getX(static_cast<std::size_t>(i))));
    } else {
        base.indices.resize(base.vertices.size());
        for (std::size_t i = 0; i < base.indices.size(); ++i)
            base.indices[i] = static_cast<std::uint32_t>(i);
    }
    return base;
}

BaseMesh selectBase(const GltfAsset& asset, std::size_t expectedVertices,
                    std::span<const std::uint32_t> expectedIndices,
                    const std::string& label) {
    std::vector<BaseMesh> candidates;
    candidates.reserve(asset.meshes.size());
    for (const auto& mesh : asset.meshes) candidates.push_back(meshToBase(*mesh));
    if (candidates.empty()) throw std::runtime_error(label + ": base mesh has no primitives");

    std::vector<std::size_t> matching;
    std::string available;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        available += " " + std::to_string(candidates[i].vertices.size());
        if (candidates[i].vertices.size() == expectedVertices) matching.push_back(i);
    }
    if (matching.empty())
        throw std::runtime_error(label + ": no primitive matches the rig vertex count " +
                                 std::to_string(expectedVertices) + " (found:" + available + ")");

    if (!expectedIndices.empty()) {
        std::vector<std::size_t> topology;
        for (const auto index : matching) {
            const auto& indices = candidates[index].indices;
            if (indices.size() == expectedIndices.size() &&
                std::equal(expectedIndices.begin(), expectedIndices.end(), indices.begin(), indices.end()))
                topology.push_back(index);
        }
        if (topology.empty())
            throw std::runtime_error(label + ": triangle indices do not match the rig topology");
        matching = std::move(topology);
    }
    if (matching.size() != 1)
        throw std::runtime_error(label + ": more than one primitive matches the rig topology");
    return std::move(candidates[matching.front()]);
}

BaseMesh staticBaseFromDocument(const nlohmann::json& document, std::size_t expectedVertices,
                                std::span<const std::uint32_t> expectedIndices,
                                const std::filesystem::path& label) {
    const auto staticDocument = stripToStaticBase(document);
    const auto asset = chrononmotion::loadGltf(staticDocument.dump());
    return selectBase(asset, expectedVertices, expectedIndices, label.string());
}

BaseMesh loadStaticBase(const std::filesystem::path& path, std::size_t expectedVertices,
                        std::span<const std::uint32_t> expectedIndices) {
    nlohmann::json document;
    if (path.extension() == ".glb" || path.extension() == ".GLB")
        document = nlohmann::json::parse(glbJsonDocument(readBytes(path), path));
    else
        document = nlohmann::json::parse(readText(path));
    return staticBaseFromDocument(document, expectedVertices, expectedIndices, path);
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
        const float fps = static_cast<float>(options.fps);

        // ── Rig document ──────────────────────────────────────────────────
        // Keep the original document: it still carries the BIN chunk, the UVs
        // and the material identity needed to derive the static base mesh.
        nlohmann::json rigDocument;
        if (options.rig.extension() == ".glb" || options.rig.extension() == ".GLB")
            rigDocument = nlohmann::json::parse(
                glbJsonDocument(readBytes(options.rig), options.rig));
        else
            rigDocument = nlohmann::json::parse(readText(options.rig));

        // Phase 2 asset sidecar: when no --texture is supplied, pull the first
        // embedded image out of the same GLB. The plan keeps a mounted-root-
        // relative identity; C3D never needs to decode a GLB for the texture.
        std::string textureAsset = options.texture;
        std::filesystem::path texturePath;
        if (!textureAsset.empty()) texturePath = textureAsset;
        std::filesystem::path extractedTexture;
        if (textureAsset.empty() && rigDocument.contains("images") &&
            rigDocument["images"].is_array() && !rigDocument["images"].empty()) {
            const auto& image = rigDocument["images"].front();
            if (image.contains("bufferView")) {
                const auto viewIndex = image["bufferView"].get<std::size_t>();
                if (!rigDocument.contains("bufferViews") ||
                    viewIndex >= rigDocument["bufferViews"].size())
                    throw std::runtime_error("embedded texture names an invalid bufferView");
                const auto& view = rigDocument["bufferViews"][viewIndex];
                const std::size_t bufferIndex = view.value("buffer", std::size_t{0});
                if (!rigDocument.contains("buffers") ||
                    bufferIndex >= rigDocument["buffers"].size())
                    throw std::runtime_error("embedded texture names an invalid buffer");
                const auto& buffer = rigDocument["buffers"][bufferIndex];
                if (buffer.contains("uri")) {
                    const std::string uri = buffer["uri"].get<std::string>();
                    const auto comma = uri.find(',');
                    if (comma != std::string::npos && uri.rfind("data:", 0) == 0) {
                        const auto binary = decodeBase64(std::string_view(uri).substr(comma + 1));
                        const std::size_t offset = view.value("byteOffset", std::size_t{0});
                        const std::size_t length = view.at("byteLength").get<std::size_t>();
                        if (offset > binary.size() || length > binary.size() - offset)
                            throw std::runtime_error("embedded texture range exceeds the GLB buffer");
                        const std::string mime = image.value("mimeType", "image/png");
                        const std::string extension = mime == "image/jpeg" ? ".jpg" : ".png";
                        extractedTexture = options.out.parent_path() /
                                            (options.out.stem().string() + extension);
                        writeBytes(extractedTexture,
                                   std::span<const std::byte>(binary).subspan(offset, length));
                        textureAsset = extractedTexture.filename().string();
                        texturePath = extractedTexture;
                    }
                }
            } else if (image.contains("uri")) {
                textureAsset = image["uri"].get<std::string>();
            }
        }

        nlohmann::json motionDocument = rigDocument;
        if (options.stripRigMaterials) {
            motionDocument.erase("materials");
            motionDocument.erase("textures");
            motionDocument.erase("images");
            // Texture sampler state is not motion data; the reader rejects
            // the whole root block, so a textured GLB would fail even with
            // its materials stripped.
            motionDocument.erase("samplers");
            for (auto& mesh : motionDocument["meshes"])
                for (auto& primitive : mesh["primitives"]) primitive.erase("material");
        }
        // The motion reader is motion-only by contract: UV, colour and
        // tangent attributes belong to the static base mesh, so they are
        // dropped here instead of making every rigged GLB unreadable.
        // POSITION/NORMAL/JOINTS_n/WEIGHTS_n are the accepted set.
        if (motionDocument.contains("meshes") && motionDocument["meshes"].is_array()) {
            for (auto& mesh : motionDocument["meshes"]) {
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
        eraseExtras(motionDocument);
        decomposeNodeMatrices(motionDocument);
        const auto motionAsset = chrononmotion::loadGltfMotion(motionDocument.dump());

        // ── Clip programs ─────────────────────────────────────────────────
        std::vector<ClipEntry> clips;
        std::vector<std::string> requestedClips;
        if (options.sequence.empty()) requestedClips.push_back(options.clip);
        else requestedClips = options.sequence;
        for (const auto& requested : requestedClips) {
            const auto& clip = selectClip(motionAsset, requested);
            ClipEntry entry;
            entry.name = clip.name.empty()
                             ? (requested.empty() ? "clip_0" : requested)
                             : clip.name;
            entry.program = makeProgram(motionAsset, clip);
            entry.duration = clipDuration(clip);
            entry.frames = std::max(1, static_cast<int>(std::llround(entry.duration * fps)));
            entry.evaluator = std::make_unique<RigProgramEvaluator>(entry.program);
            clips.push_back(std::move(entry));
        }

        std::optional<chrononmotion::motion::FaceRigResult> faceResult;
        if (!options.faceMap.empty()) {
            auto faceMap = loadFaceMap(options.faceMap);
            auto faceCues = loadFaceCues(options.faceCues);
            if (faceCues.lastFrame >= options.frames)
                throw std::runtime_error(
                    "face performance last_frame must be lower than --frames");
            faceResult = chrononmotion::motion::applyFacePerformance(
                clips.front().program, faceMap, faceCues, fps);
            clips.front().evaluator =
                std::make_unique<RigProgramEvaluator>(clips.front().program);
        }

        // Probe the geometry identity and topology from the first requested
        // clip before choosing the static base.
        const RigProgramFrame probe = clips.front().evaluator->evaluate(0, fps);
        const chrononmotion::motion::RigFrameGeometry* deformed = nullptr;
        for (const auto& geometry : probe.geometry)
            if (!geometry.positions.empty()) { deformed = &geometry; break; }
        if (!deformed) throw std::runtime_error("the clip bakes no geometry for its first frame");
        if (deformed->positions.size() > rp::kInlineMeshMaxVertices)
            throw std::runtime_error("baked geometry exceeds the inline-mesh vertex ceiling");
        const chrononmotion::motion::RigGeometryBinding* binding = nullptr;
        for (const auto& geometry : clips.front().program.geometry)
            if (geometry.id == deformed->id) { binding = &geometry; break; }
        const std::vector<std::uint32_t> bindingIndices =
            binding ? binding->indices : std::vector<std::uint32_t>{};

        // ── Static base (UV/material identity) ─────────────────────────────
        // No --mesh: derive it from the same rigged GLB, removing skin, morph,
        // animation and material blocks in memory. --mesh accepts a normal
        // textured or rigged GLB too; the old hand-stripped copy is no longer
        // required.
        BaseMesh base = options.mesh.empty()
            ? staticBaseFromDocument(rigDocument, deformed->positions.size(),
                                     std::span<const std::uint32_t>(bindingIndices), options.rig)
            : loadStaticBase(options.mesh, deformed->positions.size(),
                             std::span<const std::uint32_t>(bindingIndices));
        std::vector<std::uint32_t> indices =
            bindingIndices.empty() ? base.indices : bindingIndices;
        if (base.vertices.empty() || indices.size() < 3 || indices.size() % 3 != 0)
            throw std::runtime_error("base mesh is not a non-empty triangle list");
        if (indices.size() > rp::kInlineMeshMaxIndices)
            throw std::runtime_error("base mesh exceeds the inline-mesh index ceiling");
        for (const auto index : indices)
            if (index >= base.vertices.size())
                throw std::runtime_error("base mesh index is outside its vertex range");

        // ── Bake ────────────────────────────────────────────────────────────
        int outputFrames = options.frames;
        BakedRigProgram bake;
        if (clips.size() == 1) {
            bake = clips.front().evaluator->bake(0, options.frames - 1, fps);
        } else {
            bake = bakeSequence(clips, options.crossfade, fps, outputFrames);
        }
        if (bake.frames.size() != static_cast<std::size_t>(outputFrames))
            throw std::runtime_error("bake returned an unexpected frame count");

        // ── RenderPlan ────────────────────────────────────────────────────
        rp::RenderPlan plan;
        plan.schema = rp::kRenderPlanSchemaV3;
        plan.job_id = options.out.stem().string();
        plan.canvas.width = options.width;
        plan.canvas.height = options.height;
        plan.canvas.fps = chronon3d::FrameRate{options.fps, 1};
        plan.canvas.duration = chronon3d::Frame{outputFrames};
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
        meshPlan.indices = std::move(indices);
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
        layer.duration_frames = chronon3d::Frame{outputFrames};
        layer.enable_3d = true;
        layer.position_dimensions = 3;
        layer.asset = textureAsset;
        if (!textureAsset.empty()) layer.input_color_space = "srgb";
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
        if (clips.size() == 1) {
            manifest["clip"] = clips.front().name;
        } else {
            manifest["sequence"] = nlohmann::json::array();
            for (const auto& clip : clips)
                manifest["sequence"].push_back({
                    {"clip", clip.name},
                    {"seconds", clip.duration},
                    {"frames", clip.frames},
                });
            manifest["crossfade_seconds"] = options.crossfade;
        }
        manifest["fps"] = options.fps;
        manifest["frames"] = outputFrames;
        manifest["canvas"] = {options.width, options.height};
        manifest["geometry_id"] = deformed->id;
        manifest["vertices"] = plan.layers.front().inline_mesh->vertices.size();
        manifest["indices"] = plan.layers.front().inline_mesh->indices.size();
        manifest["animation_samples"] = plan.layers.front().inline_mesh->animation.size();
        manifest["bake_signature"] = bake.signature;
        if (faceResult) {
            const auto mapBytes = readBytes(options.faceMap);
            const auto cueBytes = readBytes(options.faceCues);
            manifest["face"] = {
                {"map", {{{"path", options.faceMap.string()},
                          {"bytes", mapBytes.size()},
                          {"sha256", sha256Hex(mapBytes)}}}},
                {"cues", {{{"path", options.faceCues.string()},
                           {"bytes", cueBytes.size()},
                           {"sha256", sha256Hex(cueBytes)}}}},
                {"morph_tracks", faceResult->morphTracks},
                {"viseme_cues", faceResult->visemeCues},
                {"blink_cues", faceResult->blinkCues},
                {"gaze_cues", faceResult->gazeCues},
                {"signature", faceResult->signature},
            };
        }
        manifest["rig"] = {
            {"path", options.rig.string()},
            {"bytes", rigBytes.size()},
            {"sha256", sha256Hex(rigBytes)},
            {"fnv1a64", hexDigest(fnv1a64(rigBytes))},
            {"stripped_materials", options.stripRigMaterials},
            {"motion_attributes_only", true},
        };
        if (!options.mesh.empty()) {
            const auto meshBytes = readBytes(options.mesh);
            manifest["mesh"] = {
                {"path", options.mesh.string()},
                {"bytes", meshBytes.size()},
                {"sha256", sha256Hex(meshBytes)},
                {"fnv1a64", hexDigest(fnv1a64(meshBytes))},
            };
        }
        if (!textureAsset.empty()) {
            manifest["texture"] = {{"asset", textureAsset}};
            if (texturePath.empty() && std::filesystem::exists(textureAsset))
                texturePath = textureAsset;
            if (!texturePath.empty() && std::filesystem::exists(texturePath)) {
                const auto textureBytes = readBytes(texturePath);
                manifest["texture"]["bytes"] = textureBytes.size();
                manifest["texture"]["sha256"] = sha256Hex(textureBytes);
            }
        }
        manifest["plan"] = {
            {"path", options.out.string()},
            {"bytes", planJson.size()},
            {"sha256", sha256Hex(planJson)},
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
