// Shared helpers for the Affinity probe-card tests: locate a probe, read it,
// decode its embedded document thumbnail (the render the importer is judged
// against), composite the recovered layers over white and measure the RMS
// difference.
//
// Probe cards are external fixtures fetched separately: PITTORE_AF_PROBE_DIR
// points at a directory of .af probe files. Nothing is baked into the source.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "engine/io/af.h"
#include "engine/io/af_layers.h"

namespace pittore::probe {

inline std::string probeDir() {
    const char* env = std::getenv("PITTORE_AF_PROBE_DIR");
    return (env && *env) ? std::string(env) : std::string();
}

// Sibling directory of sample documents, also fetched separately.
inline std::string afDesignDir() {
    const char* env = std::getenv("PITTORE_AFDESIGN_DIR");
    return (env && *env) ? std::string(env) : std::string();
}

inline std::optional<std::vector<std::uint8_t>> readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                     std::istreambuf_iterator<char>());
}

// The document thumbnail ("#Inf" → "Thmb"). The archive also stores other
// previews at the same size earlier in the tail, so picking the largest is not
// enough.
inline std::optional<std::vector<std::uint8_t>> thumbPng(
    const std::vector<std::uint8_t>& b) {
    static const std::uint8_t kSig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    const std::uint8_t kThmb[8] = {0xff, 0xff, 0xff, 0xff, 'T', 'h', 'm', 'b'};
    for (std::size_t i = 0; i + 16 <= b.size(); ++i) {
        if (std::memcmp(&b[i], kThmb, 8) != 0) continue;
        const std::uint32_t block =
            std::uint32_t(b[i + 12]) | (std::uint32_t(b[i + 13]) << 8) |
            (std::uint32_t(b[i + 14]) << 16) | (std::uint32_t(b[i + 15]) << 24);
        const std::size_t end = std::min<std::size_t>(b.size(), i + 16 + block);
        for (std::size_t k = i + 16; k + 8 <= end; ++k)
            if (std::memcmp(&b[k], kSig, 8) == 0)
                return std::vector<std::uint8_t>(b.begin() + k, b.begin() + end);
    }
    return std::nullopt;
}

inline std::optional<pittore::io::AfImage> decodeThumb(
    const std::vector<std::uint8_t>& b) {
    if (auto p = thumbPng(b)) {
        pittore::io::AfImage img;
        if (pittore::io::afDecodePngRgba16(p->data(), p->size(), img.width, img.height,
                                            img.rgba))
            return img;
    }
    return pittore::io::afDecode(b);
}

struct Canvas {
    int w = 0, h = 0;
    std::vector<std::uint8_t> px;  // RGB
};

// Composite the recovered layers (file order = bottom to top) over white,
// straight-alpha source-over scaled by each layer's opacity.
inline Canvas composite(const pittore::io::AfLayersDoc& doc) {
    Canvas c;
    c.w = static_cast<int>(doc.width);
    c.h = static_cast<int>(doc.height);
    c.px.assign(static_cast<std::size_t>(c.w) * c.h * 3, 255);
    for (const pittore::io::AfLayer& l : doc.layers) {
        if (l.isGroup || !l.visible || l.rgba.empty() || l.width == 0 || l.height == 0)
            continue;
        for (std::uint32_t y = 0; y < l.height; ++y) {
            const int dy = l.top + static_cast<int>(y);
            if (dy < 0 || dy >= c.h) continue;
            for (std::uint32_t x = 0; x < l.width; ++x) {
                const int dx = l.left + static_cast<int>(x);
                if (dx < 0 || dx >= c.w) continue;
                const std::uint16_t* s =
                    &l.rgba[(static_cast<std::size_t>(y) * l.width + x) * 4];
                std::uint8_t* d = &c.px[(static_cast<std::size_t>(dy) * c.w + dx) * 3];
                const std::uint32_t a =
                    static_cast<std::uint32_t>(s[3] >> 8) * l.opacity / 255u;
                for (int i = 0; i < 3; ++i) {
                    const std::uint32_t sv = s[i] >> 8;
                    d[i] = static_cast<std::uint8_t>((sv * a + d[i] * (255u - a)) / 255u);
                }
            }
        }
    }
    return c;
}

// Bilinear resize of the 8-bit RGB canvas to the thumbnail's size, used only
// when the thumbnail is not already at document resolution.
inline std::vector<std::uint8_t> resizeRgb(const Canvas& src, int tw, int th) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(tw) * th * 3);
    for (int y = 0; y < th; ++y) {
        const double sy = (y + 0.5) * src.h / th - 0.5;
        const int y0 = std::clamp(static_cast<int>(std::floor(sy)), 0, src.h - 1);
        const int y1 = std::min(y0 + 1, src.h - 1);
        const double fy = std::clamp(sy - y0, 0.0, 1.0);
        for (int x = 0; x < tw; ++x) {
            const double sx = (x + 0.5) * src.w / tw - 0.5;
            const int x0 = std::clamp(static_cast<int>(std::floor(sx)), 0, src.w - 1);
            const int x1 = std::min(x0 + 1, src.w - 1);
            const double fx = std::clamp(sx - x0, 0.0, 1.0);
            for (int i = 0; i < 3; ++i) {
                const double a = src.px[(static_cast<std::size_t>(y0) * src.w + x0) * 3 + i];
                const double b = src.px[(static_cast<std::size_t>(y0) * src.w + x1) * 3 + i];
                const double c = src.px[(static_cast<std::size_t>(y1) * src.w + x0) * 3 + i];
                const double d = src.px[(static_cast<std::size_t>(y1) * src.w + x1) * 3 + i];
                const double top = a + (b - a) * fx;
                const double bot = c + (d - c) * fx;
                out[(static_cast<std::size_t>(y) * tw + x) * 3 + i] =
                    static_cast<std::uint8_t>(
                        std::clamp(top + (bot - top) * fy, 0.0, 255.0) + 0.5);
            }
        }
    }
    return out;
}

// RMS of the composited layers against the thumbnail, both read as
// straight-alpha RGB composited over white.
inline double thumbRms(const pittore::io::AfLayersDoc& doc,
                       const pittore::io::AfImage& thumb) {
    const int tw = static_cast<int>(thumb.width);
    const int th = static_cast<int>(thumb.height);
    const Canvas ours = composite(doc);
    std::vector<std::uint8_t> cmp = ours.px;
    if (tw != ours.w || th != ours.h) cmp = resizeRgb(ours, tw, th);

    double sum = 0.0;
    std::uint64_t n = 0;
    const std::size_t np =
        std::min<std::size_t>(static_cast<std::size_t>(tw) * th, thumb.rgba.size() / 4);
    for (std::size_t p = 0; p < np; ++p) {
        const std::uint16_t* t = &thumb.rgba[p * 4];
        const double ta = static_cast<double>(t[3] >> 8) / 255.0;
        for (int k = 0; k < 3; ++k) {
            const double tv = static_cast<double>(t[k] >> 8) * ta + 255.0 * (1.0 - ta);
            const double d = static_cast<double>(cmp[p * 3 + k]) - tv;
            sum += d * d;
            ++n;
        }
    }
    return n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
}

}  // namespace pittore::probe
