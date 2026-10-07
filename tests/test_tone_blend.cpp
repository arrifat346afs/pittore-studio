// Tone Blend group: mid-gray invariant, gain clamp, post stages,
// detail leak, content-type invariance, alpha discipline.
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/compute/oklab.h"
#include "engine/core/pixel.h"
#include "engine/compute/tone_blend.h"
#include "test_util.h"

using pittore::RGBAf;
using pittore::compute::ToneBlendParams;

namespace {

constexpr std::uint32_t W = 64, H = 64;

std::vector<RGBAf> solid(float r, float g, float b, float a = 1.0f) {
    return std::vector<RGBAf>(std::size_t(W) * H, RGBAf{r, g, b, a});
}

float luma709(float r, float g, float b) {
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float chroma(float r, float g, float b) {
    using pittore::compute::oklab::rgb_to_oklab;
    float L, a, bb;
    rgb_to_oklab(r, g, b, L, a, bb);
    return std::sqrt(a * a + bb * bb);
}

bool finite(float v) { return std::isfinite(v); }

}  // namespace

// Mid-gray group reproduces the blurred backdrop.
static void test_midgray_reproduces_backdrop() {
    auto g = solid(0.5f, 0.5f, 0.5f);
    std::vector<RGBAf> b(std::size_t(W) * H);
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            b[std::size_t(y) * W + x] =
                x < W / 2 ? RGBAf{0.2f, 0.2f, 0.2f, 1} : RGBAf{0.8f, 0.8f, 0.8f, 1};
    std::vector<RGBAf> out(std::size_t(W) * H);
    ToneBlendParams p;  // defaults: full strength, color, no contrast, max blur
    CHECK(applyToneBlend(g.data(), b.data(), out.data(), W, H, p));
    // Sample well inside each half, away from the edge.
    const RGBAf l = out[std::size_t(H / 2) * W + 8];
    const RGBAf r = out[std::size_t(H / 2) * W + (W - 9)];
    CHECK_NEAR(l.r, 0.2f, 0.03f);
    CHECK_NEAR(l.g, 0.2f, 0.03f);
    CHECK_NEAR(l.b, 0.2f, 0.03f);
    CHECK_NEAR(r.r, 0.8f, 0.03f);
    CHECK_NEAR(r.g, 0.8f, 0.03f);
    CHECK_NEAR(r.b, 0.8f, 0.03f);
}

// Strength 0 = no-op.
static void test_strength_zero_identity() {
    auto g = solid(0.9f, 0.2f, 0.4f);
    auto b = solid(0.1f, 0.8f, 0.3f);
    std::vector<RGBAf> out(std::size_t(W) * H, RGBAf{0, 0, 0, 0});
    ToneBlendParams p;
    p.strength = 0.0f;
    CHECK(!applyToneBlend(g.data(), b.data(), out.data(), W, H, p));
}

// Near-black group on bright backdrop: gains clamp, stays finite in gamut.
static void test_gain_clamp_no_blowup() {
    auto g = solid(0.001f, 0.002f, 0.0015f);
    auto b = solid(1.0f, 0.9f, 0.8f);
    std::vector<RGBAf> out(std::size_t(W) * H);
    ToneBlendParams p;
    CHECK(applyToneBlend(g.data(), b.data(), out.data(), W, H, p));
    for (std::size_t i = 0; i < std::size_t(W) * H; i += 97) {
        CHECK(finite(out[i].r) && finite(out[i].g) && finite(out[i].b));
        CHECK(out[i].r >= 0.0f && out[i].r <= 1.0f);
        CHECK(out[i].g >= 0.0f && out[i].g <= 1.0f);
        CHECK(out[i].b >= 0.0f && out[i].b <= 1.0f);
        CHECK_EQ(out[i].a, 1.0f);
    }
}

// Color 0 desaturates but keeps tone.
static void test_color_zero_desaturates() {
    std::vector<RGBAf> g(std::size_t(W) * H);
    for (std::uint32_t x = 0; x < W; ++x) {
        const float t = x / float(W - 1);
        for (std::uint32_t y = 0; y < H; ++y)
            g[std::size_t(y) * W + x] = RGBAf{t, 1.0f - t, 0.2f, 1};
    }
    auto b = solid(0.5f, 0.5f, 0.5f);
    std::vector<RGBAf> full(std::size_t(W) * H), flat(std::size_t(W) * H);
    ToneBlendParams p;
    CHECK(applyToneBlend(g.data(), b.data(), full.data(), W, H, p));
    p.color = 0.0f;
    CHECK(applyToneBlend(g.data(), b.data(), flat.data(), W, H, p));
    double chromaFull = 0, chromaFlat = 0;
    float loFull = 1e9f, hiFull = -1e9f, loFlat = 1e9f, hiFlat = -1e9f;
    for (std::size_t i = 0; i < std::size_t(W) * H; i += 7) {
        chromaFull += chroma(full[i].r, full[i].g, full[i].b);
        chromaFlat += chroma(flat[i].r, flat[i].g, flat[i].b);
        const float lf = luma709(full[i].r, full[i].g, full[i].b);
        const float fl = luma709(flat[i].r, flat[i].g, flat[i].b);
        loFull = std::min(loFull, lf);
        hiFull = std::max(hiFull, lf);
        loFlat = std::min(loFlat, fl);
        hiFlat = std::max(hiFlat, fl);
    }
    CHECK(chromaFlat < 0.25 * chromaFull);  // desaturated
    // Tone sweep survives.
    CHECK_NEAR(hiFlat - loFlat, hiFull - loFull, 0.15f);
}

// Contrast +1 stretches, -1 flattens (identity exchange here).
static void test_contrast_direction() {
    std::vector<RGBAf> g(std::size_t(W) * H), b(std::size_t(W) * H);
    for (std::uint32_t x = 0; x < W; ++x) {
        const float v = x < W / 2 ? 0.4f : 0.6f;
        for (std::uint32_t y = 0; y < H; ++y) {
            g[std::size_t(y) * W + x] = RGBAf{v, v, v, 1};
            b[std::size_t(y) * W + x] = RGBAf{v, v, v, 1};
        }
    }
    std::vector<RGBAf> up(std::size_t(W) * H), down(std::size_t(W) * H);
    ToneBlendParams p;
    p.contrast = 1.0f;
    CHECK(applyToneBlend(g.data(), b.data(), up.data(), W, H, p));
    p.contrast = -1.0f;
    CHECK(applyToneBlend(g.data(), b.data(), down.data(), W, H, p));
    const float spreadUp = std::fabs(up[8].r - up[W - 9].r);
    const float spreadDown = std::fabs(down[8].r - down[W - 9].r);
    CHECK(spreadUp > 0.2f);      // stretched past input span
    CHECK(spreadDown < 0.05f);   // flattened toward pivot
}

// LowPass 0 leaks backdrop detail; max blur smooths it.
static void test_lowpass_detail_leak() {
    auto g = solid(0.5f, 0.5f, 0.5f);
    std::vector<RGBAf> b(std::size_t(W) * H);
    for (std::uint32_t y = 0; y < H; ++y)
        for (std::uint32_t x = 0; x < W; ++x)
            b[std::size_t(y) * W + x] =
                ((x + y) & 1) ? RGBAf{0.9f, 0.9f, 0.9f, 1}
                              : RGBAf{0.1f, 0.1f, 0.1f, 1};
    std::vector<RGBAf> smooth(std::size_t(W) * H), leaky(std::size_t(W) * H);
    ToneBlendParams p;
    CHECK(applyToneBlend(g.data(), b.data(), smooth.data(), W, H, p));
    p.lowPass = 0.0f;
    CHECK(applyToneBlend(g.data(), b.data(), leaky.data(), W, H, p));
    double varSmooth = 0, varLeaky = 0;
    for (std::size_t i = 0; i < std::size_t(W) * H; ++i) {
        varSmooth += (smooth[i].r - 0.5) * (smooth[i].r - 0.5);
        varLeaky += (leaky[i].r - 0.5) * (leaky[i].r - 0.5);
    }
    varSmooth /= std::size_t(W) * H;
    varLeaky /= std::size_t(W) * H;
    CHECK(varSmooth < 0.25 * varLeaky);  // blur melts checker
    CHECK(varLeaky > 0.05);              // full-res keeps it
}

// ContentType is a hint only, never changes math.
static void test_content_type_invariant() {
    auto g = solid(0.7f, 0.3f, 0.2f);
    auto b = solid(0.2f, 0.4f, 0.8f);
    std::vector<RGBAf> a(std::size_t(W) * H), v(std::size_t(W) * H),
        t(std::size_t(W) * H);
    ToneBlendParams p;
    p.contentType = 0;
    CHECK(applyToneBlend(g.data(), b.data(), a.data(), W, H, p));
    p.contentType = 1;
    CHECK(applyToneBlend(g.data(), b.data(), v.data(), W, H, p));
    p.contentType = 2;
    CHECK(applyToneBlend(g.data(), b.data(), t.data(), W, H, p));
    for (std::size_t i = 0; i < std::size_t(W) * H; i += 13) {
        CHECK_NEAR(a[i].r, v[i].r, 1e-6f);
        CHECK_NEAR(a[i].r, t[i].r, 1e-6f);
        CHECK_NEAR(a[i].g, v[i].g, 1e-6f);
        CHECK_NEAR(a[i].b, t[i].b, 1e-6f);
    }
}

// Alpha: transparent stays transparent, missing backdrop stays finite.
static void test_alpha_discipline() {
    std::vector<RGBAf> g(std::size_t(W) * H, RGBAf{0.8f, 0.2f, 0.2f, 1});
    g[0] = RGBAf{9.0f, 9.0f, 9.0f, 0};  // transparent garbage
    auto b = solid(0.3f, 0.3f, 0.3f);
    std::vector<RGBAf> clear(std::size_t(W) * H, RGBAf{0, 0, 0, 0});
    std::vector<RGBAf> out(std::size_t(W) * H), out2(std::size_t(W) * H);
    ToneBlendParams p;
    CHECK(applyToneBlend(g.data(), b.data(), out.data(), W, H, p));
    CHECK_EQ(out[0].a, 0.0f);
    CHECK(finite(out[1].r) && finite(out[1].g) && finite(out[1].b));
    CHECK(applyToneBlend(g.data(), clear.data(), out2.data(), W, H, p));
    CHECK(finite(out2[1].r) && finite(out2[1].g) && finite(out2[1].b));
    CHECK_EQ(out2[1].a, 1.0f);
}

// Region path: re-grading a rect must reproduce exactly what full frame
// produced there (same cached level texels, same upsample formula, same
// pivot — verified bit-identical by construction), and must leave everything
// outside the rect untouched.
static void test_region_matches_full_frame() {
    constexpr std::uint32_t w = 160, h = 96;
    const std::size_t n = std::size_t(w) * h;
    std::vector<RGBAf> g(n), b(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float x = float(i % w) / float(w);
        const float y = float(i / w) / float(h);
        g[i] = {0.45f + 0.25f * x, 0.40f + 0.20f * y, 0.55f,
                (i % 37 == 0) ? 0.0f : 1.0f};
        b[i] = {0.35f + 0.30f * y, 0.50f - 0.20f * x, 0.30f,
                (i % 53 == 0) ? 0.0f : 1.0f};
    }
    for (float lp : {0.0f, 0.35f, 1.0f}) {
        ToneBlendParams p;
        p.lowPass = lp;
        p.contrast = 0.5f;
        std::vector<RGBAf> full(n), reg(n);
        CHECK(applyToneBlend(g.data(), b.data(), full.data(), w, h, p));
        const std::uint32_t rects[][4] = {{40, 24, 100, 60},
                                          {0, 0, 50, 30},
                                          {w - 40, h - 24, w, h},
                                          {0, 0, w, h}};
        for (auto& r : rects) {
            reg = g;
            CHECK(applyToneBlendRegion(g.data(), b.data(), reg.data(), w, h,
                                       r[0], r[1], r[2], r[3], p));
            for (std::size_t i = 0; i < n; ++i) {
                const std::uint32_t x = std::uint32_t(i % w);
                const std::uint32_t y = std::uint32_t(i / w);
                const bool in =
                    x >= r[0] && x < r[2] && y >= r[1] && y < r[3];
                const RGBAf& want = in ? full[i] : g[i];
                CHECK_EQ(reg[i].r, want.r);
                CHECK_EQ(reg[i].g, want.g);
                CHECK_EQ(reg[i].b, want.b);
                CHECK_EQ(reg[i].a, want.a);
            }
        }
    }
}

// Region fallback (lowPass with no cached level — e.g. params changed since
// the last full-frame blend): must still preserve everything outside the
// rect and produce finite output inside. Strength 0 stays a no-op.
static void test_region_fallback_preserves_outside() {
    constexpr std::uint32_t w = 160, h = 96;
    const std::size_t n = std::size_t(w) * h;
    std::vector<RGBAf> g(n), b(n);
    for (std::size_t i = 0; i < n; ++i) {
        g[i] = {0.6f, 0.4f, 0.3f, 1.0f};
        b[i] = {0.3f, 0.5f, 0.4f, 1.0f};
    }
    ToneBlendParams full;
    full.lowPass = 1.0f;  // warms the cache for lp=1 only
    std::vector<RGBAf> tmp(n), reg(n);
    CHECK(applyToneBlend(g.data(), b.data(), tmp.data(), w, h, full));
    ToneBlendParams p;
    p.lowPass = 0.35f;  // cache miss -> staged-pyramid fallback
    p.contrast = 0.5f;
    reg = g;
    CHECK(applyToneBlendRegion(g.data(), b.data(), reg.data(), w, h, 20, 12,
                               120, 70, p));
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t x = std::uint32_t(i % w);
        const std::uint32_t y = std::uint32_t(i / w);
        const bool in = x >= 20 && x < 120 && y >= 12 && y < 70;
        if (!in) {
            CHECK_EQ(reg[i].r, g[i].r);
            CHECK_EQ(reg[i].g, g[i].g);
            CHECK_EQ(reg[i].b, g[i].b);
            CHECK_EQ(reg[i].a, g[i].a);
        } else {
            CHECK(finite(reg[i].r) && finite(reg[i].g) && finite(reg[i].b));
            CHECK_EQ(reg[i].a, 1.0f);
        }
    }
    ToneBlendParams off;
    off.strength = 0.0f;
    reg = g;
    CHECK(!applyToneBlendRegion(g.data(), b.data(), reg.data(), w, h, 20, 12,
                                120, 70, off));
    CHECK(std::memcmp(reg.data(), g.data(), n * sizeof(RGBAf)) == 0);
}

static void test_tone_blend() {
    test_midgray_reproduces_backdrop();
    test_strength_zero_identity();
    test_gain_clamp_no_blowup();
    test_color_zero_desaturates();
    test_contrast_direction();
    test_lowpass_detail_leak();
    test_content_type_invariant();
    test_alpha_discipline();
    test_region_matches_full_frame();
    test_region_fallback_preserves_outside();
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_tone_blend)
#endif
