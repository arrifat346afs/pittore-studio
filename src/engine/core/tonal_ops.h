#pragma once
// Host-side tonal/filter/geometry ops on RGBAf Images. Header-only.
// All ops edit in place (geometry returns a new image), in linear [0,1],
// alpha untouched unless noted. Channel codes: 0=R, 1=G, 2=B, 3=RGB.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#include <utility>
#include <vector>

#include "engine/core/image.h"
#include "engine/core/parallel.h"
#include "engine/filter/core/filter_detail.h"

namespace pittore {

// ---------------------------------------------------------------------------
// Histogram (R80)
// ---------------------------------------------------------------------------

// 256 bins per RGB channel + Rec.709 luma. Big images: downscale first,
// the panel also samples every N-th pixel.
struct Histogram256 {
    std::uint64_t rgb[3][256] = {};
    std::uint64_t luma[256] = {};

    std::uint64_t total() const {
        std::uint64_t t = 0;
        for (int c = 0; c < 3; ++c)
            for (int i = 0; i < 256; ++i) t += rgb[c][i];
        return t;
    }
};

// Bin an image into histograms. Out-of-range values clip to the end bins.
inline void computeHistogram(const Image& img, Histogram256& out) {
    const RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t r = static_cast<std::uint32_t>(std::clamp(px[i].r, 0.0f, 1.0f) * 255.0f);
        const std::uint32_t g = static_cast<std::uint32_t>(std::clamp(px[i].g, 0.0f, 1.0f) * 255.0f);
        const std::uint32_t b = static_cast<std::uint32_t>(std::clamp(px[i].b, 0.0f, 1.0f) * 255.0f);
        const std::uint32_t y = static_cast<std::uint32_t>(
            std::clamp(0.2126f * px[i].r + 0.7152f * px[i].g + 0.0722f * px[i].b, 0.0f, 1.0f) * 255.0f);
        ++out.rgb[0][r];
        ++out.rgb[1][g];
        ++out.rgb[2][b];
        ++out.luma[y];
    }
}

// ---------------------------------------------------------------------------
// Levels (R54)
// ---------------------------------------------------------------------------

// conventional Levels: [inBlack,inWhite] -> [outBlack,outWhite] with gamma.
// gamma 1 is linear; <1 darkens mids, >1 brightens (slider convention).
inline void applyLevels(Image& img, int channel, double inBlack, double inWhite,
                        double gamma, double outBlack, double outWhite) {
    const double span = std::max(inWhite - inBlack, 1e-9);
    const double g = gamma > 0.0 ? 1.0 / gamma : 1.0;
    RGBAf* px = img.data();
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    // Rows disjoint: bit-identical.
    core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            const std::size_t base = static_cast<std::size_t>(y) * w;
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t i = base + x;
                float* chans[3] = {&px[i].r, &px[i].g, &px[i].b};
                for (int c = 0; c < 3; ++c) {
                    if (channel != 3 && c != channel) continue;
                    double v = std::clamp((static_cast<double>(*chans[c]) - inBlack) / span, 0.0, 1.0);
                    v = std::pow(v, g);
                    v = outBlack + v * (outWhite - outBlack);
                    *chans[c] = static_cast<float>(std::clamp(v, 0.0, 1.0));
                }
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Curves (R54)
// ---------------------------------------------------------------------------

// Build a 256-entry LUT from control points (normalized, sorted by x,
// linear between neighbours, clamped at the ends).
inline void buildCurveLUT(const std::vector<std::pair<double, double>>& pts, float lut[256]) {
    std::vector<std::pair<double, double>> p = pts;
    if (p.empty()) p = {{0.0, 0.0}, {1.0, 1.0}};
    std::sort(p.begin(), p.end());
    for (auto& q : p) {
        q.first = std::clamp(q.first, 0.0, 1.0);
        q.second = std::clamp(q.second, 0.0, 1.0);
    }
    // One y per x: later dupes win.
    std::vector<std::pair<double, double>> unique;
    for (const auto& q : p) {
        if (!unique.empty() && std::fabs(unique.back().first - q.first) < 1e-12)
            unique.back().second = q.second;
        else
            unique.push_back(q);
    }
    if (unique.size() < 2) unique = {{0.0, 0.0}, {1.0, 1.0}};
    for (int i = 0; i < 256; ++i) {
        const double x = i / 255.0;
        double y;
        if (x <= unique.front().first)
            y = unique.front().second;
        else if (x >= unique.back().first)
            y = unique.back().second;
        else {
            std::size_t j = 1;
            while (j + 1 < unique.size() && unique[j].first < x) ++j;
            const double x0 = unique[j - 1].first, x1 = unique[j].first;
            const double y0 = unique[j - 1].second, y1 = unique[j].second;
            y = (x1 - x0 < 1e-9) ? y1 : y0 + (y1 - y0) * (x - x0) / (x1 - x0);
        }
        lut[i] = static_cast<float>(y);
    }
}

// Build a 256-entry LUT from Levels params (inBlack, inWhite, gamma,
// outBlack, outWhite). Mirrors compute/adjust.h levels_c exactly; the table
// matches direct evaluation to ~1e-4, letting the composite sample a table
// instead of running pow() per channel per pixel.
inline void buildLevelsLUT(const float* p, float lut[256]) {
    const float inBlack = p[0], inWhite = p[1], gamma = p[2];
    const float outBlack = p[3], outWhite = p[4];
    const float span =
        inWhite - inBlack > 1e-6f ? inWhite - inBlack : 1e-6f;
    const float invGamma = gamma > 0.0f ? 1.0f / gamma : 1.0f;
    for (int i = 0; i < 256; ++i) {
        const float v = i / 255.0f;
        float t = (v - inBlack) / span;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
        t = std::pow(t, invGamma);
        lut[i] = outBlack + t * (outWhite - outBlack);
    }
}

// Apply a built LUT to one channel or all three.
inline void applyCurveLUT(Image& img, int channel, const float lut[256]) {
    RGBAf* px = img.data();
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    // Rows disjoint: bit-identical.
    core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            const std::size_t base = static_cast<std::size_t>(y) * w;
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t i = base + x;
                float* chans[3] = {&px[i].r, &px[i].g, &px[i].b};
                for (int c = 0; c < 3; ++c) {
                    if (channel != 3 && c != channel) continue;
                    const int bin = static_cast<int>(std::clamp(*chans[c], 0.0f, 1.0f) * 255.0f + 0.5f);
                    *chans[c] = lut[std::clamp(bin, 0, 255)];
                }
            }
        }
    });
}

// Build + apply in one go.
inline void applyCurves(Image& img, int channel,
                        const std::vector<std::pair<double, double>>& pts) {
    float lut[256];
    buildCurveLUT(pts, lut);
    applyCurveLUT(img, channel, lut);
}

// ---------------------------------------------------------------------------
// Noise filter (R49)
// ---------------------------------------------------------------------------

// Fixed-seed PRNG so Add Noise is reproducible frame to frame.
inline std::uint64_t tonalNextRandom(std::uint64_t& state) {
    std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

// Add uniform noise in [-amount, amount]. Monochrome shares one grey
// delta across channels (grain); otherwise per-channel. Alpha kept.
inline void applyAddNoise(Image& img, double amount, bool monochrome,
                          std::uint64_t seed = 0x9e3779b97f4a7c15ULL) {
    std::uint64_t s = seed;
    const double half = std::clamp(amount, 0.0, 1.0);
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        if (monochrome) {
            const double delta = (tonalNextRandom(s) / 18446744073709551615.0 - 0.5) * 2.0 * half;
            px[i].r = static_cast<float>(std::clamp(px[i].r + delta, 0.0, 1.0));
            px[i].g = static_cast<float>(std::clamp(px[i].g + delta, 0.0, 1.0));
            px[i].b = static_cast<float>(std::clamp(px[i].b + delta, 0.0, 1.0));
        } else {
            for (int c = 0; c < 3; ++c) {
                const double delta =
                    (tonalNextRandom(s) / 18446744073709551615.0 - 0.5) * 2.0 * half;
                (&px[i].r)[c] =
                    static_cast<float>(std::clamp(static_cast<double>((&px[i].r)[c]) + delta, 0.0, 1.0));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Median filter (R49)
// ---------------------------------------------------------------------------

// Median over a (2r+1)^2 window per channel; edges replicate.
inline void applyMedianFilter(Image& img, int radius) {
    const int r = std::max(radius, 0);
    if (r == 0) return;
    const std::uint32_t w = img.width(), h = img.height();
    Image out(w, h);
    const std::size_t k = static_cast<std::size_t>(2 * r + 1) * (2 * r + 1);
    const bool small = k <= 32;
    // Rows disjoint, out separate, per-worker scratch: bit-identical.
    // Single window walk feeds all three channels (was 3x traffic).
    auto median_rows = [&](std::uint32_t y0, std::uint32_t y1) {
        std::vector<float> w0, w1, w2;
        w0.reserve(k);
        w1.reserve(k);
        w2.reserve(k);
        for (std::uint32_t y = y0; y < y1; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                w0.clear();
                w1.clear();
                w2.clear();
                for (int dy = -r; dy <= r; ++dy) {
                    const std::uint32_t sy = static_cast<std::uint32_t>(
                        std::clamp(static_cast<int>(y) + dy, 0, static_cast<int>(h) - 1));
                    for (int dx = -r; dx <= r; ++dx) {
                        const std::uint32_t sx = static_cast<std::uint32_t>(
                            std::clamp(static_cast<int>(x) + dx, 0, static_cast<int>(w) - 1));
                        const RGBAf& t = img.at(sx, sy);
                        w0.push_back(t.r);
                        w1.push_back(t.g);
                        w2.push_back(t.b);
                    }
                }
                RGBAf acc{0, 0, 0, 1.0f};
                float* wins[3] = {w0.data(), w1.data(), w2.data()};
                for (int c = 0; c < 3; ++c) {
                    float* v = wins[c];
                    if (small) {
                        for (std::size_t i = 1; i < k; ++i) {
                            const float key = v[i];
                            std::size_t j = i;
                            while (j > 0 && v[j - 1] > key) {
                                v[j] = v[j - 1];
                                --j;
                            }
                            v[j] = key;
                        }
                    } else {
                        std::nth_element(v, v + k / 2, v + k);
                    }
                    acc[c] = v[k / 2];
                }
                acc.a = img.at(x, y).a;
                out.at(x, y) = acc;
            }
        }
    };
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    unsigned threads = std::min<unsigned>(hw, (h + 63) / 64);
    if (threads <= 1) {
        median_rows(0, h);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(threads - 1);
        const std::uint32_t base = h / threads;
        const std::uint32_t rest = h % threads;
        std::uint32_t y = 0;
        for (unsigned t = 0; t + 1 < threads; ++t) {
            const std::uint32_t n = base + (t < rest ? 1 : 0);
            workers.emplace_back([&, y, n] { median_rows(y, y + n); });
            y += n;
        }
        median_rows(y, h);
        for (auto& th : workers) th.join();
    }
    std::memcpy(img.data(), out.data(), sizeof(RGBAf) * img.pixel_count());
}

// ---------------------------------------------------------------------------
// Sharpen / Unsharp Mask (R49)
// ---------------------------------------------------------------------------

// Classic Unsharp Mask: out = orig + amount*(orig - blur), where
// |orig - blur| beats threshold. Radius 0 falls back to a 3x3 blur.
// Blur base is the shared sliding-window box (O(1)/px, same clamped taps
// and alpha handling as the loops it replaces, within test tolerance).
inline void applyUnsharpMask(Image& img, double amount, int radius, double threshold) {
    const int r = std::max(radius, 1);
    const std::uint32_t w = img.width(), h = img.height();
    Image blur(w, h);
    filter::detail::boxBlurInto(img, blur, r);
    const double amt = std::clamp(amount, 0.0, 5.0);
    const double thr = std::clamp(threshold, 0.0, 1.0);
    RGBAf* px = img.data();
    core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                const RGBAf& b = blur.at(x, y);
                for (int c = 0; c < 3; ++c) {
                    const double orig = px[y * w + x][c];
                    const double diff = orig - b[c];
                    if (std::fabs(diff) <= thr) continue;
                    const double v = orig + amt * diff;
                    px[y * w + x][c] = static_cast<float>(std::clamp(v, 0.0, 1.0));
                }
            }
        }
    });
}

// ---------------------------------------------------------------------------
// Geometry: rotation / flips (R22)
// ---------------------------------------------------------------------------

// 90° clockwise, dims swapped. Dst rows disjoint: bit-identical threaded.
inline Image rotate90Cw(const Image& src) {
    Image dst(src.height(), src.width());
    const std::uint32_t sh = src.height(), sw = src.width();
    core::parallel_rows(sh, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y)
            for (std::uint32_t x = 0; x < sw; ++x)
                dst.at(sh - 1 - y, x) = src.at(x, y);
    });
    return dst;
}

// 90° counter-clockwise, dims swapped.
inline Image rotate90Ccw(const Image& src) {
    Image dst(src.height(), src.width());
    const std::uint32_t sh = src.height(), sw = src.width();
    core::parallel_rows(sh, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y)
            for (std::uint32_t x = 0; x < sw; ++x)
                dst.at(y, sw - 1 - x) = src.at(x, y);
    });
    return dst;
}

inline Image rotate180(const Image& src) {
    Image dst(src.width(), src.height());
    const std::uint32_t sh = src.height(), sw = src.width();
    core::parallel_rows(sh, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y)
            for (std::uint32_t x = 0; x < sw; ++x)
                dst.at(sw - 1 - x, sh - 1 - y) = src.at(x, y);
    });
    return dst;
}

inline Image flipHorizontal(const Image& src) {
    Image dst(src.width(), src.height());
    const std::uint32_t sh = src.height(), sw = src.width();
    core::parallel_rows(sh, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y)
            for (std::uint32_t x = 0; x < sw; ++x)
                dst.at(sw - 1 - x, y) = src.at(x, y);
    });
    return dst;
}

inline Image flipVertical(const Image& src) {
    Image dst(src.width(), src.height());
    const std::uint32_t sh = src.height(), sw = src.width();
    core::parallel_rows(sh, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y)
            for (std::uint32_t x = 0; x < sw; ++x)
                dst.at(x, sh - 1 - y) = src.at(x, y);
    });
    return dst;
}

}  // namespace pittore