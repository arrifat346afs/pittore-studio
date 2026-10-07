// Tonal brush + bucket-fill kernels: flood-fill region/tolerance/contiguity/
// antialias/erase semantics and the Dodge/Burn/Sponge tonal operator.
#include <vector>

#include "engine/compute/paint.h"
#include "engine/core/pixel.h"
#include "test_util.h"

using pittore::RGBAf;
using pittore::compute::flood_fill_host;
using pittore::compute::SelectionMask;
using pittore::compute::ToneOp;
using pittore::compute::tone_dab_host;
using pittore::compute::tone_stroke_dab_host;

namespace {

std::vector<RGBAf> solid(std::uint32_t w, std::uint32_t h, float r, float g, float b,
                         float a) {
    return std::vector<RGBAf>(static_cast<std::size_t>(w) * h, RGBAf{r, g, b, a});
}

}  // namespace

void test_paint_tools() {
    // 1. Contiguous flood on a uniform buffer fills everything, bbox included.
    {
        constexpr std::uint32_t w = 8, h = 8;
        auto buf = solid(w, h, 0.2f, 0.2f, 0.2f, 1.0f);
        int bbox[4] = {0, 0, 0, 0};
        const bool changed = flood_fill_host(buf.data(), buf.data(), w, h, 4, 4, 0.0f,
                                             true, false, 1.0f, RGBAf{1, 0, 0, 1},
                                             false, bbox);
        CHECK(changed);
        CHECK_EQ(bbox[0], 0);
        CHECK_EQ(bbox[1], 0);
        CHECK_EQ(bbox[2], static_cast<int>(w));
        CHECK_EQ(bbox[3], static_cast<int>(h));
        CHECK_NEAR(buf[0].r, 1.0, 1e-5);
        CHECK_NEAR(buf[0].g, 0.0, 1e-5);
        CHECK_NEAR(buf[0].a, 1.0, 1e-5);
    }

    // 2. Tolerance 0 stops at a different colour.
    {
        std::vector<RGBAf> buf = {RGBAf{0, 0, 0, 1}, RGBAf{1, 1, 1, 1}};
        const bool changed = flood_fill_host(buf.data(), buf.data(), 2, 1, 1, 0, 0.0f,
                                             true, false, 1.0f, RGBAf{0, 1, 0, 1},
                                             false, nullptr);
        CHECK(changed);
        CHECK_NEAR(buf[0].r, 0.0, 1e-6);  // black untouched
        CHECK_NEAR(buf[1].r, 0.0, 1e-5);  // white turned green
        CHECK_NEAR(buf[1].g, 1.0, 1e-5);
    }

    // 3. Non-contiguous floods every matching pixel, even separated ones.
    {
        std::vector<RGBAf> buf = {RGBAf{1, 1, 1, 1}, RGBAf{0, 0, 0, 1},
                                  RGBAf{1, 1, 1, 1}, RGBAf{0, 0, 0, 1},
                                  RGBAf{1, 1, 1, 1}};
        flood_fill_host(buf.data(), buf.data(), 5, 1, 0, 0, 0.0f, false, false, 1.0f,
                        RGBAf{0, 0, 1, 1}, false, nullptr);
        CHECK_NEAR(buf[0].b, 1.0, 1e-5);
        CHECK_NEAR(buf[2].b, 1.0, 1e-5);
        CHECK_NEAR(buf[4].b, 1.0, 1e-5);
        CHECK_NEAR(buf[1].b, 0.0, 1e-6);
        CHECK_NEAR(buf[3].b, 0.0, 1e-6);
    }

    // 4. Antialias does not feather a region that touches the image border…
    {
        constexpr std::uint32_t w = 4, h = 1;
        auto buf = solid(w, h, 1.0f, 1.0f, 1.0f, 1.0f);
        flood_fill_host(buf.data(), buf.data(), w, h, 1, 0, 0.0f, true, true, 1.0f,
                        RGBAf{0, 1, 0, 1}, false, nullptr);
        for (std::size_t i = 0; i < buf.size(); ++i) CHECK_NEAR(buf[i].g, 1.0, 1e-5);
    }

    // …but does soften a genuine interior boundary.
    {
        std::vector<RGBAf> buf = {RGBAf{1, 1, 1, 1}, RGBAf{1, 1, 1, 1},
                                  RGBAf{0, 0, 0, 1}};
        flood_fill_host(buf.data(), buf.data(), 3, 1, 0, 0, 0.0f, true, true, 1.0f,
                        RGBAf{0, 1, 0, 1}, false, nullptr);
        CHECK(buf[2].g > 0.0f && buf[2].g < 1.0f);  // feathered edge
    }

    // 5. Erase zeroes alpha over the flooded region.
    {
        constexpr std::uint32_t w = 2, h = 2;
        auto buf = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        const bool changed = flood_fill_host(buf.data(), buf.data(), w, h, 0, 0, 0.0f,
                                             true, false, 1.0f, RGBAf{0, 0, 0, 1},
                                             true, nullptr);
        CHECK(changed);
        for (std::size_t i = 0; i < buf.size(); ++i) CHECK_NEAR(buf[i].a, 0.0, 1e-6);
    }

    // 6. A no-op fill reports false and leaves the buffer alone.
    {
        constexpr std::uint32_t w = 2, h = 2;
        auto buf = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        const bool changed =
            flood_fill_host(buf.data(), buf.data(), w, h, 0, 0, 0.0f, true, false,
                            1.0f, RGBAf{0, 0, 0, 0}, true, nullptr);  // erase on opaque
        CHECK(changed);
        auto again = solid(w, h, 0.0f, 0.0f, 0.0f, 0.0f);
        const bool noChange = flood_fill_host(again.data(), again.data(), w, h, 0, 0,
                                              0.0f, true, false, 1.0f,
                                              RGBAf{0, 0, 0, 1}, true, nullptr);
        CHECK(!noChange);  // already transparent: nothing to erase
    }

    // 7. Dodge lightens, Burn darkens; both only inside the dab.
    {
        constexpr std::uint32_t w = 5, h = 5;
        auto dodge = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        tone_dab_host(dodge.data(), w, h, 2.5f, 2.5f, 1.0f, 1.0f, 1.0f,
                      ToneOp::Dodge, 1, false, false);
        CHECK(dodge[12].r > 0.5f);
        CHECK_NEAR(dodge[0].r, 0.5, 1e-6f);  // corner outside the dab radius

        auto burn = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        tone_dab_host(burn.data(), w, h, 2.5f, 2.5f, 1.0f, 1.0f, 1.0f, ToneOp::Burn,
                      1, false, false);
        CHECK(burn[12].r < 0.5f);
    }

    // 8. Sponge desaturate pulls a saturated colour to its luma.
    {
        constexpr std::uint32_t w = 5, h = 5;
        auto buf = solid(w, h, 1.0f, 0.0f, 0.0f, 1.0f);
        tone_dab_host(buf.data(), w, h, 2.5f, 2.5f, 1.0f, 1.0f, 1.0f,
                      ToneOp::Desaturate, 1, false, false);
        CHECK_NEAR(buf[12].r, 0.3f, 1e-3f);
        CHECK_NEAR(buf[12].g, 0.3f, 1e-3f);
        CHECK_NEAR(buf[12].b, 0.3f, 1e-3f);
    }

    // 9. A tonal dab leaves fully transparent pixels alone.
    {
        constexpr std::uint32_t w = 5, h = 5;
        auto buf = solid(w, h, 0.5f, 0.5f, 0.5f, 0.0f);
        tone_dab_host(buf.data(), w, h, 2.5f, 2.5f, 1.0f, 1.0f, 1.0f,
                      ToneOp::Dodge, 1, false, false);
        CHECK_NEAR(buf[12].r, 0.5f, 1e-6f);
        CHECK_NEAR(buf[12].a, 0.0f, 1e-6f);
    }

    // 10. A selection confines both a dab and a flood fill.
    {
        constexpr std::uint32_t w = 8, h = 8;
        SelectionMask sel;
        sel.x0 = 0.0f;
        sel.y0 = 0.0f;
        sel.x1 = 3.0f;
        sel.y1 = 8.0f;  // left half only
        auto buf = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        tone_dab_host(buf.data(), w, h, 2.5f, 2.5f, 4.0f, 1.0f, 1.0f,
                      ToneOp::Dodge, 1, false, false, &sel);
        CHECK(buf[4 * w + 1].r > 0.5f);   // inside the selection
        CHECK_NEAR(buf[4 * w + 6].r, 0.5, 1e-6f);  // outside, untouched

        auto fill = solid(w, h, 0.2f, 0.2f, 0.2f, 1.0f);
        flood_fill_host(fill.data(), fill.data(), w, h, 1, 1, 1.0f, true, false,
                        1.0f, RGBAf{1, 0, 0, 1}, false, nullptr, &sel);
        CHECK_NEAR(fill[1 * w + 1].r, 1.0, 1e-5);  // inside flooded
        CHECK_NEAR(fill[4 * w + 6].r, 0.2, 1e-5);  // outside untouched

        // A seed outside the selection never floods.
        auto out = solid(w, h, 0.2f, 0.2f, 0.2f, 1.0f);
        const bool did = flood_fill_host(out.data(), out.data(), w, h, 6, 4, 1.0f,
                                         true, false, 1.0f, RGBAf{1, 0, 0, 1},
                                         false, nullptr, &sel);
        CHECK(!did);
    }

    // 11. A tonal stroke accumulates coverage instead of compounding: a second
    //     dab over the same pixels re-renders them from the pre-stroke image, so
    //     the result matches a single dab and never races toward white/black.
    {
        constexpr std::uint32_t w = 9, h = 9;
        auto pre = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        std::vector<RGBAf> dst = pre;
        std::vector<float> cov(std::size_t(w) * h, 0.0f);
        int bbox[4] = {0, 0, 0, 0};

        CHECK(tone_stroke_dab_host(pre.data(), dst.data(), cov.data(), w, h, 4.5f,
                                   4.5f, 3.0f, 1.0f, 0.5f, ToneOp::Dodge, 1, false,
                                   false, nullptr, bbox));
        const float once = dst[4 * w + 4].r;
        CHECK(once > 0.5f);

        // Identical second dab: coverage cannot grow, so nothing is re-toned.
        const bool again = tone_stroke_dab_host(
            pre.data(), dst.data(), cov.data(), w, h, 4.5f, 4.5f, 3.0f, 1.0f, 0.5f,
            ToneOp::Dodge, 1, false, false, nullptr, bbox);
        CHECK(!again);
        CHECK_NEAR(dst[4 * w + 4].r, once, 1e-6f);

        // A single stroke dab matches the one-shot operator at the same amount.
        auto ref = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        tone_dab_host(ref.data(), w, h, 4.5f, 4.5f, 3.0f, 1.0f, 0.5f, ToneOp::Dodge,
                      1, false, false);
        CHECK_NEAR(dst[4 * w + 4].r, ref[4 * w + 4].r, 1e-6f);

        // A dab at an overlapping offset leaves the already-covered centre as
        // the single-dab result and tones the new pixels.
        const bool grew = tone_stroke_dab_host(
            pre.data(), dst.data(), cov.data(), w, h, 5.5f, 5.5f, 3.0f, 1.0f, 0.5f,
            ToneOp::Dodge, 1, false, false, nullptr, bbox);
        CHECK(grew);
        CHECK_NEAR(dst[4 * w + 4].r, once, 1e-6f);
        CHECK(dst[5 * w + 7].r > 0.5f);  // newly covered by the offset dab
    }

    // 12. Midtones hits the Levels-gamma anchors those curves were matched to:
    //     full-exposure Dodge ≡ gamma 1.5 (in^(2/3)), Burn ≡ gamma 0.5 (in²).
    {
        constexpr std::uint32_t w = 1, h = 1;
        auto dod = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        tone_dab_host(dod.data(), w, h, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f,
                      ToneOp::Dodge, 1, false, false);
        CHECK_NEAR(dod[0].r, std::pow(0.5f, 2.0f / 3.0f), 1e-5f);
        auto bur = solid(w, h, 0.5f, 0.5f, 0.5f, 1.0f);
        tone_dab_host(bur.data(), w, h, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f,
                      ToneOp::Burn, 1, false, false);
        CHECK_NEAR(bur[0].r, 0.25f, 1e-5f);
    }

    // 13. Protect Tones keeps saturation: the transfer rides the luminance and
    //     RGB is rescaled by L'/L, so the R:G:B ratios (hue + chroma) hold.
    {
        constexpr std::uint32_t w = 1, h = 1;
        auto buf = solid(w, h, 0.5f, 0.25f, 0.125f, 1.0f);
        tone_dab_host(buf.data(), w, h, 0.5f, 0.5f, 1.0f, 1.0f, 1.0f,
                      ToneOp::Dodge, 1, true, false);
        const float luma = 0.3f * 0.5f + 0.59f * 0.25f + 0.11f * 0.125f;
        const float sc = std::pow(luma, 2.0f / 3.0f) / luma;
        CHECK_NEAR(buf[0].r / (0.5f * sc), 1.0, 1e-4f);
        CHECK_NEAR(buf[0].g / (0.25f * sc), 1.0, 1e-4f);
        CHECK_NEAR(buf[0].b / (0.125f * sc), 1.0, 1e-4f);
        CHECK(buf[0].r > 0.5f);  // and the luminance genuinely moved
    }
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_paint_tools)
#endif
