// Engine tests for tonal_ops.h: histogram, levels, curves, noise, median,
// unsharp mask and image geometry. Pure host-side functions — no Qt, no UI,
// no backend.
#include <array>
#include <cmath>
#include <memory>

#include "engine/core/image.h"
#include "engine/core/tonal_ops.h"
#include "test_util.h"

using pittore::Image;
using pittore::RGBAf;

namespace {

Image makeFlat(std::uint32_t w, std::uint32_t h, float r, float g, float b,
               float a = 1.0f) {
    Image img(w, h);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            img.at(x, y) = RGBAf{r, g, b, a};
    return img;
}

// A gradient: x/width in r, y/height in g, 0.5 in b.
Image makeGradient(std::uint32_t w, std::uint32_t h) {
    Image img(w, h);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            img.at(x, y) = RGBAf{static_cast<float>(x) / (w - 1),
                                 static_cast<float>(y) / (h - 1), 0.5f, 1.0f};
    return img;
}

}  // namespace

static int testHistogram() {
    // Flat red image: all 256 bins of r are empty except bin 255, and luma is
    // 0.2126*255 ≈ 54.
    Image red = makeFlat(4, 4, 1.0f, 0.0f, 0.0f);
    pittore::Histogram256 hist;
    pittore::computeHistogram(red, hist);
    CHECK(hist.rgb[0][255] == 16);
    CHECK(hist.rgb[0][254] == 0);
    CHECK(hist.rgb[1][0] == 16);
    CHECK(hist.rgb[2][0] == 16);
    CHECK(hist.total() == 16 * 3);
    CHECK(hist.luma[static_cast<int>(0.2126 * 255 + 0.5)] == 16);

    // Gradient: bin counts spread, total still pixel count.
    Image grad = makeGradient(256, 256);
    pittore::Histogram256 gh;
    pittore::computeHistogram(grad, gh);
    CHECK(gh.total() == 256ull * 256 * 3);
    CHECK(gh.rgb[0][0] > 0 && gh.rgb[0][255] > 0);
    // Every x column has a unique r bin with one row of 256 pixels.
    CHECK(gh.rgb[0][128] == 256);
    return 0;
}

static int testLevels() {
    // Identity: black 0, white 1, gamma 1 → pixels unchanged (within fp noise).
    Image a = makeGradient(64, 64);
    Image b = a.clone();
    pittore::applyLevels(b, 3, 0.0, 1.0, 1.0, 0.0, 1.0);
    for (std::uint32_t y = 0; y < 64; ++y)
        for (std::uint32_t x = 0; x < 64; ++x)
            CHECK(std::fabs(a.at(x, y).r - b.at(x, y).r) < 1e-6);

    // Crush black: inBlack 0.5 → everything below 0.5 becomes 0.
    Image c = makeGradient(64, 64);
    pittore::applyLevels(c, 3, 0.5, 1.0, 1.0, 0.0, 1.0);
    for (std::uint32_t x = 0; x < 64; ++x) {
        // Input r = x/63; output = clamp((x/63 - 0.5)/0.5, 0, 1) = clamp(2x/63 - 1).
        const double expected = std::clamp(2.0 * x / 63.0 - 1.0, 0.0, 1.0);
        const double got = c.at(x, 0).r;
        CHECK(std::fabs(got - expected) < 1e-4);
    }

    // Gamma 0.5 (darker midtones): 0.25 input → 0.25^(1/0.5) = 0.0625.
    Image d = makeFlat(1, 1, 0.25f, 0.5f, 0.75f);
    pittore::applyLevels(d, 3, 0.0, 1.0, 0.5, 0.0, 1.0);
    CHECK(std::fabs(d.at(0, 0).r - 0.0625) < 1e-4);
    CHECK(std::fabs(d.at(0, 0).g - std::pow(0.5, 2.0)) < 1e-4);

    // Channel-restricted: only R changes.
    Image e = makeFlat(1, 1, 0.5f, 0.5f, 0.5f);
    pittore::applyLevels(e, 0, 0.75, 1.0, 1.0, 0.0, 1.0);
    CHECK(std::fabs(e.at(0, 0).r) < 1e-4);
    CHECK(std::fabs(e.at(0, 0).g - 0.5) < 1e-4);
    return 0;
}

static int testCurves() {
    // Linear curve = identity.
    Image a = makeGradient(16, 16);
    Image b = a.clone();
    pittore::applyCurves(b, 3, {{0.0, 0.0}, {1.0, 1.0}});
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x)
            CHECK(std::fabs(a.at(x, y).r - b.at(x, y).r) < 1e-6);

    // Inverted curve: 1 - x.
    Image c = a.clone();
    pittore::applyCurves(c, 3, {{0.0, 1.0}, {1.0, 0.0}});
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x)
            CHECK(std::fabs(c.at(x, y).r - (1.0 - a.at(x, y).r)) < 1e-3);

    // S-curve increases contrast of midtones: 0.5 stays ~0.5 (within the
    // 256-entry LUT's quantization), 0.25 drops.
    Image d = makeFlat(1, 1, 0.5f, 0.0f, 0.0f);
    pittore::applyCurves(d, 3, {{0.0, 0.0}, {0.25, 0.1}, {0.5, 0.5}, {0.75, 0.9}, {1.0, 1.0}});
    CHECK(std::fabs(d.at(0, 0).r - 0.5) < 0.02);
    Image e = makeFlat(1, 1, 0.25f, 0.0f, 0.0f);
    pittore::applyCurves(e, 3, {{0.0, 0.0}, {0.25, 0.1}, {0.5, 0.5}, {0.75, 0.9}, {1.0, 1.0}});
    CHECK(e.at(0, 0).r < 0.25f);

    // LUT clamps outside range and handles duplicate x points.
    float lut[256];
    pittore::buildCurveLUT({}, lut);  // empty → identity
    CHECK(std::fabs(lut[0] - 0.0f) < 1e-6);
    CHECK(std::fabs(lut[255] - 1.0f) < 1e-6);
    CHECK(std::fabs(lut[128] - 128.0f / 255.0f) < 1e-3);
    return 0;
}

static int testNoise() {
    // Deterministic for a seed (two identical runs agree).
    Image a = makeFlat(16, 16, 0.5f, 0.5f, 0.5f);
    Image b = a.clone();
    pittore::applyAddNoise(a, 0.1, false, 42);
    pittore::applyAddNoise(b, 0.1, false, 42);
    for (std::uint32_t y = 0; y < 16; ++y)
        for (std::uint32_t x = 0; x < 16; ++x) {
            CHECK(a.at(x, y).r == b.at(x, y).r);
            CHECK(a.at(x, y).a == 1.0f);
        }

    // Amount 0 → no change; amount 1 → within [0,1] and varied.
    Image c = makeFlat(8, 8, 0.5f, 0.5f, 0.5f);
    Image d = c.clone();
    pittore::applyAddNoise(d, 0.0, false, 7);
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x)
            CHECK(d.at(x, y).r == 0.5f);
    pittore::applyAddNoise(d, 1.0, false, 7);
    bool varied = false;
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x) {
            CHECK(d.at(x, y).r >= 0.0f && d.at(x, y).r <= 1.0f);
            if (d.at(x, y).r != 0.5f) varied = true;
        }
    CHECK(varied);

    // Monochrome noise: r == g == b per pixel.
    Image m = makeFlat(8, 8, 0.5f, 0.5f, 0.5f);
    pittore::applyAddNoise(m, 0.3, true, 5);
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x) {
            CHECK(std::fabs(m.at(x, y).r - m.at(x, y).g) < 1e-6);
            CHECK(std::fabs(m.at(x, y).g - m.at(x, y).b) < 1e-6);
        }
    return 0;
}

static int testMedian() {
    // Salt-and-pepper: a flat 0.4 field with a few 1.0 spikes. Median radius 1
    // removes isolated spikes.
    Image img(8, 8);
    for (std::uint32_t y = 0; y < 8; ++y)
        for (std::uint32_t x = 0; x < 8; ++x)
            img.at(x, y) = RGBAf{0.4f, 0.4f, 0.4f, 1.0f};
    img.at(4, 4) = RGBAf{1.0f, 1.0f, 1.0f, 1.0f};
    pittore::applyMedianFilter(img, 1);
    CHECK(std::fabs(img.at(4, 4).r - 0.4f) < 1e-5);  // spike gone
    CHECK(img.at(4, 4).a == 1.0f);

    // Radius 0 is a no-op.
    Image img2 = makeGradient(4, 4);
    Image img3 = img2.clone();
    pittore::applyMedianFilter(img3, 0);
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 4; ++x) {
            CHECK(img2.at(x, y).r == img3.at(x, y).r);
            CHECK(img2.at(x, y).g == img3.at(x, y).g);
        }
    return 0;
}

static int testUnsharp() {
    // A soft edge: gradient 0→1 over 32 px. Unsharp brightens the light side
    // and darkens the dark side near the edge (overshoot).
    Image grad(64, 1);
    for (std::uint32_t x = 0; x < 64; ++x) {
        const float v = x < 32 ? 0.2f : 0.6f;
        grad.at(x, 0) = RGBAf{v, v, v, 1.0f};
    }
    Image sharp = grad.clone();
    pittore::applyUnsharpMask(sharp, 1.0, 2, 0.0);
    // Light side increases, dark side decreases near the edge.
    CHECK(sharp.at(33, 0).r > 0.6f + 1e-4);
    CHECK(sharp.at(30, 0).r < 0.2f - 1e-4);

    // Threshold high enough skips everything (no |diff| exceeds 1.0).
    Image none = grad.clone();
    pittore::applyUnsharpMask(none, 1.0, 2, 1.0);
    for (std::uint32_t x = 0; x < 64; ++x)
        CHECK(std::fabs(none.at(x, 0).r - grad.at(x, 0).r) < 1e-6);

    // Flat image: nothing changes regardless of amount.
    Image flat = makeFlat(4, 4, 0.5f, 0.5f, 0.5f);
    pittore::applyUnsharpMask(flat, 2.0, 3, 0.0);
    for (std::uint32_t y = 0; y < 4; ++y)
        for (std::uint32_t x = 0; x < 4; ++x)
            CHECK(std::fabs(flat.at(x, y).r - 0.5f) < 1e-6);
    return 0;
}

static int testGeometry() {
    // 3×2 image with distinct pixel values packed as r = x + 10*y.
    Image src(3, 2);
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < 3; ++x)
            src.at(x, y) = RGBAf{static_cast<float>(x + 10 * y), 0, 0, 1};

    // 90 CW: (x,y) → (h-1-y, x); size becomes 2×3.
    Image cw = pittore::rotate90Cw(src);
    CHECK(cw.width() == 2 && cw.height() == 3);
    CHECK(std::fabs(cw.at(0, 0).r - src.at(0, 2 - 1).r) < 1e-6);  // src(0,1)=10
    CHECK(std::fabs(cw.at(0, 0).r - 10.0f) < 1e-6);

    // 90 CW twice = 180.
    Image cw2 = pittore::rotate90Cw(pittore::rotate90Cw(src));
    Image r180 = pittore::rotate180(src);
    CHECK(cw2.width() == 3 && cw2.height() == 2);
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < 3; ++x)
            CHECK(std::fabs(cw2.at(x, y).r - r180.at(x, y).r) < 1e-6);

    // CCW is the inverse of CW.
    Image ccw = pittore::rotate90Ccw(cw);
    CHECK(ccw.width() == 3 && ccw.height() == 2);
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < 3; ++x)
            CHECK(std::fabs(ccw.at(x, y).r - src.at(x, y).r) < 1e-6);

    // Flips.
    Image fh = pittore::flipHorizontal(src);
    CHECK(std::fabs(fh.at(2, 0).r - 0.0f) < 1e-6);
    CHECK(std::fabs(fh.at(0, 0).r - 2.0f) < 1e-6);
    Image fv = pittore::flipVertical(src);
    CHECK(std::fabs(fv.at(0, 1).r - 0.0f) < 1e-6);
    CHECK(std::fabs(fv.at(0, 0).r - 10.0f) < 1e-6);

    // Rotating 90° CW 4 times returns the original.
    Image rot = src;
    for (int i = 0; i < 4; ++i) rot = pittore::rotate90Cw(rot);
    CHECK(rot.width() == 3 && rot.height() == 2);
    for (std::uint32_t y = 0; y < 2; ++y)
        for (std::uint32_t x = 0; x < 3; ++x)
            CHECK(std::fabs(rot.at(x, y).r - src.at(x, y).r) < 1e-6);
    return 0;
}

int main() {
    testHistogram();
    testLevels();
    testCurves();
    testNoise();
    testMedian();
    testUnsharp();
    testGeometry();
    std::printf("checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}