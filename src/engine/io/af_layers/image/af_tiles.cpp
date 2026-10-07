#include "engine/io/af_layers.h"
#include "engine/io/af.h"
#include "engine/core/parallel.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/vector_shape.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>
#ifdef PITTORE_AF
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#endif
#ifdef PITTORE_WEBP
#include "engine/io/webp.h"
#endif
#ifdef PITTORE_JPEG
#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>
#endif
#include "engine/io/af_layers/container/af_archive.h"
#include "engine/io/af_layers/geom/af_geom.h"
#include "engine/io/af_layers/geom/af_homography.h"
#include "engine/io/af_layers/graph/af_graph.h"
#include "engine/io/af_layers/graph/af_graph_parser.h"
#include "engine/io/af_layers/image/af_image.h"
#include "engine/io/af_layers/filter/af_blur.h"
#include "engine/io/af_layers/filter/af_distort.h"
#include "engine/io/af_layers/filter/af_fx.h"
#include "engine/io/af_layers/filter/af_live.h"
#include "engine/io/af_layers/vector/af_shapes.h"
#include "engine/io/af_layers/walker/af_walker.h"

namespace pittore::io {
namespace af_detail {


std::vector<std::pair<std::size_t, std::size_t>> tileOffsets(std::size_t gridWidth,
                                                             std::size_t height) {
    const std::size_t w = gridWidth ? gridWidth : 1;
    std::vector<std::pair<std::size_t, std::size_t>> out;
    for (std::size_t i = 0;; ++i) {
        const std::size_t y = (i / w) * 256;
        if (y >= height) break;
        out.emplace_back((i % w) * 256, y);
    }
    return out;
}


void fillTile(std::vector<std::uint8_t>& plane, std::size_t pitch, std::size_t x, std::size_t y,
              const std::vector<std::uint8_t>& pattern) {
    for (std::size_t row = 0; row < 256; ++row) {
        const std::size_t base = (y + row) * pitch + x;
        if (base + 256 > plane.size()) break;
        for (std::size_t i = 0; i < 256; ++i) plane[base + i] = pattern[i % pattern.size()];
    }
}


void copySourceTile(std::vector<std::uint8_t>& plane, std::size_t pitch, std::size_t rows,
                    std::size_t bps, const Bitmap& source, std::size_t channel, std::size_t x,
                    std::size_t y) {
    if (channel >= 4 || bps == 0) return;
    const std::size_t px0 = x / bps;
    const std::size_t perTile = 256 / bps;
    const std::size_t sw = source.w;
    for (std::size_t ty = 0; ty < 256; ++ty) {
        const std::size_t sy = y + ty;
        if (sy >= source.h || sy >= rows) break;
        for (std::size_t tx = 0; tx < perTile; ++tx) {
            const std::size_t sx = px0 + tx;
            if (sx >= sw) break;
            const std::uint8_t v = source.px[(sy * sw + sx) * 4 + channel];
            const std::size_t at = sy * pitch + x + tx * bps;
            if (bps == 1) {
                plane[at] = v;
            } else if (bps == 2) {
                const std::uint16_t u = std::uint16_t(v) * 257u;
                plane[at] = static_cast<std::uint8_t>(u & 0xff);
                plane[at + 1] = static_cast<std::uint8_t>(u >> 8);
            } else {
                const float f = static_cast<float>(v) / 255.0f;
                std::memcpy(&plane[at], &f, 4);
            }
        }
    }
}


std::optional<std::vector<std::uint8_t>> tilePayload(std::vector<std::uint8_t> data) {
    if (data.size() == 0x10000) return data;
    try {
        Graph g = parseGraph(data);
        Node* root = g.root();
        if (root)
            for (auto& f : root->fields)
                if (f.second.k == Value::K::Blob && f.second.bytes.size() == 0x10000)
                    return f.second.bytes;
    } catch (...) {
    }
    return std::nullopt;
}


Bitmap interleave(const std::vector<std::vector<std::uint8_t>>& planes, std::size_t pitch,
                  std::size_t w, std::size_t h, std::size_t bps, FK kind) {
    Bitmap img;
    img.w = static_cast<std::uint32_t>(w);
    img.h = static_cast<std::uint32_t>(h);
    img.px.assign(w * h * 4, 0);
    auto sample = [&](const std::vector<std::uint8_t>& plane, std::size_t x,
                      std::size_t y) -> float {
        const std::size_t at = y * pitch + x * bps;
        if (bps == 1) return plane[at] / 255.0f;
        if (bps == 2)
            return (std::uint16_t(plane[at]) | (std::uint16_t(plane[at + 1]) << 8)) / 65535.0f;
        float f;
        std::memcpy(&f, &plane[at], 4);
        return std::clamp(f, 0.0f, 1.0f);
    };
    // Rows write disjoint output spans and only ever read the planes, so the
    // reassembly splits across cores. parallel_rows keeps the result
    // bit-identical to the serial loop: one thread per row range, same ops.
    auto row = [&](std::size_t y) {
        for (std::size_t x = 0; x < w; ++x) {
            float r = 0, gg = 0, b = 0, a = 1;
            switch (kind) {
                case FK::Rgba:
                    r = sample(planes[0], x, y);
                    gg = sample(planes[1], x, y);
                    b = sample(planes[2], x, y);
                    a = sample(planes[3], x, y);
                    break;
                case FK::Gray: {
                    const float v = sample(planes[0], x, y);
                    r = gg = b = v;
                    a = sample(planes[1], x, y);
                    break;
                }
                case FK::Mask: {
                    const float v = sample(planes[0], x, y);
                    r = gg = b = v;
                    a = 1.0f;
                    break;
                }
                case FK::Cmyk: {
                    const float c = sample(planes[0], x, y);
                    const float m = sample(planes[1], x, y);
                    const float yl = sample(planes[2], x, y);
                    const float k = sample(planes[3], x, y);
                    r = (1.0f - c) * (1.0f - k);
                    gg = (1.0f - m) * (1.0f - k);
                    b = (1.0f - yl) * (1.0f - k);
                    a = sample(planes[4], x, y);
                    break;
                }
                case FK::Lab: {
                    const float l = sample(planes[0], x, y) * 100.0f;
                    const float ac = sample(planes[1], x, y) * 255.0f - 128.0f;
                    const float bc = sample(planes[2], x, y) * 255.0f - 128.0f;
                    labToSrgb(l, ac, bc, r, gg, b);
                    a = sample(planes[3], x, y);
                    break;
                }
            }
            const std::size_t at = (y * w + x) * 4;
            img.px[at + 0] = static_cast<std::uint8_t>(r * 255.0f + 0.5f);
            img.px[at + 1] = static_cast<std::uint8_t>(gg * 255.0f + 0.5f);
            img.px[at + 2] = static_cast<std::uint8_t>(b * 255.0f + 0.5f);
            img.px[at + 3] = static_cast<std::uint8_t>(a * 255.0f + 0.5f);
        }
    };
    pittore::core::parallel_rows(
        static_cast<std::uint32_t>(h), [&](std::uint32_t y0, std::uint32_t y1) {
            for (std::size_t y = y0; y < y1; ++y) row(y);
        });
    return img;
}

}  // namespace af_detail
}  // namespace pittore::io
