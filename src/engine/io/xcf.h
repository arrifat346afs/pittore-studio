#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pittore::io {

// XCF flattened to RGBA16 (8-bit x257). Indexed goes via colormap.
struct XcfImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int depth = 8;      // source depth (XCF v1xx is always 8)
    int baseType = 0;   // 0 RGB, 1 grayscale, 2 indexed
    std::vector<std::uint16_t> rgba;  // width*height*4, straight alpha
};

// Reads XCF (v0/v1, 8-bit; raw or RLE). Nullopt on failure.
std::optional<XcfImage> xcfDecode(const std::vector<std::uint8_t>& data,
                                  std::string* error = nullptr);

// Writes RGBA16 as single-layer RLE XCF. Nullopt on bad input.
std::optional<std::vector<std::uint8_t>> xcfEncodeRgba(std::uint32_t width,
                                                       std::uint32_t height,
                                                       const std::uint16_t* rgba);

}  // namespace pittore::io