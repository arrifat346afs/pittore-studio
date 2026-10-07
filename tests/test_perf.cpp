// test_perf.cpp — CPU/GPU/UX timing that matters for optimization.
// Every line is flushed immediately so a slow item attributes correctly.
// Perf never fails on speed (only on non-finite output or exceptions).
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/compute/backend.h"
#include "engine/compute/factory.h"
#include "engine/core/image.h"
#include "engine/filter/filters.h"
#include "engine/filter/registry/filter_registry.h"
#include "test_util.h"

namespace {

double nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch())
        .count();
}

void perfLog(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
    std::fflush(stdout);
}

void makeProbe(pittore::Image& img) {
    const std::uint32_t w = img.width(), h = img.height();
    std::uint64_t salt = 0x9E3779B97F4A7C15ULL;
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            float n = 0.0f;
            if (x >= w / 2) {
                salt = salt * 6364136223846793005ULL + 1442695040888963407ULL;
                n = ((salt >> 33) & 1) ? 0.35f : -0.35f;
            }
            img.data()[std::size_t(y) * w + x] = pittore::RGBAf{
                std::clamp(float(x) / float(w) + n, 0.0f, 1.0f),
                std::clamp(float(y) / float(h) + n, 0.0f, 1.0f), 0.5f, 1.0f};
        }
}

bool finiteImage(const pittore::Image& img) {
    for (std::size_t i = 0; i < img.pixel_count(); ++i) {
        const auto& q = img.data()[i];
        if (!std::isfinite(q.r) || !std::isfinite(q.g) ||
            !std::isfinite(q.b) || !std::isfinite(q.a))
            return false;
    }
    return true;
}

const pittore::filter::FilterDef* findDef(
    const std::vector<pittore::filter::FilterDef>& defs,
    const std::string& id) {
    for (const auto& d : defs)
        if (d.id == id) return &d;
    return nullptr;
}

}  // namespace

int main() {
    using pittore::compute::BackendType;
    const auto defs = pittore::filter::allFilterDefs();
    perfLog("[perf] filters registered=%d", (int)defs.size());

    // ---- CPU: costly + common filters on 640x360 (37.5x the audit probe) ----
    {
        const std::vector<std::string> ids = {
            "angled_strokes", "note_paper", "plaster", "torn_edges", "stamp",
            "shape_blur", "reticulation", "sprayed_strokes", "palette_knife",
            "sumi_e", "lens_blur", "oil_paint", "watercolor", "chrome",
            "gaussian_blur", "box_blur", "median", "unsharp_mask", "motion_blur",
            "dust_scratches", "neural.style_transfer"};
        const std::uint32_t w = 640, h = 360;
        const double mp = double(w) * h / 1e6;
        for (const auto& id : ids) {
            const auto* def = findDef(defs, id);
            if (!def) {
                perfLog("[perf-cpu] %-24s 640x360 SKIP-MISSING", id.c_str());
                continue;
            }
            std::vector<double> par;
            for (const auto& p : def->params) par.push_back(p.def);
            pittore::Image probe(w, h);
            makeProbe(probe);
            double ms = 0;
            bool finite = true;
            std::string err;
            try {
                pittore::Image warm = probe.clone();
                pittore::filter::applyFilter(warm, def->id, par);
                pittore::Image timed = probe.clone();
                const double t0 = nowMs();
                pittore::filter::applyFilter(timed, def->id, par);
                ms = nowMs() - t0;
                finite = finiteImage(timed);
            } catch (const std::exception& e) {
                err = e.what();
            } catch (...) {
                err = "<unknown>";
            }
            const bool bad = !err.empty() || !finite;
            perfLog("[perf-cpu] %-24s 640x360 ms=%9.2f mps=%6.2f %s%s",
                    id.c_str(), ms, ms > 0 ? mp / (ms / 1000.0) : 0,
                    bad ? "BAD " : "", err.c_str());
            if (bad) CHECK(false);
        }
    }

    // ---- Backend kernels: CPU reference timing on 1920x1080 ----
    // These are the exact ops the GPU parity tests cover; the numbers below
    // are the CPU baseline the GPU speedups divide by.
    std::vector<std::pair<std::string, double>> cpuKernelMs;
    {
        const std::uint32_t w = 1920, h = 1080;
        auto cpu = pittore::compute::make_backend(BackendType::CPU);
        if (!cpu) {
            perfLog("[perf-kernel-cpu] NO-CPU-BACKEND");
        } else {
            pittore::Image probe(w, h);
            makeProbe(probe);
            const std::size_t bytes =
                std::size_t(w) * h * sizeof(pittore::RGBAf);
            auto run = [&](const char* name, auto fn) {
                auto src = cpu->make_buffer(bytes);
                auto dst = cpu->make_buffer(bytes);
                std::memcpy(src->host(), probe.data(), bytes);
                const double tU0 = nowMs();
                src->upload();
                const double up = nowMs() - tU0;
                const double t0 = nowMs();
                fn(*src, *dst);
                const double ker = nowMs() - t0;
                const double tD0 = nowMs();
                dst->download();
                const double down = nowMs() - tD0;
                perfLog("[perf-kernel-cpu] %-12s 1920x1080 up=%7.2f ker=%9.2f "
                        "down=%7.2f total=%9.2f",
                        name, up, ker, down, up + ker + down);
                cpuKernelMs.emplace_back(name, up + ker + down);
            };
            run("box_blur", [&](auto& s, auto& d) {
                cpu->box_blur(s, d, w, h, 8);
            });
            run("median", [&](auto& s, auto& d) {
                cpu->median_radius(s, d, w, h, 3);
            });
            run("unsharp", [&](auto& s, auto& d) {
                cpu->unsharp_box(s, d, w, h, 0.8f, 2, 0.05f);
            });
            run("motion", [&](auto& s, auto& d) {
                cpu->motion_blur(s, d, w, h, 30.0f, 12);
            });
            run("lens", [&](auto& s, auto& d) {
                cpu->lens_blur(s, d, w, h, 8, 15, 0.3f, 200.0f / 255.0f,
                               0.4f);
            });
        }
    }

    // ---- GPU: same 5 kernels on 1920x1080, with speedup vs CPU ----
    {
        for (const auto& dev : pittore::compute::enumerate_devices())
            perfLog("[perf-gpu-dev] %s backend=%d mem=%lluMB", dev.name.c_str(),
                    (int)dev.type,
                    (unsigned long long)dev.memory_mb);
        const std::uint32_t w = 1920, h = 1080;
        for (BackendType bt : {BackendType::CUDA, BackendType::HIP}) {
            const char* bname =
                (bt == BackendType::CUDA) ? "CUDA" : "HIP";
            auto be = pittore::compute::make_backend(bt);
            if (!be) {
                perfLog("[perf-gpu-%s] UNAVAILABLE", bname);
                continue;
            }
            pittore::Image probe(w, h);
            makeProbe(probe);
            const std::size_t bytes =
                std::size_t(w) * h * sizeof(pittore::RGBAf);
            auto run = [&](const char* name, auto fn) {
                auto src = be->make_buffer(bytes);
                auto dst = be->make_buffer(bytes);
                std::memcpy(src->host(), probe.data(), bytes);
                const double tU0 = nowMs();
                src->upload();
                const double up = nowMs() - tU0;
                const double t0 = nowMs();
                fn(*src, *dst);
                const double ker = nowMs() - t0;
                const double tD0 = nowMs();
                dst->download();
                const double down = nowMs() - tD0;
                const double total = up + ker + down;
                double speedup = 0;
                for (const auto& kv : cpuKernelMs)
                    if (kv.first == name && total > 0) speedup = kv.second / total;
                perfLog("[perf-gpu-%s] %-12s 1920x1080 up=%7.2f ker=%9.2f "
                        "down=%7.2f total=%9.2f speedup=x%.1f",
                        bname, name, up, ker, down, total, speedup);
            };
            run("box_blur", [&](auto& s, auto& d) {
                be->box_blur(s, d, w, h, 8);
            });
            run("median", [&](auto& s, auto& d) {
                be->median_radius(s, d, w, h, 3);
            });
            run("unsharp", [&](auto& s, auto& d) {
                be->unsharp_box(s, d, w, h, 0.8f, 2, 0.05f);
            });
            run("motion", [&](auto& s, auto& d) {
                be->motion_blur(s, d, w, h, 30.0f, 12);
            });
            run("lens", [&](auto& s, auto& d) {
                be->lens_blur(s, d, w, h, 8, 15, 0.3f, 200.0f / 255.0f,
                              0.4f);
            });
        }
    }

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    std::fflush(stdout);
    return rc;
}
