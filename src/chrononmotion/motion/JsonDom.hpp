// ChrononMotion — the one strict JSON document model.
//
// Internal to the library. Every JSON document this tree READS goes through
// this file, mirroring JsonWriter.hpp on the write side: one parser, one set
// of accessors, one failure vocabulary. Strictness is the contract:
//
//   * input size and nesting depth are budgeted (fail closed, never OOM);
//   * duplicate object keys fail (a config that says two things is corrupt);
//   * trailing data fails (a document is exactly one value);
//   * numbers must be finite (JSON has no NaN/Infinity, and neither do we);
//
// so a malformed, hostile or hand-edited document is rejected with a
// std::invalid_argument naming the byte or field, never half-accepted.
//
#ifndef CHRONONMOTION_MOTION_JSON_DOM_HPP
#define CHRONONMOTION_MOTION_JSON_DOM_HPP

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace chrononmotion::motion::detail {

    constexpr std::size_t kMaxJsonBytes = 16u * 1024u * 1024u;
    constexpr std::size_t kMaxJsonDepth = 128;

    [[noreturn]] inline void jsonInvalid(const std::string& message) {
        throw std::invalid_argument(message);
    }

    struct JsonValue {
        using Array = std::vector<JsonValue>;
        using Object = std::map<std::string, JsonValue, std::less<>>;
        std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value{nullptr};

        const Object& object(std::string_view where) const {
            if (const auto* result = std::get_if<Object>(&value)) return *result;
            jsonInvalid(std::string(where) + " must be an object");
        }
        const Array& array(std::string_view where) const {
            if (const auto* result = std::get_if<Array>(&value)) return *result;
            jsonInvalid(std::string(where) + " must be an array");
        }
        std::string string(std::string_view where) const {
            if (const auto* result = std::get_if<std::string>(&value)) return *result;
            jsonInvalid(std::string(where) + " must be a string");
        }
        double number(std::string_view where) const {
            if (const auto* result = std::get_if<double>(&value)) return *result;
            jsonInvalid(std::string(where) + " must be a number");
        }
        bool boolean(std::string_view where) const {
            if (const auto* result = std::get_if<bool>(&value)) return *result;
            jsonInvalid(std::string(where) + " must be a boolean");
        }
    };

    class JsonParser {
    public:
        explicit JsonParser(std::string_view input, std::size_t maxBytes = kMaxJsonBytes) : input_(input) {
            if (input.size() > maxBytes) jsonInvalid("JSON exceeds the configured input limit");
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
            jsonInvalid(std::string(message) + " at JSON byte " + std::to_string(position_));
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

    inline const JsonValue* find(const JsonValue& value, std::string_view key) {
        const auto& object = value.object("JSON value");
        const auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }
    inline const JsonValue& required(const JsonValue& value, std::string_view key, std::string_view where) {
        const auto* result = find(value, key);
        if (!result) jsonInvalid(std::string(where) + " is missing '" + std::string(key) + "'");
        return *result;
    }
    inline void rejectUnknownKeys(const JsonValue& value, std::initializer_list<std::string_view> allowed,
                                  std::string_view where) {
        for (const auto& [key, unused] : value.object(where)) {
            (void)unused;
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                jsonInvalid(std::string(where) + " contains unsupported field '" + key + "'");
        }
    }
    // Callers that build the allowed set at runtime instead of spelling it.
    inline void rejectUnknownKeys(const JsonValue& value, const std::vector<std::string_view>& allowed,
                                  std::string_view where) {
        for (const auto& [key, unused] : value.object(where)) {
            (void)unused;
            if (std::find(allowed.begin(), allowed.end(), key) == allowed.end())
                jsonInvalid(std::string(where) + " contains unsupported field '" + key + "'");
        }
    }
    inline std::size_t indexValue(const JsonValue& value, std::string_view where) {
        const double number = value.number(where);
        if (number < 0 || std::floor(number) != number || number >= static_cast<double>(std::numeric_limits<std::size_t>::max()))
            jsonInvalid(std::string(where) + " must be a non-negative integer");
        return static_cast<std::size_t>(number);
    }
    inline float floatValue(const JsonValue& value, std::string_view where) {
        const double number = value.number(where);
        const float result = static_cast<float>(number);
        if (!std::isfinite(result)) jsonInvalid(std::string(where) + " exceeds finite float range");
        return result;
    }
    inline const JsonValue& at(const JsonValue::Array& array, std::size_t index, std::string_view where) {
        if (index >= array.size()) jsonInvalid(std::string(where) + " index is out of range");
        return array[index];
    }

}// namespace chrononmotion::motion::detail

#endif//CHRONONMOTION_MOTION_JSON_DOM_HPP
