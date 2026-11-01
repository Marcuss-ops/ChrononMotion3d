#include "chrononmotion/assets/HdriReader.hpp"
#include "hdri_fixture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace chrononmotion;

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
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

template <class Function> bool rejectsHdri(Function&& fn) {
    try { fn(); }
    catch (const std::invalid_argument& error) {
        return std::string_view(error.what()).starts_with("hdri:");
    }
    return false;
}

/// Face basis of the HDRI_ENV_V1 asset: +X, -X, +Y, -Y, +Z, -Z with the same
/// u/v convention the reader writes. The test needs it to name a texel by the
/// direction it stores; it is asset format knowledge, not a sample policy.
std::array<float, 3> faceDirection(unsigned face, float u, float v) {
    std::array<float, 3> d{};
    switch (face) {
        case 0: d = {1.f, -v, -u}; break;
        case 1: d = {-1.f, -v, u}; break;
        case 2: d = {u, 1.f, v}; break;
        case 3: d = {u, -1.f, -v}; break;
        case 4: d = {u, -v, 1.f}; break;
        default: d = {-u, -v, -1.f}; break;
    }
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (float& value : d) value /= length;
    return d;
}

std::array<float, 3> texelCenterDirection(unsigned face, unsigned size, unsigned x, unsigned y) {
    const float u = (2.f * (static_cast<float>(x) + .5f) / static_cast<float>(size)) - 1.f;
    const float v = (2.f * (static_cast<float>(y) + .5f) / static_cast<float>(size)) - 1.f;
    return faceDirection(face, u, v);
}

std::array<float, 3> texel(const HdriEnvironment& env, std::size_t mip, unsigned face,
                           unsigned x, unsigned y) {
    const auto& level = env.mips.at(mip);
    const std::size_t index = (static_cast<std::size_t>(y) * level.size + x) * 3;
    return {level.faces.at(face).at(index), level.faces.at(face).at(index + 1),
            level.faces.at(face).at(index + 2)};
}

/// Mean of every stored component at one mip level, across all six faces.
/// Box filtering and mip generation must preserve total radiance, which is the
/// energy a specular consumer integrates once roughness has blurred the probe.
std::array<float, 3> levelMean(const HdriEnvironment& env, std::size_t mip) {
    const auto& level = env.mips.at(mip);
    std::array<double, 3> sum{};
    std::size_t samples = 0;
    for (const auto& face : level.faces) {
        for (std::size_t i = 0; i + 2 < face.size(); i += 3) {
            for (unsigned c = 0; c < 3; ++c) sum[c] += face[i + c];
            ++samples;
        }
    }
    std::array<float, 3> mean{};
    for (unsigned c = 0; c < 3; ++c) mean[c] = static_cast<float>(sum[c] / static_cast<double>(samples));
    return mean;
}

std::string digest(std::span<const std::byte> bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= 1099511628211ULL;
    }
    char text[17]{};
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(hash));
    return text;
}

struct Probe {
    const char* id;
    unsigned face;
    unsigned mip;
    unsigned x;
    unsigned y;
};

// Texel corners, interiors and the coarse end of the chain: the four sides a
// specular lookup reaches (faces) and the levels roughness selects.
constexpr std::array<Probe, 13> kProbes{{
    {"f0_mip0_origin", 0, 0, 0, 0},
    {"f0_mip0_center", 0, 0, 8, 8},
    {"f0_mip0_far", 0, 0, 15, 15},
    {"f1_mip0", 1, 0, 0, 0},
    {"f2_mip0", 2, 0, 0, 0},
    {"f3_mip0", 3, 0, 0, 0},
    {"f4_mip0", 4, 0, 0, 0},
    {"f5_mip0", 5, 0, 0, 0},
    {"f0_mip1", 0, 1, 0, 0},
    {"f0_mip2", 0, 2, 3, 3},
    {"f4_mip3", 4, 3, 0, 0},
    {"f5_mip4", 5, 4, 0, 0},
    {"f2_mip4", 2, 4, 0, 0},
}};

std::string formatFloat(float value) {
    std::ostringstream text;
    text.precision(9);
    text << value;
    return text.str();
}

std::string dumpGolden(const HdriEnvironment& env, std::span<const std::byte> encoded) {
    std::ostringstream out;
    out << "{\n  \"version\": 1,\n"
        << "  \"fixture\": \"linear-gradient-radiance\",\n"
        << "  \"hash\": \"" << digest(encoded) << "\",\n"
        << "  \"encoded_bytes\": " << encoded.size() << ",\n"
        << "  \"source\": [" << env.sourceWidth << ", " << env.sourceHeight << "],\n"
        << "  \"face_size\": " << env.faceSize << ",\n"
        << "  \"mips\": " << env.mips.size() << ",\n"
        << "  \"irradiance_sh\": [";
    for (std::size_t i = 0; i < env.irradianceSH.size(); ++i)
        out << (i ? ", " : "") << formatFloat(env.irradianceSH[i]);
    out << "],\n  \"radiance\": [\n";
    for (std::size_t i = 0; i < kProbes.size(); ++i) {
        const auto& probe = kProbes[i];
        const auto rgb = texel(env, probe.mip, probe.face, probe.x, probe.y);
        out << "    {\"id\": \"" << probe.id << "\", \"face\": " << probe.face
            << ", \"mip\": " << probe.mip << ", \"texel\": [" << probe.x << ", " << probe.y
            << "], \"rgb\": [" << formatFloat(rgb[0]) << ", " << formatFloat(rgb[1]) << ", "
            << formatFloat(rgb[2]) << "]}" << (i + 1 == kProbes.size() ? "" : ",") << "\n";
    }
    out << "  ],\n  \"face_mean\": [\n";
    for (std::size_t mip = 0; mip < env.mips.size(); ++mip) {
        const auto mean = levelMean(env, mip);
        out << "    {\"mip\": " << mip << ", \"rgb\": [" << formatFloat(mean[0]) << ", "
            << formatFloat(mean[1]) << ", " << formatFloat(mean[2]) << "]}"
            << (mip + 1 == env.mips.size() ? "" : ",") << "\n";
    }
    out << "  ]\n}\n";
    return out.str();
}

std::array<float, 3> parseTriple(const std::string& text) {
    std::array<float, 3> values{};
    std::istringstream input(text);
    std::string item;
    unsigned index = 0;
    while (std::getline(input, item, ',') && index < 3) values[index++] = std::stof(item);
    if (index != 3) throw std::runtime_error("golden triple does not carry three channels");
    return values;
}

struct Golden {
    std::string hash;
    std::size_t encodedBytes{0};
    unsigned faceSize{0};
    unsigned mips{0};
    std::array<float, 27> sh{};
    std::vector<std::pair<std::string, std::array<float, 3>>> radiance;
    std::vector<std::pair<unsigned, std::array<float, 3>>> means;
};

Golden readGolden(const HdriEnvironment& env, std::span<const std::byte> encoded) {
#ifndef CHRONONMOTION_HDRI_SPECULAR_GOLDEN
#error "CHRONONMOTION_HDRI_SPECULAR_GOLDEN must point to the committed golden"
#endif
    std::ifstream input(CHRONONMOTION_HDRI_SPECULAR_GOLDEN, std::ios::binary);
    if (!input)
        throw std::runtime_error(std::string("cannot read HDRI specular golden: ") +
                                 CHRONONMOTION_HDRI_SPECULAR_GOLDEN);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Golden golden;
    const auto one = [&](const std::string& pattern, unsigned group) {
        std::smatch match;
        if (!std::regex_search(text, match, std::regex(pattern)))
            throw std::runtime_error("golden is missing " + pattern);
        return match[group].str();
    };
    golden.hash = one(R"re("hash"\s*:\s*"([0-9a-f]{16})")re", 1);
    golden.encodedBytes = std::stoul(one(R"("encoded_bytes"\s*:\s*(\d+))", 1));
    golden.faceSize = static_cast<unsigned>(std::stoul(one(R"("face_size"\s*:\s*(\d+))", 1)));
    golden.mips = static_cast<unsigned>(std::stoul(one(R"("mips"\s*:\s*(\d+))", 1)));
    const std::string sh = one(R"("irradiance_sh"\s*:\s*\[([^\]]*)\])", 1);
    {
        std::istringstream values(sh);
        std::string item;
        std::size_t index = 0;
        while (std::getline(values, item, ',') && index < golden.sh.size())
            golden.sh[index++] = std::stof(item);
        if (index != golden.sh.size())
            throw std::runtime_error("golden irradiance_sh is not twenty-seven coefficients");
    }
    const std::regex probePattern(
        R"rx(\{"id"\s*:\s*"([a-z0-9_]+)"\s*,\s*"face"\s*:\s*(\d+)\s*,\s*"mip"\s*:\s*(\d+)\s*,\s*"texel"\s*:\s*\[(\d+)\s*,\s*(\d+)\]\s*,\s*"rgb"\s*:\s*\[([^\]]*)\]\})rx");
    for (std::sregex_iterator it(text.begin(), text.end(), probePattern), end; it != end; ++it)
        golden.radiance.emplace_back((*it)[1].str(), parseTriple((*it)[6].str()));
    const std::regex meanPattern(
        R"rx(\{"mip"\s*:\s*(\d+)\s*,\s*"rgb"\s*:\s*\[([^\]]*)\]\})rx");
    for (std::sregex_iterator it(text.begin(), text.end(), meanPattern), end; it != end; ++it)
        golden.means.emplace_back(static_cast<unsigned>(std::stoul((*it)[1].str())),
                                  parseTriple((*it)[2].str()));
    check(!golden.radiance.empty() && !golden.means.empty(), "golden carries radiance and level means");
    check(golden.radiance.size() == kProbes.size(), "golden covers every authored specular probe");
    check(golden.means.size() == env.mips.size(), "golden covers every mip level");
    check(golden.hash == digest(encoded) && golden.encodedBytes == encoded.size(),
          "fixture bytes match the pinned digest");
    return golden;
}

float relativeError(float actual, float expected) {
    const float scale = std::max(1.f, std::abs(expected));
    return std::abs(actual - expected) / scale;
}
} // namespace

int main(int argc, char** argv) try {
    const bool dump = argc > 1 && std::string_view(argv[1]) == "--dump-golden";

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

    // --- Quantitative specular golden -------------------------------------
    // The analytic fixture makes both halves of an environment probe concrete:
    // the diffuse SH has a closed form, and every cubemap texel has an expected
    // radiance. The golden pins what the reader emits; the closed form proves
    // the emitted numbers are the right ones rather than merely stable.
    using Fixture = chrononmotion_test::LinearGradientRadiance;
    const auto gradientBytes = chrononmotion_test::linearGradientRadianceHdr();
    const auto gradient = decodeHdriRadiance(gradientBytes);
    check(gradient.sourceWidth == Fixture::width && gradient.sourceHeight == Fixture::height,
          "analytic fixture decodes at its authored resolution");
    check(gradient.faceSize == Fixture::height / 2 && gradient.mips.size() == 5,
          "analytic fixture produces a complete cubemap mip chain");

    if (dump) {
        std::cout << dumpGolden(gradient, gradientBytes);
        return 0;
    }

    const auto golden = readGolden(gradient, gradientBytes);
    check(golden.faceSize == gradient.faceSize && golden.mips == gradient.mips.size(),
          "decoded asset shape matches the pinned golden");

    const auto analyticSH = Fixture::irradianceSH();
    for (std::size_t k = 0; k < gradient.irradianceSH.size(); ++k) {
        const float expected = analyticSH[k];
        // The fixture crosses RGBE (8-bit mantissa) and 64x32 midpoint
        // quadrature before reaching SH, which costs about 3e-3; one percent
        // still pins the band to the physical value rather than to the code.
        check(relativeError(gradient.irradianceSH[k], expected) < 1e-2f,
              "irradiance band matches the closed-form irradiance of the analytic fixture");
        check(relativeError(golden.sh[k], gradient.irradianceSH[k]) < 1e-4f,
              "irradiance coefficient matches the pinned SH golden");
    }
    for (std::size_t k = 4; k < 9; ++k) {
        for (unsigned c = 0; c < 3; ++c)
            check(std::abs(gradient.irradianceSH[k * 3 + c]) < 5e-3f,
                  "bands above L1 vanish for a linear radiance field");
    }

    // Level-zero radiance is the sampled field itself; a coarse texel is a box
    // average of the field over its footprint, so it must stay inside the
    // radiance range its face actually contains rather than equal the value at
    // the coarse texel direction.
    std::array<std::array<float, 3>, 6> faceMin{};
    std::array<std::array<float, 3>, 6> faceMax{};
    for (unsigned face = 0; face < 6; ++face) {
        for (unsigned c = 0; c < 3; ++c) {
            faceMin[face][c] = std::numeric_limits<float>::max();
            faceMax[face][c] = std::numeric_limits<float>::lowest();
        }
        for (unsigned y = 0; y < gradient.mips[0].size; ++y) {
            for (unsigned x = 0; x < gradient.mips[0].size; ++x) {
                const auto value = Fixture::radiance(
                    texelCenterDirection(face, gradient.mips[0].size, x, y));
                for (unsigned c = 0; c < 3; ++c) {
                    faceMin[face][c] = std::min(faceMin[face][c], value[c]);
                    faceMax[face][c] = std::max(faceMax[face][c], value[c]);
                }
            }
        }
    }

    std::size_t matched = 0;
    for (const auto& probe : kProbes) {
        const auto actual = texel(gradient, probe.mip, probe.face, probe.x, probe.y);
        const auto it = std::find_if(golden.radiance.begin(), golden.radiance.end(),
                                     [&](const auto& entry) { return entry.first == probe.id; });
        check(it != golden.radiance.end(), "golden carries every authored specular probe");
        for (unsigned c = 0; c < 3; ++c)
            check(relativeError(actual[c], it->second[c]) < 1e-5f,
                  "cubemap radiance matches the pinned specular golden");
        for (unsigned c = 0; c < 3; ++c) {
            if (probe.mip == 0) {
                const auto expected = Fixture::radiance(
                    texelCenterDirection(probe.face, gradient.mips[0].size, probe.x, probe.y));
                // RGBE mantissa quantization is 2^-8 of the exponent scale and
                // equirect bilinear reconstruction adds a little on top.
                check(std::abs(actual[c] - expected[c]) <= 8e-3f,
                      "level-zero texel radiance is the analytic radiance of its direction");
            } else {
                check(actual[c] >= faceMin[probe.face][c] - 1e-3f &&
                      actual[c] <= faceMax[probe.face][c] + 1e-3f,
                      "filtered texel radiance stays inside the radiance range of its face");
            }
        }
        ++matched;
    }
    check(matched == kProbes.size(), "every authored specular probe was verified");

    const auto baseMean = levelMean(gradient, 0);
    const auto baseGolden = std::find_if(golden.means.begin(), golden.means.end(),
                                         [](const auto& entry) { return entry.first == 0; });
    check(baseGolden != golden.means.end(), "golden pins the base level mean");
    for (unsigned c = 0; c < 3; ++c) {
        check(relativeError(baseMean[c], Fixture::base[c]) < 5e-2f,
              "base level mean radiance matches the analytic DC term of the fixture");
        check(relativeError(baseMean[c], baseGolden->second[c]) < 1e-5f,
              "base level mean matches the pinned golden");
    }
    for (std::size_t mip = 1; mip < gradient.mips.size(); ++mip) {
        const auto mean = levelMean(gradient, mip);
        const auto pinned = std::find_if(golden.means.begin(), golden.means.end(),
                                         [&](const auto& entry) { return entry.first == mip; });
        check(pinned != golden.means.end(), "golden pins every mip level mean");
        for (unsigned c = 0; c < 3; ++c) {
            check(relativeError(mean[c], pinned->second[c]) < 1e-5f,
                  "mip level mean matches the pinned golden");
            // Box filtering preserves the integrated radiance, so a specular
            // consumer that reaches the coarsest level sees the same energy.
            check(std::abs(mean[c] - baseMean[c]) < 2e-3f,
                  "mip chain preserves the mean radiance of the probe");
        }
    }

    std::cout << "PASS: " << checks << " HDRI_ENV_V1 checks\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "motion_hdri: " << error.what() << '\n';
    return 1;
}
