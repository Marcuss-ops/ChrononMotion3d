// ChrononMotion — the one deterministic JSON writer.
//
// Internal to the library. Every JSON document this tree emits goes through
// these three pieces, so "the same input produces the same bytes" is a property
// of one implementation instead of a promise each exporter remembers to keep.
// The rules it fixes:
//
//   * object keys are written in the order the caller writes them;
//   * a float is the shortest decimal that reads back as the same `float`
//     (`0.85`, never `0.850000024`), `-0` is normalized to `0`, and a
//     non-finite value is `null` because JSON has no representation for it;
//   * separators and indentation come from `indentSpaces` alone, so indent 0 is
//     a single line and any indent is byte-reproducible.
//
// `SceneExport` (space and time) and `RenderPlanEmission` (the offline adapter)
// share it deliberately: two writers is how one document would drift from the
// other's number formatting.

#ifndef CHRONONMOTION_MOTION_JSON_WRITER_HPP
#define CHRONONMOTION_MOTION_JSON_WRITER_HPP

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <utility>
#include <vector>

namespace chrononmotion::motion::detail {

    /// Shortest decimal that reads back as the same float. Deterministic,
    /// locale-independent and readable — an export is for humans and for a
    /// corpus, so "0.85" is the right answer and "0.850000024" is not.
    [[nodiscard]] inline std::string formatFloat(float value) {

        if (!std::isfinite(value)) return "null";
        if (value == 0.f) value = 0.f;// normalize -0 to 0

        char buffer[64];
        for (int precision = 1; precision <= 9; ++precision) {
            std::snprintf(buffer, sizeof(buffer), "%.*g", precision, static_cast<double>(value));
            if (static_cast<float>(std::strtod(buffer, nullptr)) == value) break;
        }
        return buffer;
    }

    [[nodiscard]] inline std::string escapeJson(const std::string& value) {

        std::string out;
        out.reserve(value.size());
        for (const unsigned char c : value) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\b': out += "\\b"; break;
                case '\f': out += "\\f"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20) {
                        char buffer[8];
                        std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned int>(c));
                        out += buffer;
                    } else {
                        out += static_cast<char>(c);
                    }
            }
        }
        return out;
    }

    /// Emits JSON with fixed key order and fixed separators, so the same
    /// document always produces the same bytes.
    class Writer {
    public:
        explicit Writer(int indentSpaces) : indent_(indentSpaces < 0 ? 0 : indentSpaces) {}

        void beginObject() {
            out_ += '{';
            open_.push_back(true);
        }

        void endObject() {
            const bool wasEmpty = open_.back();
            open_.pop_back();
            if (!wasEmpty) newline();
            out_ += '}';
        }

        void beginArray() {
            out_ += '[';
            open_.push_back(true);
        }

        void endArray() {
            const bool wasEmpty = open_.back();
            open_.pop_back();
            if (!wasEmpty) newline();
            out_ += ']';
        }

        /// Separate one object member. The value follows immediately.
        void key(const char* name) {
            separator();
            out_ += '"';
            out_ += name;
            out_ += indent_ > 0 ? "\": " : "\":";
        }

        /// Separate one array element. The value follows immediately.
        void item() { separator(); }

        void raw(const char* text) { out_ += text; }
        void boolean(bool value) { out_ += value ? "true" : "false"; }
        void number(float value) { out_ += formatFloat(value); }
        void number(double value) { out_ += formatFloat(static_cast<float>(value)); }
        /// Preserve the full binary64 value for identity-bearing timestamps.
        void exactNumber(double value) {
            if (!std::isfinite(value)) { out_ += "null"; return; }
            if (value == 0.0) value = 0.0;
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
            if (result.ec != std::errc{}) throw std::runtime_error("JSON double formatting failed");
            out_.append(buffer, result.ptr);
        }

        void integer(long long value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%lld", value);
            out_ += buffer;
        }

        void unsignedInteger(unsigned long long value) {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%llu", value);
            out_ += buffer;
        }

        void text(const std::string& value) {
            out_ += '"';
            out_ += escapeJson(value);
            out_ += '"';
        }

        /// A string whose content is already a JSON token, e.g. a quoted name
        /// chosen from a fixed vocabulary by the caller.
        void token(const char* value) {
            out_ += '"';
            out_ += value;
            out_ += '"';
        }

        void null() { out_ += "null"; }

        [[nodiscard]] std::string take() { return std::move(out_); }

    private:
        void separator() {
            if (!open_.back()) out_ += ',';
            open_.back() = false;
            newline();
        }

        void newline() {
            if (indent_ == 0) return;
            out_ += '\n';
            out_.append(open_.size() * static_cast<std::size_t>(indent_), ' ');
        }

        std::string out_{};
        int indent_{2};
        std::vector<bool> open_{};
    };

}// namespace chrononmotion::motion::detail

#endif//CHRONONMOTION_MOTION_JSON_WRITER_HPP
