#include "engine/io/af_layers.h"
#include "engine/io/af.h"
#include "engine/render/layer_style.h"
#include "engine/text/text_engine.h"
#include "engine/vector/vector_shape.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <zlib.h>
#ifdef PITTORE_AF
#define ZSTD_STATIC_LINKING_ONLY
#include <zstd.h>
#endif
#ifdef PITTORE_WEBP
#include "engine/io/webp.h"
#endif
#ifdef PITTORE_JPEG
#include <csetjmp>
#include <cstdio>
#include <jpeglib.h>
#endif
#include "engine/io/af_layers/container/af_archive.h"
#include "engine/io/af_layers/geom/af_geom.h"
#include "engine/io/af_layers/geom/af_homography.h"
#include "engine/io/af_layers/graph/af_graph.h"
#include "engine/io/af_layers/graph/af_graph_parser.h"
#include "engine/io/af_layers/image/af_image.h"
#include "engine/io/af_layers/filter/af_blur.h"
#include "engine/io/af_layers/filter/af_distort.h"
#include "engine/io/af_layers/filter/af_fx.h"
#include "engine/io/af_layers/filter/af_live.h"
#include "engine/io/af_layers/vector/af_shapes.h"
#include "engine/io/af_layers/walker/af_walker.h"

namespace pittore::io {
namespace af_detail {


bool homographyFromQuads(const std::array<std::pair<double, double>, 4>& src,
                         const std::array<std::pair<double, double>, 4>& dst, Homography& out) {
    double a[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = src[i].first, y = src[i].second;
        const double u = dst[i].first, v = dst[i].second;
        a[2 * i][0] = x;
        a[2 * i][1] = y;
        a[2 * i][2] = 1.0;
        a[2 * i][6] = -u * x;
        a[2 * i][7] = -u * y;
        a[2 * i][8] = u;
        a[2 * i + 1][3] = x;
        a[2 * i + 1][4] = y;
        a[2 * i + 1][5] = 1.0;
        a[2 * i + 1][6] = -v * x;
        a[2 * i + 1][7] = -v * y;
        a[2 * i + 1][8] = v;
    }
    for (int col = 0; col < 8; ++col) {
        int pivot = col;
        for (int row = col + 1; row < 8; ++row)
            if (std::abs(a[row][col]) > std::abs(a[pivot][col])) pivot = row;
        if (std::abs(a[pivot][col]) < 1e-12) return false;
        if (pivot != col)
            for (int k = 0; k < 9; ++k) std::swap(a[col][k], a[pivot][k]);
        const double d = a[col][col];
        for (int k = 0; k < 9; ++k) a[col][k] /= d;
        double piv[9];
        for (int k = 0; k < 9; ++k) piv[k] = a[col][k];
        for (int row = 0; row < 8; ++row) {
            if (row == col) continue;
            const double f = a[row][col];
            if (f == 0.0) continue;
            for (int k = col; k < 9; ++k) a[row][k] -= f * piv[k];
        }
    }
    for (int i = 0; i < 8; ++i) {
        out.h[i] = a[i][8];
        if (!std::isfinite(out.h[i])) return false;
    }
    out.h[8] = 1.0;
    return true;
}


std::optional<std::pair<double, double>> homographyApply(const Homography& H, double x, double y) {
    const double* h = H.h;
    const double w = h[6] * x + h[7] * y + h[8];
    if (std::abs(w) < 1e-12) return std::nullopt;
    const double px = (h[0] * x + h[1] * y + h[2]) / w;
    const double py = (h[3] * x + h[4] * y + h[5]) / w;
    if (!std::isfinite(px) || !std::isfinite(py)) return std::nullopt;
    return std::make_pair(px, py);
}


bool homographyInvert(const Homography& H, Homography& out) {
    const double* h = H.h;
    double inv[9] = {
        h[4] * h[8] - h[5] * h[7], h[2] * h[7] - h[1] * h[8], h[1] * h[5] - h[2] * h[4],
        h[5] * h[6] - h[3] * h[8], h[0] * h[8] - h[2] * h[6], h[2] * h[3] - h[0] * h[5],
        h[3] * h[7] - h[4] * h[6], h[1] * h[6] - h[0] * h[7], h[0] * h[4] - h[1] * h[3],
    };
    if (std::abs(inv[8]) < 1e-12) return false;
    for (double v : inv)
        if (!std::isfinite(v)) return false;
    const double s = inv[8];
    for (double& v : inv) v /= s;
    for (int i = 0; i < 9; ++i) out.h[i] = inv[i];
    return true;
}


// self applied after other (the matrix product self * other).
Homography homographyCompose(const Homography& self, const Homography& other) {
    Homography out{};
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c)
            out.h[r * 3 + c] = self.h[r * 3] * other.h[c] +
                               self.h[r * 3 + 1] * other.h[3 + c] +
                               self.h[r * 3 + 2] * other.h[6 + c];
    if (std::abs(out.h[8]) > 1e-12) {
        const double s = out.h[8];
        for (double& v : out.h) v /= s;
    }
    return out;
}


bool homographyIsIdentity(const Homography& H) {
    static const double I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    for (int i = 0; i < 9; ++i)
        if (std::abs(H.h[i] - I[i]) >= 1e-9) return false;
    return true;
}


// Resample a bitmap through a projective map in its own pixel space, returning
// the integer offset of the result within that space.
std::optional<Warped> perspectiveResample(const Bitmap& img, const Homography& h) {
    Homography inv;
    if (!homographyInvert(h, inv)) return std::nullopt;
    const double sw = img.w, sh = img.h;
    const double corners[4][2] = {{0.0, 0.0}, {sw, 0.0}, {0.0, sh}, {sw, sh}};
    double lox = std::numeric_limits<double>::infinity();
    double loy = std::numeric_limits<double>::infinity();
    double hix = -std::numeric_limits<double>::infinity();
    double hiy = -std::numeric_limits<double>::infinity();
    for (const auto& c : corners) {
        const auto p = homographyApply(h, c[0], c[1]);
        if (!p) return std::nullopt;
        lox = std::min(lox, p->first);
        loy = std::min(loy, p->second);
        hix = std::max(hix, p->first);
        hiy = std::max(hiy, p->second);
    }
    if (std::max({std::abs(lox), std::abs(loy), std::abs(hix), std::abs(hiy)}) > double(1 << 24))
        return std::nullopt;
    IntRect rect{static_cast<int>(std::floor(lox)), static_cast<int>(std::floor(loy)),
                 static_cast<int>(std::ceil(hix)), static_cast<int>(std::ceil(hiy))};
    if (rect.empty()) return std::nullopt;
    const std::size_t dw = rect.width(), dh = rect.height();
    if (dw * dh > kMaxPixels) return std::nullopt;
    const std::int64_t iw = img.w, ih = img.h;
    auto fetch = [&](std::int64_t x, std::int64_t y) -> std::array<float, 4> {
        if (x < 0 || y < 0 || x >= iw || y >= ih) return {0.0f, 0.0f, 0.0f, 0.0f};
        const std::size_t at = (static_cast<std::size_t>(y) * iw + static_cast<std::size_t>(x)) * 4;
        const float a = img.px[at + 3];
        return {img.px[at] * a, img.px[at + 1] * a, img.px[at + 2] * a, a};
    };
    Warped out;
    out.ox = rect.x0;
    out.oy = rect.y0;
    out.img.w = static_cast<std::uint32_t>(dw);
    out.img.h = static_cast<std::uint32_t>(dh);
    out.img.px.assign(dw * dh * 4, 0);
    const double* m = inv.h;
    for (std::size_t y = 0; y < dh; ++y) {
        const double py = rect.y0 + static_cast<double>(y) + 0.5;
        const double px0 = rect.x0 + 0.5;
        double nx = m[0] * px0 + m[1] * py + m[2];
        double ny = m[3] * px0 + m[4] * py + m[5];
        double nw = m[6] * px0 + m[7] * py + m[8];
        for (std::size_t x = 0; x < dw; ++x) {
            const double cx = nx, cy = ny, cw = nw;
            nx += m[0];
            ny += m[3];
            nw += m[6];
            if (std::abs(cw) < 1e-12) continue;
            const double sx = cx / cw, sy = cy / cw;
            const double fx = sx - 0.5, fy = sy - 0.5;
            if (!(fx >= -1.0 && fy >= -1.0 && fx <= sw && fy <= sh)) continue;
            const std::int64_t tx = static_cast<std::int64_t>(fx);
            const std::int64_t ty = static_cast<std::int64_t>(fy);
            const std::int64_t x0 = tx - (static_cast<double>(tx) > fx ? 1 : 0);
            const std::int64_t y0 = ty - (static_cast<double>(ty) > fy ? 1 : 0);
            const float wx = static_cast<float>(fx - static_cast<double>(x0));
            const float wy = static_cast<float>(fy - static_cast<double>(y0));
            const float wts[4] = {(1.0f - wx) * (1.0f - wy), wx * (1.0f - wy),
                                  (1.0f - wx) * wy, wx * wy};
            const std::int64_t dxs[4] = {0, 1, 0, 1};
            const std::int64_t dys[4] = {0, 0, 1, 1};
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (int k = 0; k < 4; ++k) {
                const auto p = fetch(x0 + dxs[k], y0 + dys[k]);
                for (int c = 0; c < 4; ++c) acc[c] += p[c] * wts[k];
            }
            const std::size_t at = (y * dw + x) * 4;
            if (acc[3] > 1e-6f) {
                const float un = 1.0f / acc[3];
                for (int c = 0; c < 3; ++c)
                    out.img.px[at + c] = byteClamp(static_cast<double>(acc[c] * un) + 0.5);
                out.img.px[at + 3] = byteClamp(static_cast<double>(acc[3]) + 0.5);
            }
        }
    }
    return out;
}

}  // namespace af_detail
}  // namespace pittore::io
