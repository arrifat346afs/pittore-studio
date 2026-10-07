// gpu_invariants.cpp — analytic-invariant correctness checks for the
// GPU photo-operations kernels (sharpen, brightness/contrast, hue/saturation,
// median filter). These operations have no CPU reference backend, so parity
// against CPU is impossible; instead each is checked against exact
// mathematical invariants (flat fields, impulse removal, known HSL transforms)
// which any correct kernel must reproduce, on the GPU itself.
//
// The caller decides which GPU type to probe; unavailable GPU backends are a
// clean skip, not a failure.
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "engine/compute/factory.h"
#include "engine/core/pixel.h"
#include "test_util.h"
#include "gpu_invariants.h"

using pittore::RGBAf;
using pittore::compute::BackendType;

namespace {

bool near(const RGBAf& a, const RGBAf& b, float eps) {
    return std::abs(a.r - b.r) <= eps && std::abs(a.g - b.g) <= eps &&
           std::abs(a.b - b.b) <= eps && std::abs(a.a - b.a) <= eps;
}

void expect(bool ok, const char* label) {
    ++pittore_test::checks();
    if (!ok) {
        std::string msg = std::string("invariant failed: ") + label;
        pittore_test::report_failure(__FILE__, 0, msg);
        std::printf("    FAIL %s\n", label);
    }
}

struct Probe {
    std::unique_ptr<pittore::compute::ComputeBackend> be;
    std::unique_ptr<pittore::compute::Buffer> src;
    std::unique_ptr<pittore::compute::Buffer> dst;
};

bool probe(BackendType type, Probe& p) {
    p.be = pittore::compute::make_backend(type);
    if (!p.be) {
        std::printf("  [%s] backend not built / no device — skipped\n",
                    pittore::compute::to_string(type));
        return false;
    }
    return true;
}

bool all_pixels_ok(const Probe& p, std::size_t n, const RGBAf* want, float eps) {
    const auto* got = static_cast<const RGBAf*>(p.dst->host());
    for (std::size_t i = 0; i < n; ++i)
        if (!near(got[i], want[i], eps)) return false;
    return true;
}

// --- sharpen ---------------------------------------------------------------
// Unsharp on a flat field: blur(flat) == flat, so src - blur == 0 <= threshold
// and the output must equal the input exactly, even with threshold == 0.
void check_sharpen(BackendType type) {
    constexpr std::uint32_t w = 256, h = 256;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    const RGBAf fillc{0.4f, 0.5f, 0.6f, 0.8f};

    Probe p;
    if (!probe(type, p)) return;
    std::vector<RGBAf> want(n, fillc);
    p.src = p.be->make_buffer(n * sizeof(RGBAf));
    p.dst = p.be->make_buffer(n * sizeof(RGBAf));
    std::memcpy(p.src->host(), want.data(), p.src->size());

    for (float amount : {0.5f, 1.0f, 2.0f}) {
        p.be->sharpen(*p.src, *p.dst, w, h, amount, 2.0f, 0.0f);
        std::string label = std::string(pittore::compute::to_string(type)) +
                            " sharpen flat amount=" + std::to_string(amount);
        expect(all_pixels_ok(p, n, want.data(), 1e-6f), label.c_str());
    }

    // A linear ramp must survive sharpen untouched in the interior: the
    // gaussian keeps linear functions exact, so src - blur == 0 there.
    // Channels stay inside [0,1] — the kernel rightfully clamps out-of-range.
    std::vector<RGBAf> ramp(n);
    for (std::size_t i = 0; i < n; ++i) {
        const float x = static_cast<float>(i % w);
        const float y = static_cast<float>(i / w);
        ramp[i] = RGBAf{0.1f + (x + y) / 2000.0f, 0.2f + (x - y) / 3200.0f,
                        0.5f, 1.0f};
    }
    std::memcpy(p.src->host(), ramp.data(), p.src->size());
    p.be->sharpen(*p.src, *p.dst, w, h, 1.0f, 2.0f, 0.0f);
    bool ok = true;
    const auto* got = static_cast<const RGBAf*>(p.dst->host());
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t x = i % w, y = static_cast<std::uint32_t>(i / w);
        if (x < 8 || y < 8 || x >= w - 8 || y >= h - 8) continue;  // edges
        if (!near(got[i], ramp[i], 1e-4f)) { ok = false; break; }
    }
    std::string rl = std::string(pittore::compute::to_string(type)) +
                     " sharpen ramp interior";
    expect(ok, rl.c_str());
}

// --- brightness/contrast ---------------------------------------------------
// out = clamp((src + brightness) * (1 + contrast)). Exact arithmetic.
void check_brightness_contrast(BackendType type) {
    constexpr std::uint32_t w = 128, h = 128;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    const RGBAf fill{0.5f, 0.3f, 0.7f, 0.5f};

    Probe p;
    if (!probe(type, p)) return;
    std::vector<RGBAf> want(n, fill);
    p.src = p.be->make_buffer(n * sizeof(RGBAf));
    p.dst = p.be->make_buffer(n * sizeof(RGBAf));
    std::memcpy(p.src->host(), want.data(), p.src->size());

    const float br = 0.1f, ct = 0.25f;
    const RGBAf expect_c{0.75f, 0.5f, 1.0f, 0.5f};  // 0.7*1.25=0.875→1.0 clamp
    p.be->brightness_contrast(*p.src, *p.dst, w, h, br, ct);
    std::vector<RGBAf> want2(n, expect_c);
    std::string label = std::string(pittore::compute::to_string(type)) +
                        " brightness_contrast exact";
    expect(all_pixels_ok(p, n, want2.data(), 1e-5f), label.c_str());
}

// --- hue/saturation --------------------------------------------------------
// Pure red (HSL = {0,1,0.5}):
//   hue +120°  ->  green {1/3,1,0.5}  ->  (0,1,0)
//   sat -> 0   ->  grey  {0,0,0.5}    ->  (0.5,0.5,0.5)
// A grey pixel (s == 0) must survive any hue shift / saturation.
void check_hue_saturation(BackendType type) {
    constexpr std::uint32_t w = 128, h = 128;
    const std::size_t n = static_cast<std::size_t>(w) * h;

    Probe p;
    if (!probe(type, p)) return;
    p.src = p.be->make_buffer(n * sizeof(RGBAf));
    p.dst = p.be->make_buffer(n * sizeof(RGBAf));

    const RGBAf red{1.0f, 0.0f, 0.0f, 1.0f};
    std::vector<RGBAf> reds(n, red);
    std::memcpy(p.src->host(), reds.data(), p.src->size());

    // red -> +120° hue -> green
    p.be->hue_saturation(*p.src, *p.dst, w, h, 120.0f, 0.0f, 0.0f);
    const RGBAf green{0.0f, 1.0f, 0.0f, 1.0f};
    std::vector<RGBAf> greens(n, green);
    std::string l1 = std::string(pittore::compute::to_string(type)) +
                     " hue_shift red->green";
    expect(all_pixels_ok(p, n, greens.data(), 1e-4f), l1.c_str());

    // red -> sat 0 -> grey at luminance 0.5
    p.be->hue_saturation(*p.src, *p.dst, w, h, 0.0f, -1.0f, 0.0f);
    const RGBAf grey{0.5f, 0.5f, 0.5f, 1.0f};
    std::vector<RGBAf> greys(n, grey);
    std::string l2 = std::string(pittore::compute::to_string(type)) +
                     " hue_saturation red->grey";
    expect(all_pixels_ok(p, n, greys.data(), 1e-4f), l2.c_str());

    // grey survives hue shift
    std::memcpy(p.src->host(), greys.data(), p.src->size());
    p.be->hue_saturation(*p.src, *p.dst, w, h, 240.0f, 0.5f, 0.0f);
    std::string l3 = std::string(pittore::compute::to_string(type)) +
                     " hue_saturation grey invariant";
    expect(all_pixels_ok(p, n, greys.data(), 1e-4f), l3.c_str());
}

// --- median filter ---------------------------------------------------------
// Flat in -> flat out. A single 1px salt grain in a flat field must be
// removed: every 3x3 window containing it sorts the median to the flat value.
void check_median(BackendType type) {
    constexpr std::uint32_t w = 128, h = 96;   // ragged edges for the 2D grid
    const std::size_t n = static_cast<std::size_t>(w) * h;
    const RGBAf fillc{0.3f, 0.5f, 0.7f, 0.9f};

    Probe p;
    if (!probe(type, p)) return;
    p.src = p.be->make_buffer(n * sizeof(RGBAf));
    p.dst = p.be->make_buffer(n * sizeof(RGBAf));

    std::vector<RGBAf> want(n, fillc);
    std::memcpy(p.src->host(), want.data(), p.src->size());
    p.be->median_filter(*p.src, *p.dst, w, h);
    std::string l1 = std::string(pittore::compute::to_string(type)) +
                     " median flat";
    expect(all_pixels_ok(p, n, want.data(), 0.0f), l1.c_str());

    // single hot pixel in the middle
    auto impulse = want;
    const std::size_t hot = static_cast<std::size_t>(h / 2) * w + w / 2;
    impulse[hot] = RGBAf{0.0f, 0.0f, 0.0f, 0.9f};
    std::memcpy(p.src->host(), impulse.data(), p.src->size());
    p.be->median_filter(*p.src, *p.dst, w, h);
    std::string l2 = std::string(pittore::compute::to_string(type)) +
                     " median removes impulse";
    expect(all_pixels_ok(p, n, want.data(), 1e-5f), l2.c_str());
}

}  // namespace

void run_gpu_invariants(pittore::compute::BackendType gpu_type) {
    switch (gpu_type) {
    case BackendType::CUDA:
    case BackendType::HIP:
        check_sharpen(gpu_type);
        check_brightness_contrast(gpu_type);
        check_hue_saturation(gpu_type);
        check_median(gpu_type);
        break;
    default:
        break;
    }
}