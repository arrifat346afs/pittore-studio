#pragma once
#include <cstdint>
#include <vector>

namespace pittore::io {

// WebP via libwebp (needs PITTORE_WEBP).
// Decode gives RGBA16 (8-bit x257). Encode drops to 8-bit; lossless is exact
// only for 8-bit-exact sources.

// Reads WebP into RGBA16. False if not decodable.
bool webpDecodeRgba16(const std::vector<std::uint8_t>& data,
                      std::uint32_t& width, std::uint32_t& height,
                      std::vector<std::uint16_t>& rgba);

// Writes RGBA16 (>>8) as WebP. Lossless = VP8L; lossy uses quality 0..100.
// Replaces `out`.
bool webpEncodeRgba16(std::uint32_t width, std::uint32_t height, bool lossless,
                      float quality, const std::uint16_t* rgba,
                      std::vector<std::uint8_t>& out);

}  // namespace pittore::io