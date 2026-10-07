#include "engine/compute/tone_blend.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "engine/compute/oklab.h"
#include "engine/core/parallel.h"

namespace pittore::compute {
namespace {

// Alpha-weighted box pyramid: each level halves (floored, min 1px) with an
// exact 2x2 box over contributing texels only (transparent texels carry no
// colour, so they must not darken the mean). Stops once the longest side
// fits in 8px — smooth enough for a gain field, cheap to build.
struct Pyramid {
    struct Level {
        std::uint32_t w = 0, h = 0;
        // Level 0 borrows the caller's buffer; every other level owns its
        // reduction. Borrowing instead of copying the full frame saves a
        // 133MB alloc+memcpy per pyramid at 4K (10.5ms of 16.8ms measured).
        // Safe because level 0 is read-only here: the grade loop consumes the
        // *upsampled* fields, which are materialized before dst is written —
        // so even dst == group (documented aliasing) never scribbles on data
        // still being read.
        const pittore::RGBAf* ext = nullptr;
        std::vector<pittore::RGBAf> owned;
        const pittore::RGBAf* px() const { return ext ? ext : owned.data(); }
    };
    std::vector<Level> levels;  // [0] full resolution (borrowed)
};

// Grow-only per-thread scratch. Slots 0/1 hold the low-pass fields of the
// frame being graded; the region path reuses them for its (small) rect fields
// (slots 2/3 stage the cold-fallback's region+halo copies). The old code did
// `vector.resize(w*h)` on each — value-initializing (zeroing) 133MB twice
// per call purely to overwrite every element (4.9ms of a 4.8ms upsample: the
// zero-fill cost more than the blur itself). Paid once per thread, resident
// after.
pittore::RGBAf* toneScratch(std::size_t slot, std::size_t n) {
    static thread_local std::vector<pittore::RGBAf> slots[4];
    std::vector<pittore::RGBAf>& v = slots[slot & 3];
    if (v.size() < n) v.resize(n);
    return v.data();
}

// Global contrast pivot from the last full-frame blend (mirrors the GPU
// backends' tonePivot_/tonePivotValid_). A region MUST reuse it: its own
// mean would seam against the surrounding pixels whenever contrast != 0,
// while one dab moves the global mean negligibly. Benign race if two threads
// blend at once (same as the device side — a stale-but-plausible float).
float gPivotCache = 0.5f;
bool gPivotValid = false;

// Selected low-pass level from the last full-frame blend, kept so a region
// can re-derive its field with the full frame's own upsample formula. This
// is what makes the region path seam-free: the surrounding canvas was graded
// with this exact field, so re-sampling it is bit-identical to what full
// frame produced — whereas a region-local pyramid picks its level from the
// region's *own* depth (128px cells vs the frame's 256px at 1080p/lp=1),
// i.e. a different blur scale, which measured up to 0.14 abs error. Levels
// are coarse (7x4 at 4K for lp=1) so the copy is ~KB; lp=0 needs no cache at
// all (level 0 upsamples to an exact copy of the source).
struct LevelCache {
    std::vector<pittore::RGBAf> px;
    std::uint32_t w = 0, h = 0;      // texel dims of the cached level
    std::uint32_t frameW = 0, frameH = 0;
    float lp = -1.0f;                // lowPass the field was built for
    int level = -1;                  // selected level index (0 = no copy kept)
    bool valid = false;
};
LevelCache gLvCache[2];  // [0] group, [1] backdrop

void cacheSelectedLevel(int which, const Pyramid& pyr, std::size_t li,
                        std::uint32_t fw, std::uint32_t fh, float lp) {
    LevelCache c;  // build off to the side so a reader never sees a half fill
    c.frameW = fw;
    c.frameH = fh;
    c.lp = lp;
    c.level = static_cast<int>(li);
    const Pyramid::Level& lv = pyr.levels[li];
    c.w = lv.w;
    c.h = lv.h;
    if (li > 0) c.px.assign(lv.px(), lv.px() + std::size_t(lv.w) * lv.h);
    c.valid = true;
    gLvCache[which] = std::move(c);
}

// Where a strided frame buffer's pixels live: rect origin + row stride. A
// full-frame blend points everything at {0,0,w}; a region blend points
// group/dst at the rect inside the frame while the low-pass fields are the
// compact rect buffer — or the frame itself, for lowPass=0 where the field
// IS the source.
struct RectView {
    std::uint32_t ox = 0, oy = 0;
    std::size_t stride = 0;
};

// Level count buildPyramid would produce — deterministic from the frame size
// alone, so a region can work out the full frame's level selection without
// building anything (mirrors the loop in buildPyramid exactly).
std::size_t pyramidDepth(std::uint32_t w, std::uint32_t h) {
    std::size_t n = 1;
    while (true) {
        if (w <= 8 && h <= 8) break;
        if (w <= 1 && h <= 1) break;
        w = std::max<std::uint32_t>(1, w / 2);
        h = std::max<std::uint32_t>(1, h / 2);
        ++n;
    }
    return n;
}

// The level applyToneBlend selects for this frame size + lowPass. Computed
// identically to the full-frame path (same lp * (depth-1) float product, same
// lround) so a region samples the very same texels.
std::size_t selectedLevel(std::uint32_t w, std::uint32_t h, float lp) {
    const std::size_t depth = pyramidDepth(w, h);
    return std::min<std::size_t>(
        static_cast<std::size_t>(std::lround(lp * (depth - 1))), depth - 1);
}

// Per-pixel form of upsampleLevel: identical expressions in identical order,
// evaluated for one output texel of the full frame. A rect of these equals
// the full-frame field byte-for-byte — verified by memcmp against
// upsampleLevel over whole frames.
pittore::RGBAf sampleLevelAt(const pittore::RGBAf* px, std::uint32_t lw,
                              std::uint32_t lh, std::uint32_t w,
                              std::uint32_t h, std::uint32_t x,
                              std::uint32_t y) {
    const double fy = (static_cast<double>(y) + 0.5) * lh / h - 0.5;
    const std::int64_t y0 = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::floor(fy)), 0, lh - 1);
    const std::int64_t y1 = std::min<std::int64_t>(y0 + 1, lh - 1);
    const double ty = std::clamp(fy - y0, 0.0, 1.0);
    const double fx = (static_cast<double>(x) + 0.5) * lw / w - 0.5;
    const std::int64_t x0 = std::clamp<std::int64_t>(
        static_cast<std::int64_t>(std::floor(fx)), 0, lw - 1);
    const std::int64_t x1 = std::min<std::int64_t>(x0 + 1, lw - 1);
    const double tx = std::clamp(fx - x0, 0.0, 1.0);
    const pittore::RGBAf& a = px[std::size_t(y0) * lw + x0];
    const pittore::RGBAf& b = px[std::size_t(y0) * lw + x1];
    const pittore::RGBAf& c = px[std::size_t(y1) * lw + x0];
    const pittore::RGBAf& d = px[std::size_t(y1) * lw + x1];
    pittore::RGBAf o;
    const float w00 = static_cast<float>((1.0 - tx) * (1.0 - ty));
    const float w10 = static_cast<float>(tx * (1.0 - ty));
    const float w01 = static_cast<float>((1.0 - tx) * ty);
    const float w11 = static_cast<float>(tx * ty);
    o.r = a.r * w00 + b.r * w10 + c.r * w01 + d.r * w11;
    o.g = a.g * w00 + b.g * w10 + c.g * w01 + d.g * w11;
    o.b = a.b * w00 + b.b * w10 + c.b * w01 + d.b * w11;
    o.a = a.a * w00 + b.a * w10 + c.a * w01 + d.a * w11;
    return o;
}

// Materialize the field for rect [rx0,rx0+rw) x [ry0,ry0+rh) of an (fw x fh)
// frame from one level, into a compact rw*rh buffer — the rect-sized version
// of upsampleLevel (target mapping stays the FULL frame's, which is what
// makes it exact).
void sampleLevelRect(const pittore::RGBAf* px, std::uint32_t lw,
                     std::uint32_t lh, std::uint32_t fw, std::uint32_t fh,
                     std::uint32_t rx0, std::uint32_t ry0, std::uint32_t rw,
                     std::uint32_t rh, pittore::RGBAf* out) {
    pittore::core::parallel_rows(rh, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y)
            for (std::uint32_t x = 0; x < rw; ++x)
                out[std::size_t(y) * rw + x] =
                    sampleLevelAt(px, lw, lh, fw, fh, rx0 + x, ry0 + y);
    });
}

// Mean Oklab lightness of the blurred backdrop (strided subsample; exact
// value does not matter, only stability).
float tonePivotOf(const pittore::RGBAf* bLow, std::size_t n) {
    double pivotSum = 0;
    std::size_t pivotN = 0;
    for (std::size_t i = 0; i < n; i += 16) {
        if (bLow[i].a <= 0.0f) continue;
        float L, a, b;
        oklab::rgb_to_oklab(bLow[i].r, bLow[i].g, bLow[i].b, L, a, b);
        pivotSum += L;
        ++pivotN;
    }
    return pivotN > 0 ? static_cast<float>(pivotSum / pivotN) : 0.5f;
}

// Same strided subsample for a rect of a frame buffer (view = {0,0,rw} for a
// compact buffer). Used as the region path's pivot fallback, before any
// full-frame blend has published a global one.
float tonePivotOfRect(const pittore::RGBAf* base, const RectView& v,
                      std::uint32_t rw, std::uint32_t rh) {
    double pivotSum = 0;
    std::size_t pivotN = 0;
    const std::size_t n = std::size_t(rw) * rh;
    for (std::size_t i = 0; i < n; i += 16) {
        const std::uint32_t x = static_cast<std::uint32_t>(i % rw);
        const std::uint32_t y = static_cast<std::uint32_t>(i / rw);
        const pittore::RGBAf& px =
            base[std::size_t(v.oy + y) * v.stride + v.ox + x];
        if (px.a <= 0.0f) continue;
        float L, a, b;
        oklab::rgb_to_oklab(px.r, px.g, px.b, L, a, b);
        pivotSum += L;
        ++pivotN;
    }
    return pivotN > 0 ? static_cast<float>(pivotSum / pivotN) : 0.5f;
}

// The per-pixel grade — the single copy of the math, shared by the
// full-frame and region entry points so they cannot drift. Reads `group`
// through `dstView` and the fields through `lowView`; writes `dst` (same view
// as `group`, may alias it). Views let a region grade a rect of the frame
// while its fields are either a compact rect buffer or — for lowPass=0 — the
// frame itself.
void gradeTone(const pittore::RGBAf* group, const pittore::RGBAf* gLow,
               const pittore::RGBAf* bLow, const RectView& lowView,
               pittore::RGBAf* dst, const RectView& dstView,
               std::uint32_t w, std::uint32_t h, float pivot, float strength,
               float color, float contrast) {
    constexpr float kEps = 1e-4f;
    constexpr float kGainLo = 1.0f / 256.0f;  // ±8 stops, per research §4.5
    constexpr float kGainHi = 256.0f;
    // Row-parallel: destination texels are independent (the low-pass fields
    // are read-only here).
    pittore::core::parallel_rows(
        h, [&](std::uint32_t lo, std::uint32_t hi) {
            for (std::uint32_t y = lo; y < hi; ++y) {
                const std::size_t dBase =
                    std::size_t(dstView.oy + y) * dstView.stride + dstView.ox;
                const std::size_t lBase =
                    std::size_t(lowView.oy + y) * lowView.stride +
                    lowView.ox;
                for (std::uint32_t x = 0; x < w; ++x) {
                    const std::size_t di = dBase + x;
                    const std::size_t fi = lBase + x;
                    const pittore::RGBAf& g = group[di];
                    if (g.a <= 0.0f) {
                        dst[di] = pittore::RGBAf{0, 0, 0, 0};
                        continue;
                    }
                    const pittore::RGBAf& gl = gLow[fi];
                    const pittore::RGBAf& bl = bLow[fi];
                    // Gain field (neutral where the backdrop has no coverage).
                    float kr = 1.0f, kg = 1.0f, kb = 1.0f;
                    if (bl.a > 0.0f) {
                        kr = std::clamp(bl.r / (gl.r + kEps), kGainLo, kGainHi);
                        kg = std::clamp(bl.g / (gl.g + kEps), kGainLo, kGainHi);
                        kb = std::clamp(bl.b / (gl.b + kEps), kGainLo, kGainHi);
                    }
                    float tr = g.r * kr, tg = g.g * kg, tb = g.b * kb;
                    // Post stages in Oklab.
                    float L, a, b;
                    oklab::rgb_to_oklab(tr, tg, tb, L, a, b);
                    a *= color;
                    b *= color;
                    L = pivot + (L - pivot) * (1.0f + contrast);
                    float r, gg, bb;
                    oklab::oklab_to_rgb(L, a, b, r, gg, bb);
                    oklab::gamut_map(r, gg, bb);
                    dst[di].r = g.r + (r - g.r) * strength;
                    dst[di].g = g.g + (gg - g.g) * strength;
                    dst[di].b = g.b + (bb - g.b) * strength;
                    dst[di].a = g.a;
                }
            }
        });
}

Pyramid buildPyramid(const pittore::RGBAf* src, std::uint32_t w,
                     std::uint32_t h) {
    Pyramid pyr;
    pyr.levels.push_back(Pyramid::Level{w, h, src, {}});
    while (true) {
        const Pyramid::Level& prev = pyr.levels.back();
        if (prev.w <= 8 && prev.h <= 8) break;
        if (prev.w <= 1 && prev.h <= 1) break;
        const std::uint32_t nw = std::max<std::uint32_t>(1, prev.w / 2);
        const std::uint32_t nh = std::max<std::uint32_t>(1, prev.h / 2);
        Pyramid::Level next{nw, nh, nullptr,
                            std::vector<pittore::RGBAf>(
                                std::size_t(nw) * nh)};
        // Rows disjoint, per-pixel 2x2 reduction: bit-identical threaded.
        pittore::core::parallel_rows(
            nh, [&](std::uint32_t lo, std::uint32_t hi) {
                for (std::uint32_t y = lo; y < hi; ++y) {
                    for (std::uint32_t x = 0; x < nw; ++x) {
                        double r = 0, g = 0, b = 0, a = 0;
                        for (std::uint32_t sy = 0; sy < 2; ++sy)
                            for (std::uint32_t sx = 0; sx < 2; ++sx) {
                                const std::uint32_t px = std::min(2 * x + sx, prev.w - 1);
                                const std::uint32_t py = std::min(2 * y + sy, prev.h - 1);
                                const pittore::RGBAf& c =
                                    prev.px()[std::size_t(py) * prev.w + px];
                                r += c.r * c.a;
                                g += c.g * c.a;
                                b += c.b * c.a;
                                a += c.a;
                            }
                        pittore::RGBAf& o = next.owned[std::size_t(y) * nw + x];
                        if (a > 0.0) {
                            o.r = static_cast<float>(r / a);
                            o.g = static_cast<float>(g / a);
                            o.b = static_cast<float>(b / a);
                            o.a = static_cast<float>(a / 4.0);
                        } else {
                            o = pittore::RGBAf{0, 0, 0, 0};
                        }
                    }
                }
            });
        pyr.levels.push_back(std::move(next));
    }
    return pyr;
}

// Bilinear upsample of one pyramid level to full frame (clamp-to-edge).
// Row-parallel: output texels are independent.
void upsampleLevel(const Pyramid::Level& lv, std::uint32_t w, std::uint32_t h,
                   pittore::RGBAf* out) {
    // `out` comes from toneScratch (grow-only, thread-local): no zero-fill —
    // every element is written by the loop below anyway.
    pittore::core::parallel_rows(
        h, [&](std::uint32_t lo, std::uint32_t hi) {
            for (std::uint32_t y = lo; y < hi; ++y) {
                const double fy =
                    (static_cast<double>(y) + 0.5) * lv.h / h - 0.5;
                const std::int64_t y0 =
                    std::clamp<std::int64_t>(static_cast<std::int64_t>(std::floor(fy)),
                                             0, lv.h - 1);
                const std::int64_t y1 = std::min<std::int64_t>(y0 + 1, lv.h - 1);
                const double ty = std::clamp(fy - y0, 0.0, 1.0);
                for (std::uint32_t x = 0; x < w; ++x) {
                    const double fx =
                        (static_cast<double>(x) + 0.5) * lv.w / w - 0.5;
                    const std::int64_t x0 = std::clamp<std::int64_t>(
                        static_cast<std::int64_t>(std::floor(fx)), 0, lv.w - 1);
                    const std::int64_t x1 = std::min<std::int64_t>(x0 + 1, lv.w - 1);
                    const double tx = std::clamp(fx - x0, 0.0, 1.0);
                    const pittore::RGBAf& a =
                        lv.px()[std::size_t(y0) * lv.w + x0];
                    const pittore::RGBAf& b =
                        lv.px()[std::size_t(y0) * lv.w + x1];
                    const pittore::RGBAf& c =
                        lv.px()[std::size_t(y1) * lv.w + x0];
                    const pittore::RGBAf& d =
                        lv.px()[std::size_t(y1) * lv.w + x1];
                    pittore::RGBAf& o = out[std::size_t(y) * w + x];
                    const float w00 =
                        static_cast<float>((1.0 - tx) * (1.0 - ty));
                    const float w10 = static_cast<float>(tx * (1.0 - ty));
                    const float w01 = static_cast<float>((1.0 - tx) * ty);
                    const float w11 = static_cast<float>(tx * ty);
                    o.r = a.r * w00 + b.r * w10 + c.r * w01 + d.r * w11;
                    o.g = a.g * w00 + b.g * w10 + c.g * w01 + d.g * w11;
                    o.b = a.b * w00 + b.b * w10 + c.b * w01 + d.b * w11;
                    o.a = a.a * w00 + b.a * w10 + c.a * w01 + d.a * w11;
                }
            }
        });
}

}  // namespace

bool applyToneBlend(const pittore::RGBAf* group,
                    const pittore::RGBAf* backdrop, pittore::RGBAf* dst,
                    std::uint32_t w, std::uint32_t h,
                    const ToneBlendParams& p) {
    if (!group || !backdrop || !dst || w == 0 || h == 0) return false;
    const float strength = std::clamp(p.strength, 0.0f, 1.0f);
    if (strength <= 0.0f) return false;
    const float color = std::clamp(p.color, 0.0f, 1.0f);
    const float contrast = std::clamp(p.contrast, -1.0f, 1.0f);
    const float lp = std::clamp(p.lowPass, 0.0f, 1.0f);
    (void)p.contentType;  // rasterization hint only; math is identical

    // Low-pass fields: mip level as the blur knob (lp=1 coarsest/smoothest,
    // lp=0 full resolution, i.e. backdrop detail leaks into the group).
    Pyramid gpyr = buildPyramid(group, w, h);
    Pyramid bpyr = buildPyramid(backdrop, w, h);
    const std::size_t gli =
        std::min<std::size_t>(static_cast<std::size_t>(
                                  std::lround(lp * (gpyr.levels.size() - 1))),
                              gpyr.levels.size() - 1);
    const std::size_t bli =
        std::min<std::size_t>(static_cast<std::size_t>(
                                  std::lround(lp * (bpyr.levels.size() - 1))),
                              bpyr.levels.size() - 1);
    const std::size_t n = std::size_t(w) * h;
    pittore::RGBAf* gLow = toneScratch(0, n);
    pittore::RGBAf* bLow = toneScratch(1, n);
    upsampleLevel(gpyr.levels[gli], w, h, gLow);
    upsampleLevel(bpyr.levels[bli], w, h, bLow);
    // Hand the selected levels to applyToneBlendRegion: it re-samples these
    // texels with the same upsample formula instead of rebuilding a
    // region-local pyramid, which is both cheaper and exact (see the cache
    // comment). Moved, not copied — the pyramid dies with this call, and the
    // coarsest level is a handful of texels (7x4 at 4K/lp=1); the copy only
    // gets large if lowPass selects a near-full-res level.
    cacheSelectedLevel(0, gpyr, gli, w, h, lp);
    cacheSelectedLevel(1, bpyr, bli, w, h, lp);

    // Contrast pivot from the blurred backdrop, then published for the
    // region path (gPivotCache) — the same contract the device backends
    // keep between tone_blend and tone_blend_region.
    const float pivot = tonePivotOf(bLow, n);
    gPivotCache = pivot;
    gPivotValid = true;

    const RectView full{0, 0, w};
    gradeTone(group, gLow, bLow, full, dst, full, w, h, pivot, strength, color,
              contrast);
    return true;
}

// Region-bounded tone blend over full-frame buffers: re-grades exactly
// [rx0,rx1) x [ry0,ry1) of `dst`; everything outside is preserved
// byte-for-byte. Same contract as the GPU backends' tone_blend_region.
// Reads `backdrop` while writing `dst` is safe, as with applyToneBlend.
//
// Exactness: the region NEVER builds its own pyramid — a region-local pyramid
// picks its level from the region's own depth, i.e. a different blur scale
// than the frame (128px cells vs the frame's 256px at 1080p/lp=1, measured up
// to 0.14 abs error). Instead it re-samples the level the last full-frame
// blend selected and cached (same texels, same full-frame upsample formula →
// bit-identical field), and reuses the cached global pivot. The surrounding
// canvas was graded with exactly these, so the region cannot seam. Before any
// full-frame blend has published them (first use, or a lowPass/frame-size
// change since), it falls back to a region-local staged pyramid + regional
// pivot — an approximation that self-corrects on the next full rebuild.
bool applyToneBlendRegion(const pittore::RGBAf* group,
                          const pittore::RGBAf* backdrop,
                          pittore::RGBAf* dst, std::uint32_t w,
                          std::uint32_t h, std::uint32_t rx0,
                          std::uint32_t ry0, std::uint32_t rx1,
                          std::uint32_t ry1, const ToneBlendParams& p) {
    if (!group || !backdrop || !dst || w == 0 || h == 0) return false;
    const float strength = std::clamp(p.strength, 0.0f, 1.0f);
    if (strength <= 0.0f) return false;
    const float color = std::clamp(p.color, 0.0f, 1.0f);
    const float contrast = std::clamp(p.contrast, -1.0f, 1.0f);
    const float lp = std::clamp(p.lowPass, 0.0f, 1.0f);
    (void)p.contentType;

    rx0 = std::min(rx0, w);
    rx1 = std::min(rx1, w);
    ry0 = std::min(ry0, h);
    ry1 = std::min(ry1, h);
    if (rx1 <= rx0 || ry1 <= ry0) return false;

    const std::uint32_t rw = rx1 - rx0, rh = ry1 - ry0;
    const std::size_t rn = std::size_t(rw) * rh;
    // The level full frame would select for this frame size + lowPass
    // (selectedLevel mirrors that computation exactly).
    const std::size_t li = selectedLevel(w, h, lp);

    // Warm iff the caches hold this exact frame size and lowPass at this
    // exact level (floats compared bitwise — both sides pass through the
    // same std::clamp, so equal inputs give equal bits).
    const bool warm =
        gLvCache[0].valid && gLvCache[1].valid &&
        gLvCache[0].frameW == w && gLvCache[0].frameH == h &&
        gLvCache[1].frameW == w && gLvCache[1].frameH == h &&
        gLvCache[0].lp == lp && gLvCache[1].lp == lp &&
        gLvCache[0].level == static_cast<int>(li) &&
        gLvCache[1].level == static_cast<int>(li);

    const RectView dstView{rx0, ry0, w};

    if (warm && li == 0) {
        // lowPass=0: the field IS the source (level 0 upsamples to an exact
        // copy), so grade straight off the frame buffers — no scratch, no
        // staging. Per-pixel read-then-write keeps dst==group safe.
        const RectView v{rx0, ry0, w};
        const float pivot =
            gPivotValid ? gPivotCache : tonePivotOfRect(backdrop, v, rw, rh);
        gradeTone(group, group, backdrop, v, dst, dstView, rw, rh, pivot,
                  strength, color, contrast);
        return true;
    }

    if (warm) {
        // Rect-sized fields, sampled from the cached level with the full
        // frame's own mapping — bit-identical to what full frame used here.
        pittore::RGBAf* gLow = toneScratch(0, rn);
        pittore::RGBAf* bLow = toneScratch(1, rn);
        sampleLevelRect(gLvCache[0].px.data(), gLvCache[0].w, gLvCache[0].h,
                        w, h, rx0, ry0, rw, rh, gLow);
        sampleLevelRect(gLvCache[1].px.data(), gLvCache[1].w, gLvCache[1].h,
                        w, h, rx0, ry0, rw, rh, bLow);
        const RectView lowView{0, 0, rw};
        const float pivot =
            gPivotValid ? gPivotCache : tonePivotOfRect(bLow, lowView, rw, rh);
        gradeTone(group, gLow, bLow, lowView, dst, dstView, rw, rh, pivot,
                  strength, color, contrast);
        return true;
    }

    // Cold fallback: region-local staged pyramid + regional pivot (see the
    // contract comment above). Must match kToneHalo in renderRegionTone (and
    // the device paths).
    constexpr std::uint32_t kToneHalo = 32;
    const std::uint32_t ex0 = rx0 > kToneHalo ? rx0 - kToneHalo : 0;
    const std::uint32_t ey0 = ry0 > kToneHalo ? ry0 - kToneHalo : 0;
    const std::uint32_t ex1 = std::min(w, rx1 + kToneHalo);
    const std::uint32_t ey1 = std::min(h, ry1 + kToneHalo);
    const std::uint32_t ew = ex1 - ex0, eh = ey1 - ey0;
    const std::size_t en = std::size_t(ew) * eh;

    // Stage the region+halo contiguously: pyramid/upsample walk a compact
    // frame, and staging keeps the full-frame path's buffers untouched.
    pittore::RGBAf* sg = toneScratch(2, en);
    pittore::RGBAf* sb = toneScratch(3, en);
    for (std::uint32_t y = 0; y < eh; ++y) {
        std::memcpy(sg + std::size_t(y) * ew,
                    group + std::size_t(ey0 + y) * w + ex0,
                    std::size_t(ew) * sizeof(pittore::RGBAf));
        std::memcpy(sb + std::size_t(y) * ew,
                    backdrop + std::size_t(ey0 + y) * w + ex0,
                    std::size_t(ew) * sizeof(pittore::RGBAf));
    }

    const auto gpyr = buildPyramid(sg, ew, eh);
    const auto bpyr = buildPyramid(sb, ew, eh);
    const std::size_t gli = std::min<std::size_t>(
        static_cast<std::size_t>(std::lround(lp * (gpyr.levels.size() - 1))),
        gpyr.levels.size() - 1);
    const std::size_t bli = std::min<std::size_t>(
        static_cast<std::size_t>(std::lround(lp * (bpyr.levels.size() - 1))),
        bpyr.levels.size() - 1);
    pittore::RGBAf* gLow = toneScratch(0, en);
    pittore::RGBAf* bLow = toneScratch(1, en);
    upsampleLevel(gpyr.levels[gli], ew, eh, gLow);
    upsampleLevel(bpyr.levels[bli], ew, eh, bLow);

    // Regional pivot: no global one has been published yet (first use;
    // self-corrects on the next full rebuild), exactly like the device
    // backends.
    const float pivot = gPivotValid ? gPivotCache : tonePivotOf(bLow, en);

    // In place on the staged group copy (dst may alias group, same as the
    // full-frame contract).
    const RectView staged{0, 0, ew};
    gradeTone(sg, gLow, bLow, staged, sg, staged, ew, eh, pivot, strength,
              color, contrast);

    // Write back only the requested rect; the halo stays internal and
    // everything outside the rect in dst is preserved.
    const std::size_t iw = rx1 - rx0;
    for (std::uint32_t y = ry0; y < ry1; ++y) {
        std::memcpy(dst + std::size_t(y) * w + rx0,
                    sg + std::size_t(y - ey0) * ew + (rx0 - ex0),
                    iw * sizeof(pittore::RGBAf));
    }
    return true;
}

}  // namespace pittore::compute
