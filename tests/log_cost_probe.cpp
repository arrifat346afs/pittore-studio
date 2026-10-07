// log_cost_probe.cpp — measure the fixed overhead of one log_info() write
// (stderr + routed file). The workspace may sit on slow removable storage, so
// this compares that mount against tmpfs to see whether file writes dominate
// per-op timings. Throwaway diagnostic.
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "engine/core/log.h"

int main(int argc, char** argv) {
    const char* dir = argc > 1 ? argv[1] : "/tmp";
    ::pittore::core::log::set_log_dir(dir);
    std::printf("log dir: %s\n", dir);

    const int n = 2000;
    // Warm (open handles, maybe first write on slow FS).
    for (int i = 0; i < 100; ++i)
        ::pittore::core::log::log_info("[layer] warm i=%d", i);

    auto a = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i)
        ::pittore::core::log::log_info("[layer] toggle index=%d name=\"polygon\" visible=%d ms=%.2f",
                                        i % 8, i % 2, 1.5);
    auto b = std::chrono::steady_clock::now();
    double per = std::chrono::duration<double, std::milli>(b - a).count() / n;
    std::printf("log_info x%d: %.3f ms total, %.4f ms/line on %s\n", n, per * n, per, dir);

    // Cross-check: same volume without our logging (plain fwrite to a file).
    auto* f = std::fopen((std::string(dir) + "/_lc_probe.txt").c_str(), "a");
    a = std::chrono::steady_clock::now();
    for (int i = 0; i < n; ++i) std::fprintf(f, "[layer] toggle index=%d ms=1.50\n", i % 8);
    std::fclose(f);
    b = std::chrono::steady_clock::now();
    per = std::chrono::duration<double, std::milli>(b - a).count() / n;
    std::printf("plain fwrite x%d: %.3f ms total, %.4f ms/line\n", n, per * n, per);
    return 0;
}