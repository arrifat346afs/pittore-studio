// Gaussian blur: kernel shape, energy preservation, edge clamping, identity.
#include <cmath>
#include <cstring>
#include <vector>

#include "engine/compute/factory.h"
#include "engine/core/pixel.h"
#include "test_util.h"

using pittore::RGBAf;

std::vector<float> expected_kernel(float sigma, int& radius) {
    radius = std::max(1, static_cast<int>(std::ceil(3.0f * sigma)));
    std::vector<float> k(static_cast<std::size_t>(2 * radius + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float t = static_cast<float>(i) / sigma;
        const float v = std::exp(-0.5f * t * t);
        k[static_cast<std::size_t>(i + radius)] = v;
        sum += v;
    }
    for (float& v : k) v /= sum;
    return k;
}

void test_blur() {
    auto backend = pittore::compute::make_default_backend();

    // --- Impulse test: energy preserved, shape = separable gaussian.
    constexpr std::uint32_t w = 64, h = 64;
    constexpr float sigma = 1.5f;
    const std::size_t n = static_cast<std::size_t>(w) * h;

    std::vector<RGBAf> src(n, RGBAf{0, 0, 0, 0});
    src[static_cast<std::size_t>(32) * w + 32] = RGBAf{1, 1, 1, 1};

    auto sbuf = backend->make_buffer(n * sizeof(RGBAf));
    auto dbuf = backend->make_buffer(n * sizeof(RGBAf));
    std::memcpy(sbuf->host(), src.data(), sbuf->size());

    backend->gaussian_blur(*sbuf, *dbuf, w, h, sigma);
    const auto* out = static_cast<const RGBAf*>(dbuf->host());

    int radius = 0;
    const std::vector<float> k = expected_kernel(sigma, radius);

    // Energy preserved: sum of blurred values ~= 1 (impulse mass).
    double total = 0.0;
    for (std::size_t i = 0; i < n; ++i) total += out[i].r;
    CHECK(std::abs(total - 1.0) < 0.01);

    // Center value = k[center] * k[center] (separable outer product).
    CHECK_NEAR(out[static_cast<std::size_t>(32) * w + 32].r,
               k[static_cast<std::size_t>(radius)] * k[static_cast<std::size_t>(radius)],
               1e-5f);

    // Symmetry: +1 coeff == -1 coeff.
    CHECK_NEAR(out[static_cast<std::size_t>(32) * w + 33].r,
               out[static_cast<std::size_t>(32) * w + 31].r, 1e-5f);

    // --- Edge clamp: far corner is not black; a uniform image stays uniform.
    constexpr std::uint32_t cw = 16, ch = 16;
    std::vector<RGBAf> flat(static_cast<std::size_t>(cw) * ch, RGBAf{0.3f, 0.3f, 0.3f, 1});
    auto fbuf = backend->make_buffer(flat.size() * sizeof(RGBAf));
    auto fout = backend->make_buffer(flat.size() * sizeof(RGBAf));
    std::memcpy(fbuf->host(), flat.data(), fbuf->size());
    backend->gaussian_blur(*fbuf, *fout, cw, ch, 2.0f);
    const auto* f = static_cast<const RGBAf*>(fout->host());
    CHECK_NEAR(f[0].r, 0.3f, 1e-5f);  // corner never drifts
    CHECK_NEAR(f[0].a, 1.0f, 1e-5f);

    // --- Identity at sigma <= 0.
    backend->gaussian_blur(*sbuf, *dbuf, w, h, 0.0f);
    const auto* id = static_cast<const RGBAf*>(dbuf->host());
    for (std::size_t i = 0; i < n; ++i) {
        CHECK_NEAR(id[i].r, src[i].r, 0.0f);
        CHECK_NEAR(id[i].a, src[i].a, 0.0f);
    }
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_blur)
#endif