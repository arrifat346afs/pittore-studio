#include "engine/render/layer_style.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "engine/render/style/shared/style_plane.h"
#include "engine/render/style/blend/style_blend.h"
#include "engine/render/style/blur/style_blur.h"
#include "engine/render/style/sdf/style_sdf.h"
#include "engine/render/style/fx/style_fx.h"

namespace pittore::render {
namespace detail {


// Felzenszwalb's exact Euclidean distance transform: the squared distance from
// each pixel to the nearest feature, in one pass over columns and one over
// rows. The brute-force definition computes the same quantity, so this matches
// without the O(pixels * radius^2) search.
void dt1d(const std::vector<float>& f, std::vector<float>& d, std::vector<int>& v,
            std::vector<float>& z) {
    const int n = static_cast<int>(f.size());
    d.resize(static_cast<std::size_t>(n));
    if (n == 0) return;
    int k = 0;
    v[0] = 0;
    z[0] = -1e20f;
    z[1] = 1e20f;
    for (int q = 1; q < n; ++q) {
        float s = ((f[q] + float(q) * q) - (f[v[k]] + float(v[k]) * v[k])) /
                  (2.0f * q - 2.0f * v[k]);
        while (s <= z[k]) {
            --k;
            s = ((f[q] + float(q) * q) - (f[v[k]] + float(v[k]) * v[k])) /
                (2.0f * q - 2.0f * v[k]);
        }
        ++k;
        v[k] = q;
        z[k] = s;
        z[k + 1] = 1e20f;
    }
    k = 0;
    for (int q = 0; q < n; ++q) {
        while (z[k + 1] < float(q)) ++k;
        d[static_cast<std::size_t>(q)] =
            float(q - v[k]) * float(q - v[k]) + f[v[k]];
    }
}


std::vector<float> edt(const std::vector<std::uint8_t>& feature, int w, int h) {
    const float INF = 1e12f;
    std::vector<float> f(std::size_t(w) * h);
    for (std::size_t i = 0; i < f.size(); ++i) f[i] = feature[i] ? 0.0f : INF;
    std::vector<float> d(f.size());
    std::vector<int> v(std::max(w, h) + 1);
    std::vector<float> z(std::max(w, h) + 2);
    std::vector<float> line, out;
    for (int x = 0; x < w; ++x) {
        line.resize(h);
        for (int y = 0; y < h; ++y) line[y] = f[std::size_t(y) * w + x];
        dt1d(line, out, v, z);
        for (int y = 0; y < h; ++y) d[std::size_t(y) * w + x] = out[y];
    }
    for (int y = 0; y < h; ++y) {
        line.assign(d.begin() + std::size_t(y) * w, d.begin() + std::size_t(y) * w + w);
        dt1d(line, out, v, z);
        for (int x = 0; x < w; ++x) d[std::size_t(y) * w + x] = out[x];
    }
    for (float& v2 : d) v2 = std::sqrt(v2);
    return d;
}


// Signed distance to the shape's edge in pixels, negative inside, clamped to
// `limit`; the edge lies half a pixel before the nearest other-class pixel.
std::vector<float> signedDistance(const std::vector<float>& alpha, int w, int h, float limit) {
    std::vector<std::uint8_t> inside(alpha.size()), outside(alpha.size());
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        inside[i] = alpha[i] >= 0.5f ? 1 : 0;
        outside[i] = inside[i] ? 0 : 1;
    }
    const std::vector<float> dOut = edt(inside, w, h);
    const std::vector<float> dIn = edt(outside, w, h);
    std::vector<float> out(alpha.size());
    for (std::size_t i = 0; i < out.size(); ++i) {
        const float d = inside[i] ? dIn[i] : dOut[i];
        const float best = std::max(std::min(d, limit) - 0.5f, 0.0f);
        out[i] = inside[i] ? -best : best;
    }
    return out;
}

}  // namespace detail
}  // namespace pittore::render
