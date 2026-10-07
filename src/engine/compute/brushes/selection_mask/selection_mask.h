#pragma once
// SelectionMask: destination-space clip + coverage for dab / flood kernels.
// Split from engine/compute/paint.h — include this header directly for the
// mask type, or engine/compute/paint.h for the full brush API.

#include <algorithm>
#include <cstdint>
#include <vector>

namespace pittore::compute {

struct SelectionMask {
    float x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
    bool ellipse = false;
    std::vector<std::uint8_t> pixels;
    int mw = 0, mh = 0;

    float coverage(int x, int y) const {
        const float px = static_cast<float>(x) + 0.5f;
        const float py = static_cast<float>(y) + 0.5f;
        if (px < x0 || py < y0 || px > x1 || py > y1) return 0.0f;
        if (!pixels.empty()) {
            if (mw == 0 || mh == 0) return 0.0f;
            float fx = 0.0f, fy = 0.0f;
            if (mw > 1) fx = (px - x0) * (mw - 1) / std::max(x1 - x0, 1e-6f);
            if (mh > 1) fy = (py - y0) * (mh - 1) / std::max(y1 - y0, 1e-6f);
            const int ix = std::min(std::max(int(fx), 0), mw - 1);
            const int iy = std::min(std::max(int(fy), 0), mh - 1);
            const int ix1 = std::min(ix + 1, mw - 1);
            const int iy1 = std::min(iy + 1, mh - 1);
            const float tx = mw > 1 ? fx - static_cast<float>(ix) : 0.0f;
            const float ty = mh > 1 ? fy - static_cast<float>(iy) : 0.0f;
            const auto samp = [&](int xx, int yy) {
                return pixels[static_cast<std::size_t>(yy) * mw + xx] / 255.0f;
            };
            const float a = samp(ix, iy), b = samp(ix1, iy);
            const float c = samp(ix, iy1), d = samp(ix1, iy1);
            return a * (1.0f - tx) * (1.0f - ty) + b * tx * (1.0f - ty) +
                   c * (1.0f - tx) * ty + d * tx * ty;
        }
        if (!ellipse) return 1.0f;
        const float cx = (x0 + x1) * 0.5f;
        const float cy = (y0 + y1) * 0.5f;
        const float rx = (x1 - x0) * 0.5f;
        const float ry = (y1 - y0) * 0.5f;
        if (rx <= 0.0f || ry <= 0.0f) return 1.0f;
        const float nx = (px - cx) / rx;
        const float ny = (py - cy) / ry;
        return (nx * nx + ny * ny <= 1.0f) ? 1.0f : 0.0f;
    }
};

}  // namespace pittore::compute
