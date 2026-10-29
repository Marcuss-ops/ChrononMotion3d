#include "chrononmotion/assets/HdriReader.hpp"
#include "hdri_fixture.hpp"

#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace chrononmotion;

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::vector<std::byte> constantRleHdr() {
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
    std::vector<std::byte> bytes;
    for (char c : header) bytes.push_back(static_cast<std::byte>(c));
    for (unsigned row = 0; row < 4; ++row) {
        for (unsigned value : {2u, 2u, 0u, 8u}) bytes.push_back(static_cast<std::byte>(value));
        for (unsigned channel : {128u, 0u, 0u, 129u}) {
            bytes.push_back(std::byte{136});
            bytes.push_back(static_cast<std::byte>(channel));
        }
    }
    return bytes;
}
template<class Function> bool rejectsHdri(Function&& fn) {
    try { fn(); }
    catch (const std::invalid_argument& error) {
        return std::string_view(error.what()).starts_with("hdri:");
    }
    return false;
}
}

int main() try {
    const auto bytes = chrononmotion_test::constantRadianceHdr();
    const auto env = decodeHdriRadiance(bytes);
    check(env.sourceWidth == 8 && env.sourceHeight == 4 && env.faceSize == 2,
          "source and cubemap dimensions are reported");
    check(env.mips.size() == 2 && env.mips[0].size == 2 && env.mips[1].size == 1,
          "complete cubemap mip chain reaches one texel");
    for (const auto& face : env.mips[0].faces) {
        check(face.size() == 12, "six faces have row-major RGB samples");
        for (std::size_t i = 0; i < face.size(); i += 3)
            check(std::abs(face[i] - 1.f) < 1e-6f && face[i + 1] == 0.f && face[i + 2] == 0.f,
                  "constant RGBE source reprojects consistently onto every cubemap face");
    }
    check(env.irradianceSH[0] > 10.f && env.irradianceSH[0] < 12.f &&
          std::abs(env.irradianceSH[1]) < 1e-4f && std::abs(env.irradianceSH[2]) < 1e-4f,
          "nine real SH coefficients represent constant red irradiance");
    check(decodeHdriRadiance(constantRleHdr()).serialize() == env.serialize(),
          "Radiance per-channel RLE decodes to the same canonical asset");
    const auto payload = env.serialize();
    check(payload == env.serialize() && payload.size() > 8 &&
          std::memcmp(payload.data(), "HDRIENV1", 8) == 0,
          "HDRI_ENV_V1 serialization is deterministic and version tagged");

    bool resolved = false;
    const auto external = loadHdri("studio.hdr", [&](std::string_view uri) {
        resolved = uri == "studio.hdr";
        return bytes;
    });
    check(resolved && external.serialize() == payload, "external resolver supplies bytes without implicit I/O");

    auto bad = bytes;
    bad[0] = std::byte{'x'};
    check(rejectsHdri([&] { (void)decodeHdriRadiance(bad); }), "unsupported signature fails with hdri prefix");
    bad = bytes; bad.pop_back();
    check(rejectsHdri([&] { (void)decodeHdriRadiance(bad); }), "truncated RGBE scanline fails closed");
    HdriReadPolicy small;
    small.maxWidth = 7;
    check(rejectsHdri([&] { (void)decodeHdriRadiance(bytes, small); }), "dimension cap fails closed");
    small = {}; small.maxEncodedBytes = bytes.size() - 1;
    check(rejectsHdri([&] { (void)decodeHdriRadiance(bytes, small); }), "encoded byte cap fails closed");
    check(rejectsHdri([&] { (void)loadHdri("missing", {}, {}); }), "resolver is mandatory");
    std::cout << "PASS: 40 HDRI_ENV_V1 checks\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "motion_hdri: " << error.what() << '\n';
    return 1;
}
