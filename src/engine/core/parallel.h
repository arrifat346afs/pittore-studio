#pragma once
// Tiny row-parallel splitter without an OpenMP dependency: runs fn(y0, y1)
// over contiguous row ranges covering [0, rows). Serial below a threshold
// so small regions never pay thread start-up.
//
// Results are bit-identical to the serial loop: every row is computed by
// exactly one thread with the same operations, so this is safe for the
// pixel-exact CPU reference paths. Callers must ensure rows are independent
// (no cross-row writes) and must not nest parallel_rows on the same pool
// (there is no pool — each call joins before returning).
#include <cstdint>
#include <thread>
#include <vector>

namespace pittore::core {

template <typename Fn>
void parallel_rows(std::uint32_t rows, Fn&& fn) {
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    constexpr std::uint32_t kMinRowsPerThread = 64;
    unsigned int threads = rows / kMinRowsPerThread;
    if (threads < 1) threads = 1;
    if (threads > hw) threads = hw;
    if (threads <= 1) {
        fn(std::uint32_t(0), rows);
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    const std::uint32_t base = rows / threads;
    const std::uint32_t rest = rows % threads;
    std::uint32_t y = 0;
    for (unsigned int t = 0; t + 1 < threads; ++t) {
        const std::uint32_t n = base + (t < rest ? 1 : 0);
        workers.emplace_back([&, y, n] { fn(y, y + n); });
        y += n;
    }
    fn(y, rows);
    for (auto& th : workers) th.join();
}

// Fixed-grain splitter for short but heavy loops: a dab box is tens of rows
// — far below parallel_rows' 64-row threshold — yet holds the whole dab's
// cost, so fn(i0, i1) is instead run over contiguous ranges of at least
// `grain` items covering [0, items). Same bit-identical contract as
// above: disjoint ranges, no nesting, one thread per range.
template <typename Fn>
void parallel_for(std::uint32_t items, std::uint32_t grain, Fn&& fn) {
    if (items == 0) return;
    if (grain < 1) grain = 1;
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    unsigned int threads = items / grain;
    if (threads < 1) threads = 1;
    if (threads > hw) threads = hw;
    if (threads <= 1) {
        fn(std::uint32_t(0), items);
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    const std::uint32_t base = items / threads;
    const std::uint32_t rest = items % threads;
    std::uint32_t i = 0;
    for (unsigned int t = 0; t + 1 < threads; ++t) {
        const std::uint32_t n = base + (t < rest ? 1 : 0);
        workers.emplace_back([&, i, n] { fn(i, i + n); });
        i += n;
    }
    fn(i, items);
    for (auto& th : workers) th.join();
}

// Column-range splitter for separable vertical passes whose running state
// is per-column (e.g. sliding-window box blur V-pass): runs fn(x0, x1)
// over contiguous column ranges covering [0, cols). Each column is
// computed by exactly one thread with the same operations, so results
// are bit-identical to the serial sweep. Same no-nesting rule as above.
template <typename Fn>
void parallel_cols(std::uint32_t cols, Fn&& fn) {
    unsigned int hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    constexpr std::uint32_t kMinColsPerThread = 64;
    unsigned int threads = cols / kMinColsPerThread;
    if (threads < 1) threads = 1;
    if (threads > hw) threads = hw;
    if (threads <= 1) {
        fn(std::uint32_t(0), cols);
        return;
    }
    std::vector<std::thread> workers;
    workers.reserve(threads - 1);
    const std::uint32_t base = cols / threads;
    const std::uint32_t rest = cols % threads;
    std::uint32_t x = 0;
    for (unsigned int t = 0; t + 1 < threads; ++t) {
        const std::uint32_t n = base + (t < rest ? 1 : 0);
        workers.emplace_back([&, x, n] { fn(x, x + n); });
        x += n;
    }
    fn(x, cols);
    for (auto& th : workers) th.join();
}

}  // namespace pittore::core
