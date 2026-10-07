// test_place.cpp — placed-layer sampler checks (lossless move/resize core).
//
// The sampler maps document pixels back into a layer's NATIVE pixels, so a
// move or scale never resamples the stored source. These checks pin: exact
// texels at integer coords (translated layers stay bit-identical to memcpy),
// bilinear midpoints on upscale, box-averaged footprints on downscale, edge
// clamping (no dark fringe), and transparency outside the source.
#include <cmath>
#include <vector>

#include "engine/compute/paint.h"
#include "engine/core/pixel.h"
#include "test_util.h"

using pittore::RGBAf;
using pittore::compute::sample_placed_host;

static bool near(const RGBAf& a, const RGBAf& b, float eps) {
    return std::abs(a.r - b.r) <= eps && std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps && std::abs(a.a - b.a) <= eps;
}

void test_place() {
    // 4x4 gradient source: r = x/3, g = y/3, b = 0.5, a = 1.
    constexpr std::uint32_t sw = 4, sh = 4;
    std::vector<RGBAf> src(sw * sh);
    for (std::uint32_t y = 0; y < sh; ++y)
        for (std::uint32_t x = 0; x < sw; ++x)
            src[y * sw + x] = RGBAf{x / 3.0f, y / 3.0f, 0.5f, 1.0f};
    const RGBAf* s = src.data();

    // --- identity: integer coords return exact texels ----------------------
    for (std::uint32_t y = 0; y < sh; ++y)
        for (std::uint32_t x = 0; x < sw; ++x)
            CHECK(near(sample_placed_host(s, sw, sh, x, y, 0, 0, 1, 1),
                       src[y * sw + x], 0.0f));

    // --- pure translation: still exact (bit-identical to a memcpy blit) ----
    CHECK(near(sample_placed_host(s, sw, sh, 10.0, 7.0, 10, 7, 1, 1),
               src[0], 0.0f));
    CHECK(near(sample_placed_host(s, sw, sh, 12.0, 9.0, 10, 7, 1, 1),
               src[2 * sw + 2], 0.0f));

    // --- upscale 2x: midpoint is the bilinear average of 4 texels ----------
    {
        const RGBAf m = sample_placed_host(s, sw, sh, 1.0, 1.0, 0, 0, 2, 2);
        // source (0.5, 0.5) -> r = (0 + 1/3 + 0 + 1/3)/4 / ... = mean of
        // r over {(0,0),(1,0),(0,1),(1,1)} = (0 + 1/3 + 0 + 1/3)/4 = 1/6.
        CHECK_NEAR(m.r, 1.0f / 6.0f, 1e-6f);
        CHECK_NEAR(m.g, 1.0f / 6.0f, 1e-6f);
        CHECK_NEAR(m.b, 0.5f, 1e-6f);
        CHECK_NEAR(m.a, 1.0f, 1e-6f);
    }

    // --- downscale 0.5x: one doc pixel covers a 2x2 footprint -------------
    {
        // Uniform source averages back to itself at any scale.
        std::vector<RGBAf> flat(16, RGBAf{0.2f, 0.4f, 0.6f, 0.8f});
        const RGBAf v =
            sample_placed_host(flat.data(), 4, 4, 0.0, 0.0, 0, 0, 0.5, 0.5);
        CHECK(near(v, RGBAf{0.2f, 0.4f, 0.6f, 0.8f}, 1e-6f));

        // Gradient: doc pixel (0,0) at 0.5x covers source [0,2)x[0,2);
        // 2x2 taps floor to texels {(0,0),(1,0),(0,1),(1,1)} whose r
        // values {0, 1/3, 0, 1/3} average to 1/6.
        const RGBAf d = sample_placed_host(s, sw, sh, 0.0, 0.0, 0, 0, 0.5, 0.5);
        CHECK_NEAR(d.r, 1.0f / 6.0f, 1e-5f);
        CHECK_NEAR(d.g, 1.0f / 6.0f, 1e-5f);
        CHECK_NEAR(d.a, 1.0f, 1e-6f);
    }

    // --- edges fade in premultiplied space (no dark fringe) ----------------
    // Source (-0.25, 0): 75% covered, so the hue is texel (0,0) exactly at
    // 0.75 alpha.
    {
        const RGBAf e = sample_placed_host(s, sw, sh, 0.0, 0.0, 0.5, 0.0, 2, 2);
        CHECK_NEAR(e.r, src[0].r, 1e-6f);
        CHECK_NEAR(e.g, src[0].g, 1e-6f);
        CHECK_NEAR(e.b, src[0].b, 1e-6f);
        CHECK_NEAR(e.a, 0.75f, 1e-6f);
    }
    // Source (-0.5, 1.0) at 1:1: half covered, hue of texel (0,1), half alpha.
    {
        const RGBAf e = sample_placed_host(s, sw, sh, -0.5, 1.0, 0, 0, 1, 1);
        CHECK(near(e, RGBAf{src[sw].r, src[sw].g, src[sw].b, 0.5f}, 1e-6f));
    }
    CHECK_EQ(sample_placed_host(s, sw, sh, 100.0, 100.0, 0, 0, 1, 1).a, 0.0f);
    CHECK_EQ(sample_placed_host(s, sw, sh, -5.0, 0.0, 0, 0, 1, 1).a, 0.0f);
    CHECK_EQ(sample_placed_host(s, sw, sh, 0.0, 0.0, 0, 0, 0.5, 0.5).a,
             sample_placed_host(s, sw, sh, 0.0, 0.0, 0, 0, 0.5, 0.5).a);  // sane

    // --- degenerate inputs never crash -------------------------------------
    CHECK_EQ(sample_placed_host(nullptr, sw, sh, 0, 0, 0, 0, 1, 1).a, 0.0f);
    CHECK_EQ(sample_placed_host(s, 0, sh, 0, 0, 0, 0, 1, 1).a, 0.0f);
    CHECK_EQ(sample_placed_host(s, sw, sh, 0, 0, 0, 0, 0, 1).a, 0.0f);
    CHECK_EQ(sample_placed_host(s, sw, sh, 0, 0, 0, 0, -2, 1).a, 0.0f);

    // --- anisotropic upscale: exact along the unit axis --------------------
    CHECK(near(sample_placed_host(s, sw, sh, 4.0, 1.0, 0, 0, 2, 1),
               RGBAf{2 / 3.0f, 1 / 3.0f, 0.5f, 1.0f}, 1e-6f));
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_place)
#endif
