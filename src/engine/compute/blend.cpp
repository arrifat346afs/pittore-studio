// Host-side full-buffer blend driver. The GPU full-buffer kernel derives
// document coordinates for Dissolve the exact same way (x = i % w, y = i / w),
// which is what keeps CPU≡GPU bit-for-bit at float tolerance.
#include "engine/compute/blend.h"

#include "engine/core/parallel.h"
#include "engine/core/pixel.h"

namespace pittore::compute {
namespace blend {

// Composite `src` over `dst` for `n` pixels of a `w`-wide row-major image,
// blending in place. `mode` is chosen once; Dissolve needs per-pixel document
// coordinates, everything else is coordinate-independent.
// Row-parallel: rows are disjoint, coordinates derive per-pixel, so output is
// bit-identical to the serial loop (and to the GPU kernel).
void composite_buffer(RGBAf* dst, const RGBAf* src, std::size_t n,
                      BlendMode mode, std::size_t w) {
    if (n == 0 || w == 0) return;
    const std::uint32_t rows = static_cast<std::uint32_t>(n / w);
    const std::size_t tail = n % w;
    pittore::core::parallel_rows(rows, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            const std::size_t base = static_cast<std::size_t>(y) * w;
            for (std::size_t x = 0; x < w; ++x) {
                const std::size_t i = base + x;
                const RGBAf& s = src[i];
                RGBAf& d = dst[i];
                pixel(mode, s.r, s.g, s.b, s.a, d.r, d.g, d.b, d.a,
                      static_cast<int>(x), static_cast<int>(y),
                      d.r, d.g, d.b, d.a);
            }
        }
    });
    for (std::size_t i = n - tail; i < n; ++i) {
        const RGBAf& s = src[i];
        RGBAf& d = dst[i];
        pixel(mode, s.r, s.g, s.b, s.a, d.r, d.g, d.b, d.a,
              static_cast<int>(i % w), static_cast<int>(i / w),
              d.r, d.g, d.b, d.a);
    }
}

}  // namespace blend
}  // namespace pittore::compute