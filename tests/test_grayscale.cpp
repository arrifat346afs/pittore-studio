// Grayscale kernel: Rec.709 luma, alpha preserved.
#include <cstring>
#include <vector>

#include "engine/compute/factory.h"
#include "engine/core/pixel.h"
#include "test_util.h"

using pittore::RGBAf;

void test_grayscale() {
    auto backend = pittore::compute::make_default_backend();

    constexpr std::uint32_t w = 4, h = 4;
    const std::size_t n = static_cast<std::size_t>(w) * h;

    std::vector<RGBAf> src(n);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x)
            src[static_cast<std::size_t>(y) * w + x] =
                RGBAf{1.0f, 0.5f, 0.25f, 1.0f};
    src[5] = RGBAf{0.0f, 0.0f, 0.0f, 0.0f};  // transparent black
    src[7] = RGBAf{1.0f, 1.0f, 1.0f, 0.5f};  // mid-alpha white

    auto sbuf = backend->make_buffer(n * sizeof(RGBAf));
    auto dbuf = backend->make_buffer(n * sizeof(RGBAf));
    std::memcpy(sbuf->host(), src.data(), sbuf->size());

    backend->grayscale(*sbuf, *dbuf, w, h);

    const auto* out = static_cast<const RGBAf*>(dbuf->host());
    const float expected = 0.2126f * 1.0f + 0.7152f * 0.5f + 0.0722f * 0.25f;
    CHECK_NEAR(out[0].r, expected, 1e-6f);
    CHECK_NEAR(out[0].g, expected, 1e-6f);
    CHECK_NEAR(out[0].b, expected, 1e-6f);
    CHECK_NEAR(out[0].a, 1.0f, 1e-6f);

    CHECK_NEAR(out[5].r, 0.0f, 1e-6f);
    CHECK_NEAR(out[5].a, 0.0f, 1e-6f);

    CHECK_NEAR(out[7].r, 1.0f, 1e-6f);  // white stays white
    CHECK_NEAR(out[7].a, 0.5f, 1e-6f);

    // In-place grayscale (src == dst) must also work.
    backend->grayscale(*sbuf, *sbuf, w, h);
    const auto* inplace = static_cast<const RGBAf*>(sbuf->host());
    CHECK_NEAR(inplace[0].r, expected, 1e-6f);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_grayscale)
#endif