#pragma once

#include "chrononmotion/assets/GltfReader.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace chrononmotion_test {

// Small in-memory glTF fixture shared by reader, rig, RenderPlan and showcase
// tests. It contains a triangle with one skin joint, one POSITION morph target,
// and a two-key translation channel. No file I/O or third-party asset is used.
struct GltfMotionFixture {
    std::vector<std::byte> bytes;
    std::string json;

    GltfMotionFixture() {
        const auto scalar = [&](float value) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                bytes.push_back(static_cast<std::byte>((bits >> (8 * i)) & 255));
        };
        for (float value : {0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f}) scalar(value);
        for (unsigned i = 0; i < 12; ++i) bytes.push_back(std::byte{0});
        for (unsigned vertex = 0; vertex < 3; ++vertex)
            for (float value : {1.f, 0.f, 0.f, 0.f}) scalar(value);
        for (unsigned i = 0; i < 16; ++i) scalar(i % 5 == 0 ? 1.f : 0.f);
        scalar(0.f); scalar(1.f);
        for (float value : {0.f, 0.f, 0.f, 2.f, 0.f, 0.f}) scalar(value);
        for (unsigned i = 0; i < 9; ++i) scalar(i % 3 == 1 ? 1.f : 0.f);
        for (const std::uint16_t index : {0, 1, 2}) {
            bytes.push_back(static_cast<std::byte>(index & 255));
            bytes.push_back(static_cast<std::byte>(index >> 8));
        }

        json = R"({"asset":{"version":"2.0"},"buffers":[{"byteLength":234,"uri":"fixture.bin"}],
"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":12},
{"buffer":0,"byteOffset":48,"byteLength":48},{"buffer":0,"byteOffset":96,"byteLength":64},
{"buffer":0,"byteOffset":160,"byteLength":8},{"buffer":0,"byteOffset":168,"byteLength":24},
{"buffer":0,"byteOffset":192,"byteLength":36},{"buffer":0,"byteOffset":228,"byteLength":6}],
"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":1,"componentType":5121,"count":3,"type":"VEC4"},
{"bufferView":2,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":3,"componentType":5126,"count":1,"type":"MAT4"},
{"bufferView":4,"componentType":5126,"count":2,"type":"SCALAR"},
{"bufferView":5,"componentType":5126,"count":2,"type":"VEC3"},
{"bufferView":6,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":7,"componentType":5123,"count":3,"type":"SCALAR"}],
"nodes":[{"name":"joint"},{"name":"mesh","mesh":0,"skin":0}],
"skins":[{"joints":[0],"skeleton":0,"inverseBindMatrices":3}],
"meshes":[{"weights":[0.5],"primitives":[{"attributes":{"POSITION":0,"JOINTS_0":1,"WEIGHTS_0":2},"indices":7,"targets":[{"POSITION":6}]}]}],
"animations":[{"name":"move","samplers":[{"input":4,"output":5,"interpolation":"LINEAR"}],
"channels":[{"sampler":0,"target":{"node":0,"path":"translation"}}]}]})";
    }

    [[nodiscard]] chrononmotion::GltfMotionAsset load(std::string_view source) const {
        return chrononmotion::loadGltfMotion(source, [&](std::string_view uri) {
            if (uri != "fixture.bin") throw std::invalid_argument("unknown fixture URI");
            return bytes;
        });
    }

    [[nodiscard]] std::string withAnimatedMorphChannel() const {
        auto result = json;
        replace(result,
                "\"samplers\":[{\"input\":4,\"output\":5,\"interpolation\":\"LINEAR\"}]",
                "\"samplers\":[{\"input\":4,\"output\":5,\"interpolation\":\"LINEAR\"},"
                "{\"input\":4,\"output\":4,\"interpolation\":\"LINEAR\"}]");
        replace(result,
                "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}}]",
                "\"channels\":[{\"sampler\":0,\"target\":{\"node\":0,\"path\":\"translation\"}},"
                "{\"sampler\":1,\"target\":{\"node\":1,\"path\":\"weights\"}}]");
        return result;
    }

    [[nodiscard]] std::pair<std::string, std::vector<std::byte>> withCubicTranslation() const {
        auto source = json;
        auto binary = bytes;
        binary.push_back(std::byte{0});
        binary.push_back(std::byte{0}); // align the float VEC3 output accessor
        for (const float value : {0.f,0.f,0.f, 0.f,0.f,0.f, 2.f,0.f,0.f,
                                  0.f,0.f,0.f, 2.f,0.f,0.f, 0.f,0.f,0.f}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                binary.push_back(static_cast<std::byte>((bits >> (8 * i)) & 255));
        }
        replace(source, "\"byteLength\":234", "\"byteLength\":308");
        replace(source, "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6}],",
                       "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":236,\"byteLength\":72}],");
        replace(source, "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],",
                       "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
                       "{\"bufferView\":8,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"}],");
        replace(source, "\"input\":4,\"output\":5,\"interpolation\":\"LINEAR\"",
                       "\"input\":4,\"output\":8,\"interpolation\":\"CUBICSPLINE\"");
        return {std::move(source), std::move(binary)};
    }

    [[nodiscard]] std::pair<std::string, std::vector<std::byte>> withCubicMorphWeights() const {
        auto source = json;
        auto binary = bytes;
        binary.push_back(std::byte{0}); binary.push_back(std::byte{0});
        for (const float value : {0.f, 0.f, 2.f, 0.f, 1.f, 0.f}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                binary.push_back(static_cast<std::byte>((bits >> (8 * i)) & 255));
        }
        replace(source, "\"byteLength\":234", "\"byteLength\":260");
        replace(source, "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6}],",
                       "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":236,\"byteLength\":24}],");
        replace(source, "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],",
                       "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
                       "{\"bufferView\":8,\"componentType\":5126,\"count\":6,\"type\":\"SCALAR\"}],");
        replace(source, "\"input\":4,\"output\":5,\"interpolation\":\"LINEAR\"",
                       "\"input\":4,\"output\":8,\"interpolation\":\"CUBICSPLINE\"");
        replace(source, "\"node\":0,\"path\":\"translation\"",
                       "\"node\":1,\"path\":\"weights\"");
        return {std::move(source), std::move(binary)};
    }

    [[nodiscard]] std::pair<std::string, std::vector<std::byte>> withCubicRotation() const {
        auto source = json;
        auto binary = bytes;
        binary.push_back(std::byte{0}); binary.push_back(std::byte{0});
        for (const float value : {0.f,0.f,0.f,0.f, 0.f,0.f,0.f,1.f, 0.f,0.f,2.f,0.f,
                                  0.f,0.f,0.f,0.f, 0.f,0.f,0.f,1.f, 0.f,0.f,0.f,0.f}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                binary.push_back(static_cast<std::byte>((bits >> (8 * i)) & 255));
        }
        replace(source, "\"byteLength\":234", "\"byteLength\":332");
        replace(source, "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6}],",
                       "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":236,\"byteLength\":96}],");
        replace(source, "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],",
                       "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
                       "{\"bufferView\":8,\"componentType\":5126,\"count\":6,\"type\":\"VEC4\"}],");
        replace(source, "\"input\":4,\"output\":5,\"interpolation\":\"LINEAR\"",
                       "\"input\":4,\"output\":8,\"interpolation\":\"CUBICSPLINE\"");
        replace(source, "\"node\":0,\"path\":\"translation\"",
                       "\"node\":0,\"path\":\"rotation\"");
        return {std::move(source), std::move(binary)};
    }

    [[nodiscard]] std::pair<std::string, std::vector<std::byte>> withNormalMorphTarget() const {
        auto source = json;
        auto binary = bytes;
        binary.push_back(std::byte{0}); binary.push_back(std::byte{0});
        for (const float value : {0.f,0.f,1.f, 0.f,0.f,1.f, 0.f,0.f,1.f,
                                  0.f,1.f,-1.f, 0.f,1.f,-1.f, 0.f,1.f,-1.f}) {
            const auto bits = std::bit_cast<std::uint32_t>(value);
            for (unsigned i = 0; i < 4; ++i)
                binary.push_back(static_cast<std::byte>((bits >> (8 * i)) & 255));
        }
        replace(source, "\"byteLength\":234", "\"byteLength\":308");
        replace(source, "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6}],",
                       "{\"buffer\":0,\"byteOffset\":228,\"byteLength\":6},{\"buffer\":0,\"byteOffset\":236,\"byteLength\":36},{\"buffer\":0,\"byteOffset\":272,\"byteLength\":36}],");
        replace(source, "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}],",
                       "{\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},"
                       "{\"bufferView\":8,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},"
                       "{\"bufferView\":9,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}],");
        replace(source, "\"attributes\":{\"POSITION\":0,\"JOINTS_0\":1,\"WEIGHTS_0\":2}",
                       "\"attributes\":{\"POSITION\":0,\"NORMAL\":8,\"JOINTS_0\":1,\"WEIGHTS_0\":2}");
        replace(source, "\"targets\":[{\"POSITION\":6}]",
                       "\"targets\":[{\"POSITION\":6,\"NORMAL\":9}]");
        return {std::move(source), std::move(binary)};
    }

private:
    static void replace(std::string& text, std::string_view from, std::string_view to) {
        const auto position = text.find(from);
        if (position == std::string::npos)
            throw std::runtime_error("glTF motion fixture text replacement failed");
        text.replace(position, from.size(), to);
    }
};

} // namespace chrononmotion_test
