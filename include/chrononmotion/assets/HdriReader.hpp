#ifndef CHRONONMOTION_ASSETS_HDRI_READER_HPP
#define CHRONONMOTION_ASSETS_HDRI_READER_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace chrononmotion {

using HdriExternalBufferResolver =
    std::function<std::vector<std::byte>(std::string_view uri)>;

struct HdriReadPolicy {
    bool enabled{true};
    std::size_t maxEncodedBytes{64u * 1024u * 1024u};
    std::uint32_t maxWidth{8192};
    std::uint32_t maxHeight{4096};
    std::uint64_t maxPixels{16u * 1024u * 1024u};
    std::uint32_t maxFaceSize{1024};
    std::uint64_t maxOutputFloats{32u * 1024u * 1024u};
};

struct HdriMipLevel {
    std::uint32_t size{0};
    // Six faces in +X, -X, +Y, -Y, +Z, -Z order; each is row-major RGB float.
    std::array<std::vector<float>, 6> faces{};
};

struct HdriEnvironment {
    std::uint32_t sourceWidth{0};
    std::uint32_t sourceHeight{0};
    std::uint32_t faceSize{0};
    float rotationRadians{0.f};
    float intensity{1.f};
    std::vector<HdriMipLevel> mips{};
    // Nine Lambert-convolved real SH coefficients, RGB interleaved.
    std::array<float, 27> irradianceSH{};

    /// Deterministic little-endian HDRI_ENV_V1 binary payload.
    [[nodiscard]] std::vector<std::byte> serialize() const;
};

/// Decode an in-memory Radiance RGBE file. No path or URL is opened here.
[[nodiscard]] HdriEnvironment decodeHdriRadiance(
    std::span<const std::byte> bytes, const HdriReadPolicy& policy = {});

/// Resolve bytes through the caller-owned resolver, then decode as Radiance RGBE.
[[nodiscard]] HdriEnvironment loadHdri(
    std::string_view uri, const HdriExternalBufferResolver& resolver,
    const HdriReadPolicy& policy = {});

} // namespace chrononmotion

#endif
