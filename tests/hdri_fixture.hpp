#pragma once

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
} // namespace chrononmotion_test
