#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "engine/io/af.h"

namespace pittore::io {
namespace af_preview {

struct PngInfo {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    int bitDepth = 8;
    int colorType = 0;
    int interlace = 0;
    std::uint32_t dpmX = 0;  // pHYs pixels-per-metre
    std::uint32_t dpmY = 0;
    std::vector<std::uint8_t> inflated;  // concatenated IDAT scanline data
    std::vector<std::uint8_t> palette;   // PLTE rgb triplets
    std::vector<std::uint8_t> trns;      // tRNS entries (alpha keys)
};

struct PixelPass {
    std::uint32_t x0, y0, dx, dy;
};

const PixelPass kAdam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4},
                             {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};

bool inflateIdat(const std::vector<std::uint8_t>& src, std::vector<std::uint8_t>& out,
                 std::string* why);
bool parsePng(const std::vector<std::uint8_t>& b, std::size_t start, PngInfo& png,
              std::size_t* endOut, std::string* why);
int channelsOf(int colorType);
std::uint8_t rep8(std::uint32_t v, int depth);
std::uint32_t scale16(std::uint32_t v, int depth);
void readSamples(const std::uint8_t* row, std::size_t px, int bitDepth, int channels,
                 std::uint32_t* s);
void expandPixel(std::uint32_t* s, const PngInfo& png, std::uint16_t rgba4[4]);
bool unfilterRow(std::uint8_t* row, const std::uint8_t* prev, std::size_t len,
                 std::size_t bppBytes);
std::size_t passSize(std::uint32_t total, std::uint32_t start, std::uint32_t step);
bool decodePass(const PngInfo& png, std::size_t dataPos, std::size_t pw, std::size_t ph,
                std::size_t rowBytes, std::size_t bppBytes, const PixelPass& pass,
                std::vector<std::uint16_t>& rgba);
bool decodePngToRgba16(const PngInfo& png, std::vector<std::uint16_t>& rgba,
                       std::string* why);

}  // namespace af_preview
}  // namespace pittore::io
