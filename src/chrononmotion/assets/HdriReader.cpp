#include "chrononmotion/assets/HdriReader.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace chrononmotion {
namespace {
constexpr double kPi = 3.1415926535897932384626433832795;
constexpr std::size_t kHeaderLimit = 65536;
constexpr std::size_t kHardMaxEncodedBytes = 64u * 1024u * 1024u;
constexpr std::uint32_t kHardMaxWidth = 8192;
constexpr std::uint32_t kHardMaxHeight = 4096;
constexpr std::uint64_t kHardMaxPixels = 16u * 1024u * 1024u;
constexpr std::uint32_t kHardMaxFaceSize = 1024;
constexpr std::uint64_t kHardMaxOutputFloats = 32u * 1024u * 1024u;

[[noreturn]] void invalid(const char* reason) {
    throw std::invalid_argument(std::string("hdri: ") + reason);
}

unsigned byte(std::span<const std::byte> data, std::size_t index) {
    if (index >= data.size()) invalid("truncated Radiance RGBE payload");
    return std::to_integer<unsigned char>(data[index]);
}

std::string line(std::span<const std::byte> data, std::size_t& offset) {
    const std::size_t begin = offset;
    while (offset < data.size() && byte(data, offset) != '\n') {
        if (offset - begin >= kHeaderLimit) invalid("header line exceeds the limit");
        ++offset;
    }
    if (offset >= data.size()) invalid("unterminated Radiance header");
    std::size_t end = offset++;
    if (end > begin && byte(data, end - 1) == '\r') --end;
    std::string result;
    result.reserve(end - begin);
    for (std::size_t i = begin; i < end; ++i) result.push_back(static_cast<char>(byte(data, i)));
    return result;
}

struct Rgbe { unsigned char r, g, b, e; };

float toFloat(unsigned char channel, unsigned char exponent) {
    if (exponent == 0 || channel == 0) return 0.f;
    return std::ldexp(static_cast<float>(channel), static_cast<int>(exponent) - 136);
}

std::vector<Rgbe> decodePixels(std::span<const std::byte> bytes, std::size_t& offset,
                               std::uint32_t width, std::uint32_t height) {
    const std::uint64_t total64 = static_cast<std::uint64_t>(width) * height;
    if (total64 > std::numeric_limits<std::size_t>::max()) invalid("pixel count overflows this platform");
    std::vector<Rgbe> pixels(static_cast<std::size_t>(total64));
    std::vector<unsigned char> scanline(static_cast<std::size_t>(width) * 4);
    for (std::uint32_t y = 0; y < height; ++y) {
        bool modernRle = false;
        if (width >= 8 && offset + 4 <= bytes.size()) {
            const unsigned a = byte(bytes, offset), b = byte(bytes, offset + 1);
            const unsigned encodedWidth = (byte(bytes, offset + 2) << 8) | byte(bytes, offset + 3);
            modernRle = a == 2 && b == 2 && (byte(bytes, offset + 2) & 0x80U) == 0 && encodedWidth == width;
        }
        if (modernRle) {
            offset += 4;
            for (unsigned channel = 0; channel < 4; ++channel) {
                std::size_t x = 0;
                while (x < width) {
                    const unsigned count = byte(bytes, offset++);
                    if (count > 128) {
                        const std::size_t run = count - 128;
                        if (run == 0 || run > width - x) invalid("Radiance RLE run exceeds scanline");
                        const unsigned value = byte(bytes, offset++);
                        std::fill_n(scanline.begin() + static_cast<std::ptrdiff_t>(channel * width + x),
                                    run, static_cast<unsigned char>(value));
                        x += run;
                    } else {
                        if (count == 0 || count > width - x) invalid("invalid Radiance RLE literal count");
                        for (unsigned i = 0; i < count; ++i)
                            scanline[channel * width + x++] = static_cast<unsigned char>(byte(bytes, offset++));
                    }
                }
            }
            for (std::uint32_t x = 0; x < width; ++x)
                pixels[static_cast<std::size_t>(y) * width + x] = {
                    scanline[x], scanline[width + x], scanline[2 * width + x], scanline[3 * width + x]};
        } else {
            for (std::uint32_t x = 0; x < width; ++x)
                pixels[static_cast<std::size_t>(y) * width + x] = {
                    static_cast<unsigned char>(byte(bytes, offset++)),
                    static_cast<unsigned char>(byte(bytes, offset++)),
                    static_cast<unsigned char>(byte(bytes, offset++)),
                    static_cast<unsigned char>(byte(bytes, offset++))};
        }
    }
    if (offset != bytes.size()) invalid("trailing bytes after Radiance pixel data");
    return pixels;
}

using RGB = std::array<float, 3>;

std::vector<RGB> toLinear(const std::vector<Rgbe>& source) {
    std::vector<RGB> result;
    result.reserve(source.size());
    for (const auto& p : source) result.push_back({toFloat(p.r, p.e), toFloat(p.g, p.e), toFloat(p.b, p.e)});
    return result;
}

void normalize(RGB& d) {
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length == 0.f) return;
    for (float& value : d) value /= length;
}

RGB cubeDirection(unsigned face, float u, float v) {
    RGB d{};
    switch (face) {
        case 0: d = {1.f, -v, -u}; break;
        case 1: d = {-1.f, -v, u}; break;
        case 2: d = {u, 1.f, v}; break;
        case 3: d = {u, -1.f, -v}; break;
        case 4: d = {u, -v, 1.f}; break;
        default: d = {-u, -v, -1.f}; break;
    }
    normalize(d);
    return d;
}

RGB sampleEquirect(const std::vector<RGB>& pixels, std::uint32_t width, std::uint32_t height,
                   const RGB& direction) {
    double u = std::atan2(direction[2], direction[0]) / (2.0 * kPi) + .5;
    u -= std::floor(u);
    const double v = std::acos(std::clamp(static_cast<double>(direction[1]), -1.0, 1.0)) / kPi;
    const double px = u * width - .5;
    const double py = std::clamp(v * height - .5, 0.0, static_cast<double>(height - 1));
    const int x0raw = static_cast<int>(std::floor(px));
    const int x0 = (x0raw % static_cast<int>(width) + static_cast<int>(width)) % static_cast<int>(width);
    const int x1 = (x0 + 1) % static_cast<int>(width);
    const auto y0 = static_cast<std::uint32_t>(std::floor(py));
    const auto y1 = std::min(y0 + 1, height - 1);
    const float tx = static_cast<float>(px - std::floor(px));
    const float ty = static_cast<float>(py - y0);
    RGB out{};
    for (unsigned c = 0; c < 3; ++c) {
        const float a = pixels[static_cast<std::size_t>(y0) * width + static_cast<unsigned>(x0)][c] * (1.f - tx) +
                        pixels[static_cast<std::size_t>(y0) * width + static_cast<unsigned>(x1)][c] * tx;
        const float b = pixels[static_cast<std::size_t>(y1) * width + static_cast<unsigned>(x0)][c] * (1.f - tx) +
                        pixels[static_cast<std::size_t>(y1) * width + static_cast<unsigned>(x1)][c] * tx;
        out[c] = a * (1.f - ty) + b * ty;
    }
    return out;
}

std::array<double, 9> shBasis(const RGB& d) {
    const double x = d[0], y = d[1], z = d[2];
    return {.2820947918, .4886025119 * y, .4886025119 * z, .4886025119 * x,
            1.0925484306 * x * y, 1.0925484306 * y * z,
            .3153915653 * (3.0 * z * z - 1.0), 1.0925484306 * x * z,
            .5462742153 * (x * x - y * y)};
}

void appendU32(std::vector<std::byte>& out, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 255));
}
void appendF32(std::vector<std::byte>& out, float value) { appendU32(out, std::bit_cast<std::uint32_t>(value)); }
} // namespace

HdriEnvironment decodeHdriRadiance(std::span<const std::byte> bytes, const HdriReadPolicy& policy) {
    if (!policy.enabled) invalid("HDRI import is disabled by policy");
    if (policy.maxWidth == 0 || policy.maxHeight == 0 || policy.maxPixels == 0 ||
        policy.maxFaceSize == 0 || policy.maxOutputFloats == 0) invalid("invalid zero-sized read policy");
    if (bytes.empty() || bytes.size() > std::min(policy.maxEncodedBytes, kHardMaxEncodedBytes))
        invalid("encoded HDRI exceeds the byte budget");
    std::size_t offset = 0;
    const std::string signature = line(bytes, offset);
    if (signature != "#?RADIANCE" && signature != "#?RGBE") invalid("unsupported Radiance signature");
    bool format = false;
    bool headerDone = false;
    while (offset < bytes.size() && offset <= kHeaderLimit) {
        const std::string header = line(bytes, offset);
        if (header.empty()) { headerDone = true; break; }
        if (header == "FORMAT=32-bit_rle_rgbe") format = true;
        else if (header[0] != '#') invalid("unsupported or malformed Radiance header field");
    }
    if (!headerDone || !format) invalid("missing supported RGBE format declaration");
    const std::string resolution = line(bytes, offset);
    std::istringstream dimensions(resolution);
    std::string ySign, xSign;
    std::uint32_t width = 0, height = 0;
    if (!(dimensions >> ySign >> height >> xSign >> width) || ySign != "-Y" || xSign != "+X")
        invalid("only canonical -Y height +X width orientation is supported");
    std::string trailing;
    if (dimensions >> trailing) invalid("unexpected resolution-line tokens");
    if (width == 0 || height == 0 || width > std::min(policy.maxWidth, kHardMaxWidth) ||
        height > std::min(policy.maxHeight, kHardMaxHeight) ||
        static_cast<std::uint64_t>(width) * height > std::min(policy.maxPixels, kHardMaxPixels))
        invalid("image dimensions exceed the configured limits");

    const auto rgbe = decodePixels(bytes, offset, width, height);
    const auto image = toLinear(rgbe);
    HdriEnvironment result;
    result.sourceWidth = width;
    result.sourceHeight = height;
    result.faceSize = std::bit_floor(std::min({policy.maxFaceSize, kHardMaxFaceSize,
                                               std::max(1u, height / 2)}));
    if (result.faceSize == 0) invalid("computed cubemap face size is zero");
    std::uint64_t outputFloats = 0;
    for (std::uint32_t size = result.faceSize;; size = std::max(1u, size / 2)) {
        outputFloats += static_cast<std::uint64_t>(6) * size * size * 3;
        if (size == 1) break;
    }
    if (outputFloats > std::min(policy.maxOutputFloats, kHardMaxOutputFloats))
        invalid("cubemap and mip output exceed the sample budget");

    const double dOmega = 2.0 * kPi * kPi / (static_cast<double>(width) * height);
    for (std::uint32_t y = 0; y < height; ++y) {
        const double theta = kPi * (static_cast<double>(y) + .5) / height;
        const double weight = dOmega * std::sin(theta);
        for (std::uint32_t x = 0; x < width; ++x) {
            const double phi = 2.0 * kPi * (static_cast<double>(x) + .5) / width - kPi;
            const RGB direction{static_cast<float>(std::cos(phi) * std::sin(theta)),
                                static_cast<float>(std::cos(theta)),
                                static_cast<float>(std::sin(phi) * std::sin(theta))};
            const auto basis = shBasis(direction);
            const RGB& color = image[static_cast<std::size_t>(y) * width + x];
            for (unsigned k = 0; k < 9; ++k)
                for (unsigned c = 0; c < 3; ++c)
                    result.irradianceSH[k * 3 + c] += static_cast<float>(basis[k] * color[c] * weight);
        }
    }
    for (unsigned k = 0; k < 9; ++k) {
        const float convolution = k == 0 ? static_cast<float>(kPi) : (k <= 3 ? static_cast<float>(2.0 * kPi / 3.0)
                                                                       : static_cast<float>(kPi / 4.0));
        for (unsigned c = 0; c < 3; ++c) result.irradianceSH[k * 3 + c] *= convolution;
    }

    HdriMipLevel base;
    base.size = result.faceSize;
    for (unsigned face = 0; face < 6; ++face) {
        auto& target = base.faces[face];
        target.resize(static_cast<std::size_t>(base.size) * base.size * 3);
        for (std::uint32_t y = 0; y < base.size; ++y) for (std::uint32_t x = 0; x < base.size; ++x) {
            const float u = (2.f * (static_cast<float>(x) + .5f) / base.size) - 1.f;
            const float v = (2.f * (static_cast<float>(y) + .5f) / base.size) - 1.f;
            const RGB color = sampleEquirect(image, width, height, cubeDirection(face, u, v));
            const std::size_t index = (static_cast<std::size_t>(y) * base.size + x) * 3;
            for (unsigned c = 0; c < 3; ++c) target[index + c] = color[c];
        }
    }
    result.mips.push_back(std::move(base));
    while (result.mips.back().size > 1) {
        const auto& previous = result.mips.back();
        HdriMipLevel next;
        next.size = std::max(1u, previous.size / 2);
        for (unsigned face = 0; face < 6; ++face) {
            auto& target = next.faces[face];
            const auto& source = previous.faces[face];
            target.resize(static_cast<std::size_t>(next.size) * next.size * 3);
            for (std::uint32_t y = 0; y < next.size; ++y) for (std::uint32_t x = 0; x < next.size; ++x) {
                const std::size_t output = (static_cast<std::size_t>(y) * next.size + x) * 3;
                for (unsigned c = 0; c < 3; ++c) {
                    float sum = 0.f;
                    for (unsigned dy = 0; dy < 2; ++dy) for (unsigned dx = 0; dx < 2; ++dx) {
                        const auto sx = std::min(previous.size - 1, x * 2 + dx);
                        const auto sy = std::min(previous.size - 1, y * 2 + dy);
                        sum += source[(static_cast<std::size_t>(sy) * previous.size + sx) * 3 + c];
                    }
                    target[output + c] = sum * .25f;
                }
            }
        }
        result.mips.push_back(std::move(next));
    }
    return result;
}

HdriEnvironment loadHdri(std::string_view uri, const HdriExternalBufferResolver& resolver,
                         const HdriReadPolicy& policy) {
    if (!resolver) invalid("an explicit caller buffer resolver is required");
    const auto bytes = resolver(uri);
    return decodeHdriRadiance(bytes, policy);
}

std::vector<std::byte> HdriEnvironment::serialize() const {
    if (sourceWidth == 0 || sourceHeight == 0 || faceSize == 0 || mips.empty() ||
        sourceWidth > kHardMaxWidth || sourceHeight > kHardMaxHeight ||
        static_cast<std::uint64_t>(sourceWidth) * sourceHeight > kHardMaxPixels ||
        faceSize > kHardMaxFaceSize || mips.front().size != faceSize || mips.size() > 12 ||
        !std::isfinite(rotationRadians) || !std::isfinite(intensity) || intensity < 0.f)
        invalid("cannot serialize an invalid environment value");
    std::vector<std::byte> out;
    constexpr char magic[] = "HDRIENV1";
    for (char c : magic) if (c != '\0') out.push_back(static_cast<std::byte>(c));
    appendU32(out, sourceWidth); appendU32(out, sourceHeight); appendU32(out, faceSize);
    appendU32(out, static_cast<std::uint32_t>(mips.size()));
    appendF32(out, rotationRadians); appendF32(out, intensity);
    for (float value : irradianceSH) {
        if (!std::isfinite(value)) invalid("cannot serialize non-finite SH coefficients");
        appendF32(out, value);
    }
    std::uint32_t expected = faceSize;
    std::uint64_t totalFloats = 0;
    for (std::size_t level = 0; level < mips.size(); ++level) {
        const auto& mip = mips[level];
        if (expected == 1 && level + 1 != mips.size()) invalid("mip chain contains levels after 1x1");
        if (mip.size != expected) invalid("mip dimensions are not a complete halving chain");
        appendU32(out, mip.size);
        const std::size_t expectedFloats = static_cast<std::size_t>(mip.size) * mip.size * 3;
        totalFloats += static_cast<std::uint64_t>(expectedFloats) * 6;
        if (totalFloats > kHardMaxOutputFloats) invalid("serialized environment exceeds output sample budget");
        for (const auto& face : mip.faces) {
            if (face.size() != expectedFloats) invalid("cubemap face has inconsistent sample count");
            for (float value : face) {
                if (!std::isfinite(value)) invalid("cannot serialize non-finite cubemap values");
                appendF32(out, value);
            }
        }
        expected = std::max(1u, expected / 2);
    }
    if (mips.back().size != 1) invalid("mip chain must terminate at one texel");
    return out;
}
} // namespace chrononmotion
