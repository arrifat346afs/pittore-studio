// Two-pass box blur. Clamped edges. Rows split on pool.
#include "engine/vector/svg/fe_blur.h"

#include "engine/vector/svg/thread_pool.h"

namespace pittore::svg {

FeImg feBlurBox(const FeImg& src, int radius) {
    FeImg tmp = src;
    FeImg out = src;
    if (src.w <= 0 || src.h <= 0 || radius <= 0) {
        return out;
    }
    const int w = src.w, h = src.h;
    const int win = radius * 2 + 1;
    auto pool = getPool();
    pool->dispatch_threshold(
        h, w * h > 2048, [&](int y, int) {
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    double acc = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        int xx = x + k;
                        if (xx < 0) {
                            xx = 0;
                        }
                        if (xx >= w) {
                            xx = w - 1;
                        }
                        acc += src.px[(size_t)((y * w + xx) * 4 + c)];
                    }
                    tmp.px[(size_t)((y * w + x) * 4 + c)] = (float)(acc / win);
                }
            }
        });
    pool->dispatch_threshold(
        h, w * h > 2048, [&](int y, int) {
            for (int x = 0; x < w; ++x) {
                for (int c = 0; c < 4; ++c) {
                    double acc = 0;
                    for (int k = -radius; k <= radius; ++k) {
                        int yy = y + k;
                        if (yy < 0) {
                            yy = 0;
                        }
                        if (yy >= h) {
                            yy = h - 1;
                        }
                        acc += tmp.px[(size_t)((yy * w + x) * 4 + c)];
                    }
                    out.px[(size_t)((y * w + x) * 4 + c)] = (float)(acc / win);
                }
            }
        });
    return out;
}

}  // namespace pittore::svg
