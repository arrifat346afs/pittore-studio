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


std::vector<float> boxKernel(double width) {
    const double half = std::max(width / 2.0, 0.0);
    const std::size_t n = std::min<std::size_t>(static_cast<std::size_t>(std::floor(half)),
                                                std::size_t(1) << 16);
    const float frac = static_cast<float>(half - static_cast<double>(n));
    std::vector<float> k(2 * n + 3, 1.0f);
    k[0] = frac;
    k[2 * n + 2] = frac;
    float sum = 0.0f;
    for (float v : k) sum += v;
    for (float& v : k) v /= sum;
    return k;
}


std::vector<float> box3Kernel(double width) {
    const std::vector<float> k = boxKernel(width);
    auto convolve = [](const std::vector<float>& a, const std::vector<float>& b) {
        std::vector<float> out(a.size() + b.size() - 1, 0.0f);
        for (std::size_t i = 0; i < a.size(); ++i)
            for (std::size_t j = 0; j < b.size(); ++j) out[i + j] += a[i] * b[j];
        return out;
    };
    return convolve(convolve(k, k), k);
}


std::vector<float> toPremultiplied(const std::vector<std::uint8_t>& pixels) {
    std::vector<float> out(pixels.size(), 0.0f);
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) {
        const float a = pixels[i + 3];
        out[i] = pixels[i] * a;
        out[i + 1] = pixels[i + 1] * a;
        out[i + 2] = pixels[i + 2] * a;
        out[i + 3] = a;
    }
    return out;
}


std::uint8_t* writePremultiplied(std::uint8_t* px, const float acc[4]) {
    const float a = acc[3];
    px[3] = byteClamp(static_cast<double>(a) + 0.5);
    if (a > 0.0f)
        for (int c = 0; c < 3; ++c)
            px[c] = byteClamp(static_cast<double>(acc[c] / a) + 0.5);
    return px;
}


std::vector<std::uint8_t> fromPremultiplied(const std::vector<float>& buf) {
    std::vector<std::uint8_t> out(buf.size(), 0);
    for (std::size_t i = 0; i + 3 < buf.size(); i += 4)
        writePremultiplied(&out[i], &buf[i]);
    return out;
}


std::vector<float> blurPass(std::size_t w, std::size_t h, const std::vector<float>& src,
                            const std::vector<float>& kernel, bool horizontal) {
    const std::int64_t n = static_cast<std::int64_t>(kernel.size() / 2);
    std::vector<float> out(src.size(), 0.0f);
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x) {
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t i = 0; i < kernel.size(); ++i) {
                const std::int64_t d = static_cast<std::int64_t>(i) - n;
                const std::int64_t sx =
                    horizontal ? static_cast<std::int64_t>(x) + d : static_cast<std::int64_t>(x);
                const std::int64_t sy =
                    horizontal ? static_cast<std::int64_t>(y) : static_cast<std::int64_t>(y) + d;
                if (sx < 0 || sy < 0 || sx >= static_cast<std::int64_t>(w) ||
                    sy >= static_cast<std::int64_t>(h))
                    continue;
                const std::size_t at = (static_cast<std::size_t>(sy) * w +
                                        static_cast<std::size_t>(sx)) * 4;
                for (int c = 0; c < 4; ++c) acc[c] += src[at + c] * kernel[i];
            }
            const std::size_t at = (y * w + x) * 4;
            for (int c = 0; c < 4; ++c) out[at + c] = acc[c];
        }
    return out;
}


std::vector<std::uint8_t> separableBlur(std::size_t w, std::size_t h,
                                        const std::vector<std::uint8_t>& pixels,
                                        const std::vector<float>& kernel) {
    std::vector<float> buf = toPremultiplied(pixels);
    buf = blurPass(w, h, buf, kernel, true);
    buf = blurPass(w, h, buf, kernel, false);
    return fromPremultiplied(buf);
}


std::array<float, 4> sampleBilinear(std::size_t w, std::size_t h,
                                    const std::vector<std::uint8_t>& pixels, double sx, double sy) {
    auto fetch = [&](std::int64_t x, std::int64_t y) -> std::array<float, 4> {
        if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(w) ||
            y >= static_cast<std::int64_t>(h))
            return {0.0f, 0.0f, 0.0f, 0.0f};
        const std::size_t at =
            (static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4;
        const float a = pixels[at + 3];
        return {pixels[at] * a, pixels[at + 1] * a, pixels[at + 2] * a, a};
    };
    const double fx = sx - 0.5, fy = sy - 0.5;
    const double x0d = std::floor(fx), y0d = std::floor(fy);
    const float tx = static_cast<float>(fx - x0d), ty = static_cast<float>(fy - y0d);
    const std::int64_t x0 = static_cast<std::int64_t>(x0d);
    const std::int64_t y0 = static_cast<std::int64_t>(y0d);
    const auto p00 = fetch(x0, y0), p10 = fetch(x0 + 1, y0);
    const auto p01 = fetch(x0, y0 + 1), p11 = fetch(x0 + 1, y0 + 1);
    std::array<float, 4> out{};
    for (int c = 0; c < 4; ++c) {
        const float top = p00[c] + (p10[c] - p00[c]) * tx;
        const float bot = p01[c] + (p11[c] - p01[c]) * tx;
        out[c] = top + (bot - top) * ty;
    }
    return out;
}

// Average the premultiplied samples a filter gathers for each pixel.
template <class F>

std::vector<std::uint8_t> gatherTaps(std::size_t w, std::size_t h,
                                     const std::vector<std::uint8_t>& pixels, F taps) {
    std::vector<std::uint8_t> out(w * h * 4, 0);
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x) {
            const std::vector<std::pair<double, double>> points =
                taps(static_cast<double>(x) + 0.5, static_cast<double>(y) + 0.5);
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (const auto& p : points) {
                const auto s = sampleBilinear(w, h, pixels, p.first, p.second);
                for (int c = 0; c < 4; ++c) acc[c] += s[c];
            }
            const float n = static_cast<float>(std::max<std::size_t>(points.size(), 1));
            for (int c = 0; c < 4; ++c) acc[c] /= n;
            writePremultiplied(&out[(y * w + x) * 4], acc);
        }
    return out;
}


std::vector<std::uint8_t> unsharpBlur(std::size_t w, std::size_t h,
                                      const std::vector<std::uint8_t>& pixels, double radius,
                                      double factor, double threshold) {
    if (radius < 1.0 || factor == 0.0) return pixels;
    const std::vector<std::uint8_t> blurred =
        separableBlur(w, h, pixels, box3Kernel(2.0 * radius / 3.0));
    std::vector<std::uint8_t> out = pixels;
    const float cut = static_cast<float>(threshold * 255.0);
    for (std::size_t i = 0; i + 3 < out.size(); i += 4)
        for (int c = 0; c < 3; ++c) {
            const float detail = static_cast<float>(pixels[i + c]) - blurred[i + c];
            if (std::abs(detail) <= cut) continue;
            out[i + c] = byteClamp(static_cast<double>(pixels[i + c]) +
                                   factor * static_cast<double>(detail) + 0.5);
        }
    return out;
}


std::vector<std::uint8_t> highPassBlur(std::size_t w, std::size_t h,
                                       const std::vector<std::uint8_t>& pixels, double radius,
                                       bool mono) {
    if (radius < 1.0) return pixels;
    const std::vector<std::uint8_t> blurred =
        separableBlur(w, h, pixels, box3Kernel(2.0 * radius / 3.0));
    std::vector<std::uint8_t> out = pixels;
    for (std::size_t i = 0; i + 3 < out.size(); i += 4) {
        const float d0 = static_cast<float>(pixels[i]) - blurred[i];
        const float d1 = static_cast<float>(pixels[i + 1]) - blurred[i + 1];
        const float d2 = static_cast<float>(pixels[i + 2]) - blurred[i + 2];
        const float flat = (d0 + d1 + d2) / 3.0f;
        for (int c = 0; c < 3; ++c) {
            const float d =
                mono ? flat : static_cast<float>(pixels[i + c]) - blurred[i + c];
            out[i + c] = byteClamp(roundTiesEven(127.5 + static_cast<double>(d) * 0.5));
        }
    }
    return out;
}


std::vector<std::uint8_t> windowMax1D(std::size_t w, std::size_t h,
                                      const std::vector<std::uint8_t>& pixels, std::int64_t r,
                                      bool horizontal) {
    std::vector<std::uint8_t> out(w * h * 4, 0);
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x) {
            std::uint8_t best[4] = {0, 0, 0, 0};
            for (std::int64_t d = -r; d <= r; ++d) {
                const std::int64_t sx =
                    horizontal ? static_cast<std::int64_t>(x) + d : static_cast<std::int64_t>(x);
                const std::int64_t sy =
                    horizontal ? static_cast<std::int64_t>(y) : static_cast<std::int64_t>(y) + d;
                if (sx < 0 || sy < 0 || sx >= static_cast<std::int64_t>(w) ||
                    sy >= static_cast<std::int64_t>(h))
                    continue;
                const std::size_t at = (static_cast<std::size_t>(sy) * w +
                                        static_cast<std::size_t>(sx)) * 4;
                for (int c = 0; c < 4; ++c) best[c] = std::max(best[c], pixels[at + c]);
            }
            const std::size_t at = (y * w + x) * 4;
            for (int c = 0; c < 4; ++c) out[at + c] = best[c];
        }
    return out;
}


std::vector<std::uint8_t> windowMaxDisc(std::size_t w, std::size_t h,
                                        const std::vector<std::uint8_t>& pixels, std::int64_t r) {
    std::vector<std::pair<std::int64_t, std::int64_t>> offsets;
    for (std::int64_t dy = -r; dy <= r; ++dy)
        for (std::int64_t dx = -r; dx <= r; ++dx)
            if (dx * dx + dy * dy <= r * r) offsets.push_back({dx, dy});
    std::vector<std::uint8_t> out(w * h * 4, 0);
    for (std::size_t y = 0; y < h; ++y)
        for (std::size_t x = 0; x < w; ++x) {
            std::uint8_t best[4] = {0, 0, 0, 0};
            for (const auto& [dx, dy] : offsets) {
                const std::int64_t sx = static_cast<std::int64_t>(x) + dx;
                const std::int64_t sy = static_cast<std::int64_t>(y) + dy;
                if (sx < 0 || sy < 0 || sx >= static_cast<std::int64_t>(w) ||
                    sy >= static_cast<std::int64_t>(h))
                    continue;
                const std::size_t at = (static_cast<std::size_t>(sy) * w +
                                        static_cast<std::size_t>(sx)) * 4;
                for (int c = 0; c < 4; ++c) best[c] = std::max(best[c], pixels[at + c]);
            }
            const std::size_t at = (y * w + x) * 4;
            for (int c = 0; c < 4; ++c) out[at + c] = best[c];
        }
    return out;
}


// Square-window median, per channel, off the layer counting as zero. Huang's
// sliding histogram: rebuilt once per row, then slid a column at a time.
std::vector<std::uint8_t> medianFilter(std::size_t w, std::size_t h,
                                       const std::vector<std::uint8_t>& pixels, std::int64_t r) {
    std::vector<std::uint8_t> out(w * h * 4, 0);
    const std::uint32_t full = static_cast<std::uint32_t>((2 * r + 1) * (2 * r + 1));
    const std::uint32_t rank = (full + 1) / 2;
    auto at = [&](std::int64_t x, std::int64_t y, int c) -> std::size_t {
        if (x < 0 || y < 0 || x >= static_cast<std::int64_t>(w) ||
            y >= static_cast<std::int64_t>(h))
            return 0;
        return pixels[(static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)) * 4 + c];
    };
    for (std::size_t rowY = 0; rowY < h; ++rowY) {
        const std::int64_t y = static_cast<std::int64_t>(rowY);
        for (int c = 0; c < 4; ++c) {
            std::uint32_t hist[256] = {0};
            for (std::int64_t dy = -r; dy <= r; ++dy)
                for (std::int64_t dx = -r; dx <= r; ++dx) hist[at(dx, y + dy, c)] += 1;
            std::size_t med = 0;
            std::uint32_t below = 0;
            while (below + hist[med] < rank) {
                below += hist[med];
                ++med;
            }
            out[(rowY * w) * 4 + c] = static_cast<std::uint8_t>(med);
            for (std::int64_t x = 1; x < static_cast<std::int64_t>(w); ++x) {
                for (std::int64_t dy = -r; dy <= r; ++dy) {
                    const std::size_t leave = at(x - r - 1, y + dy, c);
                    hist[leave] -= 1;
                    if (leave < med) --below;
                    const std::size_t enter = at(x + r, y + dy, c);
                    hist[enter] += 1;
                    if (enter < med) ++below;
                }
                while (below >= rank) {
                    --med;
                    below -= hist[med];
                }
                while (below + hist[med] < rank) {
                    below += hist[med];
                    ++med;
                }
                out[(rowY * w + static_cast<std::size_t>(x)) * 4 + c] =
                    static_cast<std::uint8_t>(med);
            }
        }
    }
    return out;
}


std::vector<std::uint8_t> liveBlurApply(std::uint32_t width, std::uint32_t height,
                                        const std::vector<std::uint8_t>& pixels,
                                        const LiveBlur& blur) {
    const std::size_t w = width, h = height;
    if (w == 0 || h == 0) return pixels;
    switch (blur.kind) {
        case LiveBlurKind::Gaussian:
            if (blur.radius < 1.0) return pixels;
            return separableBlur(w, h, pixels, box3Kernel(2.0 * blur.radius / 3.0));
        case LiveBlurKind::Box: {
            const std::size_t r =
                static_cast<std::size_t>(std::max(0.0, std::round(blur.radius)));
            if (r == 0) return pixels;
            return separableBlur(
                w, h, pixels,
                std::vector<float>(2 * r + 1, 1.0f / static_cast<float>(2 * r + 1)));
        }
        case LiveBlurKind::Unsharp:
            return unsharpBlur(w, h, pixels, blur.radius, blur.factor, blur.threshold);
        case LiveBlurKind::HighPass:
            return highPassBlur(w, h, pixels, blur.radius, blur.mono);
        case LiveBlurKind::Motion: {
            const double length = 2.0 * blur.radius;
            if (length < 1.0) return pixels;
            const double dx = std::cos(-blur.angleRad), dy = std::sin(-blur.angleRad);
            const std::size_t taps =
                std::max<std::size_t>(1, static_cast<std::size_t>(std::llround(length))) | 1;
            return gatherTaps(w, h, pixels, [=](double x, double y) {
                std::vector<std::pair<double, double>> pts;
                pts.reserve(taps);
                for (std::size_t i = 0; i < taps; ++i) {
                    const double t = -length / 2.0 +
                                     length * static_cast<double>(i) /
                                         static_cast<double>(std::max<std::size_t>(taps - 1, 1));
                    pts.push_back({x + t * dx, y + t * dy});
                }
                return pts;
            });
        }
        case LiveBlurKind::Radial: {
            if (std::abs(blur.angleDeg) < 0.01) return pixels;
            const double sweep = blur.angleDeg * (kPi / 180.0);
            const double corners[4][2] = {{0.0, 0.0},
                                          {static_cast<double>(w), 0.0},
                                          {0.0, static_cast<double>(h)},
                                          {static_cast<double>(w), static_cast<double>(h)}};
            double far = 0.0;
            for (const auto& c : corners)
                far = std::max(far, std::hypot(c[0] - blur.cx, c[1] - blur.cy));
            const std::size_t taps =
                static_cast<std::size_t>(
                    std::clamp(std::ceil(std::abs(sweep) * far * 4.0), 3.0, 2048.0)) |
                1;
            const double cx = blur.cx, cy = blur.cy;
            return gatherTaps(w, h, pixels, [=](double x, double y) {
                const double dx = x - cx, dy = y - cy;
                const double r = std::hypot(dx, dy);
                const double th = std::atan2(dy, dx);
                std::vector<std::pair<double, double>> pts;
                pts.reserve(taps);
                for (std::size_t i = 0; i < taps; ++i) {
                    const double a = th - sweep + 2.0 * sweep * static_cast<double>(i) /
                                                    static_cast<double>(taps - 1);
                    pts.push_back({cx + r * std::cos(a), cy + r * std::sin(a)});
                }
                return pts;
            });
        }
        case LiveBlurKind::Maximum: {
            const std::int64_t r =
                static_cast<std::int64_t>(std::max(0.0, std::round(blur.radius)));
            if (r == 0) return pixels;
            if (blur.circular) return windowMaxDisc(w, h, pixels, r);
            return windowMax1D(w, h, windowMax1D(w, h, pixels, r, true), r, false);
        }
        case LiveBlurKind::DustAndScratches: {
            const std::int64_t r =
                static_cast<std::int64_t>(std::max(0.0, std::round(blur.radius)));
            if (r == 0) return pixels;
            const std::vector<std::uint8_t> med = medianFilter(w, h, pixels, r);
            const std::int32_t cut = static_cast<std::int32_t>(blur.tolerance * 255.0);
            std::vector<std::uint8_t> out = pixels;
            for (std::size_t i = 0; i + 3 < out.size(); i += 4) {
                auto apart = [&](int c) {
                    return std::abs(static_cast<int>(pixels[i + c]) - static_cast<int>(med[i + c]));
                };
                if (blur.perChannel) {
                    for (int c = 0; c < 4; ++c)
                        if (apart(c) > cut) out[i + c] = med[i + c];
                } else {
                    int m = 0;
                    for (int c = 0; c < 4; ++c) m = std::max(m, apart(c));
                    if (m > cut)
                        for (int c = 0; c < 4; ++c) out[i + c] = med[i + c];
                }
            }
            return out;
        }
        case LiveBlurKind::Median: {
            const std::int64_t r =
                static_cast<std::int64_t>(std::max(0.0, std::round(blur.radius)));
            if (r == 0) return pixels;
            return medianFilter(w, h, pixels, r);
        }
    }
    return pixels;
}


std::vector<std::uint8_t> vignetteApply(std::uint32_t width, std::uint32_t height,
                                        const std::vector<std::uint8_t>& pixels,
                                        const Vignette& v) {
    const std::size_t w = width, h = height;
    if (w == 0 || h == 0 || v.exposure == 0.0) return pixels;
    const double cx = width / 2.0, cy = height / 2.0;
    const double a = v.scale * cx * v.shape, b = v.scale * cy;
    const double inner = std::clamp(v.hardness, 0.0, 1.0);
    const double outer = 1.0 - 0.2 * (1.0 - inner);
    std::vector<std::uint8_t> out = pixels;
    for (std::size_t i = 0; i < w * h; ++i) {
        const double x = static_cast<double>(i % w) + 0.5;
        const double y = static_cast<double>(i / w) + 0.5;
        double weight = 1.0;
        if (!(a <= 0.0 || b <= 0.0)) {
            const double rho = std::hypot((x - cx) / a, (y - cy) / b);
            const double t =
                std::clamp((rho - inner) / std::max(outer - inner, 1e-6), 0.0, 1.0);
            weight = t * t * (3.0 - 2.0 * t);
        }
        if (weight <= 0.0) continue;
        const double gain = std::exp2((v.exposure * weight) / 2.2);
        for (int c = 0; c < 3; ++c)
            out[i * 4 + c] = byteClamp(static_cast<double>(out[i * 4 + c]) * gain + 0.5);
    }
    return out;
}

}  // namespace af_detail
}  // namespace pittore::io
