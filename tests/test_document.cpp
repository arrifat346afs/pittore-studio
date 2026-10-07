#include <cmath>
#include <cstring>

#include "engine/compute/factory.h"
#include "engine/core/document.h"
#include "engine/core/pixel.h"
#include "test_util.h"

using pittore::RGBAf;
using pittore::Document;
using pittore::Layer;

static bool near(const RGBAf& a, const RGBAf& b, float eps) {
    return std::abs(a.r - b.r) <= eps && std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps && std::abs(a.a - b.a) <= eps;
}

void test_document() {
    auto be = pittore::compute::make_default_backend();
    if (!be) { std::printf("  [document] no backend — skipped\n"); return; }

    constexpr std::uint32_t w = 4, h = 2;

    // --- single opaque layer: pixels pass through unchanged ---------------
    Document doc(w, h);
    Layer& base = doc.add_layer("base");
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const float v = static_cast<float>(y * w + x) / 15.0f;
            base.pixels.at(x, y) = RGBAf{v, v * 0.5f, 1.0f - v, 1.0f};
        }

    pittore::Image out(w, h);
    doc.compose(*be, out);

    bool ok = true;
    for (std::uint32_t i = 0; i < w * h; ++i) {
        if (!near(out.data()[i], base.pixels.data()[i], 1e-6f)) { ok = false; break; }
    }
    CHECK(ok);  // single layer passthrough

    // --- opacity -----------------------------------------------------------
    Layer& top = doc.add_layer("top");
    top.pixels.fill(RGBAf{0.0f, 1.0f, 0.0f, 1.0f});
    top.opacity = 0.5f;

    doc.compose(*be, out);
    const auto& bpx = doc.layers()[0].pixels.at(0, 0);
    const RGBAf g{0.0f, 1.0f, 0.0f, 0.5f};
    // normal composite: rgb_out = rgb_top*a_top + rgb_bot*a_bot*(1-a_top).
    // a_top is 0.5 after opacity scaling, a_bot is bpx.a = 1.0.
    RGBAf expected;
    expected.r = (g.r * g.a) + (bpx.r * bpx.a) * (1.0f - g.a);
    expected.g = (g.g * g.a) + (bpx.g * bpx.a) * (1.0f - g.a);
    expected.b = (g.b * g.a) + (bpx.b * bpx.a) * (1.0f - g.a);
    expected.a = g.a + bpx.a * (1.0f - g.a);
    if (expected.a > 0.0f) { expected.r /= expected.a; expected.g /= expected.a; expected.b /= expected.a; }
    CHECK(near(out.at(0, 0), expected, 1e-4f));

    // --- hidden layer -----------------------------------------------------
    doc.layers()[1].visible = false;
    doc.compose(*be, out);
    CHECK(near(out.at(0, 0), doc.layers()[0].pixels.at(0, 0), 1e-6f));
    doc.layers()[1].visible = true;

    // --- brightness -------------------------------------------------------
    doc.layers()[1].opacity = 0.0f;  // drop top from composite
    Layer& adj = doc.add_layer("adj");
    adj.pixels.fill(RGBAf{0.2f, 0.3f, 0.4f, 1.0f});
    adj.brightness = 0.1f;
    adj.contrast = 0.0f;
    doc.compose(*be, out);
    // expected per kernel: clamp((0.2 + 0.1) * 1.0) = 0.3, etc.
    CHECK_NEAR(out.at(0, 0).r, 0.3f, 1e-5f);
    CHECK_NEAR(out.at(0, 0).g, 0.4f, 1e-5f);
    CHECK_NEAR(out.at(0, 0).b, 0.5f, 1e-5f);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_document)
#endif
