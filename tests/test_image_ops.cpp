// Engine tests for ui/image_ops.h: the pure pixel math behind the Image menu
// (auto tone/contrast/colour, equalize, gradient map, .cube LUTs, selective
// colour, shadows/highlights, HDR toning, match/replace colour, layer blends
// and the canvas-rotation resampler). Pure host-side functions — no Qt, no
// document model, no backend.
#include <cmath>
#include <string>
#include <vector>

#include "test_util.h"
#include "ui/image_ops.h"

using pittore::Image;
using pittore::RGBAf;
namespace ops = pittore::ui::imageops;

namespace {

Image makeFlat(std::uint32_t w, std::uint32_t h, float r, float g, float b,
               float a = 1.0f) {
    Image img(w, h);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            img.at(x, y) = RGBAf{r, g, b, a};
    return img;
}

// r = f(x) across the row, flat g/b: a one-dimensional ramp to stretch.
Image makeRamp(int w, float from, float to) {
    Image img(static_cast<std::uint32_t>(w), 1);
    for (int x = 0; x < w; ++x) {
        const float t = w > 1 ? static_cast<float>(x) / (w - 1) : 0.0f;
        img.at(static_cast<std::uint32_t>(x), 0) =
            RGBAf{from + (to - from) * t, 0.5f, 0.5f, 1.0f};
    }
    return img;
}

}  // namespace

static int testApplyAdjust() {
    // Brightness/Contrast through the engine kind: 0.5 + 0.1 = 0.6.
    Image a = makeFlat(2, 2, 0.5f, 0.5f, 0.5f, 0.75f);
    const float p[2] = {0.1f, 0.0f};
    ops::applyAdjust(a, pittore::compute::AdjustmentKind::BrightnessContrast, p);
    CHECK_NEAR(a.at(0, 0).r, 0.6, 1e-5);
    CHECK_NEAR(a.at(0, 0).g, 0.6, 1e-5);
    CHECK_NEAR(a.at(1, 1).b, 0.6, 1e-5);
    CHECK_NEAR(a.at(0, 0).a, 0.75, 1e-6);  // coverage untouched

    // Invert takes no parameters.
    Image b = makeFlat(1, 1, 0.25f, 0.5f, 1.0f);
    ops::applyAdjust(b, pittore::compute::AdjustmentKind::Invert, nullptr);
    CHECK_NEAR(b.at(0, 0).r, 0.75, 1e-6);
    CHECK_NEAR(b.at(0, 0).g, 0.5, 1e-6);
    CHECK_NEAR(b.at(0, 0).b, 0.0, 1e-6);
    return 0;
}

static int testDesaturate() {
    Image img = makeFlat(4, 4, 1.0f, 0.0f, 0.0f, 0.5f);
    ops::desaturate(img);
    const float l = 0.2126f;  // Rec.709 red weight
    CHECK_NEAR(img.at(1, 1).r, l, 1e-5);
    CHECK_NEAR(img.at(1, 1).g, l, 1e-5);
    CHECK_NEAR(img.at(1, 1).b, l, 1e-5);
    CHECK_NEAR(img.at(1, 1).a, 0.5, 1e-6);
    return 0;
}

static int testEqualize() {
    // A flat image has no range to redistribute: left alone.
    Image flat = makeFlat(8, 8, 0.5f, 0.5f, 0.5f);
    ops::equalize(flat);
    CHECK_NEAR(flat.at(3, 3).r, 0.5, 1e-6);

    // A squeezed ramp (0.4..0.6) spreads across nearly the whole range.
    Image narrow = makeRamp(256, 0.4f, 0.6f);
    ops::equalize(narrow);
    const float lo = narrow.at(0, 0).r;
    const float hi = narrow.at(255, 0).r;
    CHECK(lo < 0.02f);
    CHECK(hi > 0.98f);
    // Monotonic: equalize is a curve, so the ramp never inverts.
    for (int x = 1; x < 256; ++x)
        CHECK(narrow.at(static_cast<std::uint32_t>(x - 1), 0).r <=
              narrow.at(static_cast<std::uint32_t>(x), 0).r + 1e-6f);
    return 0;
}

static int testAutoTone() {
    // r ramped across 0.25..0.75; g/b flat (a flat channel keeps itself).
    Image img = makeRamp(256, 0.25f, 0.75f);
    ops::autoTone(img);
    CHECK(img.at(0, 0).r < 0.02f);
    CHECK_NEAR(img.at(255, 0).r, 1.0, 1e-3);
    CHECK_NEAR(img.at(0, 0).g, 0.5, 1e-6);
    CHECK_NEAR(img.at(255, 0).b, 0.5, 1e-6);

    // Already full-range content does not move.
    Image full = makeRamp(64, 0.0f, 1.0f);
    Image copy = full.clone();
    ops::autoTone(full);
    CHECK_NEAR(full.at(0, 0).r, 0.0, 1e-3);
    CHECK_NEAR(full.at(63, 0).r, 1.0, 1e-3);
    CHECK_NEAR(full.at(32, 0).r, copy.at(32, 0).r, 0.05);
    return 0;
}

static int testAutoContrast() {
    // A neutral ramp squeezed to 0.4..0.6 spreads across nearly the whole
    // range, and stays neutral: one curve for all three channels.
    Image grey(256, 1);
    for (int x = 0; x < 256; ++x) {
        const float v = 0.4f + 0.2f * (static_cast<float>(x) / 255.0f);
        grey.at(static_cast<std::uint32_t>(x), 0) = RGBAf{v, v, v, 1.0f};
    }
    ops::autoContrast(grey);
    CHECK(grey.at(0, 0).r < 0.02f);
    CHECK(grey.at(255, 0).r > 0.98f);
    CHECK_NEAR(grey.at(0, 0).g, grey.at(0, 0).r, 1e-6);
    CHECK_NEAR(grey.at(255, 0).b, grey.at(255, 0).r, 1e-6);
    return 0;
}

static int testAutoColor() {
    // Grey world: channel means 0.6 / 0.4 / 0.2 agree at 0.4 afterwards.
    Image img = makeFlat(4, 4, 0.6f, 0.4f, 0.2f);
    ops::autoColor(img);
    CHECK_NEAR(img.at(2, 2).r, 0.4, 1e-5);
    CHECK_NEAR(img.at(2, 2).g, 0.4, 1e-5);
    CHECK_NEAR(img.at(2, 2).b, 0.4, 1e-5);
    return 0;
}

static int testGradientMap() {
    // Black → white ramp: every pixel becomes the grey of its lightness.
    Image img = makeFlat(4, 4, 1.0f, 0.0f, 0.0f);
    ops::gradientMap(img, {{0.0f, 0.0f, 0.0f, 0.0f},
                           {1.0f, 1.0f, 1.0f, 1.0f}});
    const float l = 0.2126f;
    CHECK_NEAR(img.at(1, 1).r, l, 1e-5);
    CHECK_NEAR(img.at(1, 1).b, l, 1e-5);

    // A two-colour ramp ignores the pixel's own hue and paints the stop.
    Image duo = makeFlat(2, 2, 0.0f, 0.0f, 0.0f);
    ops::gradientMap(duo, {{0.0f, 1.0f, 0.0f, 0.0f},
                           {1.0f, 0.0f, 0.0f, 1.0f}});
    CHECK_NEAR(duo.at(0, 0).r, 1.0, 1e-6);
    CHECK_NEAR(duo.at(0, 0).g, 0.0, 1e-6);
    CHECK_NEAR(duo.at(0, 0).b, 0.0, 1e-6);

    // Fewer than two stops is not a ramp: refused, image unchanged.
    Image none = makeFlat(2, 2, 0.3f, 0.3f, 0.3f);
    ops::gradientMap(none, {{0.0f, 1.0f, 0.0f, 0.0f}});
    CHECK_NEAR(none.at(0, 0).r, 0.3, 1e-6);
    return 0;
}

static int testCube() {
    // A 2³ identity table: sampling it is a no-op.
    const std::string cube =
        "TITLE \"Identity\"\n"
        "# a comment\n"
        "LUT_3D_SIZE 2\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "1 1 0\n"
        "0 0 1\n"
        "1 0 1\n"
        "0 1 1\n"
        "1 1 1\n";
    ops::CubeLut lut;
    std::string err;
    CHECK(ops::parseCube(cube, lut, &err));
    CHECK(lut.size == 2);
    CHECK(lut.data.size() == 24);

    Image img = makeRamp(32, 0.0f, 1.0f);
    for (std::uint32_t y = 0; y < img.height(); ++y)
        for (std::uint32_t x = 0; x < img.width(); ++x)
            img.at(x, y).g = img.at(x, y).r;
    const Image before = img.clone();
    ops::applyCubeLut(img, lut);
    for (std::uint32_t y = 0; y < img.height(); ++y)
        for (std::uint32_t x = 0; x < img.width(); ++x) {
            CHECK_NEAR(img.at(x, y).r, before.at(x, y).r, 1e-5);
            CHECK_NEAR(img.at(x, y).g, before.at(x, y).g, 1e-5);
        }

    // Corners clamp: an input beyond the domain still reads a table texel.
    const RGBAf top = ops::sampleCube(lut, 5.0f, 5.0f, 5.0f);
    CHECK_NEAR(top.r, 1.0, 1e-6);
    CHECK_NEAR(top.b, 1.0, 1e-6);

    // Refusals: a 1-D table, a missing size and a short table all fail
    // loudly instead of half-loading.
    ops::CubeLut bad;
    CHECK(!ops::parseCube("LUT_1D_SIZE 4\n", bad, &err));
    CHECK(!err.empty());
    CHECK(!ops::parseCube("0 0 0\n1 1 1\n", bad, &err));
    CHECK(!ops::parseCube("LUT_3D_SIZE 2\n0 0 0\n", bad, &err));
    CHECK(!ops::parseCube("LUT_3D_SIZE 2\nnot a line\n", bad, &err));
    return 0;
}

static int testSelectiveColor() {
    float adj[9][4] = {};

    // Cyan pulls red down: pure red goes to black, nothing else moves.
    adj[0][0] = 100.0f;
    Image red = makeFlat(2, 2, 1.0f, 0.0f, 0.0f);
    ops::selectiveColor(red, adj);
    CHECK_NEAR(red.at(0, 0).r, 0.0, 1e-6);
    CHECK_NEAR(red.at(0, 0).g, 0.0, 1e-6);

    // Black darkens what it lands on: the blacks range is the dark end, so a
    // near-black neutral halves, while a mid neutral belongs to neutrals.
    float blackAdj[9][4] = {};
    blackAdj[8][3] = 50.0f;
    Image dark = makeFlat(2, 2, 0.05f, 0.05f, 0.05f);
    ops::selectiveColor(dark, blackAdj);
    CHECK_NEAR(dark.at(0, 0).r, 0.025, 1e-6);
    Image mid = makeFlat(2, 2, 0.4f, 0.4f, 0.4f);
    ops::selectiveColor(mid, blackAdj);
    CHECK_NEAR(mid.at(0, 0).r, 0.4, 1e-6);

    float neutralAdj[9][4] = {};
    neutralAdj[7][3] = 50.0f;
    ops::selectiveColor(mid, neutralAdj);
    CHECK_NEAR(mid.at(0, 0).r, 0.2, 1e-6);

    // Zeroed adjustments are a no-op.
    float none[9][4] = {};
    Image keep = makeFlat(2, 2, 0.7f, 0.2f, 0.5f);
    const Image copy = keep.clone();
    ops::selectiveColor(keep, none);
    CHECK_NEAR(keep.at(1, 1).r, copy.at(1, 1).r, 1e-6);
    CHECK_NEAR(keep.at(1, 1).b, copy.at(1, 1).b, 1e-6);
    return 0;
}

static int testShadowsHighlights() {
    const float tw = 0.5f;
    Image img(3, 1);
    img.at(0, 0) = RGBAf{0.1f, 0.1f, 0.1f, 1.0f};   // shadow
    img.at(1, 0) = RGBAf{0.5f, 0.5f, 0.5f, 1.0f};   // midtone
    img.at(2, 0) = RGBAf{0.9f, 0.9f, 0.9f, 1.0f};   // highlight
    ops::shadowsHighlights(img, 1.0f, 1.0f, tw);
    CHECK(img.at(0, 0).r > 0.1f + 1e-3f);   // shadows lift
    CHECK(img.at(2, 0).r < 0.9f - 1e-3f);   // highlights compress
    CHECK_NEAR(img.at(1, 0).r, 0.5, 1e-6);  // the middle is outside both masks
    // Hue rides the lightness move: still neutral.
    CHECK_NEAR(img.at(0, 0).r, img.at(0, 0).g, 1e-6);
    return 0;
}

static int testHdrToning() {
    // gamma 2 lifts the mids; strength 1 takes it all the way.
    Image img = makeFlat(8, 8, 0.25f, 0.25f, 0.25f);
    ops::hdrToning(img, 1.0f, 0.0f, 2.0f);
    CHECK_NEAR(img.at(4, 4).r, 0.5, 1e-4);

    // Strength 0 changes nothing, and a flat image cannot gain detail.
    Image flat = makeFlat(8, 8, 0.4f, 0.4f, 0.4f);
    Image copy = flat.clone();
    ops::hdrToning(flat, 0.0f, 1.0f, 3.0f);
    CHECK_NEAR(flat.at(4, 4).r, copy.at(4, 4).r, 1e-6);
    ops::hdrToning(flat, 1.0f, 1.0f, 1.0f);
    CHECK_NEAR(flat.at(4, 4).r, 0.4, 1e-6);

    // A step edge gains contrast under the detail term: the dark side goes
    // darker and the light side right at the edge goes lighter.
    Image step(9, 9);
    for (int y = 0; y < 9; ++y)
        for (int x = 0; x < 9; ++x) {
            const float v = x < 4 ? 0.35f : 0.65f;
            step.at(static_cast<std::uint32_t>(x),
                    static_cast<std::uint32_t>(y)) = RGBAf{v, v, v, 1.0f};
        }
    const float beforeDark = step.at(3, 4).r;
    const float beforeLight = step.at(4, 4).r;
    ops::hdrToning(step, 1.0f, 1.0f, 1.0f);
    CHECK(step.at(3, 4).r < beforeDark);
    CHECK(step.at(4, 4).r > beforeLight);
    return 0;
}

static int testMatchColor() {
    const Image target = makeFlat(4, 4, 0.6f, 0.4f, 0.2f);
    const ops::ChannelStats stats = ops::channelStats(target);
    CHECK_NEAR(stats.mean[0], 0.6, 1e-6);
    CHECK_NEAR(stats.mean[1], 0.4, 1e-6);
    CHECK_NEAR(stats.spread[0], 0.0, 1e-6);

    // Flat source: no spread to transfer, so only the means move — and at
    // intensity 1 the result is exactly the reference colour.
    Image img = makeFlat(4, 4, 0.3f, 0.3f, 0.3f);
    ops::matchColor(img, stats, 1.0f, false);
    CHECK_NEAR(img.at(1, 1).r, 0.6, 1e-5);
    CHECK_NEAR(img.at(1, 1).g, 0.4, 1e-5);
    CHECK_NEAR(img.at(1, 1).b, 0.2, 1e-5);

    // Intensity 0 is a no-op; preserving lightness keeps the original luma.
    Image untouched = makeFlat(2, 2, 0.3f, 0.3f, 0.3f);
    ops::matchColor(untouched, stats, 0.0f, false);
    CHECK_NEAR(untouched.at(0, 0).r, 0.3, 1e-6);

    Image kept = makeFlat(2, 2, 0.5f, 0.5f, 0.5f);
    ops::matchColor(kept, stats, 1.0f, true);
    CHECK_NEAR(ops::luma(kept.at(0, 0).r, kept.at(0, 0).g, kept.at(0, 0).b),
               0.5, 1e-4);
    return 0;
}

static int testReplaceColor() {
    // Exact match at tolerance 0, and the neighbouring colour stays put.
    Image img(2, 1);
    img.at(0, 0) = RGBAf{1.0f, 0.0f, 0.0f, 1.0f};
    img.at(1, 0) = RGBAf{0.0f, 0.0f, 1.0f, 1.0f};
    ops::replaceColor(img, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    CHECK_NEAR(img.at(0, 0).r, 0.0, 1e-6);
    CHECK_NEAR(img.at(0, 0).g, 1.0, 1e-6);
    CHECK_NEAR(img.at(1, 0).b, 1.0, 1e-6);

    // A wide tolerance reaches the near colour too; coverage is preserved.
    Image wide(2, 1);
    wide.at(0, 0) = RGBAf{1.0f, 0.0f, 0.0f, 0.5f};
    wide.at(1, 0) = RGBAf{0.0f, 0.0f, 1.0f, 1.0f};
    ops::replaceColor(wide, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
    CHECK_NEAR(wide.at(1, 0).b, 1.0, 1e-6);
    CHECK_NEAR(wide.at(0, 0).b, 1.0, 1e-6);
    CHECK_NEAR(wide.at(0, 0).a, 0.5, 1e-6);
    return 0;
}

static int testBlendInto() {
    Image dst = makeFlat(4, 4, 0.0f, 0.0f, 1.0f);
    const Image src = makeFlat(4, 4, 1.0f, 0.0f, 0.0f);
    ops::blendInto(dst, src, pittore::compute::BlendMode::Normal, 1.0f);
    CHECK_NEAR(dst.at(2, 2).r, 1.0, 1e-6);
    CHECK_NEAR(dst.at(2, 2).b, 0.0, 1e-6);
    CHECK_NEAR(dst.at(2, 2).a, 1.0, 1e-6);

    // Half opacity mixes with the backdrop rather than replacing it.
    Image half = makeFlat(4, 4, 0.0f, 0.0f, 1.0f);
    ops::blendInto(half, src, pittore::compute::BlendMode::Normal, 0.5f);
    CHECK_NEAR(half.at(0, 0).r, 0.5, 1e-6);
    CHECK_NEAR(half.at(0, 0).b, 0.5, 1e-6);

    // Multiply is the engine's own composite, so 0.5 × 0.5 = 0.25.
    Image mul = makeFlat(4, 4, 0.5f, 0.5f, 0.5f);
    const Image mid = makeFlat(4, 4, 0.5f, 0.5f, 0.5f);
    ops::blendInto(mul, mid, pittore::compute::BlendMode::Multiply, 1.0f);
    CHECK_NEAR(mul.at(1, 1).r, 0.25, 1e-6);

    // A source of another size still lands (sampled at the target centres).
    Image small = makeFlat(2, 2, 1.0f, 1.0f, 1.0f);
    Image big = makeFlat(8, 8, 0.0f, 0.0f, 0.0f);
    ops::blendInto(big, small, pittore::compute::BlendMode::Normal, 1.0f);
    CHECK_NEAR(big.at(7, 7).g, 1.0, 1e-6);
    return 0;
}

static int testGeometry() {
    // A quarter turn of a landscape canvas swaps the axes and shifts the
    // bounding box's origin to (0,0).
    double w = 0, h = 0, sx = 0, sy = 0;
    ops::rotatedCanvasBounds(400, 300, 90.0, w, h, sx, sy);
    CHECK_NEAR(w, 300.0, 1e-9);
    CHECK_NEAR(h, 400.0, 1e-9);
    CHECK_NEAR(sx, 50.0, 1e-9);
    CHECK_NEAR(sy, -50.0, 1e-9);
    // A square canvas turns inside itself: no shift.
    ops::rotatedCanvasBounds(256, 256, 90.0, w, h, sx, sy);
    CHECK_NEAR(w, 256.0, 1e-9);
    CHECK_NEAR(h, 256.0, 1e-9);
    CHECK_NEAR(sx, 0.0, 1e-9);
    CHECK_NEAR(sy, 0.0, 1e-9);

    // Identity map: the resampler reproduces the source exactly.
    Image src(4, 3);
    for (std::uint32_t y = 0; y < 3; ++y)
        for (std::uint32_t x = 0; x < 4; ++x)
            src.at(x, y) = RGBAf{static_cast<float>(x + 10 * y), 0.5f, 0.0f,
                                 1.0f};
    const double identity[2][3] = {{1, 0, 0}, {0, 1, 0}};
    Image same = ops::resampleAffine(src, identity, 4, 3);
    for (std::uint32_t y = 0; y < 3; ++y)
        for (std::uint32_t x = 0; x < 4; ++x)
            CHECK_NEAR(same.at(x, y).r, src.at(x, y).r, 1e-5);

    // 180° about the centre with no origin shift: corners swap.
    double inv[2][3];
    ops::rotationInverse(inv, 4, 3, 180.0, 0.0, 0.0);
    Image turn = ops::resampleAffine(src, inv, 4, 3);
    CHECK_NEAR(turn.at(0, 0).r, src.at(3, 2).r, 1e-5);
    CHECK_NEAR(turn.at(3, 2).r, src.at(0, 0).r, 1e-5);
    CHECK_NEAR(turn.at(1, 1).r, src.at(2, 1).r, 1e-5);

    // Translating the source out of frame leaves the empty side transparent.
    const double shift[2][3] = {{1, 0, -2.0}, {0, 1, 0}};
    Image moved = ops::resampleAffine(src, shift, 4, 3);
    CHECK_NEAR(moved.at(0, 0).a, 0.0, 1e-6);
    CHECK_NEAR(moved.at(2, 0).r, src.at(0, 0).r, 1e-5);

    // Bilinear sampling reads straight alpha and hands back transparency
    // past the edge rather than a clamped colour.
    const RGBAf outside = ops::sampleBilinear(src, -5.0, -5.0, false);
    CHECK_NEAR(outside.a, 0.0, 1e-6);
    return 0;
}

int main() {
    testApplyAdjust();
    testDesaturate();
    testEqualize();
    testAutoTone();
    testAutoContrast();
    testAutoColor();
    testGradientMap();
    testCube();
    testSelectiveColor();
    testShadowsHighlights();
    testHdrToning();
    testMatchColor();
    testReplaceColor();
    testBlendInto();
    testGeometry();
    std::printf("checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
