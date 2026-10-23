#include "chrononmotion/assets/GltfReader.hpp"

#include "chrononmotion/core/BufferAttribute.hpp"
#include "chrononmotion/objects/Group.hpp"
#include "chrononmotion/motion/RigGraph.hpp"
#include "JsonDom.hpp"

#include <sstream>
#include <iomanip>
#include <locale>

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
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

namespace chrononmotion {
namespace {
using namespace chrononmotion::motion::detail;

constexpr std::size_t kMaxBufferBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxTotalBufferBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxTotalVertices = 8'000'000;
constexpr std::size_t kMaxTotalIndices = 24'000'000;
constexpr std::size_t kMaxNodes = 100'000;
constexpr std::size_t kMaxMeshes = 100'000;
constexpr std::size_t kMaxPrimitives = 100'000;
constexpr std::size_t kMaxVerticesPerAccessor = 4'000'000;

[[noreturn]] void invalid(const std::string& message) {
    throw std::invalid_argument("glTF: " + message);
}


std::vector<std::byte> decodeBase64(std::string_view encoded) {
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

std::size_t componentCount(std::string_view type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT4") return 16;
    invalid("unsupported accessor shape '" + std::string(type) + "'");
}
std::size_t componentSize(std::uint32_t type) {
    switch (type) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: invalid("unsupported accessor componentType " + std::to_string(type));
    }
}

std::uint32_t readU32(const std::byte* data) {
    return std::to_integer<std::uint32_t>(data[0]) |
           (std::to_integer<std::uint32_t>(data[1]) << 8) |
           (std::to_integer<std::uint32_t>(data[2]) << 16) |
           (std::to_integer<std::uint32_t>(data[3]) << 24);
}
std::uint16_t readU16(const std::byte* data) {
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

    GltfAsset load() {
        rejectUnknownKeys(root_, {"asset", "scene", "scenes", "nodes", "buffers", "bufferViews",
                                 "accessors", "meshes", "extensionsUsed", "extensionsRequired",
                                 "extensions", "extras"}, "root");
        const auto& asset = required(root_, "asset", "root");
        rejectUnknownKeys(asset, {"version", "minVersion", "generator", "copyright", "extras"}, "asset");
        if (required(asset, "version", "asset").string("asset.version") != "2.0") invalid("only glTF 2.0 is supported");
        if (find(asset, "minVersion")) invalid("asset.minVersion is outside the static glTF reader contract");
        if (find(root_, "extensions")) invalid("glTF extensions are outside the static reader contract");
        if (find(root_, "animations")) invalid("animated glTF assets are outside the static reader contract");
        if (find(root_, "skins")) invalid("skinned meshes are not supported");
        if (find(root_, "materials") || find(root_, "textures") || find(root_, "images") ||
            find(root_, "samplers") || find(root_, "cameras"))
            invalid("materials, textures, images, samplers, and cameras are not supported");
        if (find(root_, "extensionsRequired")) {
            const auto& extensions = required(root_, "extensionsRequired", "root").array("extensionsRequired");
            if (!extensions.empty()) invalid("required extensions are not supported");
        }
        if (find(root_, "extensionsUsed")) {
            const auto& extensions = required(root_, "extensionsUsed", "root").array("extensionsUsed");
            if (!extensions.empty()) invalid("glTF extensions are outside the static reader contract");
        }
        parseBuffers();
        parseViewsAndAccessors();
        parseMeshes();
        return parseScene();
    }

private:
    JsonValue root_;
    std::vector<std::vector<std::byte>> buffers_;
    std::vector<std::size_t> declaredBufferLengths_;
    std::vector<BufferView> views_;
    std::vector<Accessor> accessors_;
    std::vector<MeshData> meshes_;
    bool hasGlbBinChunk_{false};
    GltfReadPolicy policy_{};

    void parseBuffers() {
        const auto* entries = find(root_, "buffers");
        if (!entries) invalid("buffers array is required");
        const auto& list = entries->array("buffers");
        if (list.size() > 256 || buffers_.size() > list.size()) invalid("too many buffers");
        if (buffers_.size() < list.size()) buffers_.resize(list.size());
        declaredBufferLengths_.resize(list.size());
        std::size_t totalBytes = 0;
        std::size_t totalDeclaredBytes = 0;
        for (const auto& raw : list) {
            const auto declared = indexValue(required(raw, "byteLength", "buffer"), "buffer.byteLength");
            if (declared > policy_.maxTotalBufferBytes - std::min(totalDeclaredBytes, policy_.maxTotalBufferBytes))
                invalid("aggregate declared buffer bytes exceed the 512 MiB limit");
            totalDeclaredBytes += declared;
        }
        if (hasGlbBinChunk_ && list.empty()) invalid("GLB BIN chunk has no corresponding buffer declaration");
        for (std::size_t i = 0; i < list.size(); ++i) {
            rejectUnknownKeys(list[i], {"byteLength", "uri", "name", "extras"}, "buffer");
            const std::size_t expected = indexValue(required(list[i], "byteLength", "buffer"), "buffer.byteLength");
            if (expected > policy_.maxBufferBytes) invalid("buffer exceeds the configured per-buffer limit");
            declaredBufferLengths_[i] = expected;
            if (i == 0 && hasGlbBinChunk_ && find(list[i], "uri"))
                invalid("GLB buffer 0 must omit uri when a BIN chunk is present");
            if (const auto* uriValue = find(list[i], "uri")) {
                const std::string uri = uriValue->string("buffer.uri");
                constexpr std::string_view octetPrefix = "data:application/octet-stream;base64,";
                constexpr std::string_view gltfPrefix = "data:application/gltf-buffer;base64,";
                if (uri.starts_with(octetPrefix)) buffers_[i] = decodeBase64(std::string_view(uri).substr(octetPrefix.size()));
                else if (uri.starts_with(gltfPrefix)) buffers_[i] = decodeBase64(std::string_view(uri).substr(gltfPrefix.size()));
                else {
                    if (!externalResolver_ || !static_cast<bool>(*externalResolver_))
                        invalid("external buffer URI requires a caller-supplied resolver");
                    buffers_[i] = (*externalResolver_)(uri);
                }
            } else if (buffers_[i].empty() && expected != 0) invalid("buffer has no URI or GLB BIN chunk");
            if (buffers_[i].size() < expected) invalid("buffer data is shorter than declared byteLength");
            if (i == 0 && hasGlbBinChunk_) {
                if (buffers_[i].size() - expected > 3)
                    invalid("GLB BIN chunk may contain at most three padding bytes beyond buffer.byteLength");
            } else if (buffers_[i].size() != expected) {
                invalid("non-GLB buffer data length must equal declared byteLength");
            }
            if (buffers_[i].size() > policy_.maxBufferBytes) invalid("buffer exceeds the configured per-buffer limit");
            if (buffers_[i].size() > policy_.maxTotalBufferBytes - std::min(totalBytes, policy_.maxTotalBufferBytes))
                invalid("aggregate buffer bytes exceed the 512 MiB limit");
            totalBytes += buffers_[i].size();
        }
    }

    void parseViewsAndAccessors() {
        const auto* viewList = find(root_, "bufferViews");
        if (!viewList) invalid("bufferViews array is required");
        const auto& rawViews = viewList->array("bufferViews");
        if (rawViews.size() > 1'000'000) invalid("too many bufferViews");
        views_.reserve(rawViews.size());
        for (const auto& raw : rawViews) {
            rejectUnknownKeys(raw, {"buffer", "byteOffset", "byteLength", "byteStride", "target", "name", "extras"}, "bufferView");
            const std::size_t buffer = indexValue(required(raw, "buffer", "bufferView"), "bufferView.buffer");
            const std::size_t offset = find(raw, "byteOffset") ? indexValue(*find(raw, "byteOffset"), "bufferView.byteOffset") : 0;
            const std::size_t length = indexValue(required(raw, "byteLength", "bufferView"), "bufferView.byteLength");
            const std::size_t stride = find(raw, "byteStride") ? indexValue(*find(raw, "byteStride"), "bufferView.byteStride") : 0;
            if (buffer >= buffers_.size() || offset > declaredBufferLengths_[buffer] ||
                length > declaredBufferLengths_[buffer] - offset)
                invalid("bufferView range exceeds its declared buffer byteLength");
            if (stride != 0 && (stride < 4 || stride > 252 || stride % 4 != 0)) invalid("bufferView byteStride must be a multiple of 4 in [4,252]");
            std::optional<std::size_t> target;
            if (find(raw, "target")) {
                target = indexValue(*find(raw, "target"), "bufferView.target");
                if (*target != 34962 && *target != 34963) invalid("bufferView target must be ARRAY_BUFFER or ELEMENT_ARRAY_BUFFER");
                if (*target == 34963 && stride != 0)
                    invalid("ELEMENT_ARRAY_BUFFER bufferViews cannot specify byteStride");
            }
            views_.push_back({buffer, offset, length, stride, target});
        }
        const auto* accessorList = find(root_, "accessors");
        if (!accessorList) invalid("accessors array is required");
        const auto& rawAccessors = accessorList->array("accessors");
        if (rawAccessors.size() > 1'000'000) invalid("too many accessors");
        accessors_.reserve(rawAccessors.size());
        for (const auto& raw : rawAccessors) {
            rejectUnknownKeys(raw, {"bufferView", "byteOffset", "componentType", "normalized", "count",
                                    "type", "max", "min", "name", "sparse", "extras"}, "accessor");
            if (find(raw, "sparse")) invalid("sparse accessors are not supported");
            const std::size_t view = indexValue(required(raw, "bufferView", "accessor"), "accessor.bufferView");
            const std::size_t offset = find(raw, "byteOffset") ? indexValue(*find(raw, "byteOffset"), "accessor.byteOffset") : 0;
            const std::size_t count = indexValue(required(raw, "count", "accessor"), "accessor.count");
            const auto component = indexValue(required(raw, "componentType", "accessor"), "accessor.componentType");
            if (component > std::numeric_limits<std::uint32_t>::max()) invalid("accessor componentType is out of range");
            const std::string type = required(raw, "type", "accessor").string("accessor.type");
            const std::size_t components = componentCount(type);
            const std::size_t bytes = componentSize(static_cast<std::uint32_t>(component));
            const bool normalized = find(raw, "normalized") ? find(raw, "normalized")->boolean("accessor.normalized") : false;
            if (view >= views_.size() || count == 0 || count > kMaxVerticesPerAccessor) invalid("accessor view or count is invalid");
            std::array<std::vector<double>, 2> declaredBounds;
            std::size_t boundIndex = 0;
            for (const auto metadata : {std::string_view("min"), std::string_view("max")}) {
                if (const auto* bounds = find(raw, metadata)) {
                    const auto& values = bounds->array(std::string("accessor.") + std::string(metadata));
                    if (values.size() != components) invalid("accessor min/max must match the accessor type");
                    auto& stored = declaredBounds[boundIndex];
                    stored.reserve(values.size());
                    for (const auto& bound : values) {
                        const double value = bound.number("accessor min/max");
                        if (!std::isfinite(value)) invalid("accessor min/max values must be finite");
                        stored.push_back(value);
                    }
                }
                ++boundIndex;
            }
            if (find(raw, "min") && find(raw, "max")) {
                for (std::size_t c = 0; c < components; ++c)
                    if (declaredBounds[0][c] > declaredBounds[1][c])
                        invalid("accessor min component exceeds max component");
            }
            const auto& bufferView = views_[view];
            const std::size_t elementSize = components * bytes;
            const std::size_t stride = bufferView.stride == 0 ? elementSize : bufferView.stride;
            if (stride < elementSize || stride % bytes != 0 || offset % bytes != 0 || bufferView.offset % bytes != 0 ||
                offset > std::numeric_limits<std::size_t>::max() - bufferView.offset ||
                (bufferView.offset + offset) % bytes != 0)
                invalid("accessor alignment or stride is invalid");
            if (count - 1 > (std::numeric_limits<std::size_t>::max() - elementSize) / stride)
                invalid("accessor byte range overflows");
            const std::size_t requiredBytes = (count - 1) * stride + elementSize;
            if (offset > bufferView.length || requiredBytes > bufferView.length - offset) invalid("accessor range exceeds bufferView");
            if (type == "VEC3" && bytes == 4 && component == 5126 && !normalized) {
                for (const auto metadata : {std::string_view("min"), std::string_view("max")}) {
                    const auto* bounds = find(raw, metadata);
                    if (!bounds) continue;
                    const auto& declaredValues = bounds->array("accessor min/max");
                    for (std::size_t componentIndex = 0; componentIndex < components; ++componentIndex) {
                        float actualMin = std::numeric_limits<float>::infinity();
                        float actualMax = -std::numeric_limits<float>::infinity();
                        for (std::size_t elementIndex = 0; elementIndex < count; ++elementIndex) {
                            const auto* data = buffers_[bufferView.buffer].data() + bufferView.offset + offset +
                                elementIndex * stride + componentIndex * bytes;
                            const float decoded = std::bit_cast<float>(readU32(data));
                            if (!std::isfinite(decoded)) invalid("float accessor contains non-finite values");
                            actualMin = std::min(actualMin, decoded);
                            actualMax = std::max(actualMax, decoded);
                        }
                        const double declared = declaredValues[componentIndex].number("accessor min/max");
                        const float actual = metadata == "min" ? actualMin : actualMax;
                        if (std::abs(declared - static_cast<double>(actual)) >
                            1e-5 * std::max({1.0, std::abs(declared), std::abs(static_cast<double>(actual))}))
                            invalid("accessor min/max does not match decoded VEC3 data");
                    }
                }
            }
            accessors_.push_back({view, offset, count, components, bytes,
                                  static_cast<std::uint32_t>(component), type, normalized,
                                  find(raw, "min") != nullptr, find(raw, "max") != nullptr});
        }
    }

    const std::byte* accessorElement(const Accessor& accessor, std::size_t index) const {
        const auto& view = views_[accessor.view];
        const std::size_t elementSize = accessor.components * accessor.componentBytes;
        const std::size_t stride = view.stride == 0 ? elementSize : view.stride;
        return buffers_[view.buffer].data() + view.offset + accessor.offset + index * stride;
    }

    std::vector<float> floatAttribute(const Accessor& accessor, std::size_t expectedComponents,
                                      std::string_view semantic) const {
        const bool floatValues = accessor.componentType == 5126 && !accessor.normalized;
        const bool normalizedNormal = semantic == "NORMAL" && accessor.normalized &&
            (accessor.componentType == 5120 || accessor.componentType == 5122);
        const bool normalizedUv = semantic == "TEXCOORD_0" && accessor.normalized &&
            (accessor.componentType == 5121 || accessor.componentType == 5123);
        if (accessor.components != expectedComponents ||
            (semantic == "POSITION" && !floatValues) ||
            (semantic != "POSITION" && !floatValues && !normalizedNormal && !normalizedUv))
            invalid(std::string(semantic) + " has an unsupported accessor type/normalization combination");
        if (accessor.count > std::numeric_limits<std::size_t>::max() / expectedComponents) invalid("attribute allocation overflow");
        std::vector<float> result(accessor.count * expectedComponents);
        for (std::size_t i = 0; i < accessor.count; ++i)
            for (std::size_t c = 0; c < expectedComponents; ++c) {
                const std::byte* element = accessorElement(accessor, i) + c * accessor.componentBytes;
                float value{};
                if (accessor.componentType == 5126 && !accessor.normalized) {
                    value = std::bit_cast<float>(readU32(element));
                } else if (semantic == "NORMAL" && accessor.normalized && accessor.componentType == 5120) {
                    const auto raw = static_cast<std::int8_t>(std::to_integer<std::uint8_t>(*element));
                    value = std::max(-1.f, static_cast<float>(raw) / 127.f);
                } else if (semantic == "NORMAL" && accessor.normalized && accessor.componentType == 5122) {
                    const auto raw = static_cast<std::int16_t>(readU16(element));
                    value = std::max(-1.f, static_cast<float>(raw) / 32767.f);
                } else if (semantic == "TEXCOORD_0" && accessor.normalized && accessor.componentType == 5121) {
                    value = static_cast<float>(std::to_integer<std::uint8_t>(*element)) / 255.f;
                } else if (semantic == "TEXCOORD_0" && accessor.normalized && accessor.componentType == 5123) {
                    value = static_cast<float>(readU16(element)) / 65535.f;
                } else {
                    invalid(std::string(semantic) + " has an unsupported component type or normalization mode");
                }
                if (!std::isfinite(value)) invalid(std::string(semantic) + " contains non-finite values");
                result[i * expectedComponents + c] = value;
            }
        return result;
    }

    void parseMeshes() {
        const auto& rawMeshes = required(root_, "meshes", "root").array("meshes");
        if (rawMeshes.size() > std::min(kMaxMeshes, policy_.maxMeshes)) invalid("mesh count exceeds the configured limit");
        std::size_t primitiveCount = 0;
        std::size_t totalVertices = 0;
        std::size_t totalIndices = 0;
        meshes_.reserve(rawMeshes.size());
        for (std::size_t meshIndex = 0; meshIndex < rawMeshes.size(); ++meshIndex) {
            const auto& raw = rawMeshes[meshIndex];
            rejectUnknownKeys(raw, {"name", "primitives", "weights", "extras"}, "mesh");
            MeshData mesh;
            mesh.name = find(raw, "name") ? find(raw, "name")->string("mesh.name") : "mesh_" + std::to_string(meshIndex);
            if (find(raw, "weights")) invalid("mesh morph weights are not supported");
            const auto& primitives = required(raw, "primitives", "mesh").array("mesh.primitives");
            if (primitives.empty() || primitives.size() > std::min(kMaxPrimitives, policy_.maxPrimitives) - std::min(primitiveCount, std::min(kMaxPrimitives, policy_.maxPrimitives))) invalid("primitive count exceeds the configured limit");
            primitiveCount += primitives.size();
            for (std::size_t primitiveIndex = 0; primitiveIndex < primitives.size(); ++primitiveIndex) {
                const auto& primitive = primitives[primitiveIndex];
                rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "material", "targets", "extras"}, "primitive");
                if (find(primitive, "material")) invalid("primitive materials are outside the geometry-only reader contract");
                const auto mode = find(primitive, "mode") ? indexValue(*find(primitive, "mode"), "primitive.mode") : 4;
                if (mode != 4) invalid("only TRIANGLES primitive mode is supported");
                if (find(primitive, "targets")) invalid("morph targets are not supported");
                const auto& attributes = required(primitive, "attributes", "primitive").object("primitive.attributes");
                for (const auto& [semantic, unused] : attributes) {
                    (void)unused;
                    if (semantic != "POSITION" && semantic != "NORMAL" && semantic != "TEXCOORD_0")
                        invalid("unsupported vertex attribute '" + semantic + "'");
                }
                const auto positionIt = attributes.find("POSITION");
                if (positionIt == attributes.end()) invalid("primitive has no POSITION attribute");
                const std::size_t positionIndex = indexValue(positionIt->second, "POSITION accessor");
                if (positionIndex >= accessors_.size()) invalid("POSITION accessor index is out of range");
                const auto& positionAccessor = accessors_[positionIndex];
                if (!positionAccessor.hasMin || !positionAccessor.hasMax)
                    invalid("POSITION accessor requires min and max bounds");
                const auto& positionView = views_[positionAccessor.view];
                if (positionView.target && *positionView.target != 34962)
                    invalid("vertex attributes require ARRAY_BUFFER bufferViews");
                if (positionAccessor.count > std::min(kMaxTotalVertices, policy_.maxVertices) - std::min(totalVertices, std::min(kMaxTotalVertices, policy_.maxVertices)))
                    invalid("total decoded vertices exceed the configured limit");
                totalVertices += positionAccessor.count;
                auto geometry = BufferGeometry::create();
                auto positions = floatAttribute(positionAccessor, 3, "POSITION");
                geometry->setAttribute("position", FloatBufferAttribute::create(std::move(positions), 3));
                for (const auto semantic : {std::string_view("NORMAL"), std::string_view("TEXCOORD_0")}) {
                    const auto it = attributes.find(semantic);
                    if (it == attributes.end()) continue;
                    const std::size_t accessorIndex = indexValue(it->second, semantic);
                    if (accessorIndex >= accessors_.size()) invalid(std::string(semantic) + " accessor index is out of range");
                    const std::size_t components = semantic == "NORMAL" ? 3 : 2;
                    const auto& accessor = accessors_[accessorIndex];
                    if (views_[accessor.view].target && *views_[accessor.view].target != 34962)
                        invalid("vertex attributes require ARRAY_BUFFER bufferViews");
                    if (accessor.count != positionAccessor.count) invalid(std::string(semantic) + " count must match POSITION");
                    auto data = floatAttribute(accessor, components, semantic);
                    geometry->setAttribute(std::string(semantic == "NORMAL" ? "normal" : "uv"),
                                           FloatBufferAttribute::create(std::move(data), static_cast<int>(components)));
                }
                if (const auto* indexValueJson = find(primitive, "indices")) {
                    const std::size_t index = indexValue(*indexValueJson, "primitive.indices");
                    if (index >= accessors_.size()) invalid("index accessor is out of range");
                    const auto& accessor = accessors_[index];
                    const auto& indexView = views_[accessor.view];
                    if (indexView.target && *indexView.target != 34963)
                        invalid("primitive indices require ELEMENT_ARRAY_BUFFER bufferViews");
                    if (indexView.stride != 0)
                        invalid("primitive index accessors cannot use byteStride");
                    if (accessor.type != "SCALAR" || accessor.normalized ||
                        (accessor.componentType != 5121 && accessor.componentType != 5123 && accessor.componentType != 5125))
                        invalid("indices must be an unnormalized unsigned byte, short, or int SCALAR accessor");
                    if (accessor.count % 3 != 0) invalid("triangle index count must be divisible by three");
                    if (accessor.count > kMaxTotalIndices - totalIndices)
                        invalid("total decoded indices exceed the 24000000 limit");
                    totalIndices += accessor.count;
                    std::vector<unsigned int> indices;
                    indices.reserve(accessor.count);
                    for (std::size_t i = 0; i < accessor.count; ++i) {
                        const auto* element = accessorElement(accessor, i);
                        std::uint32_t value = 0;
                        if (accessor.componentType == 5121) value = std::to_integer<std::uint8_t>(*element);
                        else if (accessor.componentType == 5123) value = readU16(element);
                        else value = readU32(element);
                        if (value >= positionAccessor.count) invalid("index exceeds POSITION vertex count");
                        indices.push_back(value);
                    }
                    geometry->setIndex(std::move(indices));
                } else if (positionAccessor.count % 3 != 0) invalid("non-indexed triangle vertex count must be divisible by three");
                geometry->computeBoundingBox();
                geometry->computeBoundingSphere();
                mesh.primitives.push_back({std::move(geometry)});
            }
            meshes_.push_back(std::move(mesh));
        }
    }

    static Vector3 vector3(const JsonValue& value, std::string_view where) {
        const auto& values = value.array(where);
        if (values.size() != 3) invalid(std::string(where) + " must contain three values");
        return {floatValue(values[0], where), floatValue(values[1], where), floatValue(values[2], where)};
    }

    GltfAsset parseScene() {
        GltfAsset asset;
        asset.scene = Scene::create();
        asset.scene->name = "glTF";
        const auto& rawNodes = required(root_, "nodes", "root").array("nodes");
        if (rawNodes.size() > std::min(kMaxNodes, policy_.maxNodes)) invalid("node count exceeds the configured limit");
        for (const auto& node : rawNodes)
            rejectUnknownKeys(node, {"name", "mesh", "children", "translation", "rotation", "scale", "matrix", "camera", "skin", "weights", "extensions", "extras"}, "node");
        for (const auto& mesh : required(root_, "meshes", "root").array("meshes")) {
            rejectUnknownKeys(mesh, {"name", "primitives", "weights", "extras"}, "mesh");
            for (const auto& primitive : required(mesh, "primitives", "mesh").array("mesh.primitives"))
                rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "material", "targets", "extras"}, "primitive");
        }
        const auto& scenes = required(root_, "scenes", "root").array("scenes");
        for (const auto& scene : scenes) {
            rejectUnknownKeys(scene, {"name", "nodes", "extensions", "extras"}, "scene");
            if (find(scene, "extensions")) invalid("scene extensions are not supported");
        }
        if (scenes.empty()) invalid("scenes must contain at least one scene");
        const std::size_t sceneIndex = find(root_, "scene") ? indexValue(*find(root_, "scene"), "root.scene") : 0;
        const auto& sceneDefinition = at(scenes, sceneIndex, "scenes");
        const auto& roots = required(sceneDefinition, "nodes", "scene").array("scene.nodes");
        std::vector<unsigned char> state(rawNodes.size(), 0);
        std::vector<bool> hasParent(rawNodes.size(), false);
        for (const auto& node : rawNodes) {
            if (const auto* children = find(node, "children")) {
                for (const auto& childValue : children->array("node.children")) {
                    const std::size_t child = indexValue(childValue, "node child");
                    if (child >= rawNodes.size()) invalid("node child index is out of range");
                    if (hasParent[child]) invalid("node has multiple parents");
                    hasParent[child] = true;
                }
            }
        }
        std::size_t nodesVisited = 0;
        std::size_t sceneMeshInstances = 0;
        constexpr std::size_t kMaxSceneMeshInstances = 100'000;
        constexpr std::size_t kMaxNodeDepth = 256;
        const auto visit = [&](const auto& self, std::size_t nodeIndex,
                               const std::shared_ptr<Object3D>& parent, std::size_t depth) -> void {
            if (depth > kMaxNodeDepth) invalid("selected scene node depth exceeds 256");
            if (nodeIndex >= rawNodes.size()) invalid("scene root node index is out of range");
            if (state[nodeIndex] == 1) invalid("node hierarchy contains a cycle");
            if (state[nodeIndex] == 2) invalid("scene contains a node more than once");
            if (++nodesVisited > kMaxNodes) invalid("scene node visit limit exceeded");
            state[nodeIndex] = 1;
            const auto& raw = rawNodes[nodeIndex];
            if (find(raw, "camera") || find(raw, "skin") || find(raw, "weights"))
                invalid("camera, skin, and morph-weight nodes are unsupported");
            if (find(raw, "matrix") && find(raw, "matrix")->array("node.matrix").size() != 16)
                invalid("node.matrix must contain 16 values");
            if (find(raw, "translation") && find(raw, "translation")->array("node.translation").size() != 3)
                invalid("node.translation must contain three values");
            if (find(raw, "rotation") && find(raw, "rotation")->array("node.rotation").size() != 4)
                invalid("node.rotation must contain four values");
            if (find(raw, "scale") && find(raw, "scale")->array("node.scale").size() != 3)
                invalid("node.scale must contain three values");
            if (find(raw, "extensions")) invalid("node extensions are not supported");
            auto group = Group::create();
            group->name = find(raw, "name") ? find(raw, "name")->string("node.name") : "node_" + std::to_string(nodeIndex);
            const bool hasMatrix = find(raw, "matrix") != nullptr;
            const bool hasTrs = find(raw, "translation") || find(raw, "rotation") || find(raw, "scale");
            if (hasMatrix && hasTrs) invalid("node may specify matrix or TRS, not both");
            if (hasMatrix) {
                const auto& matrix = find(raw, "matrix")->array("node.matrix");
                if (matrix.size() != 16) invalid("node.matrix must contain 16 values");
                std::array<float, 16> elements{};
                for (std::size_t i = 0; i < 16; ++i) elements[i] = floatValue(matrix[i], "node.matrix");
                if (std::abs(elements[3]) > 1e-6f || std::abs(elements[7]) > 1e-6f ||
                    std::abs(elements[11]) > 1e-6f || std::abs(elements[15] - 1.f) > 1e-6f)
                    invalid("node.matrix must be affine");
                group->matrix->fromArray(elements);
                group->matrixAutoUpdate = false;
            } else {
                if (const auto* translation = find(raw, "translation")) group->position.copy(vector3(*translation, "node.translation"));
                if (const auto* scale = find(raw, "scale")) group->scale.copy(vector3(*scale, "node.scale"));
                if (const auto* rotation = find(raw, "rotation")) {
                    const auto& values = rotation->array("node.rotation");
                    if (values.size() != 4) invalid("node.rotation must contain four values");
                    Quaternion quaternion(floatValue(values[0], "node.rotation"), floatValue(values[1], "node.rotation"),
                                          floatValue(values[2], "node.rotation"), floatValue(values[3], "node.rotation"));
                    const float length = quaternion.length();
                    if (!std::isfinite(length) || std::abs(length - 1.f) > 1e-3f) invalid("node.rotation quaternion must be normalized");
                    quaternion.normalize();
                    group->quaternion.copy(quaternion);
                    group->rotation.setFromQuaternion(quaternion);
                }
            }
            parent->add(group);
            if (const auto* meshValue = find(raw, "mesh")) {
                const std::size_t meshIndex = indexValue(*meshValue, "node.mesh");
                if (meshIndex >= meshes_.size()) invalid("node.mesh index is out of range");
                const auto& mesh = meshes_[meshIndex];
                if (mesh.primitives.size() > kMaxSceneMeshInstances - sceneMeshInstances)
                    invalid("selected scene mesh instance count exceeds 100000");
                sceneMeshInstances += mesh.primitives.size();
                for (std::size_t primitive = 0; primitive < mesh.primitives.size(); ++primitive) {
                    auto object = Mesh::create(mesh.primitives[primitive].geometry);
                    object->name = mesh.name + "_primitive_" + std::to_string(primitive);
                    group->add(object);
                    asset.meshes.push_back(std::move(object));
                }
            }
            if (const auto* children = find(raw, "children"))
                for (const auto& childValue : children->array("node.children"))
                    self(self, indexValue(childValue, "node child"), group, depth + 1);
            state[nodeIndex] = 2;
        };
        for (const auto& root : roots) {
            const std::size_t nodeIndex = indexValue(root, "scene root node");
            if (nodeIndex >= hasParent.size()) invalid("scene root node index is out of range");
            if (hasParent[nodeIndex]) invalid("scene root node is also a child in the node graph");
            visit(visit, nodeIndex, asset.scene, 0);
        }
        if (asset.meshes.empty()) invalid("selected scene contains no mesh primitives");
        asset.scene->updateMatrixWorld(true);
        return asset;
    }

public:
    GltfMotionAsset loadMotion();
    void setExternalResolver(const GltfExternalBufferResolver* resolver) { externalResolver_ = resolver; }
private:
    const GltfExternalBufferResolver* externalResolver_{nullptr};
};

GltfMotionAsset Document::loadMotion() {
    using namespace motion;
    rejectUnknownKeys(root_, {"asset", "scene", "scenes", "nodes", "buffers", "bufferViews", "accessors", "meshes", "skins", "animations"}, "motion root");
    const auto& asset = required(root_, "asset", "root");
    rejectUnknownKeys(asset, {"version", "generator", "copyright"}, "asset");
    if (required(asset, "version", "asset").string("version") != "2.0") invalid("only glTF 2.0 motion supported");
    parseBuffers(); parseViewsAndAccessors();
    GltfMotionAsset result;
    const auto& nodes = required(root_, "nodes", "motion root").array("nodes");
    if (nodes.size() > 10000) invalid("motion node budget exceeded");
    std::vector<RigNodeId> parents(nodes.size(), 0);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        rejectUnknownKeys(nodes[i], {"name", "children", "translation", "rotation", "scale", "mesh", "skin"}, "motion node");
        if (const auto* children = find(nodes[i], "children")) for (const auto& raw : children->array("children")) {
            const auto child = indexValue(raw, "child");
            if (child >= nodes.size() || parents[child]) invalid("invalid or multiply parented motion node");
            parents[child] = static_cast<RigNodeId>(i + 1);
        }
    }
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        RigNode node; node.id = static_cast<RigNodeId>(i + 1); node.parent = parents[i];
        if (const auto* p = find(nodes[i], "translation")) node.rest.position = vector3(*p, "translation");
        if (const auto* s = find(nodes[i], "scale")) node.rest.scale = vector3(*s, "scale");
        if (const auto* r = find(nodes[i], "rotation")) {
            const auto& q = r->array("rotation"); if (q.size() != 4) invalid("rotation requires four components");
            node.rest.orientation.set(floatValue(q[0], "rotation"), floatValue(q[1], "rotation"), floatValue(q[2], "rotation"), floatValue(q[3], "rotation"));
        }
        node.bind = node.rest; result.graph.nodes.push_back(node);
    }
    const auto rig = result.graph.compile();
    if (const auto* scenes = find(root_, "scenes")) {
        const auto& list = scenes->array("scenes");
        const auto selected = find(root_, "scene") ? indexValue(*find(root_, "scene"), "scene") : 0;
        if (selected >= list.size()) invalid("motion selected scene out of range");
        for (const auto& scene : list) {
            rejectUnknownKeys(scene, {"name", "nodes"}, "motion scene");
            std::set<std::size_t> roots;
            for (const auto& value : required(scene, "nodes", "scene").array("scene roots")) {
                const auto root = indexValue(value, "scene root");
                if (root >= nodes.size() || parents[root] || !roots.insert(root).second) invalid("invalid motion scene root");
            }
        }
    } else if (find(root_, "scene")) invalid("scene selection requires scenes");
    const auto bind = rig.bindPose();
    const auto nodeIndex = [&](const JsonValue& v) {
        const auto index = indexValue(v, "node index"); if (index >= nodes.size()) invalid("motion node reference out of range"); return index;
    };
    const auto floats = [&](const JsonValue& v, std::size_t components) {
        const auto i = indexValue(v, "accessor"); if (i >= accessors_.size()) invalid("motion accessor out of range");
        return floatAttribute(accessors_[i], components, "motion");
    };
    if (const auto* skins = find(root_, "skins")) {
        if (skins->array("skins").size() > 256) invalid("skin budget exceeded");
        for (const auto& raw : skins->array("skins")) {
            rejectUnknownKeys(raw, {"name", "joints", "skeleton", "inverseBindMatrices"}, "skin");
            GltfMotionSkin skin;
            const auto& joints = required(raw, "joints", "skin").array("joints");
            if (joints.empty() || joints.size() > 10000) invalid("invalid skin joint count");
            for (const auto& joint : joints) {
                const auto id = static_cast<RigNodeId>(nodeIndex(joint) + 1);
                if (std::find(skin.joints.begin(), skin.joints.end(), id) != skin.joints.end()) invalid("duplicate skin joint");
                skin.joints.push_back(id);
            }
            if (const auto* skeleton = find(raw, "skeleton")) skin.skeleton = static_cast<RigNodeId>(nodeIndex(*skeleton) + 1);
            skin.inverseBindMatrices.resize(joints.size());
            if (const auto* inverse = find(raw, "inverseBindMatrices")) {
                const auto accessor = indexValue(*inverse, "inverse bind accessor");
                if (accessor >= accessors_.size() || accessors_[accessor].type != "MAT4") invalid("inverse bind accessor must be MAT4");
                const auto values = floats(*inverse, 16);
                if (values.size() != joints.size() * 16) invalid("inverse bind count mismatch");
                for (std::size_t i = 0; i < joints.size(); ++i) {
                    skin.inverseBindMatrices[i].fromArray(values, i * 16);
                    const auto& m = skin.inverseBindMatrices[i].elements;
                    if (m[3] != 0.f || m[7] != 0.f || m[11] != 0.f || m[15] != 1.f || skin.inverseBindMatrices[i].determinant() == 0.f)
                        invalid("inverse bind must be nonsingular affine");
                }
            }
            result.skins.push_back(std::move(skin));
        }
    }
    const auto* rawMeshes = find(root_, "meshes");
    if (rawMeshes) {
        if (rawMeshes->array("meshes").size() > 10000) invalid("motion mesh budget exceeded");
        for (const auto& mesh : rawMeshes->array("meshes")) {
            rejectUnknownKeys(mesh, {"name", "primitives", "weights"}, "motion mesh");
            const auto& primitives = required(mesh, "primitives", "mesh").array("primitives");
            if (primitives.empty() || primitives.size() > 10000) invalid("invalid motion primitive count");
            for (const auto& primitive : primitives)
                rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "targets"}, "motion primitive");
        }
    }
    std::size_t vertexBudget = 0;
    std::size_t morphVertexBudget = 0;
    std::vector<std::vector<std::size_t>> nodeGeometry(nodes.size());
    for (std::size_t node = 0; node < nodes.size(); ++node) {
        const auto* meshRef = find(nodes[node], "mesh");
        if (!meshRef) { if (find(nodes[node], "skin")) invalid("skin requires mesh"); continue; }
        if (!rawMeshes) invalid("missing meshes");
        const auto& mesh = at(rawMeshes->array("meshes"), indexValue(*meshRef, "mesh"), "mesh");
        rejectUnknownKeys(mesh, {"name", "primitives", "weights"}, "motion mesh");
        const auto* skinRef = find(nodes[node], "skin");
        const GltfMotionSkin* skin = nullptr;
        if (skinRef) { const auto i = indexValue(*skinRef, "skin"); if (i >= result.skins.size()) invalid("skin out of range"); skin = &result.skins[i]; }
        const auto& primitives = required(mesh, "primitives", "mesh").array("primitives");
        for (const auto& primitive : primitives) {
            rejectUnknownKeys(primitive, {"attributes", "indices", "mode", "targets"}, "motion primitive");
            if (find(primitive, "mode") && indexValue(*find(primitive, "mode"), "mode") != 4) invalid("motion supports triangles only");
            const auto& attrs = required(primitive, "attributes", "primitive");
            rejectUnknownKeys(attrs, {"POSITION", "JOINTS_0", "WEIGHTS_0"}, "motion attributes");
            const auto data = floats(required(attrs, "POSITION", "attributes"), 3);
            const auto count = data.size() / 3;
            if (count > 1000000 - vertexBudget || result.geometry.size() >= 10000) invalid("motion geometry budget exceeded");
            vertexBudget += count;
            RigGeometryBinding geometry; geometry.id = static_cast<std::uint32_t>(result.geometry.size() + 1);
            geometry.rigidNode = static_cast<RigNodeId>(node + 1);
            for (std::size_t i = 0; i < count; ++i) geometry.source.push_back({data[i * 3], data[i * 3 + 1], data[i * 3 + 2]});
            if (const auto* indices = find(primitive, "indices")) {
                const auto index = indexValue(*indices, "indices"); if (index >= accessors_.size()) invalid("index accessor out of range");
                const auto& a = accessors_[index];
                if (a.components != 1 || a.normalized || (a.componentType != 5121 && a.componentType != 5123 && a.componentType != 5125) || a.count % 3)
                    invalid("invalid triangle index accessor");
                for (std::size_t i = 0; i < a.count; ++i) {
                    const auto* e = accessorElement(a, i);
                    const auto v = a.componentType == 5121 ? std::to_integer<unsigned>(*e) : a.componentType == 5123 ? readU16(e) : readU32(e);
                    if (v >= count) invalid("triangle index out of range");
                    geometry.indices.push_back(v);
                }
            } else if (count % 3) invalid("nonindexed triangles require multiple of three vertices");
            if (const auto* targets = find(primitive, "targets")) {
                if (targets->array("targets").size() > 256) invalid("morph budget exceeded");
                for (const auto& target : targets->array("targets")) {
                    rejectUnknownKeys(target, {"POSITION"}, "morph target");
                    if (count > 1000000 - morphVertexBudget) invalid("morph vertex budget exceeded");
                    morphVertexBudget += count;
                    const auto delta = floats(required(target, "POSITION", "target"), 3);
                    if (delta.size() != data.size()) invalid("morph topology mismatch");
                    RigMorphTarget morph; morph.positions = geometry.source;
                    for (std::size_t i = 0; i < count; ++i) morph.positions[i].add({delta[i * 3], delta[i * 3 + 1], delta[i * 3 + 2]});
                    geometry.morphs.push_back(std::move(morph));
                }
            }
            if (const auto* weights = find(mesh, "weights")) {
                if (weights->array("weights").size() != geometry.morphs.size()) invalid("morph weight count mismatch");
                for (std::size_t i = 0; i < geometry.morphs.size(); ++i) geometry.morphs[i].weight = floatValue(weights->array("weights")[i], "weight");
            }
            if (skin) {
                const auto jointAccessor = indexValue(required(attrs, "JOINTS_0", "attributes"), "JOINTS_0");
                if (jointAccessor >= accessors_.size()) invalid("joint accessor out of range");
                const auto& j = accessors_[jointAccessor];
                if (j.components != 4 || j.normalized || (j.componentType != 5121 && j.componentType != 5123) || j.count != count)
                    invalid("invalid joint accessor");
                const auto weightIndex = indexValue(required(attrs, "WEIGHTS_0", "attributes"), "WEIGHTS_0");
                if (weightIndex >= accessors_.size()) invalid("weight accessor out of range");
                const auto& w = accessors_[weightIndex];
                std::vector<float> weights;
                if (w.componentType == 5126 && !w.normalized) weights = floats(required(attrs, "WEIGHTS_0", "attributes"), 4);
                else {
                    if (w.components != 4 || !w.normalized || (w.componentType != 5121 && w.componentType != 5123))
                        invalid("weights require float or normalized unsigned VEC4");
                    for (std::size_t vertex = 0; vertex < w.count; ++vertex) for (unsigned influence = 0; influence < 4; ++influence) {
                        const auto* e = accessorElement(w, vertex) + influence * w.componentBytes;
                        weights.push_back(w.componentType == 5121 ? static_cast<float>(std::to_integer<unsigned>(*e)) / 255.f :
                                          static_cast<float>(readU16(e)) / 65535.f);
                    }
                }
                if (weights.size() != count * 4) invalid("skin weight topology mismatch");
                geometry.rigidNode = 0; geometry.skin.resize(count);
                for (std::size_t i = 0; i < count; ++i) for (unsigned influence = 0; influence < 4; ++influence) {
                    const auto* e = accessorElement(j, i) + influence * j.componentBytes;
                    const auto joint = j.componentType == 5121 ? std::to_integer<unsigned>(*e) : readU16(e);
                    if (joint >= skin->joints.size()) invalid("vertex joint outside skin");
                    const float weight = weights[i * 4 + influence];
                    geometry.skin[i].weights[influence] = weight;
                    if (weight != 0.f) geometry.skin[i].joints[influence] = skin->joints[joint];
                }
                for (std::size_t joint = 0; joint < skin->joints.size(); ++joint)
                    geometry.inverseBinds.push_back({skin->joints[joint], skin->inverseBindMatrices[joint]});
                (void) skinRigGeometry(rig, bind, geometry.source, geometry.skin, geometry.inverseBinds);
            } else if (find(attrs, "JOINTS_0") || find(attrs, "WEIGHTS_0")) invalid("joint attributes require node skin");
            (void) mixRigMorphs(geometry.source, geometry.morphs);
            nodeGeometry[node].push_back(result.geometry.size());
            result.geometry.push_back(std::move(geometry));
        }
    }
    if (const auto* animations = find(root_, "animations")) {
        if (animations->array("animations").size() > 256) invalid("clip budget exceeded");
        std::size_t keyBudget = 0;
        for (const auto& animation : animations->array("animations")) {
            rejectUnknownKeys(animation, {"name", "samplers", "channels"}, "animation");
            GltfMotionClip clip; if (find(animation, "name")) clip.name = find(animation, "name")->string("name");
            const auto& samplers = required(animation, "samplers", "animation").array("samplers");
            const auto& channels = required(animation, "channels", "animation").array("channels");
            std::set<std::pair<std::size_t, std::string>> unique;
            for (const auto& channel : channels) {
                rejectUnknownKeys(channel, {"sampler", "target"}, "channel");
                const auto& target = required(channel, "target", "channel"); rejectUnknownKeys(target, {"node", "path"}, "target");
                const auto node = nodeIndex(required(target, "node", "target"));
                const auto path = required(target, "path", "target").string("path");
                if (path != "translation" && path != "rotation" && path != "scale" && path != "weights") invalid("unsupported motion animation path");
                if (!unique.emplace(node, path).second) invalid("duplicate animation channel");
                const auto& sampler = at(samplers, indexValue(required(channel, "sampler", "channel"), "sampler"), "sampler");
                rejectUnknownKeys(sampler, {"input", "output", "interpolation"}, "sampler");
                const auto interpolation = find(sampler, "interpolation") ? find(sampler, "interpolation")->string("interpolation") : "LINEAR";
                if (interpolation != "LINEAR" && interpolation != "STEP") invalid("only LINEAR and STEP motion interpolation supported");
                const auto times = floats(required(sampler, "input", "sampler"), 1);
                if (path == "weights") {
                    if (nodeGeometry[node].empty()) invalid("weight channel requires mesh node");
                    const auto morphCount = result.geometry[nodeGeometry[node].front()].morphs.size();
                    if (!morphCount) invalid("weight channel requires morph targets");
                    const auto values = floats(required(sampler, "output", "sampler"), 1);
                    if (values.size() != times.size() * morphCount) invalid("morph animation topology mismatch");
                    keyBudget += values.size(); if (keyBudget > 1000000) invalid("morph animation key budget exceeded");
                    for (const auto geometryIndex : nodeGeometry[node]) {
                        const auto& geometry = result.geometry[geometryIndex];
                        if (geometry.morphs.size() != morphCount) invalid("mesh primitives disagree on morph count");
                        GltfMotionClip::MorphChannel morph; morph.geometryId = geometry.id; morph.weights.resize(morphCount);
                        float previous = -1.f;
                        for (std::size_t key = 0; key < times.size(); ++key) {
                            if (times[key] < 0.f || times[key] <= previous) invalid("morph times must increase");
                            previous = times[key]; clip.duration = std::max(clip.duration, times[key]);
                            for (std::size_t targetIndex = 0; targetIndex < morphCount; ++targetIndex) {
                                const auto value = values[key * morphCount + targetIndex];
                                if (value < 0.f || value > 1.f) invalid("morph animated weight outside 0..1");
                                if (interpolation == "STEP") morph.weights[targetIndex].addDiscrete(times[key], value);
                                else morph.weights[targetIndex].add(times[key], value);
                            }
                        }
                        clip.morphs.push_back(std::move(morph));
                    }
                    continue;
                }
                const auto values = floats(required(sampler, "output", "sampler"), path == "rotation" ? 4 : 3);
                keyBudget += times.size(); if (keyBudget > 1000000 || values.size() != times.size() * (path == "rotation" ? 4 : 3)) invalid("clip key budget/topology invalid");
                auto it = std::find_if(clip.animation.begin(), clip.animation.end(), [node](const RigAnimatedNode& a) { return a.id == node + 1; });
                if (it == clip.animation.end()) { clip.animation.push_back({static_cast<RigNodeId>(node + 1), {}}); it = clip.animation.end() - 1; }
                float previous = -1.f;
                for (std::size_t key = 0; key < times.size(); ++key) {
                    const float time = times[key]; if (time < 0.f || time <= previous) invalid("animation time must be increasing nonnegative"); previous = time;
                    if (path == "rotation") {
                        Quaternion q(values[key * 4], values[key * 4 + 1], values[key * 4 + 2], values[key * 4 + 3]);
                        if (std::abs(q.lengthSq() - 1.f) > 1e-4f) invalid("animation quaternion must be normalized");
                        if (interpolation == "STEP") it->tracks.orientation.addDiscrete(time, q); else it->tracks.orientation.add(time, q);
                    } else {
                        Vector3 value{values[key * 3], values[key * 3 + 1], values[key * 3 + 2]};
                        auto& track = path == "translation" ? it->tracks.position : it->tracks.scale;
                        if (interpolation == "STEP") track.addDiscrete(time, value); else track.add(time, value);
                    }
                    clip.duration = std::max(clip.duration, time);
                }
            }
            std::sort(clip.animation.begin(), clip.animation.end(), [](const RigAnimatedNode& a, const RigAnimatedNode& b) { return a.id < b.id; });
            RigProgram program; program.graph = result.graph; program.animation = clip.animation; program.geometry = result.geometry;
            for (const auto& morph : clip.morphs) program.geometry[morph.geometryId - 1].morphTracks = morph.weights;
            (void) RigProgramEvaluator(program);
            result.clips.push_back(std::move(clip));
        }
    }
    return result;
}

GltfAsset loadDocument(std::string_view json, std::vector<std::vector<std::byte>> buffers,
                      const GltfExternalBufferResolver& resolver, bool hasGlbBinChunk = false,
                      const GltfReadPolicy& policy = {}) {
    JsonValue root = JsonParser(json, std::min(kMaxJsonBytes, policy.maxJsonBytes)).parse();
    Document document(std::move(root), std::move(buffers), hasGlbBinChunk, policy);
    document.setExternalResolver(&resolver);
    return document.load();
}

std::uint32_t littleU32(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) invalid("truncated GLB header or chunk header");
    return readU32(bytes.data() + offset);
}

} // namespace

GltfMotionAsset loadGltfMotion(std::string_view json, const GltfExternalBufferResolver& resolver,
                               const GltfReadPolicy& policy) {
    if (!policy.enabled || !policy.allowMotionGltfImport) invalid("motion import is disabled by authoring policy");
    if (policy.audit) policy.audit(true, "gltf-json-motion");
    Document document(JsonParser(json, std::min(kMaxJsonBytes, policy.maxJsonBytes)).parse(), {}, false, policy); document.setExternalResolver(&resolver);
    return document.loadMotion();
}

GltfAsset loadGltf(std::string_view json, const GltfExternalBufferResolver& resolveExternalBuffer,
                   const GltfReadPolicy& policy) {
    if (!policy.enabled || !policy.allowStaticGltfImport) invalid("static import is disabled by authoring policy");
    if (policy.audit) policy.audit(false, "gltf-json-static");
    return loadDocument(json, {}, resolveExternalBuffer, false, policy);
}

GltfAsset loadGlb(std::span<const std::byte> bytes, const GltfExternalBufferResolver& resolveExternalBuffer,
                   const GltfReadPolicy& policy) {
    if (!policy.enabled || !policy.allowStaticGltfImport) invalid("static import is disabled by authoring policy");
    if (policy.audit) policy.audit(false, "glb-static");
    if (bytes.size() < 12 || bytes.size() > policy.maxTotalBufferBytes + std::min(kMaxJsonBytes, policy.maxJsonBytes) + 1024) invalid("GLB size is invalid");
    if (littleU32(bytes, 0) != 0x46546c67u) invalid("invalid GLB magic");
    if (littleU32(bytes, 4) != 2) invalid("only GLB version 2 is supported");
    if (littleU32(bytes, 8) != bytes.size()) invalid("GLB declared length does not match input size");
    std::size_t offset = 12;
    std::string_view json;
    std::vector<std::byte> binary;
    bool gotJson = false;
    bool gotBin = false;
    while (offset < bytes.size()) {
        if (bytes.size() - offset < 8) invalid("truncated GLB chunk header");
        const std::size_t length = littleU32(bytes, offset);
        const std::uint32_t type = littleU32(bytes, offset + 4);
        offset += 8;
        if (length % 4 != 0 || length > bytes.size() - offset) invalid("GLB chunk length or alignment is invalid");
        if (type == 0x4e4f534au) {
            if (gotJson || gotBin || offset != 20) invalid("GLB must contain exactly one first JSON chunk");
            json = std::string_view(reinterpret_cast<const char*>(bytes.data() + offset), length);
            while (!json.empty() && json.back() == ' ') json.remove_suffix(1);
            if (json.find('\0') != std::string_view::npos)
                invalid("GLB JSON chunk padding must use spaces, not NUL bytes");
            gotJson = true;
        } else if (type == 0x004e4942u) {
            if (!gotJson || gotBin) invalid("GLB BIN chunk must follow JSON and occur at most once");
            binary.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                          bytes.begin() + static_cast<std::ptrdiff_t>(offset + length));
            gotBin = true;
        } else invalid("unknown GLB chunk type");
        offset += length;
    }
    if (!gotJson) invalid("GLB has no JSON chunk");
    std::vector<std::vector<std::byte>> buffers;
    if (gotBin) buffers.push_back(std::move(binary));
    return loadDocument(json, std::move(buffers), resolveExternalBuffer, gotBin, policy);
}


} // namespace chrononmotion
