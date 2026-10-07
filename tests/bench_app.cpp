// bench_app.cpp — app-level performance numbers (not a test).
//
// Covers the full engine surface so no effect can regress silently:
//   - rebuildComposite through the real AppState path (CPU + GPU)
//   - every ComputeBackend kernel (CPU + each GPU backend)
//   - every filter ID in the registry (full sweep at small res + reps at 1080p)
//   - every layer-style component + combined
//   - tonal ops / adjustments / mask finishing / curve LUTs / host blur
//   - IO codecs (PSD, project, XCF)
//   - brush / warp / tone micro-benches
//
// Logging is built to answer "what is causing the slowness":
//   - per-op median/mean/min/max + MPix/s + MB/s where relevant
//   - 10 ms single-digit budget verdict per row (OK vs SLOW xN)
//   - per-section totals + section bottleneck line
//   - global bottleneck ranking at the end (slowest first) with cause hints
//
//   ninja -C build tests/bench_app && ./build/tests/bench_app [--quick] [--filter=substr] [--csv=path] [cpu]
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QtGlobal>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

#include "engine/compute/adjust.h"
#include "engine/compute/dither.h"
#include "engine/compute/factory.h"
#include "engine/compute/layer_mask.h"
#include "engine/compute/paint.h"
#include "engine/compute/brushes/blur/blur_dab.h"
#include "engine/compute/brushes/heal/heal.h"
#include "engine/compute/brushes/history/history_dab.h"
#include "engine/compute/brushes/stamp/stamp.h"
#include "ui/persona/vector_node.h"
#include "ui/persona/vector_point_ops.h"
#include "engine/compute/tone_blend.h"
#include "engine/compute/warp.h"
#include "engine/core/image.h"
#include "engine/core/log.h"
#include "engine/core/tonal_ops.h"
#include "engine/filter/core/filter_detail.h"
#include "engine/filter/filters.h"
#include "engine/filter/registry/filter_registry.h"
#include "engine/io/project.h"
#include "engine/io/psd.h"
#include "engine/io/tiff.h"
#include "engine/io/webp.h"
#include "engine/io/xcf.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/boolean.h"
#include "engine/vector/path.h"
#include "engine/vector/svg_parse.h"
#include "engine/vector/vector_shape.h"
#include "ui/app_state.h"
#include "ui/mask_finish.h"
#ifdef PITTORE_HAS_ONNX
#include "engine/ai/bg_remove.h"
#include "engine/ai/shared/ai_preprocess.h"
#else
#include "engine/ai/bg_remove.h"
#endif

using namespace pittore::ui;
using hclock = std::chrono::high_resolution_clock;

// ---------------------------------------------------------------------------
// Bench harness: stats + budget verdicts + global bottleneck ranking.
// ---------------------------------------------------------------------------

namespace {

constexpr double kBudgetMs = 10.0;  // single-digit-ms budget for every op

struct BenchOpts {
    bool cpuOnly = false;
    bool quick = false;
    std::string filter;  // only print rows whose label contains this
    std::string csvPath;
    std::string onlySection;  // only RUN sections whose name contains this
};

BenchOpts g_opts;

struct Result {
    std::string section;
    std::string label;
    int w = 0, h = 0;
    double median = 0, mean = 0, min = 0, max = 0;
    double mpix = 0;  // MPix/s at median (0 when n/a)
    double mbs = 0;   // MB/s at median (0 when n/a)
    std::string hint;
};

std::vector<Result> g_results;

bool passFilter(const std::string& label) {
    return g_opts.filter.empty() || label.find(g_opts.filter) != std::string::npos;
}

// Section gate: unlike --filter (print-only), this skips the WORK, so a
// micro-measurement finishes in seconds without contending the GPU.
bool sectionActive(const char* section) {
    return g_opts.onlySection.empty() ||
           std::string(section).find(g_opts.onlySection) != std::string::npos;
}

double msBetween(hclock::time_point a, hclock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Stats {
    double median = 0, mean = 0, min = 0, max = 0;
};

Stats summarize(std::vector<double> ts) {
    Stats s;
    if (ts.empty()) return s;
    std::sort(ts.begin(), ts.end());
    s.median = ts[ts.size() / 2];
    s.min = ts.front();
    s.max = ts.back();
    s.mean = std::accumulate(ts.begin(), ts.end(), 0.0) / ts.size();
    return s;
}

// Time fn() iters times (after warmup iters, first timed iter also warms).
// Returns per-iteration stats in ms.
template <typename Fn>
Stats timeIters(Fn&& fn, int iters, int warmup = 1) {
    for (int i = 0; i < warmup; ++i) fn();
    std::vector<double> ts;
    ts.reserve(iters);
    for (int i = 0; i < iters; ++i) {
        const auto t0 = hclock::now();
        fn();
        ts.push_back(msBetween(t0, hclock::now()));
    }
    return summarize(ts);
}

const char* verdict(double medianMs) {
    return medianMs <= kBudgetMs ? "OK" : "SLOW";
}

void record(const std::string& section, const std::string& label, int w, int h,
            const Stats& s, double mpix, double mbs, const std::string& hint) {
    g_results.push_back({section, label, w, h, s.median, s.mean, s.min, s.max,
                         mpix, mbs, hint});
}

// Rich row: median + spread + throughput + budget verdict + cause hint.
void rowStats(const std::string& section, const std::string& label, int w, int h,
              const Stats& s, double mpix = 0.0, double mbs = 0.0,
              const std::string& hint = {}) {
    if (!passFilter(label)) return;
    record(section, label, w, h, s, mpix, mbs, hint);
    const double over = s.median / kBudgetMs;
    if (mpix > 0.0 && mbs > 0.0)
        std::printf("  %-36s %9.3f ms (mean %8.3f min %8.3f max %8.3f) %8.1f MPix/s %9.1f MB/s  [%s%s] %s\n",
                    label.c_str(), s.median, s.mean, s.min, s.max, mpix, mbs,
                    verdict(s.median),
                    s.median > kBudgetMs ? (" x" + std::to_string(over).substr(0, 4)).c_str() : "",
                    hint.c_str());
    else if (mpix > 0.0)
        std::printf("  %-36s %9.3f ms (mean %8.3f min %8.3f max %8.3f) %8.1f MPix/s  [%s%s] %s\n",
                    label.c_str(), s.median, s.mean, s.min, s.max, mpix,
                    verdict(s.median),
                    s.median > kBudgetMs ? (" x" + std::to_string(over).substr(0, 4)).c_str() : "",
                    hint.c_str());
    else if (mbs > 0.0)
        std::printf("  %-36s %9.3f ms (mean %8.3f min %8.3f max %8.3f) %9.1f MB/s  [%s%s] %s\n",
                    label.c_str(), s.median, s.mean, s.min, s.max, mbs,
                    verdict(s.median),
                    s.median > kBudgetMs ? (" x" + std::to_string(over).substr(0, 4)).c_str() : "",
                    hint.c_str());
    else
        std::printf("  %-36s %9.3f ms (mean %8.3f min %8.3f max %8.3f)  [%s%s] %s\n",
                    label.c_str(), s.median, s.mean, s.min, s.max,
                    verdict(s.median),
                    s.median > kBudgetMs ? (" x" + std::to_string(over).substr(0, 4)).c_str() : "",
                    hint.c_str());
}

void rowUs(const std::string& section, const std::string& label, double usPerOp,
           const std::string& hint = {}) {
    if (!passFilter(label)) return;
    // Sub-ms dab costs: keep in us but still record ms for the ranking.
    const double ms = usPerOp / 1000.0;
    Stats s{ms, ms, ms, ms};
    record(section, label, 0, 0, s, 0.0, 0.0, hint);
    std::printf("  %-36s %9.3f us/op  [%s] %s\n", label.c_str(), usPerOp,
                verdict(ms), hint.c_str());
}

void sectionHead(const char* title) {
    std::printf("\n== %s ==\n", title);
}

void sysInfo() {
    const unsigned hw = std::thread::hardware_concurrency();
    std::printf("bench_app: %s | threads=%u | Qt=%s | budget=%.0f ms/op\n",
                g_opts.quick ? "quick" : "full", hw, qVersion(), kBudgetMs);
    for (const auto& d : pittore::compute::enumerate_devices())
        std::printf("  device: %s (%s, %lu MB, arch %d.%d)\n",
                    pittore::compute::to_string(d.type), d.name.c_str(),
                    static_cast<unsigned long>(d.memory_mb), d.compute_major,
                    d.compute_minor);
    if (!g_opts.filter.empty())
        std::printf("  filter: rows matching '%s' only\n", g_opts.filter.c_str());
}

// Full-canvas photo pixels + half mask, native size.
LayerItem photoLayer(const QString& name, int w, int h, bool mask) {
    LayerItem l;
    l.name = name;
    l.kind = LayerItem::Kind::Pixel;
    auto img = std::make_shared<pittore::Image>(
        static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    for (int i = 0; i < w * h; ++i) {
        const float v = static_cast<float>((i * 31) % 251) / 251.0f;
        img->data()[i] = {v, 1.0f - v, v * v, 1.0f};
    }
    l.pixels = std::move(img);
    l.sourceStamp = 1;
    if (mask) {
        auto m = std::make_shared<pittore::Image>(
            static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                m->data()[y * w + x] =
                    pittore::compute::make_mask_pixel(x < w / 2 ? 1.0f : 0.0f);
        l.mask = std::move(m);
        l.hasMask = true;
    }
    return l;
}

DocumentItem* makeDoc(AppState& state, int w, int h, int layers, bool mask) {
    DocumentItem* doc = state.addDocument(QStringLiteral("b"), QSize(w, h), 72);
    if (!doc) return nullptr;
    doc->layers.clear();
    for (int i = 0; i < layers; ++i)
        doc->layers.append(photoLayer(QStringLiteral("l%1").arg(i), w, h, mask));
    return doc;
}

int benchItersHeavy() { return g_opts.quick ? 3 : 7; }
int benchItersStd() { return g_opts.quick ? 5 : 11; }
int benchItersLight() { return g_opts.quick ? 11 : 21; }

// ---------------------------------------------------------------------------
// 1. rebuildComposite through the real AppState path.
// ---------------------------------------------------------------------------

void benchRebuilds(bool gpu) {
    AppState state;
    AppSettings s = state.settings();
    s.gpuEnabled = gpu;
    state.applySettings(s);
    char title[160];
    std::snprintf(title, sizeof(title), "rebuildComposite [%s backend: %s]",
                  gpu ? "GPU" : "CPU",
                  state.computeDeviceLabel().toLocal8Bit().constData());
    sectionHead(title);
    std::printf("  -- full-canvas composite incl. gather+blend+blit+overlay; SLOW = over %.0f ms budget\n",
                kBudgetMs);
    struct Case {
        const char* label;
        int w, h, layers;
        bool mask;
        const char* hint;
    };
    std::vector<Case> cases = {
        {"1080p x2 plain", 1920, 1080, 2, false, "baseline: 2 full-frame blends + blit"},
        {"1080p x2 masked", 1920, 1080, 2, true, "adds mask sample+coverage per pixel"},
        {"1080p x8 masked", 1920, 1080, 8, true, "8x blend depth; watch linear growth"},
        {"4K x2 plain", 3840, 2160, 2, false, "4x pixels of 1080p; must stay <10ms"},
        {"4K x2 masked", 3840, 2160, 2, true, "mask path at 4K"},
    };
    if (!g_opts.quick)
        cases.push_back({"4K x8 masked", 3840, 2160, 8, true, "worst-case depth at 4K"});
    const int iters = benchItersStd();
    for (const auto& c : cases) {
        DocumentItem* doc = makeDoc(state, c.w, c.h, c.layers, c.mask);
        if (!doc) continue;
        auto fn = [&] { doc->rebuildComposite(); };
        Stats st = timeIters(fn, iters, 1);
        rowStats(title, c.label, c.w, c.h, st, (c.w * c.h) / st.median / 1000.0, 0.0, c.hint);
    }
    // One curves layer over one photo (LUT build + apply).
    {
        DocumentItem* doc = makeDoc(state, 1920, 1080, 1, false);
        LayerItem cu;
        cu.name = QStringLiteral("curves");
        cu.kind = LayerItem::Kind::Adjustment;
        cu.adjustmentKind =
            static_cast<int>(pittore::compute::AdjustmentKind::Curves);
        cu.adjustmentCurve = {QPointF(0, 0), QPointF(0.5, 0.75),
                              QPointF(1, 1)};
        cu.adjustmentCurveR = {QPointF(0, 0), QPointF(1, 0.5)};
        rebuildAdjustmentLUT(cu);
        doc->layers.prepend(cu);
        Stats st = timeIters([&] { doc->rebuildComposite(); }, iters, 1);
        rowStats(title, "1080p curves (master+R)", 1920, 1080, st,
                 (1920 * 1080) / st.median / 1000.0, 0.0,
                 "adjustment sampling inside composite_many walk");
    }
    // Tone-blend group over a photo.
    {
        DocumentItem* doc = makeDoc(state, 1920, 1080, 2, false);
        LayerItem g;
        g.name = QStringLiteral("G");
        g.kind = LayerItem::Kind::Group;
        g.toneBlendGroup = true;
        doc->layers[1].indent = 1;
        doc->layers.insert(1, g);
        Stats st = timeIters([&] { doc->rebuildComposite(); }, benchItersHeavy(), 1);
        rowStats(title, "1080p tone group", 1920, 1080, st,
                 (1920 * 1080) / st.median / 1000.0, 0.0,
                 "pyramid build + upsample + Oklab re-grade; see tone rows");
    }
    // Blend-mode stress: Hue/Saturation/Color go through the HSL path.
    {
        DocumentItem* doc = makeDoc(state, 1920, 1080, 3, false);
        if (doc && doc->layers.size() >= 3) {
            doc->layers[1].blendMode = QStringLiteral("Hue");
            doc->layers[2].blendMode = QStringLiteral("Color");
            Stats st = timeIters([&] { doc->rebuildComposite(); }, iters, 1);
            rowStats(title, "1080p hue+color blends", 1920, 1080, st,
                     (1920 * 1080) / st.median / 1000.0, 0.0,
                     "non-separable HSL blend path vs Normal");
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Every ComputeBackend kernel, CPU + each GPU backend.
// ---------------------------------------------------------------------------

void benchOneBackendKernels(pittore::compute::ComputeBackend& be,
                            const std::string& section, int W, int H) {
    using namespace pittore::compute;
    const std::size_t n = static_cast<std::size_t>(W) * H;
    const std::size_t bytes = n * sizeof(pittore::RGBAf);
    auto src = be.make_buffer(bytes);
    auto dst = be.make_buffer(bytes);
    auto top = be.make_buffer(bytes);
    auto* sp = static_cast<pittore::RGBAf*>(src->host());
    for (std::size_t i = 0; i < n; ++i)
        sp[i] = pittore::RGBAf{0.5f, 0.3f, 0.7f, 0.9f};
    std::memcpy(top->host(), sp, bytes);
    src->upload();
    top->upload();
    const double mpixDiv = static_cast<double>(W * H) / 1000.0;
    const int std = benchItersStd(), heavy = benchItersHeavy(), light = benchItersLight();
    // 4K heavy ops (median/motion/lens) are minutes-per-iter on CPU: cap them.
    const int huge = (W >= 3840) ? 2 : heavy;

    auto mpixOf = [&](const Stats& st) { return mpixDiv / st.median; };

    Stats st;
    st = timeIters([&] { be.grayscale(*src, *dst, W, H); }, std);
    rowStats(section, "grayscale", W, H, st, mpixOf(st), 0, "streaming 1-pass; bound by H2D+D2H on GPU");
    st = timeIters([&] { be.composite(*dst, *top, W, H, BlendMode::Normal); }, std);
    rowStats(section, "composite Normal", W, H, st, mpixOf(st), 0, "fast path; SLOW = transfer-dominated");
    st = timeIters([&] { be.composite(*dst, *top, W, H, BlendMode::Multiply); }, std);
    rowStats(section, "composite Multiply", W, H, st, mpixOf(st), 0, "separable arithmetic");
    st = timeIters([&] { be.composite(*dst, *top, W, H, BlendMode::Hue); }, std);
    rowStats(section, "composite Hue", W, H, st, mpixOf(st), 0, "HSL path: rgb_to_hsl+hsl_to_rgb per px");
    st = timeIters([&] { be.composite(*dst, *top, W, H, BlendMode::Dissolve); }, std);
    rowStats(section, "composite Dissolve", W, H, st, mpixOf(st), 0, "hash per px + branch");
    {
        const std::uint32_t x0 = W / 4, y0 = H / 4, x1 = x0 + W / 4, y1 = y0 + H / 4;
        st = timeIters([&] { be.composite_region(*dst, *top, W, H, x0, y0, x1, y1, BlendMode::Normal); }, light);
        rowStats(section, "composite_region 1/16", W / 4, H / 4, st,
                 (W / 4 * H / 4) / st.median / 1000.0, 0, "incremental repaint; SLOW = falls back to full");
    }
    st = timeIters([&] { be.clear(*dst); }, light);
    rowStats(section, "clear", W, H, st, mpixOf(st), 0, "memset; host fill on CPU, device memset on GPU");
    {
        std::vector<std::uint8_t> argb(n * 4);
        const std::size_t pitch = static_cast<std::size_t>(W) * 4;
        st = timeIters([&] { be.blit_premul(*src, argb.data(), W, 0, 0, W, H, pitch); }, std);
        rowStats(section, "blit_premul full", W, H, st, mpixOf(st),
                 (W * H * 4.0) / st.median / 1000.0,
                 "canvas upload; GPU converts on-device, host loops pixels");
    }
    for (float sigma : {0.7f, 2.0f, 6.0f}) {
        char lab[32];
        std::snprintf(lab, sizeof(lab), "gaussian_blur s=%.1f", sigma);
        st = timeIters([&] { be.gaussian_blur(*src, *dst, W, H, sigma); }, sigma > 3 ? heavy : std);
        rowStats(section, lab, W, H, st, mpixOf(st), 0, "separable 2-pass O(w*h*r); r=ceil(3*sigma)");
    }
    st = timeIters([&] { be.sharpen(*src, *dst, W, H, 0.5f, 2.0f, 0.02f); }, std);
    rowStats(section, "sharpen", W, H, st, mpixOf(st), 0, "blur + gate; SLOW = double blur cost");
    st = timeIters([&] { be.brightness_contrast(*src, *dst, W, H, 0.05f, 0.2f); }, light);
    rowStats(section, "brightness_contrast", W, H, st, mpixOf(st), 0, "1-pass pointwise");
    st = timeIters([&] { be.hue_saturation(*src, *dst, W, H, 30.0f, 0.3f, 0.05f); }, std);
    rowStats(section, "hue_saturation", W, H, st, mpixOf(st), 0, "HSL round-trip + fmod per px");
    st = timeIters([&] { be.median_filter(*src, *dst, W, H); }, heavy);
    rowStats(section, "median 3x3", W, H, st, mpixOf(st), 0, "9-tap sort x4 channels per px");
    st = timeIters([&] { be.box_blur(*src, *dst, W, H, 4); }, std);
    rowStats(section, "box_blur r4", W, H, st, mpixOf(st), 0, "separable box; sliding O(1)/px both passes");
    st = timeIters([&] { be.median_radius(*src, *dst, W, H, 2); }, huge);
    rowStats(section, "median_radius r2", W, H, st, mpixOf(st), 0, "25-tap nth_element per px per channel");
    st = timeIters([&] { be.unsharp_box(*src, *dst, W, H, 1.0f, 4, 0.02f); }, heavy);
    rowStats(section, "unsharp_box r4", W, H, st, mpixOf(st), 0, "box blur + gate");
    st = timeIters([&] { be.motion_blur(*src, *dst, W, H, 30.0f, 20); }, huge);
    rowStats(section, "motion_blur d20", W, H, st, mpixOf(st), 0, "41 bilinear taps per px");
    if (!g_opts.quick) {
        st = timeIters([&] { be.lens_blur(*src, *dst, W, H, 8, 0, 0.0f, 0.8f, 0.0f); }, huge > 3 ? 3 : huge, 1);
        rowStats(section, "lens_blur r8 disc", W, H, st, mpixOf(st), 0, "O(r^2) shaped kernel + specular mix");
    }
    // Placed + batched composite (the interactive move/resize path).
    st = timeIters([&] { be.composite_placed(*dst, *src, W, H, 0, 0, 1.0, 1.0, W, 0, 0, W, H, 1.0f, BlendMode::Normal); }, std);
    rowStats(section, "composite_placed 1:1", W, H, st, mpixOf(st), 0, "sample+fold+blend fused");
    {
        PlacedLayer layers[2];
        layers[0].src = src.get(); layers[0].sw = W; layers[0].sh = H;
        layers[0].ox = 0; layers[0].oy = 0; layers[0].sx = 1; layers[0].sy = 1;
        layers[0].x0 = 0; layers[0].y0 = 0; layers[0].x1 = W; layers[0].y1 = H;
        layers[0].fold = 1.0f; layers[0].mode = BlendMode::Normal;
        layers[1] = layers[0];
        layers[1].src = top.get();
        layers[1].fold = 0.5f; layers[1].mode = BlendMode::Multiply;
        be.clear(*dst);
        st = timeIters([&] { be.composite_many_into(*dst, W, 0, 0, W, H, layers, 2); }, std);
        rowStats(section, "composite_many x2", W, H, st, mpixOf(st), 0, "batched single launch/walk");
    }
    // Warp (liquify): identity mesh over a 512x512 region.
    {
        WarpMesh mesh = make_warp_mesh(W, H);
        const int x0 = W / 4, y0 = H / 4, x1 = x0 + 512, y1 = y0 + 512;
        WarpSubgrid grid = warp_subgrid(mesh, x0, y0, std::min(x1, W), std::min(y1, H));
        if (!grid.empty()) {
            if (be.type() != BackendType::CPU) src->upload();  // frozen snapshot resident
            st = timeIters([&] { be.warp(*dst, *src, W, H, grid, x0, y0, std::min(x1, W), std::min(y1, H)); }, heavy);
            rowStats(section, "warp 512 region", 512, 512, st,
                     (512.0 * 512.0) / st.median / 1000.0, 0, "mesh sample + premul bilinear fetch");
        }
    }
    // Tone blend (device-resident contract on GPU: upload once, time kernel).
    {
        ToneBlendParams tp;
        if (be.type() != BackendType::CPU) { src->upload(); top->upload(); }
        st = timeIters([&] { be.tone_blend(*dst, *src, W, H, tp); }, heavy);
        rowStats(section, "tone_blend", W, H, st, mpixOf(st), 0, "pyramid + upsample + pivot + Oklab grade");
        // Region tone blend (erase-dab sized region + halo).
        const std::uint32_t rw = 256, rh = 256;
        const std::uint32_t rx0 = W / 2 - rw / 2, ry0 = H / 2 - rh / 2;
        st = timeIters([&] { be.tone_blend_region(*dst, *src, W, H, rx0, ry0, rx0 + rw, ry0 + rh, tp); }, heavy);
        rowStats(section, "tone_blend_region 256", rw, rh, st,
                 (rw * rh) / st.median / 1000.0, 0, "sub-rect pyramid + halo; erase-in-tone-group path");
    }
    // Brush dabs at three radii (per-touch stroke cost).
    for (float r : {8.0f, 24.0f, 64.0f}) {
        be.clear(*dst);
        const pittore::RGBAf col{1.0f, 0.2f, 0.1f, 1.0f};
        const int N = r > 32 ? 50 : 200;
        auto t0 = hclock::now();
        for (int i = 0; i < N; ++i)
            be.paint_dab(*dst, W, H, W / 2.0f + (i % 17), H / 2.0f, r, 0.8f, 1.0f, col);
        const double us = msBetween(t0, hclock::now()) / N * 1000.0;
        char lab[32];
        std::snprintf(lab, sizeof(lab), "paint_dab r%.0f", r);
        rowUs(section, lab, us, "bbox launch; SLOW = full-frame staging fallback");
    }
}

void benchComputeKernels() {
    sectionHead("compute kernels (1080p per backend)");
    std::printf("  -- every ComputeBackend virtual; SLOW = over %.0f ms budget\n", kBudgetMs);
    std::vector<pittore::compute::BackendType> types = {pittore::compute::BackendType::CPU};
    if (!g_opts.cpuOnly) {
        types.push_back(pittore::compute::BackendType::CUDA);
        types.push_back(pittore::compute::BackendType::HIP);
    }
    for (auto t : types) {
        auto be = pittore::compute::make_backend(t);
        if (!be) {
            std::printf("  [%s] unavailable, skipped\n", pittore::compute::to_string(t));
            continue;
        }
        std::string section = std::string("kernel[") + be->name() + "]";
        std::printf("  [%s]\n", be->name().c_str());
        benchOneBackendKernels(*be, section, 1920, 1080);
    }
    // 4K spot-check on the default backend only (keeps runtime sane).
    if (!g_opts.quick) {
        auto be = g_opts.cpuOnly
                      ? pittore::compute::make_backend(pittore::compute::BackendType::CPU)
                      : pittore::compute::make_default_backend();
        if (be) {
            std::string section = std::string("kernel4K[") + be->name() + "]";
            std::printf("  [4K spot-check: %s]\n", be->name().c_str());
            benchOneBackendKernels(*be, section, 3840, 2160);
        }
    }
}

// ---------------------------------------------------------------------------
// 3. Every filter ID in the registry.
// ---------------------------------------------------------------------------

void benchFilters() {
    sectionHead("filters (full registry sweep)");
    auto defs = pittore::filter::allFilterDefs();
    std::printf("  -- %zu filter IDs; sweep at 640x360 (fast) + reps at 1080p; SLOW = over %.0f ms\n",
                defs.size(), kBudgetMs);
    const int SW = 640, SH = 360;
    pittore::Image base(SW, SH);
    for (std::size_t i = 0; i < base.pixel_count(); ++i) {
        const float v = static_cast<float>((i * 31) % 251) / 251.0f;
        base.data()[i] = {v, 1.0f - v, v * v, 1.0f};
    }
    // Full sweep at small res: catches pathological outliers cheaply.
    for (const auto& d : defs) {
        std::string label = std::string("filter ") + d.id;
        if (!passFilter(label) && !passFilter(std::string("filter sweep"))) continue;
        auto par = pittore::filter::defaultParams(d.id);
        Stats st = timeIters([&] {
            pittore::Image img = base.clone();
            pittore::filter::applyFilter(img, d.id, par);
        }, 3, 1);
        const double mpix = (SW * SH) / st.median / 1000.0;
        // Always record; only print SLOW rows (or when filtering) so the
        // sweep summary stays readable while the ranking stays complete.
        if (st.median > kBudgetMs || !g_opts.filter.empty())
            rowStats("filter sweep", label, SW, SH, st, mpix, 0.0, d.category);
        else
            record("filter sweep", label, SW, SH, st, mpix, 0.0, d.category);
    }
    // Slowest-10 at sweep res: the bottleneck ranking for filters.
    {
        std::vector<Result> sweep;
        for (const auto& r : g_results)
            if (r.section == "filter sweep") sweep.push_back(r);
        std::sort(sweep.begin(), sweep.end(),
                  [](const Result& a, const Result& b) { return a.median > b.median; });
        std::printf("  -- slowest 10 filters at %dx%d (bottlenecks first):\n", SW, SH);
        for (std::size_t i = 0; i < std::min<std::size_t>(10, sweep.size()); ++i)
            std::printf("     %-28s %9.3f ms  %6.1f MPix/s  [%s]\n",
                        sweep[i].label.c_str(), sweep[i].median, sweep[i].mpix,
                        verdict(sweep[i].median));
    }
    // Per-category representative at 1080p: the number users actually feel.
    sectionHead("filters (1080p representatives, one per category)");
    const std::vector<std::string> reps = {
        "gaussian_blur", "field_blur", "unsharp_mask", "add_noise",
        "cutout", "mosaic", "photocopy", "twirl", "lens_correction",
        "oil_paint", "sponge", "stained_glass", "clouds", "bump_map",
        "deinterlace", "neural.style_transfer", "high_pass",
    };
    const int W = 1920, H = 1080;
    pittore::Image big(W, H);
    for (std::size_t i = 0; i < big.pixel_count(); ++i) {
        const float v = static_cast<float>((i * 31) % 251) / 251.0f;
        big.data()[i] = {v, 1.0f - v, v * v, 1.0f};
    }
    for (const auto& id : reps) {
        if (!pittore::filter::findFilter(id)) continue;
        auto par = pittore::filter::defaultParams(id);
        Stats st = timeIters([&] {
            pittore::Image img = big.clone();
            pittore::filter::applyFilter(img, id, par);
        }, benchItersHeavy(), 1);
        rowStats("filter 1080p", std::string("filter ") + id, W, H, st,
                 (W * H) / st.median / 1000.0, 0.0, "default params; clone+apply incl.");
    }
}

// ---------------------------------------------------------------------------
// 4. Layer styles.
// ---------------------------------------------------------------------------

void benchStyles() {
    sectionHead("layer styles (512px Rgba8)");
    const int S = 512;
    pittore::render::Rgba8Image base;
    base.w = S; base.h = S;
    base.px.assign(static_cast<std::size_t>(S) * S * 4, 0);
    for (int y = S / 4; y < 3 * S / 4; ++y)
        for (int x = S / 4; x < 3 * S / 4; ++x) {
            const std::size_t o = (static_cast<std::size_t>(y) * S + x) * 4;
            base.px[o] = base.px[o + 1] = base.px[o + 2] = 200;
            base.px[o + 3] = 255;
        }
    struct StyleCase { const char* label; pittore::render::LayerStyle st; const char* hint; };
    std::vector<StyleCase> cases;
    { pittore::render::LayerStyle s; s.hasDropShadow = true; cases.push_back({"style drop shadow", s, "alpha blur + offset + composite"}); }
    { pittore::render::LayerStyle s; s.hasInnerShadow = true; cases.push_back({"style inner shadow", s, "blurred inverted mask shading"}); }
    { pittore::render::LayerStyle s; s.hasOuterGlow = true; cases.push_back({"style outer glow", s, "blur + screen blend"}); }
    { pittore::render::LayerStyle s; s.hasInnerGlow = true; cases.push_back({"style inner glow", s, "edge-band glow"}); }
    { pittore::render::LayerStyle s; s.hasColorOverlay = true; cases.push_back({"style color overlay", s, "flat fill blend"}); }
    { pittore::render::LayerStyle s; s.hasGradient = true; cases.push_back({"style gradient overlay", s, "per-pixel gradient ramp"}); }
    { pittore::render::LayerStyle s; s.hasStroke = true; s.stroke.size = 6; cases.push_back({"style stroke 6px", s, "SDF band along edge"}); }
    { pittore::render::LayerStyle s; s.hasBevel = true; cases.push_back({"style bevel", s, "blurred slope + dual lighting"}); }
    { pittore::render::LayerStyle s; s.hasSatin = true; cases.push_back({"style satin", s, "dual offset difference"}); }
    { pittore::render::LayerStyle s; s.hasBlur = true; s.blur.radius = 5; cases.push_back({"style layer blur", s, "separable blur of layer"}); }
    { pittore::render::LayerStyle s; s.hasDropShadow = true; s.hasStroke = true; s.hasBevel = true; s.hasColorOverlay = true;
      cases.push_back({"style combined x4", s, "stacked effects; watch super-linear growth"}); }
    for (const auto& c : cases) {
        Stats st = timeIters([&] {
            pittore::render::Rgba8Image img = base;
            int grow = 0;
            pittore::render::applyLayerStyle(img, c.st, &grow);
        }, benchItersHeavy(), 1);
        rowStats("style", c.label, S, S, st, (S * S) / st.median / 1000.0, 0.0, c.hint);
    }
}

// ---------------------------------------------------------------------------
// 5. Tonal ops + every adjustment kind.
// ---------------------------------------------------------------------------

void benchTonalAdjust() {
    sectionHead("tonal ops + adjustments (4K unless noted)");
    const std::uint32_t w = 3840, h = 2160;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    pittore::Image base(w, h);
    for (std::size_t i = 0; i < n; ++i) {
        const float v = static_cast<float>((i * 31) % 251) / 251.0f;
        base.data()[i] = {v, 1.0f - v, v * v, 1.0f};
    }
    Stats st;
    st = timeIters([&] {
        pittore::Histogram256 hh{};
        pittore::computeHistogram(base, hh);
    }, benchItersStd(), 1);
    rowStats("tonal", "histogram", w, h, st, n / st.median / 1000.0, 0, "1-pass binning; SLOW = per-px clamp+float->int");
    st = timeIters([&] {
        pittore::Image img = base.clone();
        pittore::applyLevels(img, 3, 0.05, 0.95, 1.2, 0.0, 1.0);
    }, benchItersStd(), 1);
    rowStats("tonal", "levels RGB", w, h, st, n / st.median / 1000.0, 0, "pow() per channel per px");
    {
        float lut[256];
        std::vector<std::pair<double, double>> pts = {{0.0, 0.0}, {0.5, 0.75}, {1.0, 1.0}};
        auto t0 = hclock::now();
        const int N = 200;
        for (int i = 0; i < N; ++i) pittore::buildCurveLUT(pts, lut);
        std::vector<double> one = {msBetween(t0, hclock::now()) / N};
        Stats s = summarize(one);  // single-sample wrapper
        // Re-time properly for spread:
        s = timeIters([&] { pittore::buildCurveLUT(pts, lut); }, 200, 5);
        rowStats("tonal", "buildCurveLUT", 0, 0, s, 0, 0, "256-entry table; should be us");
        st = timeIters([&] {
            pittore::Image img = base.clone();
            pittore::applyCurveLUT(img, 3, lut);
        }, benchItersStd(), 1);
        rowStats("tonal", "applyCurveLUT RGB", w, h, st, n / st.median / 1000.0, 0, "LUT sample per channel per px");
        float lp[5] = {0.05f, 0.95f, 1.2f, 0.0f, 1.0f};
        st = timeIters([&] {
            float l2[256];
            pittore::buildLevelsLUT(lp, l2);
        }, 200, 5);
        rowStats("tonal", "buildLevelsLUT", 0, 0, st, 0, 0, "pow-free table path");
    }
    st = timeIters([&] {
        pittore::Image img = base.clone();
        pittore::applyAddNoise(img, 0.1, true);
    }, benchItersHeavy(), 1);
    rowStats("tonal", "addNoise mono", w, h, st, n / st.median / 1000.0, 0, "PRNG per px");
    st = timeIters([&] {
        pittore::Image img = base.clone();
        pittore::applyUnsharpMask(img, 1.0, 4, 0.02);
    }, benchItersHeavy(), 1);
    rowStats("tonal", "unsharpMask r4", w, h, st, n / st.median / 1000.0, 0, "2x box O(r) + gate");
    // Every adjustment kind through the shared adjust::apply fast path.
    {
        std::vector<pittore::RGBAf> px(n);
        for (std::size_t i = 0; i < n; ++i) px[i] = base.data()[i];
        std::vector<pittore::RGBAf> out(n);
        float lut[768];
        for (int i = 0; i < 768; ++i) lut[i] = (i % 256) / 255.0f;
        for (int k = 1; k <= 14; ++k) {
            auto kind = static_cast<pittore::compute::AdjustmentKind>(k);
            float p[16] = {};
            p[0] = 0.1f; p[1] = 0.1f; p[2] = 0.1f; p[3] = 0.5f; p[4] = 0.5f;
            const char* names[] = {"", "brightness", "levels", "curves", "exposure",
                                   "vibrance", "hueSat", "invert", "threshold",
                                   "posterize", "photoFilter", "whiteBalance",
                                   "blackWhite", "channelMixer", "colorBalance"};
            Stats s = timeIters([&] {
                for (std::size_t i = 0; i < n; ++i) {
                    float r, g, b;
                    pittore::compute::adjust::apply(kind, p, lut, px[i].r, px[i].g,
                                                     px[i].b, r, g, b);
                    out[i].r = r; out[i].g = g; out[i].b = b;
                }
            }, 5, 1);
            rowStats("adjust", std::string("adjust ") + names[k], w, h, s,
                     n / s.median / 1000.0, 0, "per-px transfer; pow/log/HSL kinds cost most");
        }
    }
}

// ---------------------------------------------------------------------------
// 6. IO codecs.
// ---------------------------------------------------------------------------

void benchIO() {
    sectionHead("IO codecs [1080p doc: bg + masked child + curves]");
    std::printf("  -- encode+decode round-trip; MB/s over encoded bytes; SLOW = over %.0f ms\n",
                kBudgetMs);
    pittore::io::PsdLayersDoc doc;
    doc.width = 1920;
    doc.height = 1080;
    doc.depth = 8;
    auto solid = [&](int w, int h, std::uint16_t v) {
        pittore::io::PsdLayerFile l;
        l.width = w;
        l.height = h;
        l.rgba.assign(static_cast<std::size_t>(w) * h * 4, v);
        return l;
    };
    {
        auto bg = solid(1920, 1080, 40000);
        bg.name = "bg";
        doc.layers.push_back(std::move(bg));
    }
    {
        auto mid = solid(960, 540, 20000);
        mid.name = "mid";
        mid.left = 100;
        mid.top = 100;
        mid.hasMask = true;
        mid.maskWidth = 960;
        mid.maskHeight = 540;
        mid.mask.assign(960u * 540u, 32768);
        doc.layers.push_back(std::move(mid));
    }
    {
        pittore::io::PsdLayerFile cu;
        cu.width = 1920;
        cu.height = 1080;
        cu.name = "cu";
        cu.isAdjustment = true;
        cu.adjustmentKind = 3;
        cu.adjustmentCurve = {{0.0, 0.0}, {1.0, 1.0}};
        cu.adjustmentCurveR = {{0.0, 0.0}, {1.0, 0.5}};
        doc.layers.push_back(std::move(cu));
    }
    std::string err;
    std::optional<std::vector<std::uint8_t>> bytes;
    {
        auto t0 = hclock::now();
        bytes = pittore::io::psdEncodeLayers(doc, &err);
        Stats s = summarize(std::vector<double>{msBetween(t0, hclock::now())});
        // Re-time for spread (encode is slow; 3 iters).
        s = timeIters([&] { pittore::io::psdEncodeLayers(doc, &err); }, g_opts.quick ? 1 : 3, 0);
        if (bytes)
            rowStats("io", "psdEncodeLayers", 1920, 1080, s, 0.0,
                     bytes->size() / s.median / 1000.0, "RLE + layer records; SLOW = per-px branches");
    }
    if (bytes) {
        Stats s = timeIters([&] { pittore::io::psdDecodeLayers(*bytes, &err); }, g_opts.quick ? 1 : 3, 0);
        rowStats("io", "psdDecodeLayers", 1920, 1080, s, 0.0,
                 bytes->size() / s.median / 1000.0, "parse + RLE expand + curves tables");
    }
    pittore::io::ProjectFileDoc p;
    p.width = 1920;
    p.height = 1080;
    p.dpi = 72;
    p.name = "bench";
    for (int i = 0; i < 3; ++i) {
        pittore::io::ProjectLayerFile l;
        l.name = "l" + std::to_string(i);
        l.width = 1920;
        l.height = 1080;
        l.rgba.assign(1920u * 1080u * 4u, 30000);
        p.layers.push_back(std::move(l));
    }
    std::optional<std::vector<std::uint8_t>> pb;
    {
        Stats s = timeIters([&] { pittore::io::projectEncode(p, &err); }, g_opts.quick ? 1 : 3, 0);
        pb = pittore::io::projectEncode(p, &err);
        if (pb)
            rowStats("io", "projectEncode", 1920, 1080, s, 0.0,
                     pb->size() / s.median / 1000.0, "zip of float tiles; SLOW = deflate on 100MB+");
    }
    if (pb) {
        Stats s = timeIters([&] { pittore::io::projectDecode(*pb, &err); }, g_opts.quick ? 1 : 3, 0);
        rowStats("io", "projectDecode", 1920, 1080, s, 0.0,
                 pb->size() / s.median / 1000.0, "inflate + tile reassembly");
    }
    // XCF single-layer round-trip (1080p RGBA16).
    {
        std::vector<std::uint16_t> rgba(static_cast<std::size_t>(1920) * 1080 * 4, 30000);
        Stats s = timeIters([&] { pittore::io::xcfEncodeRgba(1920, 1080, rgba.data()); }, g_opts.quick ? 1 : 3, 0);
        auto xb = pittore::io::xcfEncodeRgba(1920, 1080, rgba.data());
        if (xb)
            rowStats("io", "xcfEncodeRgba", 1920, 1080, s, 0.0,
                     xb->size() / s.median / 1000.0, "RLE rows; SLOW = per-run branches");
        if (xb) {
            Stats s2 = timeIters([&] { pittore::io::xcfDecode(*xb, &err); }, g_opts.quick ? 1 : 3, 0);
            rowStats("io", "xcfDecode", 1920, 1080, s2, 0.0,
                     xb->size() / s2.median / 1000.0, "RLE expand + colormap paths");
        }
    }
}

// ---------------------------------------------------------------------------
// 7. Engine gaps: brush / tone / mask / LUT / host blur + breakdowns.
// ---------------------------------------------------------------------------

void benchEngine() {
    sectionHead("engine gaps [4K + breakdowns]");
    // Brush dab throughput on 1080p (the per-touch cost of a stroke).
    for (int bei = 0; bei < 2; ++bei) {
        auto backend = bei == 0
                           ? pittore::compute::make_backend(
                                 pittore::compute::BackendType::CPU)
                           : pittore::compute::make_backend(
                                 pittore::compute::BackendType::CUDA);
        if (!backend) continue;
        if (bei == 1 && backend->type() != pittore::compute::BackendType::CUDA)
            continue;  // HIP covered by parity; bench CUDA explicitly
        const std::uint32_t w = 1920, h = 1080;
        auto buf = backend->make_buffer(static_cast<std::size_t>(w) * h *
                                        sizeof(pittore::RGBAf));
        backend->clear(*buf);
        const pittore::RGBAf col{1.0f, 0.2f, 0.1f, 1.0f};
        auto t0 = hclock::now();
        const int N = 200;
        for (int i = 0; i < N; ++i)
            backend->paint_dab(*buf, w, h, 960.0f + (i % 17), 540.0f,
                               24.0f, 0.8f, 1.0f, col);
        const double ms = msBetween(t0, hclock::now()) / N;
        rowUs("engine", std::string("paint_dab r24 ") + backend->name(), ms * 1000.0,
              "bbox only; SLOW = full-frame upload/download fallback");
    }
    // Erase dab kernels on 1080p (host kernels on every backend).
    {
        const std::uint32_t w = 1920, h = 1080;
        pittore::Image img(w, h);
        for (std::size_t i = 0; i < img.pixel_count(); ++i)
            img.data()[i] = {0.5f, 0.4f, 0.3f, 1.0f};
        auto t0 = hclock::now();
        const int N = 200;
        for (int i = 0; i < N; ++i)
            pittore::compute::erase_dab_host(img.data(), w, h, 960.0f + (i % 17),
                                              540.0f, 24.0f, 0.8f, 1.0f, nullptr,
                                              nullptr, nullptr, nullptr);
        rowUs("engine", "erase_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "alpha multiply-down; must match paint kernel cost");
        pittore::compute::AutoTip tip;
        tip.ratio = 0.5f;
        tip.angleDeg = 30.0f;
        tip.hardness = 0.8f;
        t0 = hclock::now();
        for (int i = 0; i < N; ++i)
            pittore::compute::erase_tip_dab_host(img.data(), w, h, 960.0f + (i % 17),
                                                  540.0f, 24.0f, tip, 1.0f, nullptr,
                                                  nullptr, nullptr, nullptr);
        rowUs("engine", "erase_tip_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "rotated ellipse measure per px");
    }
    // Blur / Sharpen dabs on 1080p content (per-touch stroke cost of the
    // Blur and Sharpen tools; CPU shared core, bbox-scoped).
    {
        const std::uint32_t w = 1920, h = 1080;
        pittore::Image img(w, h);
        std::uint64_t salt = 0x9E3779B97F4A7C15ULL;
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                salt = salt * 6364136223846793005ULL + 1442695040888963407ULL;
                const float n = ((salt >> 33) & 1) ? 0.2f : -0.2f;
                img.data()[std::size_t(y) * w + x] = pittore::RGBAf{
                    std::clamp(float(x) / w + n, 0.0f, 1.0f),
                    std::clamp(float(y) / h + n, 0.0f, 1.0f), 0.5f, 1.0f};
            }
        int bbox[4] = {0, 0, 0, 0};
        auto t0 = hclock::now();
        const int N = 50;
        for (int i = 0; i < N; ++i)
            pittore::compute::blur_sharpen_dab_host(
                img.data(), w, h, 960.0f + (i % 17), 540.0f, 24.0f, 0.8f,
                0.5f, false, false, bbox);
        rowUs("engine", "blur_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "bbox blur + mask mix; SLOW = kernel radius or alloc per dab");
        t0 = hclock::now();
        for (int i = 0; i < N; ++i)
            pittore::compute::blur_sharpen_dab_host(
                img.data(), w, h, 960.0f + (i % 17), 540.0f, 64.0f, 0.8f,
                0.5f, false, false, bbox);
        rowUs("engine", "blur_dab r64 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "large-bbox path (GPU fastpath at >=96px in-app)");
        t0 = hclock::now();
        for (int i = 0; i < N; ++i)
            pittore::compute::blur_sharpen_dab_host(
                img.data(), w, h, 960.0f + (i % 17), 540.0f, 24.0f, 0.8f,
                0.5f, true, true, bbox);
        rowUs("engine", "sharpen_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "unsharp mix + Protect Detail gate");
    }
    // Background Eraser dab (discontiguous): host core µs + backend
    // fastpath rows (bbox kernel, bbox-only traffic).
    {
        const std::uint32_t w = 1920, h = 1080;
        pittore::Image img(w, h);
        for (std::size_t i = 0; i < img.pixel_count(); ++i)
            img.data()[i] = {0.9f, 0.9f, 0.9f, 1.0f};
        const pittore::RGBAf sample{0.9f, 0.9f, 0.9f, 1.0f};
        const pittore::RGBAf fg{1.0f, 0.0f, 0.0f, 1.0f};
        int bbox[4] = {0, 0, 0, 0};
        auto t0 = hclock::now();
        const int N = 50;
        for (int i = 0; i < N; ++i)
            pittore::compute::background_erase_dab_host(
                img.data(), w, h, 960.0f + (i % 17), 540.0f, 24.0f, 0.8f,
                1.0f, sample, 0.5f, false, fg, bbox);
        rowUs("engine", "bg_erase_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "tolerance match + rim mix; SLOW = serial fallback");
        for (int bei = 0; bei < 2; ++bei) {
            auto backend = bei == 0
                               ? pittore::compute::make_backend(
                                     pittore::compute::BackendType::CPU)
                               : pittore::compute::make_backend(
                                     pittore::compute::BackendType::CUDA);
            if (!backend) continue;
            if (bei == 1 &&
                backend->type() != pittore::compute::BackendType::CUDA)
                continue;
            auto buf = backend->make_buffer(static_cast<std::size_t>(w) * h *
                                            sizeof(pittore::RGBAf));
            std::memcpy(buf->host(), img.data(), buf->size());
            buf->upload();
            t0 = hclock::now();
            for (int i = 0; i < N; ++i) {
                int bb[4] = {0, 0, 0, 0};
                backend->bg_erase(*buf, w, h, 960.0f + (i % 17), 540.0f,
                                  24.0f, 0.8f, 1.0f, sample, 0.5f, false,
                                  fg, bb);
            }
            rowUs("engine",
                  std::string("bg_erase_dab r24 ") + backend->name(),
                  msBetween(t0, hclock::now()) / N * 1000.0,
                  "bbox kernel; SLOW = full-frame staging fallback");
        }
    }
    // Pattern stamp dab: host core µs + backend fastpath rows.
    {
        const std::uint32_t w = 1920, h = 1080;
        pittore::Image img(w, h);
        for (std::size_t i = 0; i < img.pixel_count(); ++i)
            img.data()[i] = {1.0f, 1.0f, 1.0f, 1.0f};
        const auto tile = pittore::compute::make_pattern_tile(2);
        int bbox[4] = {0, 0, 0, 0};
        auto t0 = hclock::now();
        const int N = 50;
        for (int i = 0; i < N; ++i)
            pittore::compute::pattern_stamp_dab_host(
                img.data(), w, h, 960.0f + (i % 17), 540.0f, 24.0f, 0.8f,
                1.0f, tile, 0.0f, 0.0f, bbox);
        rowUs("engine", "pattern_stamp r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "tile source-over + rim; SLOW = per-dab tile rebuild");
        for (int bei = 0; bei < 2; ++bei) {
            auto backend = bei == 0
                               ? pittore::compute::make_backend(
                                     pittore::compute::BackendType::CPU)
                               : pittore::compute::make_backend(
                                     pittore::compute::BackendType::CUDA);
            if (!backend) continue;
            if (bei == 1 &&
                backend->type() != pittore::compute::BackendType::CUDA)
                continue;
            auto buf = backend->make_buffer(static_cast<std::size_t>(w) * h *
                                            sizeof(pittore::RGBAf));
            auto tileBuf = backend->make_buffer(64u * 64u * sizeof(pittore::RGBAf));
            std::memcpy(buf->host(), img.data(), buf->size());
            std::memcpy(tileBuf->host(), tile.px.data(),
                        64u * 64u * sizeof(pittore::RGBAf));
            buf->upload();
            tileBuf->upload();
            t0 = hclock::now();
            for (int i = 0; i < N; ++i) {
                int bb[4] = {0, 0, 0, 0};
                backend->pattern_stamp(*buf, w, h, 960.0f + (i % 17),
                                       540.0f, 24.0f, 0.8f, 1.0f, *tileBuf,
                                       0.0f, 0.0f, bb);
            }
            rowUs("engine",
                  std::string("pattern_stamp r24 ") + backend->name(),
                  msBetween(t0, hclock::now()) / N * 1000.0,
                  "bbox kernel, 64KB tile; SLOW = full-frame staging");
        }
    }
    // History Brush dab: host core µs + backend fastpath rows.
    {
        const std::uint32_t w = 1920, h = 1080;
        pittore::Image dst(w, h), src(w, h);
        for (std::size_t i = 0; i < dst.pixel_count(); ++i) {
            dst.data()[i] = {1.0f, 0.0f, 0.0f, 1.0f};
            src.data()[i] = {1.0f, 1.0f, 1.0f, 1.0f};
        }
        int bbox[4] = {0, 0, 0, 0};
        auto t0 = hclock::now();
        const int N = 50;
        for (int i = 0; i < N; ++i)
            pittore::compute::history_brush_dab_host(
                dst.data(), src.data(), w, h, 960.0f + (i % 17), 540.0f,
                24.0f, 0.8f, 1.0f, bbox);
        rowUs("engine", "history_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "snapshot source-over; SLOW = per-dab snapshot copy");
        for (int bei = 0; bei < 2; ++bei) {
            auto backend = bei == 0
                               ? pittore::compute::make_backend(
                                     pittore::compute::BackendType::CPU)
                               : pittore::compute::make_backend(
                                     pittore::compute::BackendType::CUDA);
            if (!backend) continue;
            if (bei == 1 &&
                backend->type() != pittore::compute::BackendType::CUDA)
                continue;
            auto buf = backend->make_buffer(static_cast<std::size_t>(w) * h *
                                            sizeof(pittore::RGBAf));
            auto srcBuf = backend->make_buffer(static_cast<std::size_t>(w) * h *
                                                sizeof(pittore::RGBAf));
            std::memcpy(buf->host(), dst.data(), buf->size());
            std::memcpy(srcBuf->host(), src.data(), srcBuf->size());
            buf->upload();
            srcBuf->upload();
            t0 = hclock::now();
            for (int i = 0; i < N; ++i) {
                int bb[4] = {0, 0, 0, 0};
                backend->history_dab(*buf, w, h, 960.0f + (i % 17), 540.0f,
                                     24.0f, 0.8f, 1.0f, *srcBuf, bb);
            }
            rowUs("engine",
                  std::string("history_dab r24 ") + backend->name(),
                  msBetween(t0, hclock::now()) / N * 1000.0,
                  "bbox kernel; SLOW = full-frame staging");
        }
    }
    // Healing Brush: donor translate (once per stroke/press) + heal dab.
    // CPU only (serial donor search, like booleans); no device kernel.
    // Content is a realistic blemish (dark blob on smooth gradient): the
    // global match goes exact and skips per-pixel refinement. Full-frame
    // noise would force refinement everywhere (its worst case).
    // Content is a realistic blemish (dark blob on smooth gradient): the
    // global match goes exact and skips per-pixel refinement. Full-frame
    // noise would force refinement everywhere (its worst case).
    {
        const std::uint32_t w = 1920, h = 1080;
        std::vector<pittore::RGBAf> base(std::size_t(w) * h);
        for (std::uint32_t y = 0; y < h; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                float v = 0.55f + 0.2f * float(x) / w + 0.1f * float(y) / h;
                const float dx = float(x) - 960.0f, dy = float(y) - 540.0f;
                if (dx * dx + dy * dy < 30.0f * 30.0f) v = 0.1f;  // blemish
                base[std::size_t(y) * w + x] = pittore::RGBAf{v, v, v, 1.0f};
            }
        std::vector<pittore::RGBAf> donor(std::size_t(w) * h);
        Stats st = timeIters(
            [&] {
                pittore::compute::translate_heal_donor_host(
                    base.data(), donor.data(), w, h, 24.0f, -12.0f);
            },
            5, 1);
        rowStats("engine", "heal donor translate", w, h, st,
                 std::size_t(w) * h / st.median / 1000.0, 0,
                 "one full-frame pass per stroke/press, amortized over dabs");
        pittore::Image img(w, h);
        std::memcpy(img.data(), base.data(), base.size() * sizeof(pittore::RGBAf));
        int bbox[4] = {0, 0, 0, 0};
        auto t0 = hclock::now();
        const int N = 20;
        for (int i = 0; i < N; ++i)
            pittore::compute::spot_heal_host(
                img.data(), w, h, 960.0f + (i % 17), 540.0f, 24.0f, 0.8f,
                donor.data(),
                pittore::compute::HealType::Proximity, 5, bbox);
        rowUs("engine", "heal_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "single-donor proximity (live brush); ContentAware reserved for Patch");
        // ContentAware refine: the per-pixel PatchMatch path the live brush
        // pays whenever the global match is inexact — coarse level, structure
        // planes, serial rank-flood sweeps, parallel apply. Content is a
        // smooth incommensurate wave (no donor can match it exactly, so
        // refinement always runs; a periodic pattern would match exactly and
        // skip the very code this row exists to measure).
        {
            std::vector<pittore::RGBAf> tex(std::size_t(w) * h);
            for (std::uint32_t y = 0; y < h; ++y)
                for (std::uint32_t x = 0; x < w; ++x) {
                    const float v =
                        0.55f + 0.18f * std::sin(float(x) * 0.017f) *
                                    std::cos(float(y) * 0.013f) +
                        0.12f * std::sin(float(x + y) * 0.007f);
                    const float dx = float(x) - 960.0f, dy = float(y) - 540.0f;
                    const float c = v - 0.30f * (1.0f - std::min(
                        1.0f, (dx * dx + dy * dy) / (24.0f * 24.0f)));
                    tex[std::size_t(y) * w + x] =
                        pittore::RGBAf{c, c, c, 1.0f};
                }
            pittore::Image img2(w, h);
            std::memcpy(img2.data(), tex.data(),
                        tex.size() * sizeof(pittore::RGBAf));
            int bboxOut[4] = {0, 0, 0, 0};
            auto t1 = hclock::now();
            const int M = 10;
            for (int i = 0; i < M; ++i)
                pittore::compute::spot_heal_host(
                    img2.data(), w, h, 960.0f + float(i % 17), 540.0f, 48.0f,
                    0.8f, tex.data(), pittore::compute::HealType::ContentAware,
                    5, bboxOut);
            rowUs("engine", "heal_dab r48 ContentAware",
                  msBetween(t1, hclock::now()) / M * 1000.0,
                  "refine (pyramid+structure) + apply; SLOW = refine cost");
        }
    }
    // Red-eye fix + Art History stamp on 1080p (one-shot / per-dab costs).
    {
        const std::uint32_t w = 1920, h = 1080;
        pittore::Image img(w, h);
        for (std::size_t i = 0; i < img.pixel_count(); ++i)
            img.data()[i] = {0.9f, 0.1f, 0.1f, 1.0f};
        int bbox[4] = {0, 0, 0, 0};
        Stats st = timeIters(
            [&] {
                (void)pittore::compute::redeye_fix_host(
                    img.data(), w, h, 900, 480, 1020, 600, 0.5f, bbox);
            },
            5, 1);
        rowStats("engine", "redeye_fix 120px box", w, h, st,
                 120.0 * 120.0 / st.median / 1000.0, 0,
                 "one-shot pupil fix; SLOW = serial fallback");
        auto t0 = hclock::now();
        const int N = 50;
        for (int i = 0; i < N; ++i)
            pittore::compute::arthistory_dab_host(
                img.data(), img.data(), w, h, 960.0f + (i % 17), 540.0f,
                24.0f, 0.8f, 0.5f, 6.0f, 0.0f, 25.0f, 0.0f, bbox);
        rowUs("engine", "arthistory_dab r24 CPU", msBetween(t0, hclock::now()) / N * 1000.0,
              "styled history stamp; SLOW = stamp larger than the mask");
    }
    // Patch transfer (one-shot, AppState path): blemish + selection on
    // 1080p, donor drop onto clean texture. CPU only by design.
    {
        AppState state;
        DocumentItem* doc =
            state.addDocument(QStringLiteral("patchbench"), QSize(1920, 1080), 72);
        LayerItem& bg = doc->layers[0];
        for (std::uint32_t y = 0; y < bg.pixels->height(); ++y)
            for (std::uint32_t x = 0; x < bg.pixels->width(); ++x) {
                float v = 0.55f + 0.2f * float(x) / 1920.0f;
                const float dx = float(x) - 960.0f, dy = float(y) - 540.0f;
                if (dx * dx + dy * dy < 40.0f * 40.0f) v = 0.1f;
                bg.pixels->data()[std::size_t(y) * 1920 + x] =
                    pittore::RGBAf{v, v, v, 1.0f};
            }
        ++bg.sourceStamp;
        doc->rebuildComposite();
        state.setActiveTool(ToolId::Patch);
        state.setSelection(QRectF(QPointF(900, 480), QPointF(1020, 600)),
                          false);
        Stats st = timeIters(
            [&] {
                state.beginUndoStep();
                const bool ok = state.patchTransfer(QPointF(240, 0), 1, 4,
                                                    5, false);
                if (ok)
                    state.commitUndoStep(QStringLiteral("Patch"),
                                         QStringLiteral("patch"));
                else
                    state.discardUndoStep();
            },
            3, 0);
        rowStats("engine", "patch_transfer 120px", 1920, 1080, st,
                 1920.0 * 1080.0 / st.median / 1000.0, 0,
                 "selection transfer + feather + lowfreq; one-shot, not per-dab");
    }
    // Content-Aware Move (one-shot, AppState path): spot on white,
    // selection moved, hole healed. CPU only by design.
    {
        AppState state;
        DocumentItem* doc = state.addDocument(QStringLiteral("camovebench"),
                                              QSize(1920, 1080), 72);
        LayerItem& cbg = doc->layers[0];
        for (std::uint32_t y = 0; y < cbg.pixels->height(); ++y)
            for (std::uint32_t x = 0; x < cbg.pixels->width(); ++x)
                cbg.pixels->data()[std::size_t(y) * 1920 + x] =
                    (x >= 950 && x < 970 && y >= 530 && y < 550)
                        ? pittore::RGBAf{0, 0, 0, 1}
                        : pittore::RGBAf{1, 1, 1, 1};
        ++cbg.sourceStamp;
        doc->rebuildComposite();
        state.setSelection(QRectF(QPointF(940, 520), QPointF(980, 560)),
                          false);
        Stats st = timeIters(
            [&] {
                state.beginUndoStep();
                const bool ok = state.contentAwareMove(QPointF(240, 0),
                                                       false, 4, 5, false);
                if (ok)
                    state.commitUndoStep(QStringLiteral("CAMove"),
                                         QStringLiteral("camove"));
                else
                    state.discardUndoStep();
            },
            3, 0);
        rowStats("engine", "camove_transfer 40px", 1920, 1080, st,
                 1920.0 * 1080.0 / st.median / 1000.0, 0,
                 "paste + hole heal; one-shot, not per-dab");
    }
    // Content-Aware Tracing (one-shot, AppState path): rect selection to
    // polygon Shape layer. CPU only by design.
    {
        AppState state;
        DocumentItem* doc =
            state.addDocument(QStringLiteral("tracebench"), QSize(1920, 1080), 72);
        state.setSelection(QRectF(QPointF(900, 480), QPointF(1020, 600)),
                          false);
        Stats st = timeIters(
            [&] { (void)state.traceSelection(1, 100); }, 5, 0);
        rowStats("engine", "trace_rect_to_mask", 1920, 1080, st,
                 1920.0 * 1080.0 / st.median / 1000.0, 0,
                 "marquee polygon + mask fill; one-shot (Shape shares it)");
        // Undo the 5 committed trace selections so the doc stays clean.
        for (int i = 0; i < 5; ++i) state.undo();
    }
    const std::uint32_t w = 3840, h = 2160;
    const std::size_t n = static_cast<std::size_t>(w) * h;
    std::vector<pittore::RGBAf> a(n, {0.4f, 0.5f, 0.6f, 1.0f});
    std::vector<pittore::RGBAf> b(n, {0.7f, 0.3f, 0.2f, 1.0f});
    std::vector<pittore::RGBAf> o(n);
    pittore::compute::ToneBlendParams tp;
    Stats st = timeIters([&] { pittore::compute::applyToneBlend(a.data(), b.data(), o.data(), w, h, tp); }, 5, 1);
    rowStats("engine", "applyToneBlend", w, h, st, n / st.median / 1000.0, 0,
             "2x pyramid + 2x upsample + pivot + Oklab grade; profile sub-rows");
    // Tone sub-breakdown: strength 0 early-out vs lowPass extremes.
    {
        pittore::compute::ToneBlendParams off;
        off.strength = 0.0f;
        Stats s = timeIters([&] { pittore::compute::applyToneBlend(a.data(), b.data(), o.data(), w, h, off); }, 5, 0);
        const double mpix0 = s.median < 0.001 ? 0.0 : n / s.median / 1000.0;
        rowStats("engine", "applyToneBlend strength0", w, h, s, mpix0, 0,
                 "early-out path; non-zero = real pyramid cost above");
    }
    pittore::Image m(w, h);
    for (std::size_t i = 0; i < n; ++i) m.data()[i] = {0.5f, 0.5f, 0.5f, 1.0f};
    st = timeIters([&] { (void)finishMaskImage(m, 0.5f, 0.0f); }, 5, 1);
    rowStats("engine", "finishMask density", w, h, st, n / st.median / 1000.0, 0,
             "memcpy + per-px density scale");
    st = timeIters([&] { (void)finishMaskImage(m, 1.0f, 5.0f); }, 3, 1);
    rowStats("engine", "finishMask feather5", w, h, st, n / st.median / 1000.0, 0,
             "gaussBlur dominates; see host blur row");
    // Region re-finish of a feathered mask (the per-event mask-paint cost):
    // must match full finish inside the rect at a fraction of the work.
    {
        auto full = finishMaskImage(m, 1.0f, 5.0f);
        pittore::Image inc = *full;
        // Perturb the raw mask, then patch only the dirty rect.
        pittore::Image raw = m;
        for (std::uint32_t y = 500; y < 620; ++y)
            for (std::uint32_t x = 800; x < 960; ++x)
                raw.data()[static_cast<std::size_t>(y) * w + x] = {0.1f, 0.1f, 0.1f, 1.0f};
        st = timeIters([&] {
            (void)patchFinishedMaskRegion(raw, 1.0f, 5.0f, 800, 500, 960, 620, inc);
        }, 5, 1);
        rowStats("engine", "patchFinish feather5", 160, 120, st,
                 (160.0 * 120.0) / st.median / 1000.0, 0.0,
                 "halo-exact region re-bake; SLOW = full finish per event");
    }
    float lut[768];
    std::vector<std::pair<double, double>> pts = {{0.0, 0.0}, {1.0, 1.0}};
    st = timeIters([&] {
        pittore::buildCurveLUT(pts, lut);
        pittore::buildCurveLUT(pts, lut + 256);
        pittore::buildCurveLUT(pts, lut + 512);
    }, 200, 5);
    rowStats("engine", "buildCurveLUT x3", 0, 0, st, 0, 0, "table build; should be us");
    pittore::Image g(w, h);
    for (std::size_t i = 0; i < n; ++i) g.data()[i] = {0.5f, 0.5f, 0.5f, 1.0f};
    st = timeIters([&] { pittore::filter::detail::gaussBlur(g, 2.0); }, 3, 0);
    rowStats("engine", "gaussBlur host s2", w, h, st, n / st.median / 1000.0, 0,
             "3x boxBlurInto; SLOW = O(w*h) memcpys + scalar taps");
    // Host box/median/shaped breakdowns behind the filter rows.
    {
        pittore::Image img = g.clone(), scratch(w, h);
        st = timeIters([&] { pittore::filter::detail::boxBlurInto(img, scratch, 4); }, 3, 0);
        rowStats("engine", "boxBlurInto r4 host", w, h, st, n / st.median / 1000.0, 0,
                 "sliding O(1)/px; SLOW = tmp Image alloc + double accum");
    }
    {
        // Blit breakdown: host float->premul convert alone (same math as the
        // canvas blit: ordered Bayer dither via dither::quantize).
        QImage qimg(1920, 1080, QImage::Format_ARGB32_Premultiplied);
        pittore::Image img(1920, 1080);
        for (std::size_t i = 0; i < img.pixel_count(); ++i) img.data()[i] = {0.5f, 0.4f, 0.3f, 1.0f};
        st = timeIters([&] {
            const pittore::RGBAf* row = img.data();
            uchar* out = qimg.bits();
            for (std::uint32_t y = 0; y < 1080; ++y)
                for (std::uint32_t x = 0; x < 1920; ++x) {
                    const pittore::RGBAf& s = row[static_cast<std::size_t>(y) * 1920 + x];
                    out[0] = pittore::compute::dither::quantize(s.b * s.a, x, y);
                    out[1] = pittore::compute::dither::quantize(s.g * s.a, x, y);
                    out[2] = pittore::compute::dither::quantize(s.r * s.a, x, y);
                    out[3] = pittore::compute::dither::quantize(s.a, x, y);
                    out += 4;
                }
        }, benchItersStd(), 1);
        rowStats("engine", "blitRGBAfToPremul 1080p", 1920, 1080, st,
                 (1920 * 1080) / st.median / 1000.0, 0, "dither+quantize per px; GPU does this on-device");
    }
}

// ---------------------------------------------------------------------------
// 8. Text layout + raster (fontconfig/HarfBuzz/FreeType).
// ---------------------------------------------------------------------------

void benchText() {
    sectionHead("text layout + raster");
    using namespace pittore::text;
    const std::string fam = defaultFamily();
    if (!hasFamily(fam)) {
        std::printf("  no font family available, skipped\n");
        return;
    }
    TextSpec warm;
    warm.text = "H";
    warm.family = fam;
    warm.size = 48.0f;
    (void)rasterize(warm);  // warm the face cache; see cold row below
    const std::string sentence = "Hamburgefonstiv pack my box";
    std::string lorem;
    for (int i = 0; i < 200; ++i) lorem += sentence + " ";
    struct TC {
        const char* label;
        std::string text;
        float size;
        std::optional<float> wrap;
        bool isRaster;
        const char* hint;
    };
    std::vector<TC> cases = {
        {"layout word 48px", sentence, 48.0f, std::nullopt, false, "shaping + advance (no raster)"},
        {"layout wrap 200px", sentence + " " + sentence, 48.0f, 200.0f, false, "re-shape per wrapped word"},
        {"layout lorem 12px", lorem, 12.0f, 800.0f, false, "paragraph shaping + line break"},
        {"raster word 48px", sentence, 48.0f, std::nullopt, true, "FT load+render per glyph, no glyph cache"},
        {"raster lorem 12px", lorem, 12.0f, 800.0f, true, "glyph-count scaling"},
        {"raster 144px", sentence, 144.0f, std::nullopt, true, "W*H fill + splat scaling"},
    };
    for (const auto& c : cases) {
        TextSpec s;
        s.text = c.text;
        s.family = fam;
        s.size = c.size;
        s.wrapWidth = c.wrap;
        Stats st = timeIters([&] {
            if (c.isRaster) (void)rasterize(s);
            else (void)layoutText(s);
        }, c.isRaster ? benchItersHeavy() : benchItersStd(), 1);
        std::size_t px = 0;
        if (c.isRaster) {
            if (auto r = rasterize(s)) px = r->coverage.size();
        }
        rowStats("text", c.label, 0, 0, st,
                 px > 0 ? px / st.median / 1000.0 : 0.0, 0.0, c.hint);
    }
    // Decorated path (forces colorRgba + bars) and cold face lookup.
    {
        TextSpec s;
        s.text = lorem;
        s.family = fam;
        s.size = 12.0f;
        s.wrapWidth = 800.0f;
        s.underline = 1;
        s.backgroundColor = {1.0f, 1.0f, 0.0f, 1.0f};
        Stats st = timeIters([&] { (void)rasterize(s); }, benchItersHeavy(), 1);
        rowStats("text", "raster decorated", 0, 0, st, 0.0, 0.0,
                 "colorRgba path + underline/background bars");
    }
    {
        Stats st = timeIters([&] {
            TextSpec s;
            s.text = "H";
            s.family = fam + "_missing_xyz";
            s.size = 48.0f;
            (void)rasterize(s);
        }, benchItersLight(), 1);
        rowStats("text", "face miss (cached)", 0, 0, st, 0.0, 0.0,
                 "negative face-cache hit; SLOW = FcFontMatch per call");
    }
    // Caret / hit-test queries over a laid-out paragraph.
    {
        TextSpec s;
        s.text = lorem;
        s.family = fam;
        s.size = 12.0f;
        s.wrapWidth = 800.0f;
        auto lo = layoutText(s);
        if (lo) {
            const int N = 10000;
            auto t0 = hclock::now();
            for (int i = 0; i < N; ++i) (void)caretAt(s, *lo, (i * 37) % (lorem.size() + 1));
            rowUs("text", "caretAt query", msBetween(t0, hclock::now()) / N * 1000.0,
                  "linear char scan; SLOW = needs index");
            t0 = hclock::now();
            for (int i = 0; i < N; ++i)
                (void)hitTest(s, *lo, float((i * 53) % 800), float((i * 29) % 400));
            rowUs("text", "hitTest query", msBetween(t0, hclock::now()) / N * 1000.0,
                  "linear line+char scan; SLOW = needs index");
        }
    }
}

// ---------------------------------------------------------------------------
// 9. Vector path raster + boolean + SVG parse.
// ---------------------------------------------------------------------------

void benchVector() {
    sectionHead("vector path + boolean + svg");
    using namespace pittore::vector;
    // Scanline rasterize at several sizes (the O(H*E) core).
    for (int S : {64, 512, 2048}) {
        const bool lineal = S <= 512;
        const int iters = S >= 2048 ? 3 : (lineal ? benchItersLight() : benchItersStd());
        IRect rect = IRect::fromSize(S, S);
        Path p = PathBuilder().rect(rect).build(0.25f);
        Stats st = timeIters([&] { (void)rasterize(p, rect, FillRule::NonZero); }, iters, 1);
        char lab[48];
        std::snprintf(lab, sizeof(lab), "path rect %dpx", S);
        rowStats("vector", lab, S, S, st, (S * S) / st.median / 1000.0, 0.0,
                 "edge build + per-row sort + spans");
    }
    {
        IRect rect = IRect::fromSize(512, 512);
        Path tri = PathBuilder().polygon(rect, 3).build(0.25f);
        Stats st = timeIters([&] { (void)rasterize(tri, rect, FillRule::NonZero); },
                             benchItersStd(), 1);
        rowStats("vector", "path triangle AA 512px", 512, 512, st,
                 (512.0 * 512.0) / st.median / 1000.0, 0.0, "diagonal edge AA");
        Path ell = PathBuilder().ellipse(rect).build(0.1f);
        st = timeIters([&] { (void)rasterize(ell, rect, FillRule::NonZero); },
                       benchItersStd(), 1);
        rowStats("vector", "path ellipse tol0.1", 512, 512, st,
                 (512.0 * 512.0) / st.median / 1000.0, 0.0, "flatten steps ~ sqrt(len/tol)");
    }
    // Full shape raster: fill + stroke + gradient variants.
    {
        auto rectShape = [](int x0, int y0, int x1, int y1) {
            VectorShape s;
            SubPath sp;
            sp.closed = true;
            sp.anchors = {Anchor::corner(float(x0), float(y0)),
                          Anchor::corner(float(x1), float(y0)),
                          Anchor::corner(float(x1), float(y1)),
                          Anchor::corner(float(x0), float(y1))};
            s.path.subpaths.push_back(sp);
            s.fill = {200, 100, 50, 255};
            return s;
        };
        VectorShape solid = rectShape(10, 10, 502, 502);
        Stats st = timeIters([&] { (void)rasterizeShape(solid, nullptr); },
                             benchItersHeavy(), 1);
        rowStats("vector", "shape solid 512px", 512, 512, st,
                 (512.0 * 512.0) / st.median / 1000.0, 0.0, "flatten + mask + solid pass");
        VectorShape stroked = solid;
        stroked.hasStroke = true;
        stroked.stroke = {20, 20, 20, 255};
        stroked.strokeWidth = 8.0f;
        st = timeIters([&] { (void)rasterizeShape(stroked, nullptr); },
                       benchItersHeavy(), 1);
        rowStats("vector", "shape stroke 512px", 512, 512, st,
                 (512.0 * 512.0) / st.median / 1000.0, 0.0, "adds strokeToPath + 3rd pass");
        GradientFill grad;
        for (int i = 0; i < 5; ++i)
            grad.stops.push_back({i / 4.0f, {std::uint8_t(50 * i), 100, 200, 255}});
        grad.startX = 10; grad.startY = 10; grad.endX = 502; grad.endY = 502;
        st = timeIters([&] { (void)rasterizeShape(solid, &grad); },
                       benchItersHeavy(), 1);
        rowStats("vector", "shape gradient 512px", 512, 512, st,
                 (512.0 * 512.0) / st.median / 1000.0, 0.0, "per-px colorAt over stops");
    }
    // Boolean ops: 4-pt squares + 100-vertex rings (O(ne^2) + O(S*E)).
    {
        BoolRing a = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
        BoolRing b = {{5, 5}, {15, 5}, {15, 15}, {5, 15}};
        const std::vector<BoolRing> subj{a}, clip{b};
        const char* names[] = {"Union", "Inter", "Diff", "Xor"};
        for (int k = 0; k < 4; ++k) {
            Stats st = timeIters([&] { (void)booleanOp(subj, clip, BoolOp(k)); },
                                 benchItersLight(), 5);
            rowStats("vector", std::string("boolean ") + names[k], 0, 0, st, 0.0, 0.0,
                     "segment split + classify + trace");
        }
        BoolRing big1, big2;
        for (int i = 0; i < 100; ++i) {
            const double t = i * 6.283185307179586 / 100;
            big1.emplace_back(50 + 40 * std::cos(t), 50 + 40 * std::sin(t));
            big2.emplace_back(70 + 40 * std::cos(t), 50 + 40 * std::sin(t));
        }
        Stats st = timeIters([&] { (void)booleanOp({big1}, {big2}, BoolOp::Union); },
                             21, 1);
        rowStats("vector", "boolean 100-gon", 0, 0, st, 0.0, 0.0,
                 "O(ne^2) crossing pass dominates");
    }
    // SVG parse: identical (coalesced) vs distinct rects.
    for (int N : {10, 10000}) {
        std::string xml = "<svg width=\"100\" height=\"100\">";
        for (int i = 0; i < N; ++i)
            xml += "<rect x=\"10\" y=\"10\" width=\"80\" height=\"80\"/>";
        xml += "</svg>";
        Stats st = timeIters([&] { (void)parse_svg(xml, "bench"); }, N > 100 ? 3 : 21, 1);
        char lab[48];
        std::snprintf(lab, sizeof(lab), "svg parse %d rects", N);
        rowStats("vector", lab, 0, 0, st, 0.0, 0.0, "char scan + per-number allocs");
    }
    // Anchor point ops on a rect node (per-click cost of the point tools).
    {
        using namespace pittore::ui;
        auto rectNode = [] {
            pittore::vector::ArtNode node;
            auto seg = [](pittore::vector::Segment::Kind kind, float x,
                          float y) {
                pittore::vector::Segment s;
                s.kind = kind;
                s.x = x;
                s.y = y;
                return s;
            };
            using Kind = pittore::vector::Segment::Kind;
            node.segments = {seg(Kind::MoveTo, 10, 10),
                             seg(Kind::LineTo, 50, 10),
                             seg(Kind::LineTo, 50, 30),
                             seg(Kind::LineTo, 10, 30),
                             seg(Kind::Close, 0, 0)};
            return node;
        };
        auto t0 = hclock::now();
        const int N = 2000;
        for (int i = 0; i < N; ++i) {
            auto node = rectNode();
            int seg = -1;
            (void)insertAnchorPoint(node, QPointF(30, 10), 16.0, &seg);
        }
        rowUs("vector", "anchor insert", msBetween(t0, hclock::now()) / N * 1000.0,
              "span walk + split; SLOW = flatten per span");
        t0 = hclock::now();
        for (int i = 0; i < N; ++i) {
            auto node = rectNode();
            (void)deleteAnchorPoint(node, 2);
        }
        rowUs("vector", "anchor delete", msBetween(t0, hclock::now()) / N * 1000.0,
              "erase + bridge straighten");
        t0 = hclock::now();
        for (int i = 0; i < N; ++i) {
            auto node = rectNode();
            convertNodePoint(node, 1, !isSmoothAnchor(node, 1));
        }
        rowUs("vector", "anchor convert", msBetween(t0, hclock::now()) / N * 1000.0,
              "toggle smooth/corner");
    }
}

// ---------------------------------------------------------------------------
// 10. Extra codecs: TIFF + WebP (1080p raw-buffer round-trips).
// ---------------------------------------------------------------------------

void benchCodecsExtra() {
    sectionHead("codecs extra (TIFF/WebP 1080p)");
    const std::uint32_t W = 1920, H = 1080;
    std::vector<std::uint16_t> rgba(static_cast<std::size_t>(W) * H * 4);
    for (std::size_t i = 0; i < rgba.size(); ++i)
        rgba[i] = static_cast<std::uint16_t>(((i * 37) % 256) * 257);  // 8-bit-exact
#ifdef PITTORE_TIFF
    {
        std::vector<std::uint8_t> enc;
        Stats st = timeIters([&] { pittore::io::tiffEncodeRgba16(W, H, 300, rgba.data(), enc); },
                             g_opts.quick ? 1 : 3, 0);
        rowStats("codec", "tiffEncodeRgba16", W, H, st, 0.0,
                 enc.size() / st.median / 1000.0, "LZW scanlines, u16>>8 rows");
        std::uint32_t dw = 0, dh = 0;
        std::vector<std::uint16_t> dec;
        int dpi = 0;
        st = timeIters([&] { pittore::io::tiffDecodeRgba16(enc, dw, dh, dec, dpi); },
                       g_opts.quick ? 1 : 3, 0);
        rowStats("codec", "tiffDecodeRgba16", W, H, st, 0.0,
                 enc.size() / st.median / 1000.0, "RGBAImage read + x257 expand");
        pittore::io::TiffEncodeOptions opt;
        opt.bits = 8;
        opt.grayscale = false;
        opt.withAlpha = true;
        opt.compression = 1;
        std::vector<std::uint8_t> eenc;
        st = timeIters([&] { pittore::io::tiffEncodeExport(W, H, 300, rgba.data(), opt, eenc); },
                       g_opts.quick ? 1 : 3, 0);
        rowStats("codec", "tiffEncodeExport", W, H, st, 0.0,
                 eenc.size() / st.median / 1000.0, "tag setup + 8-bit path, no predictor");
        std::vector<std::uint8_t> band(static_cast<std::size_t>(W) * H * 4);
        std::function<bool(std::uint32_t, const std::uint8_t*)> sink =
            [&](std::uint32_t y, const std::uint8_t* row) {
                std::memcpy(band.data() + static_cast<std::size_t>(y) * W * 4, row,
                            static_cast<std::size_t>(W) * 4);
                return true;
            };
        st = timeIters([&] { pittore::io::tiffDecodeBufferBanded(enc, sink); },
                       g_opts.quick ? 1 : 3, 0);
        rowStats("codec", "tiffBanded", W, H, st, (W * H) / st.median / 1000.0, 0.0,
                 "1 row in flight; proxy path");
    }
#else
    std::printf("  PITTORE_TIFF off, skipped\n");
#endif
#ifdef PITTORE_WEBP
    {
        std::vector<std::uint8_t> wll, wly;
        Stats st = timeIters([&] { pittore::io::webpEncodeRgba16(W, H, true, 90.0f, rgba.data(), wll); },
                             g_opts.quick ? 1 : 3, 0);
        rowStats("codec", "webpEncode lossless", W, H, st, 0.0,
                 wll.size() / st.median / 1000.0, "VP8L, u16>>8 convert");
        st = timeIters([&] { pittore::io::webpEncodeRgba16(W, H, false, 80.0f, rgba.data(), wly); },
                       g_opts.quick ? 1 : 3, 0);
        rowStats("codec", "webpEncode lossy q80", W, H, st, 0.0,
                 wly.size() / st.median / 1000.0, "VP8 lossy + convert");
        std::uint32_t ww = 0, wh = 0;
        std::vector<std::uint16_t> wout;
        st = timeIters([&] { pittore::io::webpDecodeRgba16(wll, ww, wh, wout); },
                       g_opts.quick ? 1 : 3, 0);
        rowStats("codec", "webpDecode", W, H, st, 0.0,
                 wll.size() / st.median / 1000.0, "libwebp internal threads + x257");
    }
#else
    std::printf("  PITTORE_WEBP off, skipped\n");
#endif
}

// ---------------------------------------------------------------------------
// 11. AI no-model CPU stages (preprocess/postprocess without .onnx files).
// ---------------------------------------------------------------------------

void benchAI() {
    sectionHead("ai (no-model CPU stages)");
    std::printf("  onnx_available=%d version=%s\n", pittore::ai::onnx_available(),
                pittore::ai::onnx_version().c_str());
    const std::uint32_t W = 1920, H = 1080;
    const std::size_t n = static_cast<std::size_t>(W) * H;
    std::vector<std::uint8_t> rgba(n * 4);
    for (std::size_t i = 0; i < n; ++i) {
        rgba[i * 4] = static_cast<std::uint8_t>((i * 31) % 251);
        rgba[i * 4 + 1] = static_cast<std::uint8_t>(255 - (i * 31) % 251);
        rgba[i * 4 + 2] = static_cast<std::uint8_t>((i * 17) % 251);
        rgba[i * 4 + 3] = 255;
    }
    std::vector<float> alpha(n, 0.6f);
    for (std::size_t i = 0; i < n; i += 7) alpha[i] = 0.1f;
    Stats st = timeIters([&] {
        std::vector<float> a = alpha;
        pittore::ai::guided_refine_alpha(rgba.data(), W, H, a, 10, 1e-3f);
    }, benchItersHeavy(), 1);
    rowStats("ai", "guided_refine r10", W, H, st, n / st.median / 1000.0, 0.0,
             "~10 float buffers + 6x boxBlurF; post-inference dominant");
    std::vector<std::uint8_t> gray(n, 128);
    st = timeIters([&] {
        std::vector<float> a = alpha;
        (void)pittore::ai::align_alpha_to_reference(a, gray.data(), W, H);
    }, benchItersStd(), 1);
    rowStats("ai", "align_alpha", W, H, st, n / st.median / 1000.0, 0.0,
             "single copy loop; should already be OK");
#ifdef PITTORE_HAS_ONNX
    for (int S : {320, 1024}) {
        std::vector<float> dst(static_cast<std::size_t>(3) * S * S);
        Stats s = timeIters([&] {
            pittore::ai::ai_detail::resizeRgba8ToNchwRgb(rgba.data(), W, H, dst.data(), S, true);
        }, benchItersHeavy(), 1);
        char lab[48];
        std::snprintf(lab, sizeof(lab), "preprocess NCHW %d", S);
        rowStats("ai", lab, W, H, s, (W * H) / s.median / 1000.0, 0.0,
                 "bilinear + ImageNet normalize, scalar, no SIMD");
    }
    {
        std::vector<float> logits(256 * 256, 0.2f);
        Stats s = timeIters([&] {
            (void)pittore::ai::ai_detail::resizeMaskToSize(logits.data(), 256, 256, W, H);
        }, benchItersHeavy(), 1);
        rowStats("ai", "maskUpsample 256->1080p", W, H, s, n / s.median / 1000.0, 0.0,
                 "logits to full-res per decode");
    }
#else
    std::printf("  PITTORE_HAS_ONNX off: preprocess rows skipped\n");
#endif
}

// ---------------------------------------------------------------------------
// 12. Paint stroke (AppState dab + flush per input event at 1080p).
// ---------------------------------------------------------------------------

void benchStroke() {
    sectionHead("paint stroke (dab+flush per event, 1080p x2)");
    std::printf("  -- full stroke pipeline incl. device refresh + region composite\n");
    for (int gi = 0; gi < 2; ++gi) {
        const bool gpu = gi == 1;
        if (gpu && g_opts.cpuOnly) continue;
        AppState state;
        AppSettings s = state.settings();
        s.gpuEnabled = gpu;
        state.applySettings(s);
        DocumentItem* doc = makeDoc(state, 1920, 1080, 2, false);
        if (!doc) continue;
        state.setActiveLayerIndex(1);
        // Warm caches (device slots, thumbnails) so steady-state is measured.
        for (int i = 0; i < 5; ++i) {
            state.paintDab(QPointF(960 + i, 540), 24.0, 0.8, 1.0,
                           QColor(255, 40, 20));
            state.flushPaint();
        }
        const int N = 60;
        auto t0 = hclock::now();
        for (int i = 0; i < N; ++i) {
            state.paintDab(QPointF(960 + (i % 40), 540 + (i % 9)), 24.0, 0.8,
                           1.0, QColor(255, 40, 20));
            state.flushPaint();
        }
        Stats st;
        st.median = st.mean = st.min = st.max = msBetween(t0, hclock::now()) / N;
        char lab[64];
        std::snprintf(lab, sizeof(lab), "stroke event r24 %s",
                      gpu ? "GPU" : "CPU");
        rowStats("stroke", lab, 1920, 1080, st, 0.0, 0.0,
                 "dab + region refresh + region composite + blit");
    }
}

// ---------------------------------------------------------------------------
// Global bottleneck ranking: slowest rows first, with cause hints.
// ---------------------------------------------------------------------------

void bottleneckReport() {
    sectionHead("bottleneck ranking (slowest first, all sections)");
    std::vector<Result> sorted = g_results;
    std::sort(sorted.begin(), sorted.end(),
              [](const Result& x, const Result& y) { return x.median > y.median; });
    std::printf("  -- budget %.0f ms; SLOW rows must reach single-digit ms\n", kBudgetMs);
    int slow = 0;
    for (const auto& r : sorted) if (r.median > kBudgetMs) ++slow;
    std::printf("  -- %d/%zu rows SLOW\n", slow, sorted.size());
    const std::size_t top = std::min<std::size_t>(g_opts.quick ? 15 : 25, sorted.size());
    for (std::size_t i = 0; i < top; ++i) {
        const auto& r = sorted[i];
        std::printf("  #%02zu %-40s %9.3f ms  [%s] %s\n", i + 1,
                    (r.section + " :: " + r.label).substr(0, 40).c_str(), r.median,
                    verdict(r.median), r.hint.c_str());
    }
    // Section totals: where does the wall-clock actually go?
    std::printf("\n  -- section totals (median sum; rebuilds dominate UX):\n");
    std::vector<std::pair<std::string, double>> totals;
    for (const auto& r : sorted) {
        auto it = std::find_if(totals.begin(), totals.end(),
                               [&](const auto& t) { return t.first == r.section; });
        if (it == totals.end()) totals.emplace_back(r.section, r.median);
        else it->second += r.median;
    }
    std::sort(totals.begin(), totals.end(),
              [](const auto& x, const auto& y) { return x.second > y.second; });
    for (const auto& t : totals)
        std::printf("     %-32s %10.1f ms total\n", t.first.c_str(), t.second);
}

void writeCsv() {
    if (g_opts.csvPath.empty()) return;
    FILE* f = std::fopen(g_opts.csvPath.c_str(), "w");
    if (!f) {
        std::printf("cannot write csv %s\n", g_opts.csvPath.c_str());
        return;
    }
    std::fprintf(f, "section,label,w,h,median_ms,mean_ms,min_ms,max_ms,mpix_s,mb_s,verdict,hint\n");
    for (const auto& r : g_results)
        std::fprintf(f, "\"%s\",\"%s\",%d,%d,%.3f,%.3f,%.3f,%.3f,%.1f,%.1f,%s,\"%s\"\n",
                     r.section.c_str(), r.label.c_str(), r.w, r.h, r.median,
                     r.mean, r.min, r.max, r.mpix, r.mbs, verdict(r.median),
                     r.hint.c_str());
    std::fclose(f);
    std::printf("\nwrote %zu rows to %s\n", g_results.size(), g_opts.csvPath.c_str());
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-bench-app-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "cpu") g_opts.cpuOnly = true;
        else if (a == "--quick") g_opts.quick = true;
        else if (a.rfind("--filter=", 0) == 0) g_opts.filter = a.substr(9);
        else if (a.rfind("--csv=", 0) == 0) g_opts.csvPath = a.substr(6);
        else if (a.rfind("--only-section=", 0) == 0) g_opts.onlySection = a.substr(15);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: bench_app [cpu] [--quick] [--filter=substr] [--csv=path] [--only-section=name]\n");
            return 0;
        }
    }
    sysInfo();
    if (sectionActive("rebuilds")) {
        benchRebuilds(false);  // CPU first: stable baseline
        if (!g_opts.cpuOnly) benchRebuilds(true);
    }
    if (sectionActive("kernels")) benchComputeKernels();
    if (sectionActive("filters")) benchFilters();
    if (sectionActive("styles")) benchStyles();
    if (sectionActive("tonal")) benchTonalAdjust();
    if (sectionActive("io")) benchIO();
    if (sectionActive("engine")) benchEngine();
    if (sectionActive("text")) benchText();
    if (sectionActive("vector")) benchVector();
    if (sectionActive("codec")) benchCodecsExtra();
    if (sectionActive("ai")) benchAI();
    if (sectionActive("stroke")) benchStroke();
    bottleneckReport();
    writeCsv();
    return 0;
}
