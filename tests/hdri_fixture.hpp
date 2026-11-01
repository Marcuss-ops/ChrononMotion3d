#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

namespace chrononmotion_test {
inline std::vector<std::byte> constantRadianceHdr() {
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 4 +X 8\n";
    std::vector<std::byte> bytes;
    for (char c : header) bytes.push_back(static_cast<std::byte>(c));
    for (unsigned i = 0; i < 8 * 4; ++i) {
        bytes.push_back(std::byte{128});
        bytes.push_back(std::byte{0});
        bytes.push_back(std::byte{0});
        bytes.push_back(std::byte{129});
    }
    return bytes;
}

/// Analytic radiance model of `linearGradientRadianceHdr`.
///
/// The image is not a photograph and not a downscaled capture: every texel is
/// `base[channel] + slope[channel] * direction[channel]`, so the irradiance SH
/// has a closed form (only the L00 and L1 bands are non-zero) and every
/// cubemap texel has an exact expected value. That is what makes a specular
/// golden quantitative instead of a screenshot of whatever the code emits.
struct LinearGradientRadiance {
    static constexpr unsigned width = 64;
    static constexpr unsigned height = 32;
    static constexpr std::array<float, 3> base{0.55f, 0.65f, 0.75f};
    static constexpr std::array<float, 3> slope{0.35f, 0.30f, 0.20f};

    /// Direction of an equirect texel, matching the reader's integrator:
    /// phi sweeps -pi..pi with x, theta sweeps 0..pi with y, y is up.
    [[nodiscard]] static std::array<float, 3> direction(unsigned x, unsigned y) {
        constexpr double kPi = 3.14159265358979323846;
        const double theta = kPi * (static_cast<double>(y) + .5) / height;
        const double phi = 2.0 * kPi * (static_cast<double>(x) + .5) / width - kPi;
        return {static_cast<float>(std::cos(phi) * std::sin(theta)),
                static_cast<float>(std::cos(theta)),
                static_cast<float>(std::sin(phi) * std::sin(theta))};
    }

    [[nodiscard]] static std::array<float, 3> radiance(const std::array<float, 3>& d) {
        return {base[0] + slope[0] * d[0], base[1] + slope[1] * d[1], base[2] + slope[2] * d[2]};
    }

    /// Closed-form Lambert-convolved SH of `radiance`, in the reader's ordering
    /// [L00, L1-1(y), L10(z), L11(x), ...]. Every band above L1 integrates to
    /// zero against a linear radiance field.
    [[nodiscard]] static std::array<float, 27> irradianceSH() {
        constexpr double kPi = 3.14159265358979323846;
        constexpr double kSh0 = .2820947918;
        constexpr double kSh1 = .4886025119;
        constexpr double kSphere = 4.0 * kPi;      // integral of dOmega
        constexpr double kAxis = 4.0 * kPi / 3.0;  // integral of a squared axis
        constexpr double kBand0 = kPi;             // convolution of L00
        constexpr double kBand1 = 2.0 * kPi / 3.0; // convolution of L1
        std::array<float, 27> coefficients{};
        for (unsigned channel = 0; channel < 3; ++channel) {
            const double constant = base[channel];
            const double gradient = slope[channel];
            coefficients[0 + channel] =
                static_cast<float>(kBand0 * kSh0 * constant * kSphere);
            // Channel c follows axis c: red the X band (L11), green the Y band
            // (L1-1), blue the Z band (L10). The other four L1 slots stay zero.
            const unsigned band = channel == 0 ? 3u : channel == 1 ? 1u : 2u;
            coefficients[band * 3 + channel] =
                static_cast<float>(kBand1 * kSh1 * gradient * kAxis);
        }
        return coefficients;
    }
};

/// Encode `linearGradientRadianceHdr` as flat (non-RLE) Radiance RGBE.
inline std::vector<std::byte> linearGradientRadianceHdr() {
    const std::string header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 32 +X 64\n";
    std::vector<std::byte> bytes;
    bytes.reserve(header.size() + LinearGradientRadiance::width * LinearGradientRadiance::height * 4);
    for (char c : header) bytes.push_back(static_cast<std::byte>(c));
    for (unsigned y = 0; y < LinearGradientRadiance::height; ++y) {
        for (unsigned x = 0; x < LinearGradientRadiance::width; ++x) {
            const auto rgb = LinearGradientRadiance::radiance(LinearGradientRadiance::direction(x, y));
            const float peak = std::max({rgb[0], rgb[1], rgb[2]});
            if (peak < 1e-32f) {
                for (int i = 0; i < 4; ++i) bytes.push_back(std::byte{0});
                continue;
            }
            int exponent = 0;
            const float mantissa = std::frexp(peak, &exponent);
            const float scale = mantissa * 256.f / peak;
            const auto channel = [scale](float value) {
                return static_cast<std::byte>(
                    std::clamp(static_cast<int>(value * scale), 0, 255));
            };
            bytes.push_back(channel(rgb[0]));
            bytes.push_back(channel(rgb[1]));
            bytes.push_back(channel(rgb[2]));
            bytes.push_back(static_cast<std::byte>(exponent + 128));
        }
    }
    return bytes;
}
} // namespace chrononmotion_test
