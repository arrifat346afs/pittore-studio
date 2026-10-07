#pragma once
// Pixel math behind the Image menu (window/menus/ops/menu_ops_image.cpp).
// Engine types only — pittore::Image plus the compute/core headers — no Qt
// and no document model, so tests/test_image_ops.cpp can exercise every op
// without a window.
//
// Every routine works on straight (non-premultiplied) RGB and leaves alpha
// alone, the way the adjustment kernels in engine/compute/adjust.h do: the
// compositor folds coverage in afterwards.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "engine/compute/adjust.h"
#include "engine/compute/blend.h"
#include "engine/core/image.h"
#include "engine/core/parallel.h"
#include "engine/core/tonal_ops.h"

namespace pittore::ui::imageops {

using ::pittore::Image;
using ::pittore::RGBAf;

// --- shared helpers -------------------------------------------------------

// Rec.709 lightness with the weights adjust.h and the grayscale conversion
// use, so an op's idea of "bright" matches the rest of the engine.
inline float luma(float r, float g, float b) {
    return compute::adjust::luma709(r, g, b);
}

// Sample a tabled transfer with the bin rule adjust::curve_lut_c uses, so a
// tabled op and its direct twin agree on every input.
inline float sampleLut(const float lut[256], float v) {
    return compute::adjust::curve_lut_c(v, lut);
}

// Bilinear sample at centre-based texel coordinates (0 = centre of texel 0).
// `clampEdges` picks what a coordinate past the edge does: repeat the edge
// texel, or — for a resample that walks off the source — come back
// transparent. Coordinates up to half a texel outside always clamp, which
// keeps the fringe on the boundary from fringing in colour.
inline RGBAf sampleBilinear(const Image& img, double x, double y,
                            bool clampEdges) {
    const int w = static_cast<int>(img.width());
    const int h = static_cast<int>(img.height());
    if (w <= 0 || h <= 0) return RGBAf{0, 0, 0, 0};
    if (!clampEdges && (x < -0.5 || y < -0.5 || x > w - 0.5 || y > h - 0.5))
        return RGBAf{0, 0, 0, 0};
    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    const double fx = std::clamp(x - x0, 0.0, 1.0);
    const double fy = std::clamp(y - y0, 0.0, 1.0);
    const int x1 = std::clamp(x0 + 1, 0, w - 1);
    const int y1 = std::clamp(y0 + 1, 0, h - 1);
    x0 = std::clamp(x0, 0, w - 1);
    y0 = std::clamp(y0, 0, h - 1);
    const RGBAf& a = img.at(static_cast<std::uint32_t>(x0),
                            static_cast<std::uint32_t>(y0));
    const RGBAf& b = img.at(static_cast<std::uint32_t>(x1),
                            static_cast<std::uint32_t>(y0));
    const RGBAf& c = img.at(static_cast<std::uint32_t>(x0),
                            static_cast<std::uint32_t>(y1));
    const RGBAf& d = img.at(static_cast<std::uint32_t>(x1),
                            static_cast<std::uint32_t>(y1));
    RGBAf out;
    out.r = static_cast<float>((a.r * (1 - fx) + b.r * fx) * (1 - fy) +
                               (c.r * (1 - fx) + d.r * fx) * fy);
    out.g = static_cast<float>((a.g * (1 - fx) + b.g * fx) * (1 - fy) +
                               (c.g * (1 - fx) + d.g * fx) * fy);
    out.b = static_cast<float>((a.b * (1 - fx) + b.b * fx) * (1 - fy) +
                               (c.b * (1 - fx) + d.b * fx) * fy);
    out.a = static_cast<float>((a.a * (1 - fx) + b.a * fx) * (1 - fy) +
                               (c.a * (1 - fx) + d.a * fx) * fy);
    return out;
}

// --- adjustments the engine already knows ---------------------------------

// Run one engine adjustment kind over every pixel (adjust.h documents the
// parameter semantics per kind). Alpha passes through untouched.
inline void applyAdjust(Image& img, compute::AdjustmentKind kind,
                        const float* p) {
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        float r, g, b;
        compute::adjust::apply(kind, p, nullptr, px[i].r, px[i].g, px[i].b, r,
                               g, b);
        px[i].r = r;
        px[i].g = g;
        px[i].b = b;
    }
}

// --- one-shot Image ▸ Adjustments entries ---------------------------------

// Desaturate: collapse every pixel onto its Rec.709 lightness (grey, same
// coverage), which is the whole of the operation.
inline void desaturate(Image& img) {
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float l = luma(px[i].r, px[i].g, px[i].b);
        px[i].r = l;
        px[i].g = l;
        px[i].b = l;
    }
}

// Equalize: redistribute lightness across the full range from the lightness
// histogram's cumulative distribution — one curve for all three channels, so
// hue relationships ride along instead of each channel stretching its own
// cast into the picture. A tonally flat image has no range to spread out and
// is left alone.
inline void equalize(Image& img) {
    Histogram256 hist;
    computeHistogram(img, hist);
    const double n = static_cast<double>(img.pixel_count());
    if (n <= 0.0) return;
    std::uint64_t cdf[256];
    std::uint64_t run = 0;
    int lo = -1;
    int hi = -1;
    for (int i = 0; i < 256; ++i) {
        run += hist.luma[i];
        cdf[i] = run;
        if (hist.luma[i] > 0) {
            if (lo < 0) lo = i;
            hi = i;
        }
    }
    if (lo < 0 || hi <= lo) return;
    const double below = lo > 0 ? static_cast<double>(cdf[lo - 1]) : 0.0;
    const double denom = n - below;
    if (denom <= 0.0) return;
    float lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = static_cast<float>(
            std::clamp((static_cast<double>(cdf[i]) - below) / denom, 0.0, 1.0));
    RGBAf* px = img.data();
    const std::size_t count = img.pixel_count();
    for (std::size_t i = 0; i < count; ++i) {
        px[i].r = sampleLut(lut, px[i].r);
        px[i].g = sampleLut(lut, px[i].g);
        px[i].b = sampleLut(lut, px[i].b);
    }
}

// --- auto tone / contrast / colour ----------------------------------------

// Build the clipped linear stretch for one 256-bin histogram: the bins that
// hold the outer `clip` fraction of the samples become the new 0 and 1, so a
// couple of stray extreme pixels cannot swallow the range. Returns false when
// the spread is too small to stretch (a flat channel keeps its own LUT).
inline bool stretchLut(const std::uint64_t* bins, std::uint64_t n, float lut[256],
                       double clip = 0.005) {
    if (n == 0) return false;
    const double skip = n * clip;
    std::uint64_t run = 0;
    int lo = -1;
    for (int i = 0; i < 256; ++i) {
        run += bins[i];
        if (bins[i] > 0 && run > skip) {
            lo = i;
            break;
        }
    }
    run = 0;
    int hi = -1;
    for (int i = 255; i >= 0; --i) {
        run += bins[i];
        if (bins[i] > 0 && run > skip) {
            hi = i;
            break;
        }
    }
    if (lo < 0 || hi < 0 || hi <= lo) return false;
    for (int i = 0; i < 256; ++i) {
        const double v = (static_cast<double>(i) - lo) / (hi - lo);
        lut[i] = static_cast<float>(std::clamp(v, 0.0, 1.0));
    }
    return true;
}

// Auto Tone: the clipped stretch per channel — each channel gets its own
// endpoints, which is what removes a cast as well as opening the range. A
// channel too flat to stretch is left bit-for-bit alone.
inline void autoTone(Image& img) {
    std::uint64_t bins[3][256] = {};
    const RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float v[3] = {px[i].r, px[i].g, px[i].b};
        for (int c = 0; c < 3; ++c) {
            const int b = static_cast<int>(std::clamp(v[c], 0.0f, 1.0f) * 255.0f);
            ++bins[c][b];
        }
    }
    float lut[3][256];
    bool stretched[3];
    for (int c = 0; c < 3; ++c)
        stretched[c] = stretchLut(bins[c], static_cast<std::uint64_t>(n), lut[c]);
    if (!stretched[0] && !stretched[1] && !stretched[2]) return;
    RGBAf* out = img.data();
    for (std::size_t i = 0; i < n; ++i) {
        if (stretched[0]) out[i].r = sampleLut(lut[0], out[i].r);
        if (stretched[1]) out[i].g = sampleLut(lut[1], out[i].g);
        if (stretched[2]) out[i].b = sampleLut(lut[2], out[i].b);
    }
}

// Auto Contrast: the same clipped stretch, measured on lightness and applied
// as one curve — contrast only, no colour cast is touched.
inline void autoContrast(Image& img) {
    std::uint64_t bins[256] = {};
    const RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const int b = static_cast<int>(
            std::clamp(luma(px[i].r, px[i].g, px[i].b), 0.0f, 1.0f) * 255.0f);
        ++bins[b];
    }
    float lut[256];
    if (!stretchLut(bins, static_cast<std::uint64_t>(n), lut)) return;
    RGBAf* out = img.data();
    for (std::size_t i = 0; i < n; ++i) {
        out[i].r = sampleLut(lut, out[i].r);
        out[i].g = sampleLut(lut, out[i].g);
        out[i].b = sampleLut(lut, out[i].b);
    }
}

// Auto Color: grey-world cast removal — scale each channel so the three
// channel means agree, leaving the overall exposure where it was.
inline void autoColor(Image& img) {
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    if (n == 0) return;
    double sum[3] = {0, 0, 0};
    for (std::size_t i = 0; i < n; ++i) {
        sum[0] += px[i].r;
        sum[1] += px[i].g;
        sum[2] += px[i].b;
    }
    const double mean[3] = {sum[0] / n, sum[1] / n, sum[2] / n};
    const double target = (mean[0] + mean[1] + mean[2]) / 3.0;
    double gain[3];
    for (int c = 0; c < 3; ++c) {
        // A channel that is already black has nothing to correct with; the
        // ceiling stops a near-black mean from exploding into noise.
        gain[c] = mean[c] > 1e-4 ? std::clamp(target / mean[c], 0.0, 8.0) : 1.0;
    }
    for (std::size_t i = 0; i < n; ++i) {
        px[i].r = compute::adjust::clamp01(static_cast<float>(px[i].r * gain[0]));
        px[i].g = compute::adjust::clamp01(static_cast<float>(px[i].g * gain[1]));
        px[i].b = compute::adjust::clamp01(static_cast<float>(px[i].b * gain[2]));
    }
}

// --- gradient map ---------------------------------------------------------

// One stop of a lightness ramp.
struct ColorStop {
    float t = 0.0f;  // lightness position, 0..1
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
};

// Gradient Map: paint each pixel with the ramp colour at its lightness.
// Stops are sorted by position; lightness outside the ramp clamps to the ends.
inline void gradientMap(Image& img, std::vector<ColorStop> stops) {
    if (stops.size() < 2) return;
    std::sort(stops.begin(), stops.end(),
              [](const ColorStop& a, const ColorStop& b) { return a.t < b.t; });
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float t = compute::adjust::clamp01(
            luma(px[i].r, px[i].g, px[i].b));
        std::size_t seg = 0;
        while (seg + 2 < stops.size() && stops[seg + 1].t < t) ++seg;
        const ColorStop& a = stops[seg];
        const ColorStop& b = stops[seg + 1];
        const float span = b.t - a.t;
        const float f = span > 1e-6f ? std::clamp((t - a.t) / span, 0.0f, 1.0f)
                                     : 0.0f;
        px[i].r = a.r + (b.r - a.r) * f;
        px[i].g = a.g + (b.g - a.g) * f;
        px[i].b = a.b + (b.b - a.b) * f;
    }
}

// --- colour LUTs (.cube) --------------------------------------------------

// A parsed .cube 3-D LUT: `size`³ RGB triples, red index fastest.
struct CubeLut {
    int size = 0;
    float domainMin[3] = {0.0f, 0.0f, 0.0f};
    float domainMax[3] = {1.0f, 1.0f, 1.0f};
    std::vector<float> data;
};

// Parse the .cube text format: TITLE, DOMAIN_MIN / DOMAIN_MAX, LUT_3D_SIZE
// and whitespace-separated triples, '#' comments. 1-D tables and unrecognised
// lines are refused rather than guessed at — a LUT that half-loads would
// recolour the picture wrongly. Returns false and sets `error` on bad input.
inline bool parseCube(const std::string& text, CubeLut& out,
                      std::string* error = nullptr) {
    auto fail = [error](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    CubeLut lut;
    bool sawSize = false;
    std::size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        ++lineNo;
        const std::size_t hash = line.find('#');
        if (hash != std::string::npos) line.resize(hash);
        std::istringstream in(line);
        std::vector<std::string> tok;
        std::string word;
        while (in >> word) tok.push_back(word);
        if (tok.empty()) continue;
        auto num = [](const std::string& s, float* v) {
            try {
                std::size_t used = 0;
                *v = std::stof(s, &used);
                return used == s.size();
            } catch (...) {
                return false;
            }
        };
        if (tok[0] == "TITLE") continue;
        if (tok[0] == "LUT_1D_SIZE")
            return fail("1-D LUTs are not supported; use a 3-D table.");
        if (tok[0] == "LUT_3D_SIZE") {
            float v = 0.0f;
            if (tok.size() < 2 || !num(tok[1], &v))
                return fail("LUT_3D_SIZE needs a size.");
            const int n = static_cast<int>(v);
            if (n < 2 || n > 128)
                return fail("LUT_3D_SIZE must be between 2 and 128.");
            lut.size = n;
            sawSize = true;
            continue;
        }
        if (tok[0] == "DOMAIN_MIN" || tok[0] == "DOMAIN_MAX") {
            const bool isMin = tok[0] == "DOMAIN_MIN";
            float v[3];
            if (tok.size() < 4 || !num(tok[1], &v[0]) || !num(tok[2], &v[1]) ||
                !num(tok[3], &v[2]))
                return fail(tok[0] + " needs three numbers.");
            for (int c = 0; c < 3; ++c)
                (isMin ? lut.domainMin[c] : lut.domainMax[c]) = v[c];
            continue;
        }
        float v[3];
        if (tok.size() >= 3 && num(tok[0], &v[0]) && num(tok[1], &v[1]) &&
            num(tok[2], &v[2])) {
            lut.data.push_back(v[0]);
            lut.data.push_back(v[1]);
            lut.data.push_back(v[2]);
            continue;
        }
        return fail("Line " + std::to_string(lineNo) +
                    ": not a keyword or three numbers.");
    }
    if (!sawSize) return fail("No LUT_3D_SIZE line.");
    const std::size_t want =
        static_cast<std::size_t>(lut.size) * lut.size * lut.size * 3;
    if (lut.data.size() != want)
        return fail("Expected " + std::to_string(want / 3) + " entries, found " +
                    std::to_string(lut.data.size() / 3) + ".");
    for (int c = 0; c < 3; ++c) {
        if (lut.domainMax[c] - lut.domainMin[c] < 1e-6f)
            return fail("DOMAIN_MAX must exceed DOMAIN_MIN.");
    }
    out = std::move(lut);
    return true;
}

// Sample a parsed LUT at straight RGB (trilinear, edges clamped).
inline RGBAf sampleCube(const CubeLut& lut, float r, float g, float b) {
    const int n = lut.size;
    RGBAf out{r, g, b, 1.0f};
    if (n < 2 || lut.data.empty()) return out;
    const float in[3] = {r, g, b};
    int i0[3];
    int i1[3];
    float f[3];
    for (int c = 0; c < 3; ++c) {
        const float span = lut.domainMax[c] - lut.domainMin[c];
        const float u = (compute::adjust::clamp01(in[c]) - lut.domainMin[c]) /
                        span * static_cast<float>(n - 1);
        const int i = std::clamp(static_cast<int>(std::floor(u)), 0, n - 1);
        i0[c] = i;
        i1[c] = std::min(i + 1, n - 1);
        f[c] = std::clamp(u - static_cast<float>(i), 0.0f, 1.0f);
    }
    auto corner = [&](int br, int bg, int bb) -> const float* {
        const std::size_t idx =
            (static_cast<std::size_t>(bb) * n + bg) * n + br;
        return &lut.data[idx * 3];
    };
    float acc[3] = {0, 0, 0};
    for (int dz = 0; dz < 2; ++dz) {
        const float wz = dz ? f[2] : 1.0f - f[2];
        for (int dy = 0; dy < 2; ++dy) {
            const float wy = dy ? f[1] : 1.0f - f[1];
            for (int dx = 0; dx < 2; ++dx) {
                const float wx = dx ? f[0] : 1.0f - f[0];
                const float w = wx * wy * wz;
                const float* v = corner(dx ? i1[0] : i0[0], dy ? i1[1] : i0[1],
                                        dz ? i1[2] : i0[2]);
                acc[0] += v[0] * w;
                acc[1] += v[1] * w;
                acc[2] += v[2] * w;
            }
        }
    }
    out.r = acc[0];
    out.g = acc[1];
    out.b = acc[2];
    return out;
}

// Colour Lookup: push every pixel through the table.
inline void applyCubeLut(Image& img, const CubeLut& lut) {
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const RGBAf v = sampleCube(lut, px[i].r, px[i].g, px[i].b);
        px[i].r = v.r;
        px[i].g = v.g;
        px[i].b = v.b;
    }
}

// --- selective colour -----------------------------------------------------

// Which of the nine Selective Color ranges a pixel belongs to: the six hue
// sectors by dominant hue, or — under the saturation floor — whites,
// neutrals and blacks by lightness.
inline int selectiveRange(float r, float g, float b) {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    const float sat = mx > 1e-6f ? (mx - mn) / mx : 0.0f;
    if (sat < 0.15f) {
        const float l = 0.5f * (mx + mn);
        if (l > 0.9f) return 6;  // whites
        if (l < 0.1f) return 8;  // blacks
        return 7;                // neutrals
    }
    const float d = mx - mn;
    float h;
    if (mx == r)
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g)
        h = (b - r) / d + 2.0f;
    else
        h = (r - g) / d + 4.0f;
    const int sector = static_cast<int>(std::floor(h));
    return sector < 0 ? 0 : (sector > 5 ? 5 : sector);
}

// Selective Color: nine ranges × (cyan, magenta, yellow, black) percentages in
// [-100, 100] — 0 reds, 1 yellows, 2 greens, 3 cyans, 4 blues, 5 magentas,
// 6 whites, 7 neutrals, 8 blacks. The CMY shifts are additive in CMY space
// (cyan pulls red down, and so on) and black scales the pixel; the
// range-by-range masking is what makes one range move without the others.
inline void selectiveColor(Image& img, const float adj[9][4]) {
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float r = px[i].r, g = px[i].g, b = px[i].b;
        const float* a = adj[selectiveRange(r, g, b)];
        if (a[0] == 0.0f && a[1] == 0.0f && a[2] == 0.0f && a[3] == 0.0f)
            continue;
        float c = 1.0f - r + a[0] / 100.0f;
        float m = 1.0f - g + a[1] / 100.0f;
        float y = 1.0f - b + a[2] / 100.0f;
        float nr = compute::adjust::clamp01(1.0f - compute::adjust::clampf(c, 0.0f, 1.0f));
        float ng = compute::adjust::clamp01(1.0f - compute::adjust::clampf(m, 0.0f, 1.0f));
        float nb = compute::adjust::clamp01(1.0f - compute::adjust::clampf(y, 0.0f, 1.0f));
        if (a[3] != 0.0f) {
            const float k = 1.0f - a[3] / 100.0f;  // >1 lightens, <1 darkens
            nr = compute::adjust::clamp01(nr * k);
            ng = compute::adjust::clamp01(ng * k);
            nb = compute::adjust::clamp01(nb * k);
        }
        px[i].r = nr;
        px[i].g = ng;
        px[i].b = nb;
    }
}

// --- tone shaping ---------------------------------------------------------

// Shadows/Highlights: lift the dark end and pull the bright end down, each
// under a lightness mask with `tonalWidth` reach — the tone-curve half of the
// repair, without the neighbourhood-weighted local adaptation (that needs a
// downsampled lightness pass). Lightness moves, so hue rides along.
inline void shadowsHighlights(Image& img, float shadowAmount,
                              float highlightAmount, float tonalWidth) {
    const float tw = std::clamp(tonalWidth, 0.05f, 1.0f);
    const float sa = compute::adjust::clamp01(shadowAmount);
    const float ha = compute::adjust::clamp01(highlightAmount);
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float l = luma(px[i].r, px[i].g, px[i].b);
        const float ws = compute::adjust::clamp01(1.0f - l / tw);
        const float wh = compute::adjust::clamp01((l - (1.0f - tw)) / tw);
        const float dl = sa * ws * (1.0f - l) - ha * wh * l;
        px[i].r = compute::adjust::clamp01(px[i].r + dl);
        px[i].g = compute::adjust::clamp01(px[i].g + dl);
        px[i].b = compute::adjust::clamp01(px[i].b + dl);
    }
}

// HDR Toning: a gamma curve (tone curve), a 3×3 lightness detail boost (local
// contrast) and a strength mix — the tone-curve half of an HDR merge. The
// blur reads a rolling window of *original* lightness rows (three rows of
// scratch), so pixels already rewritten can never feed their own neighbourhood.
inline void hdrToning(Image& img, float strength, float detail, float gamma) {
    const float k = compute::adjust::clamp01(strength);
    if (k <= 0.0f) return;
    const float dtl = compute::adjust::clamp01(detail);
    const float gm = std::max(gamma, 0.05f);
    const int w = static_cast<int>(img.width());
    const int h = static_cast<int>(img.height());
    if (w <= 0 || h <= 0) return;
    const Image& src = img;
    std::vector<float> prev, cur, next;
    auto rowLuma = [&](int y, std::vector<float>& out) {
        out.resize(static_cast<std::size_t>(w));
        const std::uint32_t cy =
            static_cast<std::uint32_t>(std::clamp(y, 0, h - 1));
        for (int x = 0; x < w; ++x) {
            const RGBAf& p = src.at(static_cast<std::uint32_t>(x), cy);
            out[static_cast<std::size_t>(x)] = luma(p.r, p.g, p.b);
        }
    };
    rowLuma(0, prev);  // row -1 clamps to the first row
    rowLuma(0, cur);
    rowLuma(1, next);
    RGBAf* px = img.data();
    for (int y = 0; y < h; ++y) {
        if (y > 0) {
            prev.swap(cur);
            cur.swap(next);
            rowLuma(y + 1, next);
        }
        for (int x = 0; x < w; ++x) {
            const int xm = std::max(x - 1, 0);
            const int xp = std::min(x + 1, w - 1);
            const float blur =
                (prev[xm] + prev[x] + prev[xp] + cur[xm] + cur[x] + cur[xp] +
                 next[xm] + next[x] + next[xp]) /
                9.0f;
            const float l = cur[static_cast<std::size_t>(x)];
            // gamma > 1 lifts the mids (the slider convention of Levels).
            const float curve = compute::adjust::adj_pow(l, 1.0f / gm);
            const float shaped =
                compute::adjust::clamp01(curve + dtl * (curve - blur));
            const float dl = k * (shaped - l);
            RGBAf& p = px[static_cast<std::size_t>(y) * w + x];
            p.r = compute::adjust::clamp01(p.r + dl);
            p.g = compute::adjust::clamp01(p.g + dl);
            p.b = compute::adjust::clamp01(p.b + dl);
        }
    }
}

// --- match / replace ------------------------------------------------------

// Per-channel mean and standard deviation over the pixels that carry
// coverage — the statistics Match Color transfers between images.
struct ChannelStats {
    double mean[3] = {0, 0, 0};
    double spread[3] = {0, 0, 0};
};

inline ChannelStats channelStats(const Image& img) {
    ChannelStats s;
    double sum[3] = {0, 0, 0};
    double sq[3] = {0, 0, 0};
    std::uint64_t n = 0;
    const RGBAf* px = img.data();
    const std::size_t count = img.pixel_count();
    for (std::size_t i = 0; i < count; ++i) {
        if (px[i].a <= 0.0f) continue;  // clear pixels are not colour
        const double v[3] = {px[i].r, px[i].g, px[i].b};
        for (int c = 0; c < 3; ++c) {
            sum[c] += v[c];
            sq[c] += v[c] * v[c];
        }
        ++n;
    }
    if (n == 0) return s;
    for (int c = 0; c < 3; ++c) {
        s.mean[c] = sum[c] / static_cast<double>(n);
        const double var = sq[c] / static_cast<double>(n) - s.mean[c] * s.mean[c];
        s.spread[c] = std::sqrt(std::max(var, 0.0));
    }
    return s;
}

// Match Color: remap img's per-channel mean and spread onto `reference`'s,
// mixed in by `intensity`. `preserveLuma` puts each pixel back on its own
// lightness afterwards, so only the cast and the relative separation move.
inline void matchColor(Image& img, const ChannelStats& reference,
                       float intensity, bool preserveLuma) {
    const ChannelStats own = channelStats(img);
    const float k = compute::adjust::clamp01(intensity);
    if (k <= 0.0f) return;
    double gain[3];
    for (int c = 0; c < 3; ++c)
        gain[c] = (own.spread[c] > 1e-6 && reference.spread[c] > 1e-6)
                      ? reference.spread[c] / own.spread[c]
                      : 1.0;
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float in[3] = {px[i].r, px[i].g, px[i].b};
        const float l0 = luma(in[0], in[1], in[2]);
        float out[3];
        for (int c = 0; c < 3; ++c) {
            const float mapped = static_cast<float>(
                (in[c] - own.mean[c]) * gain[c] + reference.mean[c]);
            out[c] = compute::adjust::clamp01(in[c] + (mapped - in[c]) * k);
        }
        if (preserveLuma) {
            const float l1 = luma(out[0], out[1], out[2]);
            if (l1 > 1e-5f) {
                const float f = l0 / l1;
                for (int c = 0; c < 3; ++c)
                    out[c] = compute::adjust::clamp01(out[c] * f);
            }
        }
        px[i].r = out[0];
        px[i].g = out[1];
        px[i].b = out[2];
    }
}

// Replace Color: recolour every pixel whose distance from the target colour
// is within `tolerance` (distance normalised to 0..1 across the RGB cube, so
// 0 matches an exact colour only). A hard swap: partial matches change fully,
// which is what keeps a recoloured object's own shading intact.
inline void replaceColor(Image& img, float tr, float tg, float tb, float rr,
                         float rg, float rb, float tolerance) {
    const float tol = compute::adjust::clamp01(tolerance);
    constexpr float kInv = 1.0f / 1.7320508f;  // 1 / sqrt(3)
    RGBAf* px = img.data();
    const std::size_t n = img.pixel_count();
    for (std::size_t i = 0; i < n; ++i) {
        const float dr = px[i].r - tr;
        const float dg = px[i].g - tg;
        const float db = px[i].b - tb;
        const float d = std::sqrt(dr * dr + dg * dg + db * db) * kInv;
        if (d <= tol) {
            px[i].r = compute::adjust::clamp01(rr);
            px[i].g = compute::adjust::clamp01(rg);
            px[i].b = compute::adjust::clamp01(rb);
        }
    }
}

// --- layer blends (Apply Image / Calculations) ----------------------------

// Blend `src` over `dst` through the engine's own per-pixel composite, then
// mix the result in by `opacity` — the backdrop is the target layer, so
// Normal at 100% writes the source through and Multiply multiplies. The
// source is sampled at the target's texel centres with its edges held, so a
// source of another size applies without a separate resample step.
inline void blendInto(Image& dst, const Image& src, compute::BlendMode mode,
                      float opacity) {
    const float op = compute::adjust::clamp01(opacity);
    if (op <= 0.0f) return;
    const int dw = static_cast<int>(dst.width());
    const int dh = static_cast<int>(dst.height());
    const double sw = static_cast<double>(src.width());
    const double sh = static_cast<double>(src.height());
    core::parallel_rows(static_cast<std::uint32_t>(dh),
                        [&](std::uint32_t y0, std::uint32_t y1) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            const double sy = (static_cast<double>(y) + 0.5) * sh / dh - 0.5;
            for (int x = 0; x < dw; ++x) {
                const double sx =
                    (static_cast<double>(x) + 0.5) * sw / dw - 0.5;
                const RGBAf s = sampleBilinear(src, sx, sy, true);
                RGBAf& d = dst.at(static_cast<std::uint32_t>(x), y);
                float orr = 0.0f, og = 0.0f, ob = 0.0f, oa = 0.0f;
                compute::blend::pixel(mode, s.r, s.g, s.b, s.a, d.r, d.g, d.b,
                                      d.a, static_cast<int>(x),
                                      static_cast<int>(y), orr, og, ob, oa);
                d.r = compute::adjust::clamp01(d.r + (orr - d.r) * op);
                d.g = compute::adjust::clamp01(d.g + (og - d.g) * op);
                d.b = compute::adjust::clamp01(d.b + (ob - d.b) * op);
                d.a = compute::adjust::clamp01(d.a + (oa - d.a) * op);
            }
        }
    });
}

// --- geometry (Image Rotation ▸ Arbitrary…) -------------------------------

// The canvas a `w`×`h` rectangle covers once rotated about its centre by
// `degrees` (counter-clockwise, y growing downward): the output size and the
// origin shift that puts the bounding box's top-left at (0,0).
inline void rotatedCanvasBounds(int w, int h, double degrees, double& outW,
                                double& outH, double& shiftX, double& shiftY) {
    const double a = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    const double cx = w * 0.5;
    const double cy = h * 0.5;
    const double px[4] = {0.0, static_cast<double>(w),
                          static_cast<double>(w), 0.0};
    const double py[4] = {0.0, 0.0, static_cast<double>(h),
                          static_cast<double>(h)};
    double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    for (int i = 0; i < 4; ++i) {
        const double dx = px[i] - cx;
        const double dy = py[i] - cy;
        const double x = c * dx + s * dy + cx;
        const double y = -s * dx + c * dy + cy;
        if (i == 0) {
            minX = maxX = x;
            minY = maxY = y;
        } else {
            minX = std::min(minX, x);
            maxX = std::max(maxX, x);
            minY = std::min(minY, y);
            maxY = std::max(maxY, y);
        }
    }
    outW = maxX - minX;
    outH = maxY - minY;
    shiftX = minX;
    shiftY = minY;
}

// The inverse of that rotation as the 2×3 matrix resampleAffine wants: it
// maps a destination texel centre (edge-based destination coordinates) back
// to source texels. Rotations are orthogonal, so the forward matrix's
// transpose is the inverse; the origin shift folds into the translation.
inline void rotationInverse(double inv[2][3], int srcW, int srcH,
                            double degrees, double shiftX, double shiftY) {
    const double a = degrees * 3.14159265358979323846 / 180.0;
    const double c = std::cos(a);
    const double s = std::sin(a);
    const double cx = srcW * 0.5;
    const double cy = srcH * 0.5;
    const double tx = shiftX - cx;
    const double ty = shiftY - cy;
    inv[0][0] = c;
    inv[0][1] = -s;
    inv[0][2] = c * tx - s * ty + cx;
    inv[1][0] = s;
    inv[1][1] = c;
    inv[1][2] = s * tx + c * ty + cy;
}

// Sample `src` through an affine map into a fresh `dstW`×`dstH` image:
// `inv` maps destination texel centres to source coordinates, edge-based on
// both sides. Bilinear, and transparent where the map leaves the source —
// the primitive the arbitrary canvas rotation drives for pixels and masks.
inline Image resampleAffine(const Image& src, const double inv[2][3], int dstW,
                            int dstH) {
    Image out(static_cast<std::uint32_t>(dstW),
              static_cast<std::uint32_t>(dstH));
    core::parallel_rows(static_cast<std::uint32_t>(dstH),
                        [&](std::uint32_t y0, std::uint32_t y1) {
        for (std::uint32_t y = y0; y < y1; ++y) {
            const double qy = static_cast<double>(y) + 0.5;
            const double sy = inv[1][1] * qy + inv[1][2];
            for (int x = 0; x < dstW; ++x) {
                const double qx = static_cast<double>(x) + 0.5;
                const double sx = inv[0][0] * qx + inv[0][1] * qy + inv[0][2];
                const double py = inv[1][0] * qx + sy;
                out.at(static_cast<std::uint32_t>(x), y) =
                    sampleBilinear(src, sx - 0.5, py - 0.5, false);
            }
        }
    });
    return out;
}

}  // namespace pittore::ui::imageops
