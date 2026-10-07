// Appearance RGBA16 -> ink planes. Strips the RGB triplet of each pixel
// into a contiguous device-domain run (so the profiled transform pays one
// cmsDoTransform call per chunk, not per pixel), separates, then interleaves
// the inks back out. Alpha is copied verbatim when requested.

#include "engine/color/separate.h"

#include <algorithm>
#include <cstddef>

#include "engine/color/icc_convert.h"

namespace pittore::color {
namespace {

// Chunk bounds: 262144 pixels * (3+4) * 2 B ≈ 3.5 MB of scratch, and a
// batched LCMS call that stays in cache.
constexpr std::size_t kChunk = 1u << 18;

}  // namespace

void separateRgba16(const std::uint16_t* rgba, std::size_t pixels,
                    const std::uint8_t* icc, std::size_t iccLen,
                    bool withAlpha, std::vector<std::uint16_t>& out) {
    const std::size_t outStride = withAlpha ? 5u : 4u;
    out.assign(pixels * outStride, 0);
    if (!rgba || pixels == 0) return;

    SrgbToCmyk sep(icc, iccLen);

    std::vector<std::uint16_t> rgb(kChunk * 3);
    std::vector<std::uint16_t> ink(kChunk * 4);
    for (std::size_t base = 0; base < pixels; base += kChunk) {
        const std::size_t n = std::min(kChunk, pixels - base);
        for (std::size_t i = 0; i < n; ++i) {
            const std::uint16_t* px = rgba + (base + i) * 4;
            rgb[i * 3 + 0] = px[0];
            rgb[i * 3 + 1] = px[1];
            rgb[i * 3 + 2] = px[2];
        }
        sep.convert16(rgb.data(), ink.data(), n);
        for (std::size_t i = 0; i < n; ++i) {
            std::uint16_t* dst = out.data() + (base + i) * outStride;
            const std::uint16_t* src = ink.data() + i * 4;
            dst[0] = src[0];
            dst[1] = src[1];
            dst[2] = src[2];
            dst[3] = src[3];
            if (withAlpha) dst[4] = rgba[(base + i) * 4 + 3];
        }
    }
}

}  // namespace pittore::color
