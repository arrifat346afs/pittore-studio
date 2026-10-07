#include "engine/render/layer_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/core/parallel.h"
#include "engine/render/style/shared/style_plane.h"
#include "engine/render/style/blend/style_blend.h"
#include "engine/render/style/blur/style_blur.h"
#include "engine/render/style/sdf/style_sdf.h"
#include "engine/render/style/fx/style_fx.h"

namespace pittore::render {
namespace detail {


// Three box passes approximating a Gaussian of `sigma`, tuned to stay within a
// fraction of a percent.
void boxRadii(float sigma, int out[3]) {
    const float N = 3.0f;
    const float ideal = std::sqrt(12.0f * sigma * sigma / N + 1.0f);
    float lower = std::floor(ideal);
    if (static_cast<int>(lower) % 2 == 0) lower -= 1.0f;
    lower = std::max(1.0f, lower);
    const float upper = lower + 2.0f;
    int m = static_cast<int>(std::lround(
        (12.0f * sigma * sigma - N * lower * lower - 4.0f * N * lower - 3.0f * N) /
        (-4.0f * lower - 4.0f)));
    m = std::clamp(m, 0, 3);
    const int lo = static_cast<int>((lower - 1.0f) / 2.0f);
    const int hi = static_cast<int>((upper - 1.0f) / 2.0f);
    for (int i = 0; i < 3; ++i) out[i] = i < m ? lo : hi;
}


void boxPass(const float* src, float* dst, int w, int h, int r, bool vertical) {
    const int outer = vertical ? w : h;
    const int inner = vertical ? h : w;
    const std::size_t stride = vertical ? static_cast<std::size_t>(w) : 1;
    const std::size_t step = vertical ? 1 : static_cast<std::size_t>(w);
    const float window = static_cast<float>(r * 2 + 1);
    // Every output line is independent: it reads src and writes only its own
    // slot. parallel_for runs contiguous ranges so each line is produced by
    // exactly one thread with the same operations — bit-identical to the
    // serial sweep, which is what the pixel-exact style paths require.
    auto line = [&](int o) {
        const std::size_t base = static_cast<std::size_t>(o) * step;
        float acc = 0.0f;
        for (int k = 0; k <= r; ++k)
            acc += src[base + static_cast<std::size_t>(std::min(k, inner - 1)) * stride];
        acc += src[base] * static_cast<float>(r);
        for (int i = 0; i < inner; ++i) {
            dst[base + static_cast<std::size_t>(i) * stride] = acc / window;
            const float add =
                src[base + static_cast<std::size_t>(std::min(i + r + 1, inner - 1)) * stride];
            const float sub =
                src[base + static_cast<std::size_t>(std::max(0, i - r)) * stride];
            acc += add - sub;
        }
    };
    pittore::core::parallel_for(static_cast<std::uint32_t>(outer), 64,
                                [&](std::uint32_t o0, std::uint32_t o1) {
                                    for (std::uint32_t o = o0; o < o1; ++o)
                                        line(static_cast<int>(o));
                                });
}


void gaussianBlurPlane(std::vector<float>& a, int w, int h, float radius) {
    if (radius < 0.5f || w <= 0 || h <= 0) return;
    const float sigma = radius / std::sqrt(3.0f);
    int radii[3];
    boxRadii(sigma, radii);
    std::vector<float> tmp(a.size());
    for (int i = 0; i < 3; ++i) {
        boxPass(a.data(), tmp.data(), w, h, radii[i], false);
        boxPass(tmp.data(), a.data(), w, h, radii[i], true);
    }
}

}  // namespace detail
}  // namespace pittore::render
