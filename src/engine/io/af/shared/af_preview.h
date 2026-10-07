#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/io/af.h"

namespace pittore::io {
namespace af_preview {

const std::uint8_t kPngMagic[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
const std::uint8_t kZstdMagic[4] = {0x28, 0xb5, 0x2f, 0xfd};

// (the raster tile frames are exactly 64 KiB for 8-bit RGB tiles).
constexpr std::uint64_t kMetaMinSize = 200 * 1024ull;
constexpr std::uint64_t kMaxDecompressed = 1ull << 30;   // 1 GiB safety cap
constexpr std::uint32_t kMaxPixels = 1u << 28;           // 256 M samples cap
constexpr std::uint32_t kMaxPngDimension = 1u << 20;

std::uint32_t u32be(const std::uint8_t* p);
std::uint32_t u32le(const std::uint8_t* p);
bool hasBytes(const std::vector<std::uint8_t>& b, const std::uint8_t* m,
              std::size_t mlen, std::size_t at);
std::string jsonTailString(const std::vector<std::uint8_t>& b, const char* key);
std::uint32_t jsonTailU32(const std::vector<std::uint8_t>& b, const char* key);
void walkFrames(const std::vector<std::uint8_t>& b, AfDocument& doc);

}  // namespace af_preview
}  // namespace pittore::io
