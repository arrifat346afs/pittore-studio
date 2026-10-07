// bench_gpu.cpp — GPU compute benchmark.
//
// Runs real photo operations at 4K on every available GPU backend and reports
// per-operation time/throughput plus a single "Raster Score" (geometric mean of
// operation speedups over a fixed reference, ×1000). Scores are linear — twice
// the score is twice the raster performance — and comparable across machines.
//
// Standalone:   ./bench_gpu
// Via test-all: ./test-all --bench

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "engine/compute/factory.h"
#include "engine/core/pixel.h"

using pittore::RGBAf;

namespace {

constexpr std::uint32_t W = 3840;
constexpr std::uint32_t H = 2160;
constexpr std::size_t   N = static_cast<std::size_t>(W) * H;
constexpr std::size_t   BYTES = N * sizeof(RGBAf);

constexpr int WARMUP     = 5;
constexpr int KERNEL_ITERS = 50;
constexpr int BW_ITERS     = 20;

using clock = std::chrono::high_resolution_clock;
using pittore::compute::BlendMode;

double ms_between(clock::time_point a, clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// Reference times (ms) for a baseline 4K raster machine — the reference scores
// exactly 1000. Fixed values keep results comparable across machines.
int g_score_count;
double g_log_score;

double measure_and_score(pittore::compute::ComputeBackend& /*be*/, auto fn,
                         int iters, const char* label, double baseline_ms) {
    auto t0 = clock::now();
    for (int i = 0; i < iters; ++i) fn();
    auto t1 = clock::now();
    double per = ms_between(t0, t1) / iters;
    double mpx = static_cast<double>(N) / per / 1000.0;
    double ratio = baseline_ms / per;   // >1 = faster than reference
    std::printf("  %-20s %8.3f ms   %7.1f MPix/s   x%.2f\n", label, per, mpx, ratio);
    if (baseline_ms > 0.0) {
        ++g_score_count;
        g_log_score += std::log(ratio);
    }
    return per;
}

double end_score() {
    if (g_score_count == 0) return 0.0;
    const double score = 1000.0 * std::exp(g_log_score / g_score_count);
    std::printf("\n  raster score:        %8.1f\n", score);
    std::printf("  ops scored:          %d\n", g_score_count);
    return score;
}

void bench_backend(pittore::compute::ComputeBackend& be) {
    std::printf("  device:  %s\n", be.name().c_str());
    std::printf("  memory:  %lu MB\n", static_cast<unsigned long>(be.device().memory_mb));
    std::printf("  arch:    %d.%d\n\n", be.device().compute_major, be.device().compute_minor);

    auto src = be.make_buffer(BYTES);
    auto dst = be.make_buffer(BYTES);
    auto top = be.make_buffer(BYTES);

    auto* p = static_cast<RGBAf*>(src->host());
    for (std::size_t i = 0; i < N; ++i)
        p[i] = RGBAf{0.5f, 0.3f, 0.7f, 0.9f};
    std::memcpy(top->host(), p, BYTES);
    src->upload();
    top->upload();

    // warm up — compile + settle each code path
    for (int i = 0; i < WARMUP; ++i) {
        be.grayscale(*src, *dst, W, H);
        be.composite(*dst, *top, W, H, BlendMode::Normal);
        be.composite(*dst, *top, W, H, BlendMode::Multiply);
        be.gaussian_blur(*src, *dst, W, H, 2.0f);
        be.gaussian_blur(*src, *dst, W, H, 6.0f);
        be.sharpen(*src, *dst, W, H, 0.5f, 2.0f, 0.02f);
        be.brightness_contrast(*src, *dst, W, H, 0.05f, 0.2f);
        be.hue_saturation(*src, *dst, W, H, 30.0f, 0.3f, 0.05f);
        be.median_filter(*src, *dst, W, H);
    }

    g_score_count = 0;
    g_log_score = 0.0;

    measure_and_score(be, [&]{ be.grayscale(*src, *dst, W, H); }, KERNEL_ITERS,
                      "grayscale", 25.0);
    measure_and_score(be, [&]{ be.composite(*dst, *top, W, H, BlendMode::Normal); },
                      KERNEL_ITERS, "composite", 50.0);
    measure_and_score(be, [&]{ be.composite(*dst, *top, W, H, BlendMode::Multiply); },
                      KERNEL_ITERS, "composite mul", 60.0);
    measure_and_score(be, [&]{ be.gaussian_blur(*src, *dst, W, H, 0.7f); },
                      KERNEL_ITERS, "blur σ=0.7", 70.0);
    measure_and_score(be, [&]{ be.gaussian_blur(*src, *dst, W, H, 2.0f); },
                      KERNEL_ITERS, "blur σ=2", 90.0);
    measure_and_score(be, [&]{ be.gaussian_blur(*src, *dst, W, H, 6.0f); },
                      KERNEL_ITERS, "blur σ=6", 140.0);
    measure_and_score(be, [&]{ be.sharpen(*src, *dst, W, H, 0.5f, 2.0f, 0.02f); },
                      KERNEL_ITERS, "sharpen", 120.0);
    measure_and_score(be, [&]{ be.brightness_contrast(*src, *dst, W, H, 0.05f, 0.2f); },
                      KERNEL_ITERS, "brightness", 25.0);
    measure_and_score(be, [&]{ be.hue_saturation(*src, *dst, W, H, 30.0f, 0.3f, 0.05f); },
                      KERNEL_ITERS, "hue/saturation", 60.0);
    measure_and_score(be, [&]{ be.median_filter(*src, *dst, W, H); },
                      KERNEL_ITERS, "median 3x3", 55.0);

    // memory bandwidth (reported, not scored)
    measure_and_score(be, [&]{ src->upload(); }, BW_ITERS, "H2D", 0.0);
    measure_and_score(be, [&]{ dst->download(); }, BW_ITERS, "D2H", 0.0);

    end_score();
}

}  // namespace

void run_benchmarks() {
    std::printf("=== Pittore Studio — GPU raster benchmark ===\n");
    std::printf("resolution: %ux%u (%.1f MPix, %.1f MB)\n\n", W, H,
                static_cast<double>(N) / 1e6, static_cast<double>(BYTES) / 1e6);

    for (auto type : {pittore::compute::BackendType::CUDA,
                      pittore::compute::BackendType::HIP}) {
        auto be = pittore::compute::make_backend(type);
        if (!be) continue;
        std::printf("[%s]\n", pittore::compute::to_string(type));
        bench_backend(*be);
        std::printf("\n");
    }

    std::printf("=== all done ===\n");
}

#ifndef PITTORE_TEST_NO_MAIN
int main() {
    run_benchmarks();
    return 0;
}
#endif