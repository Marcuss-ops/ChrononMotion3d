#pragma once

#include "chrononmotion/assets/GltfReader.hpp"
#include "chrononmotion/core/BufferAttribute.hpp"
#include "chrononmotion/objects/Group.hpp"
#include "chrononmotion/motion/RigGraph.hpp"
#include "../motion/JsonDom.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <cstddef>

namespace chrononmotion {
namespace gltf_detail {

using chrononmotion::motion::detail::at;
using chrononmotion::motion::detail::find;
using chrononmotion::motion::detail::floatValue;
using chrononmotion::motion::detail::indexValue;
using chrononmotion::motion::detail::kMaxJsonBytes;
using chrononmotion::motion::detail::rejectUnknownKeys;
using chrononmotion::motion::detail::required;
using chrononmotion::motion::detail::JsonValue;

constexpr std::size_t kMaxBufferBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxTotalBufferBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxTotalVertices = 8'000'000;
constexpr std::size_t kMaxTotalIndices = 24'000'000;
constexpr std::size_t kMaxNodes = 100'000;
constexpr std::size_t kMaxMeshes = 100'000;
constexpr std::size_t kMaxPrimitives = 100'000;
constexpr std::size_t kMaxVerticesPerAccessor = 4'000'000;

[[noreturn]] inline void invalid(const std::string& message) {
    throw std::invalid_argument("glTF: " + message);
}

inline std::vector<std::byte> decodeBase64(std::string_view encoded) {
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (encoded.size() % 4 != 0 || encoded.size() > (kMaxBufferBytes * 4 / 3 + 8)) invalid("invalid or oversized base64 buffer URI");
    std::vector<std::byte> output;
    output.reserve(encoded.size() / 4 * 3);
    for (std::size_t offset = 0; offset < encoded.size(); offset += 4) {
        std::uint32_t value = 0;
        unsigned padding = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const char c = encoded[offset + i];
            value <<= 6;
            if (c == '=') {
                ++padding;
                if (i < 2 || offset + 4 != encoded.size()) invalid("invalid base64 padding");
            } else {
                if (padding != 0) invalid("base64 data follows padding");
                const auto position = alphabet.find(c);
                if (position == std::string_view::npos) invalid("invalid base64 character");
                value |= static_cast<std::uint32_t>(position);
            }
        }
        if (padding > 2) invalid("invalid base64 padding");
        if ((padding == 1 && (alphabet.find(encoded[offset + 2]) & 0x3u) != 0) ||
            (padding == 2 && (alphabet.find(encoded[offset + 1]) & 0xfu) != 0))
            invalid("non-zero unused bits in base64 padding");
        output.push_back(static_cast<std::byte>((value >> 16) & 0xff));
        if (padding < 2) output.push_back(static_cast<std::byte>((value >> 8) & 0xff));
        if (padding == 0) output.push_back(static_cast<std::byte>(value & 0xff));
    }
    if (output.size() > kMaxBufferBytes) invalid("decoded buffer exceeds the 256 MiB limit");
    return output;
}

struct BufferView {
    std::size_t buffer, offset, length, stride;
    std::optional<std::size_t> target;
};
struct Accessor {
    std::size_t view, offset, count, components, componentBytes;
    std::uint32_t componentType;
    std::string type;
    bool normalized;
    bool hasMin;
    bool hasMax;
};
struct Primitive { std::shared_ptr<BufferGeometry> geometry; };
struct MeshData { std::string name; std::vector<Primitive> primitives; };

inline std::size_t componentCount(std::string_view type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    invalid("unsupported accessor shape '" + std::string(type) + "'");
}
inline std::size_t componentSize(std::uint32_t type) {
    switch (type) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: invalid("unsupported accessor componentType " + std::to_string(type));
    }
}

inline std::uint32_t readU32(const std::byte* data) {
    return std::to_integer<std::uint32_t>(data[0]) |
           (std::to_integer<std::uint32_t>(data[1]) << 8) |
           (std::to_integer<std::uint32_t>(data[2]) << 16) |
           (std::to_integer<std::uint32_t>(data[3]) << 24);
}
inline std::uint16_t readU16(const std::byte* data) {
    return static_cast<std::uint16_t>(std::to_integer<unsigned>(data[0]) |
                                      (std::to_integer<unsigned>(data[1]) << 8));
}

class Document {
public:
    Document(JsonValue root, std::vector<std::vector<std::byte>> buffers, bool hasGlbBinChunk,
             GltfReadPolicy policy = {})
        : root_(std::move(root)), buffers_(std::move(buffers)), hasGlbBinChunk_(hasGlbBinChunk),
          policy_(std::move(policy)) {
        if (!policy_.enabled) invalid("import is disabled by authoring policy");
    }

    GltfAsset load();
    GltfMotionAsset loadMotion();
    void setExternalResolver(const GltfExternalBufferResolver* resolver) { externalResolver_ = resolver; }

private:
    JsonValue root_;
    std::vector<std::vector<std::byte>> buffers_;
    std::vector<std::size_t> declaredBufferLengths_;
    std::vector<BufferView> views_;
    std::vector<Accessor> accessors_;
    std::vector<MeshData> meshes_;
    bool hasGlbBinChunk_{false};
    GltfReadPolicy policy_{};
    const GltfExternalBufferResolver* externalResolver_{nullptr};

    void parseBuffers();
    void parseViewsAndAccessors();
    const std::byte* accessorElement(const Accessor& accessor, std::size_t index) const {
        const auto& view = views_[accessor.view];
        const std::size_t elementSize = accessor.components * accessor.componentBytes;
        const std::size_t stride = view.stride == 0 ? elementSize : view.stride;
        return buffers_[view.buffer].data() + view.offset + accessor.offset + index * stride;
    }
    std::vector<float> floatAttribute(const Accessor& accessor, std::size_t expectedComponents,
                                      std::string_view semantic) const;
    void parseMeshes();
    void parseMotionAnimations(const std::vector<JsonValue>& nodes,
                               const std::vector<std::vector<std::size_t>>& nodeGeometry,
                               GltfMotionAsset& result);
    static Vector3 vector3(const JsonValue& value, std::string_view where) {
        const auto& values = value.array(where);
        if (values.size() != 3) invalid(std::string(where) + " must contain three values");
        return {floatValue(values[0], where), floatValue(values[1], where), floatValue(values[2], where)};
    }
    GltfAsset parseScene();
};

} // namespace gltf_detail
} // namespace chrononmotion
