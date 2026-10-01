#include "chrononmotion/assets/GltfReader.hpp"

#include "chrononmotion/core/BufferAttribute.hpp"
#include "chrononmotion/objects/Group.hpp"
#include "chrononmotion/motion/RigGraph.hpp"

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

constexpr std::size_t kMaxJsonBytes = 16u * 1024u * 1024u;
constexpr std::size_t kMaxBufferBytes = 256u * 1024u * 1024u;
constexpr std::size_t kMaxTotalBufferBytes = 512u * 1024u * 1024u;
constexpr std::size_t kMaxTotalVertices = 8'000'000;
constexpr std::size_t kMaxTotalIndices = 24'000'000;
constexpr std::size_t kMaxNodes = 100'000;
constexpr std::size_t kMaxMeshes = 100'000;
constexpr std::size_t kMaxPrimitives = 100'000;
constexpr std::size_t kMaxVerticesPerAccessor = 4'000'000;
constexpr std::size_t kMaxJsonDepth = 128;

[[noreturn]] void invalid(const std::string& message) {
    throw std::invalid_argument("glTF: " + message);
}

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue, std::less<>>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value{nullptr};

    const Object& object(std::string_view where) const {
        if (const auto* result = std::get_if<Object>(&value)) return *result;
        invalid(std::string(where) + " must be an object");
    }
    const Array& array(std::string_view where) const {
        if (const auto* result = std::get_if<Array>(&value)) return *result;
        invalid(std::string(where) + " must be an array");
    }
    std::string string(std::string_view where) const {
        if (const auto* result = std::get_if<std::string>(&value)) return *result;
        invalid(std::string(where) + " must be a string");
    }
    double number(std::string_view where) const {
        if (const auto* result = std::get_if<double>(&value)) return *result;
        invalid(std::string(where) + " must be a number");
    }
    bool boolean(std::string_view where) const {
        if (const auto* result = std::get_if<bool>(&value)) return *result;
        invalid(std::string(where) + " must be a boolean");
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string_view input) : input_(input) {
        if (input.size() > kMaxJsonBytes) invalid("JSON exceeds the 16 MiB input limit");
    }

    JsonValue parse() {
        skipWhitespace();
        JsonValue result = parseValue(0);
        skipWhitespace();
        if (position_ != input_.size()) fail("trailing data after JSON value");
        return result;
    }

private:
    std::string_view input_;
    std::size_t position_{0};

    [[noreturn]] void fail(const char* message) const {
        invalid(std::string(message) + " at JSON byte " + std::to_string(position_));
    }
    void skipWhitespace() {
        while (position_ < input_.size() && (input_[position_] == ' ' || input_[position_] == '\t' ||
               input_[position_] == '\r' || input_[position_] == '\n')) ++position_;
    }
    bool consume(char expected) {
        if (position_ < input_.size() && input_[position_] == expected) { ++position_; return true; }
        return false;
    }
    void expect(char expected) { if (!consume(expected)) fail("unexpected token"); }

    static void appendUtf8(std::string& output, std::uint32_t codepoint) {
        if (codepoint <= 0x7f) output.push_back(static_cast<char>(codepoint));
        else if (codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }
    std::uint32_t hex4() {
        if (input_.size() - position_ < 4) fail("truncated unicode escape");
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = input_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return value;
    }
    std::string parseString() {
        expect('"');
        std::string result;
        while (position_ < input_.size()) {
            const unsigned char c = static_cast<unsigned char>(input_[position_++]);
            if (c == '"') return result;
            if (c < 0x20) fail("control character in string");
            if (c != '\\') { result.push_back(static_cast<char>(c)); continue; }
            if (position_ >= input_.size()) fail("truncated string escape");
            switch (input_[position_++]) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = hex4();
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                        if (input_.size() - position_ < 2 || input_[position_] != '\\' || input_[position_ + 1] != 'u')
                            fail("missing low surrogate");
                        position_ += 2;
                        const std::uint32_t low = hex4();
                        if (low < 0xdc00 || low > 0xdfff) fail("invalid low surrogate");
                        codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                    } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) fail("unpaired low surrogate");
                    appendUtf8(result, codepoint);
                    break;
                }
                default: fail("invalid string escape");
            }
        }
        fail("unterminated string");
    }
    JsonValue parseNumber() {
        const std::size_t start = position_;
        consume('-');
        if (consume('0')) {
            if (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9')
                fail("leading zero in number");
        } else {
            if (position_ >= input_.size() || input_[position_] < '1' || input_[position_] > '9') fail("invalid number");
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
        }
        if (consume('.')) {
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (digits == position_) fail("fraction has no digits");
        }
        if (position_ < input_.size() && (input_[position_] == 'e' || input_[position_] == 'E')) {
            ++position_;
            if (position_ < input_.size() && (input_[position_] == '+' || input_[position_] == '-')) ++position_;
            const std::size_t digits = position_;
            while (position_ < input_.size() && input_[position_] >= '0' && input_[position_] <= '9') ++position_;
            if (digits == position_) fail("exponent has no digits");
        }
        double result{};
        const auto [end, error] = std::from_chars(input_.data() + start, input_.data() + position_, result,
                                                   std::chars_format::general);
        if (error != std::errc{} || end != input_.data() + position_ || !std::isfinite(result))
            fail("number is outside finite range");
        JsonValue output; output.value = result; return output;
    }
    JsonValue parseValue(std::size_t depth) {
        if (depth > kMaxJsonDepth) fail("maximum JSON nesting depth exceeded");
        skipWhitespace();
        if (position_ >= input_.size()) fail("unexpected end of input");
        if (input_[position_] == '"') { JsonValue v; v.value = parseString(); return v; }
        if (input_[position_] == '{') {
            ++position_; skipWhitespace(); JsonValue::Object object;
            if (consume('}')) { JsonValue v; v.value = std::move(object); return v; }
            while (true) {
                skipWhitespace(); if (position_ >= input_.size() || input_[position_] != '"') fail("object key must be a string");
                std::string key = parseString(); skipWhitespace(); expect(':');
                auto [it, inserted] = object.emplace(std::move(key), parseValue(depth + 1));
                (void)it;
                if (!inserted) fail("duplicate object key");
                skipWhitespace(); if (consume('}')) break; expect(',');
            }
            JsonValue v; v.value = std::move(object); return v;
        }
        if (input_[position_] == '[') {
            ++position_; skipWhitespace(); JsonValue::Array array;
            if (consume(']')) { JsonValue v; v.value = std::move(array); return v; }
            while (true) { array.push_back(parseValue(depth + 1)); skipWhitespace(); if (consume(']')) break; expect(','); }
            JsonValue v; v.value = std::move(array); return v;
        }
        if (input_[position_] == '-' || (input_[position_] >= '0' && input_[position_] <= '9')) return parseNumber();
        const auto literal = [&](std::string_view text, JsonValue value) {
            if (input_.substr(position_, text.size()) != text) fail("invalid literal");
            position_ += text.size(); return value;
        };
        if (input_[position_] == 't') { JsonValue v; v.value = true; return literal("true", std::move(v)); }
        if (input_[position_] == 'f') { JsonValue v; v.value = false; return literal("false", std::move(v)); }
        if (input_[position_] == 'n') return literal("null", JsonValue{});
        fail("invalid JSON value");
    }
};

const JsonValue* find(const JsonValue& value, std::string_view key) {
    const auto& object = value.object("JSON value");
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &it->second;
}
const JsonValue& required(const JsonValue& value, std::string_view key, std::string_view where) {
    const auto* result = find(value, key);
    if (!result) invalid(std::string(where) + " is missing '" + std::string(key) + "'");
    return *result;
}
void rejectUnknownKeys(const JsonValue& value, std::initializer_list<std::string_view> allowed,
                       std::string_view where) {
    for (const auto& [key, unused] : value.object(where)) {
        (void)unused;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
            invalid(std::string(where) + " contains unsupported field '" + key + "'");
    }
}
std::size_t indexValue(const JsonValue& value, std::string_view where) {
    const double number = value.number(where);        if (number < 0 || std::floor(number) != number || number >= static_cast<double>(std::numeric_limits<std::size_t>::max()))
        invalid(std::string(where) + " must be a non-negative integer");
    return static_cast<std::size_t>(number);
}
float floatValue(const JsonValue& value, std::string_view where) {
    const double number = value.number(where);
    const float result = static_cast<float>(number);
    if (!std::isfinite(result)) invalid(std::string(where) + " exceeds finite float range");
    return result;
}
const JsonValue& at(const JsonValue::Array& array, std::size_t index, std::string_view where) {
    if (index >= array.size()) invalid(std::string(where) + " index is out of range");
    return array[index];
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
    Document(JsonValue root, std::vector<std::vector<std::byte>> buffers, bool hasGlbBinChunk)
        : root_(std::move(root)), buffers_(std::move(buffers)), hasGlbBinChunk_(hasGlbBinChunk) {}

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
            if (declared > kMaxTotalBufferBytes - totalDeclaredBytes)
                invalid("aggregate declared buffer bytes exceed the 512 MiB limit");
            totalDeclaredBytes += declared;
        }
        if (hasGlbBinChunk_ && list.empty()) invalid("GLB BIN chunk has no corresponding buffer declaration");
        for (std::size_t i = 0; i < list.size(); ++i) {
            rejectUnknownKeys(list[i], {"byteLength", "uri", "name", "extras"}, "buffer");
            const std::size_t expected = indexValue(required(list[i], "byteLength", "buffer"), "buffer.byteLength");
            if (expected > kMaxBufferBytes) invalid("buffer exceeds the 256 MiB limit");
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
            if (buffers_[i].size() > kMaxBufferBytes) invalid("buffer exceeds the 256 MiB limit");
            if (buffers_[i].size() > kMaxTotalBufferBytes - totalBytes)
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
        if (rawMeshes.size() > kMaxMeshes) invalid("mesh count exceeds the 100000 limit");
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
            if (primitives.empty() || primitives.size() > kMaxPrimitives - primitiveCount) invalid("primitive count exceeds the 100000 limit");
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
                if (positionAccessor.count > kMaxTotalVertices - totalVertices)
                    invalid("total decoded vertices exceed the 8000000 limit");
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
        if (rawNodes.size() > kMaxNodes) invalid("node count exceeds the 100000 limit");
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
                      const GltfExternalBufferResolver& resolver, bool hasGlbBinChunk = false) {
    JsonValue root = JsonParser(json).parse();
    Document document(std::move(root), std::move(buffers), hasGlbBinChunk);
    document.setExternalResolver(&resolver);
    return document.load();
}

std::uint32_t littleU32(std::span<const std::byte> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) invalid("truncated GLB header or chunk header");
    return readU32(bytes.data() + offset);
}

} // namespace

GltfMotionAsset loadGltfMotion(std::string_view json, const GltfExternalBufferResolver& resolver) {
    Document document(JsonParser(json).parse(), {}, false); document.setExternalResolver(&resolver);
    return document.loadMotion();
}

GltfAsset loadGltf(std::string_view json, const GltfExternalBufferResolver& resolveExternalBuffer) {
    return loadDocument(json, {}, resolveExternalBuffer);
}

GltfAsset loadGlb(std::span<const std::byte> bytes, const GltfExternalBufferResolver& resolveExternalBuffer) {
    if (bytes.size() < 12 || bytes.size() > kMaxTotalBufferBytes + kMaxJsonBytes + 1024) invalid("GLB size is invalid");
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
    return loadDocument(json, std::move(buffers), resolveExternalBuffer, gotBin);
}

namespace motion {
namespace {
    Vector3 rigVector(const JsonValue& value) {
        const auto& a = value.array("rig vector");
        if (a.size() != 3) invalid("rig vector requires three components");
        return {floatValue(a[0], "rig vector"), floatValue(a[1], "rig vector"), floatValue(a[2], "rig vector")};
    }
    RigNodeId rigId(const JsonValue& value) {
        const auto id = indexValue(value, "rig ID");
        if (id > std::numeric_limits<RigNodeId>::max()) invalid("rig ID overflow");
        return static_cast<RigNodeId>(id);
    }
    Transform rigTransform(const JsonValue& value) {
        rejectUnknownKeys(value, {"position", "anchor", "scale", "rotation", "orientation", "opacity"}, "rig transform");
        Transform t;
        t.position = rigVector(required(value, "position", "transform"));
        t.anchor = rigVector(required(value, "anchor", "transform"));
        t.scale = rigVector(required(value, "scale", "transform"));
        t.rotation = rigVector(required(value, "rotation", "transform"));
        const auto& q = required(value, "orientation", "transform").array("orientation");
        if (q.size() != 4) invalid("orientation requires four components");
        t.orientation.set(floatValue(q[0], "orientation"), floatValue(q[1], "orientation"),
                          floatValue(q[2], "orientation"), floatValue(q[3], "orientation"));
        t.opacity = floatValue(required(value, "opacity", "transform"), "opacity");
        return t;
    }
    void writeRigVector(std::ostream& out, const Vector3& v) { out << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
    void writeRigTransform(std::ostream& out, const Transform& t) {
        out << "{\"position\":"; writeRigVector(out, t.position);
        out << ",\"anchor\":"; writeRigVector(out, t.anchor);
        out << ",\"scale\":"; writeRigVector(out, t.scale);
        out << ",\"rotation\":"; writeRigVector(out, t.rotation);
        out << ",\"orientation\":[" << t.orientation.x << ',' << t.orientation.y << ',' << t.orientation.z << ',' << t.orientation.w
            << "],\"opacity\":" << t.opacity << '}';
    }
}
    void writeRigNodes(std::ostream& out, const std::vector<RigNode>& nodes) {
    bool first = true;
    for (const auto& n : nodes) {
        if (!first) out << ',';
        first = false;
        out << "{\"id\":" << n.id << ",\"parent\":" << n.parent << ",\"kind\":" << static_cast<unsigned>(n.kind)
            << ",\"layerId\":" << n.layerId << ",\"length\":" << n.length << ",\"primaryAxis\":";
        writeRigVector(out, n.primaryAxis);
        out << ",\"bind\":"; writeRigTransform(out, n.bind);
        out << ",\"rest\":"; writeRigTransform(out, n.rest);
        out << ",\"dependencies\":[";
        auto deps = n.dependencies; std::sort(deps.begin(), deps.end());
        for (std::size_t i = 0; i < deps.size(); ++i) { if (i) out << ','; out << deps[i]; }
        out << "],\"constraints\":[";
        for (std::size_t i = 0; i < n.constraints.size(); ++i) {
            if (i) out << ',';
            const auto& c = n.constraints[i];
            if (c.path || c.amountTrack) invalid("rig graph serialization does not accept path/track handles");
            out << "{\"kind\":" << static_cast<unsigned>(c.kind) << ",\"targetId\":" << c.targetId
                << ",\"amount\":" << c.amount << ",\"distance\":" << c.distance << ",\"progress\":" << c.progress
                << ",\"restLength\":" << c.restLength << ",\"damping\":" << c.damping << ",\"roll\":" << c.roll
                << ",\"orientToPath\":" << (c.orientToPath ? "true" : "false") << ",\"axis\":";
            writeRigVector(out, c.axis); out << ",\"minimum\":"; writeRigVector(out, c.minimum);
            out << ",\"maximum\":"; writeRigVector(out, c.maximum);
            out << ",\"surfaceNormal\":"; writeRigVector(out, c.surfaceNormal);
            out << ",\"surfaceOffset\":"; writeRigVector(out, c.surfaceOffset); out << '}';
        }
        out << "]}";
    }
}
std::string serializeRigGraph(const RigGraph& graph) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"schema\":\"chrononmotion.rig.v1\",\"nodes\":[";
    writeRigNodes(out, graph.compile().nodes());
    out << "]}";
    return out.str();
}
RigGraph deserializeRigGraph(std::string_view json) {
    const auto root = JsonParser(json).parse();
    rejectUnknownKeys(root, {"schema", "nodes"}, "rig root");
    if (required(root, "schema", "rig root").string("schema") != "chrononmotion.rig.v1") invalid("unknown rig schema");
    const auto& nodes = required(root, "nodes", "rig root").array("nodes");
    if (nodes.size() > 10000) invalid("rig node budget exceeded");
    RigGraph graph;
    std::size_t edges = 0;
    for (const auto& raw : nodes) {
        rejectUnknownKeys(raw, {"id", "parent", "kind", "layerId", "length", "primaryAxis", "bind", "rest", "dependencies", "constraints"}, "rig node");
        RigNode n;
        n.id = rigId(required(raw, "id", "node")); n.parent = rigId(required(raw, "parent", "node"));
        const auto kind = indexValue(required(raw, "kind", "node"), "kind");
        if (kind > static_cast<unsigned>(RigNodeKind::Pin)) invalid("unknown rig node kind");
        n.kind = static_cast<RigNodeKind>(kind); n.layerId = rigId(required(raw, "layerId", "node"));
        n.length = floatValue(required(raw, "length", "node"), "length");
        n.primaryAxis = rigVector(required(raw, "primaryAxis", "node"));
        n.bind = rigTransform(required(raw, "bind", "node")); n.rest = rigTransform(required(raw, "rest", "node"));
        const auto& deps = required(raw, "dependencies", "node").array("dependencies");
        const auto& constraints = required(raw, "constraints", "node").array("constraints");
        edges += deps.size() + constraints.size();
        if (edges > 160000 || constraints.size() > 256) invalid("rig dependency budget exceeded");
        for (const auto& dep : deps) n.dependencies.push_back(rigId(dep));
        for (const auto& value : constraints) {
            rejectUnknownKeys(value, {"kind", "targetId", "amount", "distance", "progress", "restLength", "damping", "roll", "orientToPath",
                                      "axis", "minimum", "maximum", "surfaceNormal", "surfaceOffset"}, "constraint");
            ConstraintSpec c;
            const auto constraintKind = indexValue(required(value, "kind", "constraint"), "kind");
            if (constraintKind > static_cast<unsigned>(ConstraintKind::LookAtWithRoll)) invalid("unknown constraint kind");
            c.kind = static_cast<ConstraintKind>(constraintKind);
            c.targetId = rigId(required(value, "targetId", "constraint"));
            c.amount = floatValue(required(value, "amount", "constraint"), "amount");
            c.distance = floatValue(required(value, "distance", "constraint"), "distance");
            c.progress = floatValue(required(value, "progress", "constraint"), "progress");
            c.restLength = floatValue(required(value, "restLength", "constraint"), "restLength");
            c.damping = floatValue(required(value, "damping", "constraint"), "damping");
            c.roll = floatValue(required(value, "roll", "constraint"), "roll");
            c.orientToPath = required(value, "orientToPath", "constraint").boolean("orientToPath");
            c.axis = rigVector(required(value, "axis", "constraint")); c.minimum = rigVector(required(value, "minimum", "constraint"));
            c.maximum = rigVector(required(value, "maximum", "constraint")); c.surfaceNormal = rigVector(required(value, "surfaceNormal", "constraint"));
            c.surfaceOffset = rigVector(required(value, "surfaceOffset", "constraint")); n.constraints.push_back(c);
        }
        graph.nodes.push_back(std::move(n));
    }
    (void) graph.compile();
    return graph;
}

RigGraph readRigGraphObject(const JsonValue& root) {
    const auto& nodes = required(root, "nodes", "rig root").array("nodes");
    if (nodes.size() > 10000) invalid("rig node budget exceeded");
    RigGraph graph;
    std::size_t edges = 0;
    for (const auto& raw : nodes) {
        rejectUnknownKeys(raw, {"id", "parent", "kind", "layerId", "length", "primaryAxis", "bind", "rest", "dependencies", "constraints"}, "rig node");
        RigNode n;
        n.id = rigId(required(raw, "id", "node")); n.parent = rigId(required(raw, "parent", "node"));
        const auto kind = indexValue(required(raw, "kind", "node"), "kind");
        if (kind > static_cast<unsigned>(RigNodeKind::Pin)) invalid("unknown rig node kind");
        n.kind = static_cast<RigNodeKind>(kind); n.layerId = rigId(required(raw, "layerId", "node"));
        n.length = floatValue(required(raw, "length", "node"), "length");
        n.primaryAxis = rigVector(required(raw, "primaryAxis", "node"));
        n.bind = rigTransform(required(raw, "bind", "node")); n.rest = rigTransform(required(raw, "rest", "node"));
        const auto& deps = required(raw, "dependencies", "node").array("dependencies");
        const auto& constraints = required(raw, "constraints", "node").array("constraints");
        edges += deps.size() + constraints.size();
        if (edges > 160000 || constraints.size() > 256) invalid("rig dependency budget exceeded");
        for (const auto& dep : deps) n.dependencies.push_back(rigId(dep));
        for (const auto& value : constraints) {
            rejectUnknownKeys(value, {"kind", "targetId", "amount", "distance", "progress", "restLength", "damping", "roll", "orientToPath",
                                      "axis", "minimum", "maximum", "surfaceNormal", "surfaceOffset"}, "constraint");
            ConstraintSpec c;
            const auto constraintKind = indexValue(required(value, "kind", "constraint"), "kind");
            if (constraintKind > static_cast<unsigned>(ConstraintKind::LookAtWithRoll)) invalid("unknown constraint kind");
            c.kind = static_cast<ConstraintKind>(constraintKind);
            c.targetId = rigId(required(value, "targetId", "constraint"));
            c.amount = floatValue(required(value, "amount", "constraint"), "amount");
            c.distance = floatValue(required(value, "distance", "constraint"), "distance");
            c.progress = floatValue(required(value, "progress", "constraint"), "progress");
            c.restLength = floatValue(required(value, "restLength", "constraint"), "restLength");
            c.damping = floatValue(required(value, "damping", "constraint"), "damping");
            c.roll = floatValue(required(value, "roll", "constraint"), "roll");
            c.orientToPath = required(value, "orientToPath", "constraint").boolean("orientToPath");
            c.axis = rigVector(required(value, "axis", "constraint")); c.minimum = rigVector(required(value, "minimum", "constraint"));
            c.maximum = rigVector(required(value, "maximum", "constraint")); c.surfaceNormal = rigVector(required(value, "surfaceNormal", "constraint"));
            c.surfaceOffset = rigVector(required(value, "surfaceOffset", "constraint")); n.constraints.push_back(c);
        }
        graph.nodes.push_back(std::move(n));
    }
    (void) graph.compile();
    return graph;
}

void writeRigProgramTransformArray(std::ostream& out, const std::vector<Transform>& transforms) {
    for (std::size_t i = 0; i < transforms.size(); ++i) { if (i) out << ','; writeRigTransform(out, transforms[i]); }
}
void writeRigTrack(std::ostream& out, const Track<float>& track) {
    out << '[';
    for (std::size_t i = 0; i < track.keys().size(); ++i)
        out << (i ? "," : "") << '[' << track.keys()[i].time << ',' << track.keys()[i].value << ']';
    out << ']';
}
Track<float> readRigTrack(const JsonValue& value) {
    Track<float> track;
    float previous = -1.f;
    for (const auto& key : value.array("track")) {
        const auto& pair = key.array("track key");
        if (pair.size() != 2) invalid("track key requires time and value");
        const float time = floatValue(pair[0], "track time");
        if (time < 0.f || time <= previous) invalid("track times must increase");
        previous = time;
        track.add(time, floatValue(pair[1], "track value"));
    }
    return track;
}
void writeRigDeformer(std::ostream& out, const RigDeformer& d) {
    out << "{\"kind\":" << static_cast<unsigned>(d.kind) << ",\"amount\":" << d.amount << ",\"strength\":" << d.strength
        << ",\"wavelength\":" << d.wavelength << ",\"phase\":" << d.phase << ",\"origin\":";
    writeRigVector(out, d.origin);
    out << ",\"seed\":" << d.seed << ",\"lattice\":{\"columns\":" << d.lattice.columns << ",\"rows\":" << d.lattice.rows
        << ",\"minimum\":"; writeRigVector(out, d.lattice.minimum);
    out << ",\"maximum\":"; writeRigVector(out, d.lattice.maximum);
    out << ",\"offsets\":[";
    for (std::size_t i = 0; i < d.lattice.offsets.size(); ++i) { if (i) out << ','; writeRigVector(out, d.lattice.offsets[i]); }
    out << "]},\"pins\":[";
    for (std::size_t i = 0; i < d.pins.size(); ++i) {
        if (i) out << ',';
        const auto& p = d.pins[i];
        out << "{\"kind\":" << static_cast<unsigned>(p.kind) << ",\"rest\":"; writeRigVector(out, p.rest);
        out << ",\"target\":"; writeRigVector(out, p.target);
        out << ",\"rotation\":[" << p.rotation.x << ',' << p.rotation.y << ',' << p.rotation.z << ',' << p.rotation.w
            << "],\"scale\":"; writeRigVector(out, p.scale);
        out << ",\"radius\":" << p.radius << ",\"weight\":" << p.weight << '}';
    }
    out << "]}";
}
RigDeformer readRigDeformer(const JsonValue& value) {
    rejectUnknownKeys(value, {"kind", "amount", "strength", "wavelength", "phase", "origin", "seed", "lattice", "pins"}, "deformer");
    RigDeformer d;
    const auto kind = indexValue(required(value, "kind", "deformer"), "kind");
    if (kind > static_cast<unsigned>(RigDeformerKind::SquashStretch)) invalid("unknown deformer kind");
    d.kind = static_cast<RigDeformerKind>(kind);
    d.amount = floatValue(required(value, "amount", "deformer"), "amount");
    d.strength = floatValue(required(value, "strength", "deformer"), "strength");
    d.wavelength = floatValue(required(value, "wavelength", "deformer"), "wavelength");
    d.phase = floatValue(required(value, "phase", "deformer"), "phase");
    d.origin = rigVector(required(value, "origin", "deformer"));
    d.seed = indexValue(required(value, "seed", "deformer"), "seed");
    const auto& lattice = required(value, "lattice", "deformer");
    rejectUnknownKeys(lattice, {"columns", "rows", "minimum", "maximum", "offsets"}, "lattice");
    d.lattice.columns = indexValue(required(lattice, "columns", "lattice"), "columns");
    d.lattice.rows = indexValue(required(lattice, "rows", "lattice"), "rows");
    d.lattice.minimum = rigVector(required(lattice, "minimum", "lattice"));
    d.lattice.maximum = rigVector(required(lattice, "maximum", "lattice"));
    for (const auto& offset : required(lattice, "offsets", "lattice").array("offsets")) d.lattice.offsets.push_back(rigVector(offset));
    for (const auto& raw : required(value, "pins", "deformer").array("pins")) {
        rejectUnknownKeys(raw, {"kind", "rest", "target", "rotation", "scale", "radius", "weight"}, "pin");
        RigPin p;
        const auto pinKind = indexValue(required(raw, "kind", "pin"), "kind");
        if (pinKind > static_cast<unsigned>(RigPinKind::Stiffness)) invalid("unknown pin kind");
        p.kind = static_cast<RigPinKind>(pinKind);
        p.rest = rigVector(required(raw, "rest", "pin")); p.target = rigVector(required(raw, "target", "pin"));
        const auto& rotation = required(raw, "rotation", "pin").array("rotation");
        if (rotation.size() != 4) invalid("pin rotation requires four components");
        p.rotation.set(floatValue(rotation[0], "rotation"), floatValue(rotation[1], "rotation"),
                       floatValue(rotation[2], "rotation"), floatValue(rotation[3], "rotation"));
        p.scale = rigVector(required(raw, "scale", "pin"));
        p.radius = floatValue(required(raw, "radius", "pin"), "radius");
        p.weight = floatValue(required(raw, "weight", "pin"), "weight");
        d.pins.push_back(p);
    }
    return d;
}
std::string serializeRigProgram(const RigProgram& program) {
    (void) RigProgramEvaluator(program); // Fail closed on invalid programs before encoding.
    for (const auto& geometry : program.geometry) {
        for (const auto& op : geometry.deformers.operators)
            if (op.path) invalid("RigProgram serialization does not accept MotionPath handles");
        if (!geometry.inverseBinds.empty() || !geometry.morphTracks.empty())
            invalid("RigProgram serialization excludes import-derived data; re-derive it from glTF");
    }
    if (!program.animation.empty() || !program.spaces.empty() || !program.ik.empty() ||
        !program.weights.empty() || !program.secondary.empty())
        invalid("RigProgram serialization v1 covers graph, controllers, constraints, deformers and poses; schedules are re-derived at authoring time");
    const auto rig = program.graph.compile();
    std::ostringstream out; out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << "{\"schema\":\"chrononmotion.rig.program.v1\",\"graph\":{\"nodes\":[";
    writeRigNodes(out, rig.nodes());
    out << "]},\"drivers\":{\"controllers\":[";
    for (std::size_t i = 0; i < program.drivers.controllers.size(); ++i) {
        if (i) out << ',';
        const auto& c = program.drivers.controllers[i];
        out << "{\"id\":" << c.id << ",\"kind\":" << static_cast<unsigned>(c.kind) << ",\"neutral\":";
        writeRigVector(out, c.neutral); out << ",\"minimum\":"; writeRigVector(out, c.minimum);
        out << ",\"maximum\":"; writeRigVector(out, c.maximum); out << '}';
    }
    out << "],\"nodes\":[";
    for (std::size_t i = 0; i < program.drivers.nodes.size(); ++i) {
        if (i) out << ',';
        const auto& n = program.drivers.nodes[i];
        out << "{\"id\":" << n.id << ",\"kind\":" << static_cast<unsigned>(n.kind) << ",\"inputs\":[" << n.inputs[0] << ',' << n.inputs[1]
            << "],\"controller\":" << n.controller << ",\"component\":" << n.component << ",\"first\":" << n.first
            << ",\"second\":" << n.second << ",\"value\":" << n.value << ",\"inMin\":" << n.inMin << ",\"inMax\":" << n.inMax
            << ",\"outMin\":" << n.outMin << ",\"outMax\":" << n.outMax << ",\"seed\":" << n.seed << ",\"track\":";
        writeRigTrack(out, n.track); out << '}';
    }
    out << "],\"outputs\":[";
    for (std::size_t i = 0; i < program.drivers.outputs.size(); ++i) {
        if (i) out << ',';
        const auto& o = program.drivers.outputs[i];
        out << "{\"driver\":" << o.driver << ",\"target\":" << o.target << ",\"property\":" << static_cast<unsigned>(o.property)
            << ",\"component\":" << o.component << '}';
    }
    out << "]},\"poses\":[";
    for (std::size_t i = 0; i < program.poses.size(); ++i) {
        if (i) out << ',';
        const auto& layer = program.poses[i];
        out << "{\"mode\":" << static_cast<unsigned>(layer.mode) << ",\"weight\":" << layer.weight << ",\"mask\":[";
        for (std::size_t k = 0; k < layer.mask.size(); ++k) { if (k) out << ','; out << layer.mask[k]; }
        out << "],\"pose\":[";
        for (std::size_t k = 0; k < layer.pose.size(); ++k) {
            if (k) out << ',';
            out << "{\"id\":" << layer.pose[k].id << ",\"transform\":"; writeRigTransform(out, layer.pose[k].local); out << '}';
        }
        out << "]}";
    }
    out << "],\"geometry\":[";
    for (std::size_t i = 0; i < program.geometry.size(); ++i) {
        if (i) out << ',';
        const auto& g = program.geometry[i];
        out << "{\"id\":" << g.id << ",\"rigidNode\":" << g.rigidNode << ",\"source\":[";
        for (std::size_t k = 0; k < g.source.size(); ++k) { if (k) out << ','; writeRigVector(out, g.source[k]); }
        out << "],\"morphs\":[";
        for (std::size_t k = 0; k < g.morphs.size(); ++k) {
            if (k) out << ',';
            out << "{\"weight\":" << g.morphs[k].weight << ",\"positions\":[";
            for (std::size_t v = 0; v < g.morphs[k].positions.size(); ++v) { if (v) out << ','; writeRigVector(out, g.morphs[k].positions[v]); }
            out << "]}";
        }
        out << "],\"skin\":[";
        for (std::size_t k = 0; k < g.skin.size(); ++k) {
            if (k) out << ',';
            out << "{\"j\":[" << g.skin[k].joints[0] << ',' << g.skin[k].joints[1] << ',' << g.skin[k].joints[2] << ',' << g.skin[k].joints[3]
                << "],\"w\":[" << g.skin[k].weights[0] << ',' << g.skin[k].weights[1] << ',' << g.skin[k].weights[2] << ',' << g.skin[k].weights[3] << "]}";
        }
        out << "],\"indices\":[";
        for (std::size_t k = 0; k < g.indices.size(); ++k) { if (k) out << ','; out << g.indices[k]; }
        out << "],\"deformers\":[";
        for (std::size_t k = 0; k < g.deformers.operators.size(); ++k) { if (k) out << ','; writeRigDeformer(out, g.deformers.operators[k]); }
        out << "]}";
    }
    out << "]}";
    return out.str();
}
RigProgram deserializeRigProgram(std::string_view json) {
    const auto root = JsonParser(json).parse();
    rejectUnknownKeys(root, {"schema", "graph", "drivers", "poses", "geometry"}, "rig program root");
    if (required(root, "schema", "rig program root").string("schema") != "chrononmotion.rig.program.v1")
        invalid("unknown rig program schema");
    RigProgram program;
    program.graph = readRigGraphObject(required(root, "graph", "rig program root"));
    (void) program.graph; // Schema/keys already validated by the program reader.
    const auto& drivers = required(root, "drivers", "rig program root");
    rejectUnknownKeys(drivers, {"controllers", "nodes", "outputs"}, "drivers");
    for (const auto& raw : required(drivers, "controllers", "drivers").array("controllers")) {
        rejectUnknownKeys(raw, {"id", "kind", "neutral", "minimum", "maximum"}, "controller");
        RigController c;
        c.id = rigId(required(raw, "id", "controller"));
        const auto kind = indexValue(required(raw, "kind", "controller"), "kind");
        if (kind > static_cast<unsigned>(RigControllerKind::Enum)) invalid("unknown controller kind");
        c.kind = static_cast<RigControllerKind>(kind);
        c.neutral = rigVector(required(raw, "neutral", "controller"));
        c.minimum = rigVector(required(raw, "minimum", "controller"));
        c.maximum = rigVector(required(raw, "maximum", "controller"));
        program.drivers.controllers.push_back(c);
    }
    for (const auto& raw : required(drivers, "nodes", "drivers").array("nodes")) {
        rejectUnknownKeys(raw, {"id", "kind", "inputs", "controller", "component", "first", "second", "value",
                                "inMin", "inMax", "outMin", "outMax", "seed", "track"}, "driver node");
        RigDriverNode n;
        n.id = indexValue(required(raw, "id", "driver node"), "driver id");
        const auto kind = indexValue(required(raw, "kind", "driver node"), "kind");
        if (kind > static_cast<unsigned>(RigDriverKind::Noise)) invalid("unknown driver kind");
        n.kind = static_cast<RigDriverKind>(kind);
        const auto& inputs = required(raw, "inputs", "driver node").array("inputs");
        if (inputs.size() != 2) invalid("driver inputs require two slots");
        n.inputs = {static_cast<std::uint32_t>(indexValue(inputs[0], "input")), static_cast<std::uint32_t>(indexValue(inputs[1], "input"))};
        n.controller = rigId(required(raw, "controller", "driver node"));
        n.component = indexValue(required(raw, "component", "driver node"), "component");
        n.first = rigId(required(raw, "first", "driver node")); n.second = rigId(required(raw, "second", "driver node"));
        n.value = floatValue(required(raw, "value", "driver node"), "value");
        n.inMin = floatValue(required(raw, "inMin", "driver node"), "inMin");
        n.inMax = floatValue(required(raw, "inMax", "driver node"), "inMax");
        n.outMin = floatValue(required(raw, "outMin", "driver node"), "outMin");
        n.outMax = floatValue(required(raw, "outMax", "driver node"), "outMax");
        n.seed = indexValue(required(raw, "seed", "driver node"), "seed");
        n.track = readRigTrack(required(raw, "track", "driver node"));
        program.drivers.nodes.push_back(n);
    }
    for (const auto& raw : required(drivers, "outputs", "drivers").array("outputs")) {
        rejectUnknownKeys(raw, {"driver", "target", "property", "component"}, "driver output");
        RigDriverOutput o;
        o.driver = indexValue(required(raw, "driver", "output"), "driver");
        o.target = rigId(required(raw, "target", "output"));
        const auto property = indexValue(required(raw, "property", "output"), "property");
        if (property > static_cast<unsigned>(RigDrivenProperty::Opacity)) invalid("unknown driven property");
        o.property = static_cast<RigDrivenProperty>(property);
        o.component = indexValue(required(raw, "component", "output"), "component");
        program.drivers.outputs.push_back(o);
    }
    for (const auto& raw : required(root, "poses", "rig program root").array("poses")) {
        rejectUnknownKeys(raw, {"mode", "weight", "mask", "pose"}, "pose layer");
        RigPoseLayer layer;
        const auto mode = indexValue(required(raw, "mode", "pose layer"), "mode");
        if (mode > static_cast<unsigned>(RigPoseBlendMode::MaskedBlend)) invalid("unknown pose blend mode");
        layer.mode = static_cast<RigPoseBlendMode>(mode);
        layer.weight = floatValue(required(raw, "weight", "pose layer"), "weight");
        for (const auto& mask : required(raw, "mask", "pose layer").array("mask")) layer.mask.push_back(rigId(mask));
        for (const auto& value : required(raw, "pose", "pose layer").array("pose")) {
            rejectUnknownKeys(value, {"id", "transform"}, "pose value");
            layer.pose.push_back({rigId(required(value, "id", "pose value")), rigTransform(required(value, "transform", "pose value"))});
        }
        program.poses.push_back(std::move(layer));
    }
    for (const auto& raw : required(root, "geometry", "rig program root").array("geometry")) {
        rejectUnknownKeys(raw, {"id", "rigidNode", "source", "morphs", "skin", "indices", "deformers"}, "geometry");
        RigGeometryBinding g;
        g.id = indexValue(required(raw, "id", "geometry"), "geometry id");
        g.rigidNode = rigId(required(raw, "rigidNode", "geometry"));
        for (const auto& point : required(raw, "source", "geometry").array("source")) g.source.push_back(rigVector(point));
        for (const auto& morph : required(raw, "morphs", "geometry").array("morphs")) {
            rejectUnknownKeys(morph, {"weight", "positions"}, "morph");
            RigMorphTarget m;
            m.weight = floatValue(required(morph, "weight", "morph"), "weight");
            for (const auto& point : required(morph, "positions", "morph").array("positions")) m.positions.push_back(rigVector(point));
            g.morphs.push_back(std::move(m));
        }
        for (const auto& vertex : required(raw, "skin", "geometry").array("skin")) {
            rejectUnknownKeys(vertex, {"j", "w"}, "skin vertex");
            RigSkinVertex s;
            const auto& joints = required(vertex, "j", "skin vertex").array("joints");
            const auto& weights = required(vertex, "w", "skin vertex").array("weights");
            if (joints.size() != 4 || weights.size() != 4) invalid("skin vertex requires four influences");
            for (unsigned i = 0; i < 4; ++i) {
                const auto joint = rigId(joints[i]);
                if (joint > std::numeric_limits<RigNodeId>::max()) invalid("joint overflow");
                s.joints[i] = joint; s.weights[i] = floatValue(weights[i], "weight");
            }
            g.skin.push_back(s);
        }
        for (const auto& index : required(raw, "indices", "geometry").array("indices")) {
            const auto value = indexValue(index, "triangle index");
            if (value > std::numeric_limits<std::uint32_t>::max()) invalid("triangle index overflow");
            g.indices.push_back(static_cast<std::uint32_t>(value));
        }
        for (const auto& value : required(raw, "deformers", "geometry").array("deformers")) g.deformers.operators.push_back(readRigDeformer(value));
        program.geometry.push_back(std::move(g));
    }
    (void) RigProgramEvaluator(program);
    return program;
}
} // namespace motion

} // namespace chrononmotion
