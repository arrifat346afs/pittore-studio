#include "engine/io/af_layers.h"
#include "engine/io/af.h"
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


std::pair<double, double> distortSource(const Distort& d, double x, double y) {
    const double dx = x - d.cx, dy = y - d.cy;
    const double r = std::hypot(dx, dy);
    switch (d.kind) {
        case DistortKind::Twirl: {
            if (r >= d.radius || d.radius <= 0.0) return {x, y};
            const double ease = 1.0 - r / d.radius;
            const double turn = (d.angleDeg * ease * ease) * (kPi / 180.0);
            const double a = std::atan2(dy, dx) - turn;
            return {d.cx + r * std::cos(a), d.cy + r * std::sin(a)};
        }
        case DistortKind::Pinch: {
            if (r >= d.radius || d.radius <= 0.0 || r == 0.0) return {x, y};
            const double ease = 1.0 - r / d.radius;
            const double scale = 1.0 - d.amount * ease * ease;
            return {d.cx + dx * scale, d.cy + dy * scale};
        }
        case DistortKind::Spherical: {
            if (r >= d.radius || d.radius <= 0.0 || r == 0.0) return {x, y};
            const double t = r / d.radius;
            const double full = d.amount >= 0.0
                                    ? d.radius * (2.0 / kPi) * std::asin(t)
                                    : d.radius * std::sin((kPi / 2.0) * t);
            const double sr = r + std::abs(d.amount) * (full - r);
            const double scale = sr / r;
            return {d.cx + dx * scale, d.cy + dy * scale};
        }
        case DistortKind::Ripple: {
            if (d.intensity <= 0.0 || r == 0.0) return {x, y};
            const double wavelength = 1440.0 / d.intensity;
            const double amp =
                kRippleAmplitude * std::pow(d.intensity / 100.0, kRippleExponent);
            const double turn = (amp * std::sin(kTau * r / wavelength)) * (kPi / 180.0);
            const double a = std::atan2(dy, dx) + turn;
            return {d.cx + r * std::cos(a), d.cy + r * std::sin(a)};
        }
        case DistortKind::Lens: {
            if (d.radX <= 0.0 || d.radY <= 0.0) return {x, y};
            const double rho = std::hypot(dx / d.radX, dy / d.radY);
            const double scale = 1.0 - d.amount * (1.0 - rho / std::sqrt(2.0));
            return {d.cx + dx * scale, d.cy + dy * scale};
        }
        case DistortKind::Pixelate:
            return {x, y};
    }
    return {x, y};
}


std::vector<std::uint8_t> pixelateFilter(std::uint32_t width, std::uint32_t height,
                                         const std::vector<std::uint8_t>& pixels, double size) {
    const std::size_t w = width, h = height;
    const std::int64_t q =
        static_cast<std::int64_t>(std::clamp(std::round(size), 1.0, 4294967295.0));
    if (q <= 1) return pixels;
    const std::int64_t half = q / 2;
    auto block = [&](std::int64_t i) -> std::pair<std::int64_t, std::int64_t> {
        const std::int64_t k = floorDiv(i + half, q);
        return {k * q - half, k * q - half + q};
    };
    auto span = [&](std::int64_t lo, std::int64_t hi, std::int64_t n,
                    double& cov) -> std::pair<std::int64_t, std::int64_t> {
        const std::int64_t inside =
            std::max<std::int64_t>(0, std::min(hi, n) - std::max(lo, std::int64_t(0)));
        if (lo < 0) {
            cov = static_cast<double>(inside) / static_cast<double>(hi - lo);
            return {0, 1};
        }
        if (hi > n) {
            cov = static_cast<double>(inside) / static_cast<double>(hi - lo);
            return {n - 1, n};
        }
        cov = 1.0;
        return {lo, hi};
    };
    std::vector<std::pair<std::int64_t, std::int64_t>> xb(w);
    std::vector<double> xc(w);
    for (std::size_t x = 0; x < w; ++x) {
        auto [lo, hi] = block(static_cast<std::int64_t>(x));
        xb[x] = span(lo, hi, static_cast<std::int64_t>(w), xc[x]);
    }
    std::vector<std::uint8_t> out(w * h * 4, 0);
    for (std::size_t y = 0; y < h; ++y) {
        auto [ly0, ly1] = block(static_cast<std::int64_t>(y));
        double ycov = 1.0;
        auto [by0, by1] = span(ly0, ly1, static_cast<std::int64_t>(h), ycov);
        for (std::size_t x = 0; x < w; ++x) {
            const auto [bx0, bx1] = xb[x];
            double acc[4] = {0.0, 0.0, 0.0, 0.0};
            for (std::int64_t sy = by0; sy < by1; ++sy)
                for (std::int64_t sx = bx0; sx < bx1; ++sx) {
                    const std::size_t src = (static_cast<std::size_t>(sy) * w +
                                             static_cast<std::size_t>(sx)) * 4;
                    const double a = pixels[src + 3];
                    acc[0] += pixels[src] * a;
                    acc[1] += pixels[src + 1] * a;
                    acc[2] += pixels[src + 2] * a;
                    acc[3] += a;
                }
            const double n = static_cast<double>((bx1 - bx0) * (by1 - by0));
            const std::size_t at = (y * w + x) * 4;
            out[at + 3] = byteClamp(roundTiesEven(acc[3] / n * xc[x] * ycov));
            if (acc[3] > 0.0)
                for (int c = 0; c < 3; ++c)
                    out[at + c] = byteClamp(roundTiesEven(acc[c] / acc[3]));
        }
    }
    return out;
}


std::vector<std::uint8_t> distortApply(std::uint32_t width, std::uint32_t height,
                                       const std::vector<std::uint8_t>& pixels,
                                       const Distort& filter) {
    if (filter.kind == DistortKind::Pixelate)
        return pixelateFilter(width, height, pixels, filter.size);
    const std::size_t w = width, h = height;
    const double fw = width, fh = height;
    auto fetch = [&](std::int64_t x, std::int64_t y) -> std::array<float, 4> {
        if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(w) ||
            y >= static_cast<std::int64_t>(h))
            return {0.0f, 0.0f, 0.0f, 0.0f};
        const std::size_t at =
            (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
        const float a = pixels[at + 3];
        return {pixels[at] * a, pixels[at + 1] * a, pixels[at + 2] * a, a};
    };
    std::vector<std::uint8_t> out(w * h * 4, 0);
    for (std::size_t y = 0; y < h; ++y) {
        for (std::size_t x = 0; x < w; ++x) {
            const auto [sx, sy] =
                distortSource(filter, static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5);
            if (!(std::isfinite(sx) && std::isfinite(sy)) || sx < -1.0 || sy < -1.0 ||
                sx > fw + 1.0 || sy > fh + 1.0)
                continue;
            const double fx = sx - 0.5, fy = sy - 0.5;
            const double x0d = std::floor(fx), y0d = std::floor(fy);
            const std::int64_t x0 = static_cast<std::int64_t>(x0d);
            const std::int64_t y0 = static_cast<std::int64_t>(y0d);
            const float wx = static_cast<float>(fx - x0d), wy = static_cast<float>(fy - y0d);
            const auto p00 = fetch(x0, y0), p10 = fetch(x0 + 1, y0);
            const auto p01 = fetch(x0, y0 + 1), p11 = fetch(x0 + 1, y0 + 1);
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int c = 0; c < 4; ++c) {
                const float top = p00[c] + (p10[c] - p00[c]) * wx;
                const float bot = p01[c] + (p11[c] - p01[c]) * wx;
                acc[c] = top + (bot - top) * wy;
            }
            const float a = acc[3];
            const std::size_t at = (y * w + x) * 4;
            out[at + 3] = byteClamp(static_cast<double>(a) + 0.5);
            if (a > 0.0f)
                for (int c = 0; c < 3; ++c)
                    out[at + c] = byteClamp(static_cast<double>(acc[c] / a) + 0.5);
        }
    }
    return out;
}

}  // namespace af_detail
}  // namespace pittore::io
