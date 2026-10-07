// Blend kernels vs hand-computed reference values across the full 27-mode
// table (plus Pass Through). The reference below is written independently
// from the engine (straight-alpha compositing + per-mode B()) so the matrix is
// a genuine cross-check, and a few hand-derived values lock the spec itself.
#include <cmath>
#include <cstring>
#include <vector>

#include "engine/compute/factory.h"
#include "engine/compute/blend.h"
#include "engine/core/pixel.h"
#include "test_util.h"

using pittore::RGBAf;
using pittore::compute::BlendMode;

// --- Hand-derived reference implementations (spec formulas) ----------------

namespace ref {

float bmin(float a, float b) { return a < b ? a : b; }
float bmax(float a, float b) { return a > b ? a : b; }
float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

float lum(float r, float g, float b) { return 0.3f * r + 0.59f * g + 0.11f * b; }
float sat(float r, float g, float b) {
    return bmax(r, bmax(g, b)) - bmin(r, bmin(g, b));
}

void clip(float r, float g, float b, float& cr, float& cg, float& cb) {
    const float l = lum(r, g, b);
    const float n = bmin(r, bmin(g, b));
    const float x = bmax(r, bmax(g, b));
    cr = r;
    cg = g;
    cb = b;
    if (n < 0.0f) {
        const float k = l / (l - n);
        cr = l + (cr - l) * k;
        cg = l + (cg - l) * k;
        cb = l + (cb - l) * k;
    }
    if (x > 1.0f) {
        const float k = (1.0f - l) / (x - l);
        cr = l + (cr - l) * k;
        cg = l + (cg - l) * k;
        cb = l + (cb - l) * k;
    }
}

void set_lum(float r, float g, float b, float l, float& cr, float& cg, float& cb) {
    const float d = l - lum(r, g, b);
    clip(r + d, g + d, b + d, cr, cg, cb);
}

// set_sat(c, t): scale c's channel span to t, keeping the mid channel's ratio.
void set_sat(const float* c, float t, float* out) {
    const float mn = bmin(c[0], bmin(c[1], c[2]));
    const float mx = bmax(c[0], bmax(c[1], c[2]));
    const float mid = c[0] + c[1] + c[2] - mn - mx;
    out[0] = out[1] = out[2] = 0.0f;
    if (mx > mn) {
        int imin = 0, imax = 0;
        if (c[1] < c[0]) imin = 1;
        if (c[2] < (imin == 1 ? c[1] : c[0])) imin = 2;
        if (c[1] > c[0]) imax = 1;
        if (c[2] > (imax == 1 ? c[1] : c[0])) imax = 2;
        const int imid = 3 - imin - imax;
        out[imid] = (mid - mn) * t / (mx - mn);
        out[imax] = t;
    }
}

// Separable B() for one channel (b = backdrop, s = source), clamped to [0,1].
float sep(BlendMode m, float b, float s) {
    switch (m) {
        case BlendMode::Darken: return bmin(b, s);
        case BlendMode::Multiply: return b * s;
        case BlendMode::ColorBurn:
            if (b >= 1.0f) return 1.0f;
            if (s <= 0.0f) return 0.0f;
            return 1.0f - bmin((1.0f - b) / s, 1.0f);
        case BlendMode::LinearBurn: return b + s - 1.0f;
        case BlendMode::Lighten: return bmax(b, s);
        case BlendMode::Screen: return b + s - b * s;
        case BlendMode::ColorDodge:
            if (b <= 0.0f) return 0.0f;
            if (s >= 1.0f) return 1.0f;
            return bmin(b / (1.0f - s), 1.0f);
        case BlendMode::LinearDodge: return b + s;
        case BlendMode::Overlay:  // hard_light(s, b)
            return b <= 0.5f ? 2.0f * b * s
                             : 2.0f * b + 2.0f * s - 2.0f * b * s - 1.0f;
        case BlendMode::SoftLight:
            if (s <= 0.5f) return b - (1.0f - 2.0f * s) * b * (1.0f - b);
            {
                const float d = b <= 0.25f ? ((16.0f * b - 12.0f) * b + 4.0f) * b
                                           : std::sqrt(b);
                return b + (2.0f * s - 1.0f) * (d - b);
            }
        case BlendMode::HardLight:  // hard_light(b, s)
            return s <= 0.5f ? 2.0f * b * s
                             : 2.0f * b + 2.0f * s - 2.0f * b * s - 1.0f;
        case BlendMode::VividLight:
            if (s <= 0.5f) {  // color_burn(b, 2s)
                if (b >= 1.0f) return 1.0f;
                if (2.0f * s <= 0.0f) return 0.0f;
                return 1.0f - bmin((1.0f - b) / (2.0f * s), 1.0f);
            }
            {  // color_dodge(b, 2s - 1)
                if (b <= 0.0f) return 0.0f;
                if (2.0f * s - 1.0f >= 1.0f) return 1.0f;
                return bmin(b / (1.0f - (2.0f * s - 1.0f)), 1.0f);
            }
        case BlendMode::LinearLight: return b + 2.0f * s - 1.0f;
        case BlendMode::PinLight:
            return s <= 0.5f ? bmin(b, 2.0f * s) : bmax(b, 2.0f * s - 1.0f);
        case BlendMode::HardMix: return b + s >= 1.0f ? 1.0f : 0.0f;
        case BlendMode::Difference: return b > s ? b - s : s - b;
        case BlendMode::Exclusion: return b + s - 2.0f * b * s;
        case BlendMode::Subtract: return b - s;
        case BlendMode::Divide: return s <= 0.0f ? 1.0f : b / s;
        default: return s;  // Normal / Dissolve / PassThrough / HSL family
    }
}

// Full-colour B() for the HSL family (b/rgb = backdrop, s/rgb = source).
void color(BlendMode m, float br, float bg, float bb, float sr, float sg,
           float sb, float& cr, float& cg, float& cb) {
    switch (m) {
        case BlendMode::DarkerColor:
            if (lum(sr, sg, sb) < lum(br, bg, bb)) { cr = sr; cg = sg; cb = sb; }
            else { cr = br; cg = bg; cb = bb; }
            return;
        case BlendMode::LighterColor:
            if (lum(sr, sg, sb) > lum(br, bg, bb)) { cr = sr; cg = sg; cb = sb; }
            else { cr = br; cg = bg; cb = bb; }
            return;
        default:
            break;
    }
    const float b[3] = {br, bg, bb};
    const float s[3] = {sr, sg, sb};
    if (m == BlendMode::Hue) {
        // set_lum(set_sat(cs, sat(cb)), lum(cb))
        float o[3];
        set_sat(s, sat(b[0], b[1], b[2]), o);
        set_lum(o[0], o[1], o[2], lum(b[0], b[1], b[2]), cr, cg, cb);
    } else if (m == BlendMode::Saturation) {
        // set_lum(set_sat(cb, sat(cs)), lum(cb))
        float o[3];
        set_sat(b, sat(s[0], s[1], s[2]), o);
        set_lum(o[0], o[1], o[2], lum(b[0], b[1], b[2]), cr, cg, cb);
    } else if (m == BlendMode::Color) {
        set_lum(s[0], s[1], s[2], lum(b[0], b[1], b[2]), cr, cg, cb);
    } else {  // Luminosity
        set_lum(b[0], b[1], b[2], lum(s[0], s[1], s[2]), cr, cg, cb);
    }
}

// One composite pixel: B() then the straight-alpha compositing equation.
RGBAf pixel(BlendMode m, const RGBAf& s, const RGBAf& d, int x, int y) {
    const float as = s.a, ab = d.a;
    if (m == BlendMode::Dissolve) {
        std::uint32_t h = static_cast<std::uint32_t>(x) * 0x9E3779B9u ^
                          static_cast<std::uint32_t>(y) * 0x85EBCA6Bu;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        const float hash = static_cast<float>(h >> 8) / 16777216.0f;
        if (as > hash) return RGBAf{s.r, s.g, s.b, 1.0f};
        return d;
    }
    if (as <= 0.0f) return d;
    float br, bg, bb;
    const bool hsl = m == BlendMode::Hue || m == BlendMode::Saturation ||
                     m == BlendMode::Color || m == BlendMode::Luminosity ||
                     m == BlendMode::DarkerColor || m == BlendMode::LighterColor;
    if (hsl) {
        color(m, d.r, d.g, d.b, s.r, s.g, s.b, br, bg, bb);
    } else {
        br = clamp01(sep(m, d.r, s.r));
        bg = clamp01(sep(m, d.g, s.g));
        bb = clamp01(sep(m, d.b, s.b));
    }
    const float ao = as + ab * (1.0f - as);
    if (ao <= 0.0f) return RGBAf{0, 0, 0, 0};
    const float inv = 1.0f / ao;
    return RGBAf{(s.r * as * (1.0f - ab) + br * as * ab + d.r * ab * (1.0f - as)) * inv,
                 (s.g * as * (1.0f - ab) + bg * as * ab + d.g * ab * (1.0f - as)) * inv,
                 (s.b * as * (1.0f - ab) + bb * as * ab + d.b * ab * (1.0f - as)) * inv,
                 ao};
}

}  // namespace ref

// --- Legacy Normal / Multiply reference (kept from the original suite) ------

RGBAf composite_one(const RGBAf& dst, const RGBAf& src, bool multiply) {
    const float as = src.a, ab = dst.a;
    if (as <= 0.0f) return dst;  // matches the engine's transparent-source short-cut
    const float inv_as = 1.0f - as, inv_ab = 1.0f - ab;
    float cr, cg, cb;
    if (multiply) {
        cr = inv_ab * (src.r * as) + inv_as * (dst.r * ab) + (src.r * as) * (dst.r * ab);
        cg = inv_ab * (src.g * as) + inv_as * (dst.g * ab) + (src.g * as) * (dst.g * ab);
        cb = inv_ab * (src.b * as) + inv_as * (dst.b * ab) + (src.b * as) * (dst.b * ab);
    } else {
        cr = (src.r * as) + (dst.r * ab) * inv_as;
        cg = (src.g * as) + (dst.g * ab) * inv_as;
        cb = (src.b * as) + (dst.b * ab) * inv_as;
    }
    const float a_out = as + ab * inv_as;
    if (a_out > 0.0f) return RGBAf{cr / a_out, cg / a_out, cb / a_out, a_out};
    return RGBAf{0.0f, 0.0f, 0.0f, 0.0f};
}

void check_pixel(const RGBAf& got, const RGBAf& want) {
    CHECK_NEAR(got.r, want.r, 1e-5f);
    CHECK_NEAR(got.g, want.g, 1e-5f);
    CHECK_NEAR(got.b, want.b, 1e-5f);
    CHECK_NEAR(got.a, want.a, 1e-5f);
}

void test_blend() {
    auto backend = pittore::compute::make_default_backend();

    constexpr std::uint32_t w = 8, h = 1;
    const std::size_t n = static_cast<std::size_t>(w) * h;

    // bottom: red opaque, teal half-alpha, fully transparent
    std::vector<RGBAf> bottom = {
        RGBAf{1, 0, 0, 1},      RGBAf{0, 1, 1, 0.5f}, RGBAf{0.5f, 0.5f, 0.5f, 0},
        RGBAf{0.1f, 0.2f, 0.3f, 1}, RGBAf{0, 0, 1, 1}, RGBAf{1, 1, 0, 1},
        RGBAf{0, 0, 0, 0},      RGBAf{0.25f, 0.5f, 0.75f, 0.8f},
    };
    // top: translucent blue over everything, plus a fully transparent column
    std::vector<RGBAf> top = {
        RGBAf{0, 0, 1, 0.5f}, RGBAf{0, 0, 1, 0.5f}, RGBAf{0, 0, 0, 0},
        RGBAf{0, 1, 0, 0.4f}, RGBAf{1, 0, 0, 1},    RGBAf{0, 0, 0, 0},
        RGBAf{1, 1, 1, 0.25f}, RGBAf{1, 1, 1, 0.25f},
    };

    auto bbuf = backend->make_buffer(n * sizeof(RGBAf));
    std::memcpy(bbuf->host(), bottom.data(), bbuf->size());
    auto tbuf = backend->make_buffer(n * sizeof(RGBAf));
    std::memcpy(tbuf->host(), top.data(), tbuf->size());

    backend->composite(*bbuf, *tbuf, w, h, pittore::compute::BlendMode::Normal);
    const auto* normal = static_cast<const RGBAf*>(bbuf->host());
    for (std::size_t i = 0; i < n; ++i)
        check_pixel(normal[i], composite_one(bottom[i], top[i], false));

    // Reset and test multiply.
    std::memcpy(bbuf->host(), bottom.data(), bbuf->size());
    backend->composite(*bbuf, *tbuf, w, h, pittore::compute::BlendMode::Multiply);
    const auto* mult = static_cast<const RGBAf*>(bbuf->host());
    for (std::size_t i = 0; i < n; ++i)
        check_pixel(mult[i], composite_one(bottom[i], top[i], true));

    // Opaque multiply sanity: red over blue -> black.
    CHECK_NEAR(mult[4].r, 0.0f, 1e-6f);
    CHECK_NEAR(mult[4].g, 0.0f, 1e-6f);
    CHECK_NEAR(mult[4].b, 0.0f, 1e-6f);

    // --- Hand-derived values lock the spec itself. Source = blue, backdrop =
    // red, both opaque, so the compositing equation collapses to B() directly.
    {
        const RGBAf red{0.8f, 0.2f, 0.4f, 1.0f};
        const RGBAf blue{0.3f, 0.7f, 0.5f, 1.0f};
        float rr, rg, rb, ra;
        auto& bl = pittore::compute::blend::pixel;

        bl(BlendMode::Screen, blue.r, blue.g, blue.b, blue.a,
           red.r, red.g, red.b, red.a, 0, 0, rr, rg, rb, ra);
        CHECK_NEAR(rr, 0.86f, 1e-6f);  // 0.8 + 0.3 - 0.8*0.3
        CHECK_NEAR(rg, 0.76f, 1e-6f);  // 0.2 + 0.7 - 0.2*0.7
        CHECK_NEAR(rb, 0.70f, 1e-6f);  // 0.4 + 0.5 - 0.4*0.5

        bl(BlendMode::Difference, blue.r, blue.g, blue.b, blue.a,
           red.r, red.g, red.b, red.a, 0, 0, rr, rg, rb, ra);
        CHECK_NEAR(rr, 0.5f, 1e-6f);
        CHECK_NEAR(rg, 0.5f, 1e-6f);
        CHECK_NEAR(rb, 0.1f, 1e-6f);

        bl(BlendMode::Divide, blue.r, blue.g, blue.b, blue.a,
           red.r, red.g, red.b, red.a, 0, 0, rr, rg, rb, ra);
        CHECK_NEAR(rr, 1.0f, 1e-6f);        // 0.8/0.3 clamped
        CHECK_NEAR(rg, 0.2f / 0.7f, 1e-6f); // 0.2/0.7
        CHECK_NEAR(rb, 0.8f, 1e-6f);        // 0.4/0.5

        bl(BlendMode::HardMix, blue.r, blue.g, blue.b, blue.a,
           red.r, red.g, red.b, red.a, 0, 0, rr, rg, rb, ra);
        CHECK_NEAR(rr, 1.0f, 1e-6f);  // 0.8+0.3 >= 1
        CHECK_NEAR(rg, 0.0f, 1e-6f);  // 0.2+0.7 <  1
        CHECK_NEAR(rb, 0.0f, 1e-6f);  // 0.4+0.5 <  1

        bl(BlendMode::Multiply, blue.r, blue.g, blue.b, blue.a,
           red.r, red.g, red.b, red.a, 0, 0, rr, rg, rb, ra);
        CHECK_NEAR(rr, 0.24f, 1e-6f);
    }

    // --- Full mode matrix against the independent reference.
    constexpr BlendMode allModes[] = {
        BlendMode::Normal,        BlendMode::Multiply,    BlendMode::Dissolve,
        BlendMode::Darken,        BlendMode::ColorBurn,   BlendMode::LinearBurn,
        BlendMode::DarkerColor,   BlendMode::Lighten,     BlendMode::Screen,
        BlendMode::ColorDodge,    BlendMode::LinearDodge, BlendMode::LighterColor,
        BlendMode::Overlay,       BlendMode::SoftLight,   BlendMode::HardLight,
        BlendMode::VividLight,    BlendMode::LinearLight, BlendMode::PinLight,
        BlendMode::HardMix,       BlendMode::Difference,  BlendMode::Exclusion,
        BlendMode::Subtract,      BlendMode::Divide,      BlendMode::Hue,
        BlendMode::Saturation,    BlendMode::Color,       BlendMode::Luminosity,
        BlendMode::PassThrough,
    };

    for (BlendMode mode : allModes) {
        std::vector<RGBAf> want(n);
        for (std::size_t i = 0; i < n; ++i)
            want[i] = ref::pixel(mode, top[i], bottom[i],
                                 static_cast<int>(i % w), static_cast<int>(i / w));
        std::memcpy(bbuf->host(), bottom.data(), bbuf->size());
        backend->composite(*bbuf, *tbuf, w, h, mode);
        const auto* out = static_cast<const RGBAf*>(bbuf->host());
        std::size_t mismatches = 0;
        for (std::size_t i = 0; i < n; ++i) {
            const RGBAf& g = out[i];
            const RGBAf& e = want[i];
            if (std::abs(g.r - e.r) > 1e-5f || std::abs(g.g - e.g) > 1e-5f ||
                std::abs(g.b - e.b) > 1e-5f || std::abs(g.a - e.a) > 1e-5f)
                ++mismatches;
        }
        CHECK_EQ(mismatches, 0u);
    }

    // --- Pass Through is per-pixel Normal.
    std::memcpy(bbuf->host(), bottom.data(), bbuf->size());
    backend->composite(*bbuf, *tbuf, w, h, BlendMode::PassThrough);
    const auto* pt = static_cast<const RGBAf*>(bbuf->host());
    for (std::size_t i = 0; i < n; ++i) check_pixel(pt[i], normal[i]);

    // --- Dissolve determinism + alpha-1 / alpha-0 edge cases.
    {
        std::memcpy(bbuf->host(), bottom.data(), bbuf->size());
        backend->composite(*bbuf, *tbuf, w, h, BlendMode::Dissolve);
        const auto* r1 = static_cast<const RGBAf*>(bbuf->host());
        std::memcpy(bbuf->host(), bottom.data(), bbuf->size());
        backend->composite(*bbuf, *tbuf, w, h, BlendMode::Dissolve);
        const auto* r2 = static_cast<const RGBAf*>(bbuf->host());
        std::size_t diffs = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (r1[i].r != r2[i].r || r1[i].g != r2[i].g || r1[i].b != r2[i].b ||
                r1[i].a != r2[i].a)
                ++diffs;
        CHECK_EQ(diffs, 0u);

        // Opaque source always reveals (the hash is < 1): pixel 4 → red, opaque.
        CHECK_NEAR(r1[4].r, 1.0f, 0.0f);
        CHECK_NEAR(r1[4].a, 1.0f, 0.0f);
        // Fully transparent source never reveals: pixel 2 keeps the backdrop.
        CHECK_EQ(r1[2].a, 0.0f);
    }

    // --- Display-name round trip.
    for (BlendMode mode : allModes) {
        const char* name = pittore::compute::blend::display_name(mode);
        CHECK_EQ(pittore::compute::blend::from_display_name(name), mode);
    }
    CHECK_EQ(pittore::compute::blend::from_display_name("Linear Dodge (Add)"),
             BlendMode::LinearDodge);
    CHECK_EQ(pittore::compute::blend::from_display_name("nonsense"),
             BlendMode::Normal);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_blend)
#endif