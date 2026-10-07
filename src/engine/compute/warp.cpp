#include "engine/compute/warp.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <tuple>
#include <utility>

#include "engine/core/parallel.h"

namespace pittore::compute {

namespace {

// Clamp a region to a rectangle with exclusive right/bottom.
inline void clamp_region(int& x0, int& y0, int& x1, int& y1, int left, int top,
                         int right, int bottom) {
    x0 = std::max(x0, left);
    y0 = std::max(y0, top);
    x1 = std::min(x1, right);
    y1 = std::min(y1, bottom);
}

// Bilinear displacement from a flat interleaved offset array. Shared by the
// full-mesh and subgrid samplers so both express the identical float math.
inline void sample_offsets(const float* offsets, std::uint32_t cols,
                           std::uint32_t rows, int left, int top, float x,
                           float y, float& dx, float& dy) {
    dx = dy = 0.0f;
    if (cols < 2 || rows < 2) return;
    const float fx = std::clamp((x - left) / kWarpCell, 0.0f,
                                static_cast<float>(cols - 1));
    const float fy = std::clamp((y - top) / kWarpCell, 0.0f,
                                static_cast<float>(rows - 1));
    const auto c0 = static_cast<unsigned>(fx);
    const auto r0 = static_cast<unsigned>(fy);
    const auto c1 = std::min(c0 + 1, cols - 1);
    const auto r1 = std::min(r0 + 1, rows - 1);
    const float tx = fx - c0;
    const float ty = fy - r0;
    const auto at = [offsets, cols](unsigned c, unsigned r) {
        const std::size_t i = (static_cast<std::size_t>(r) * cols + c) * 2;
        return std::pair<float, float>{offsets[i], offsets[i + 1]};
    };
    const auto [a, b, cc, d] =
        std::tuple{at(c0, r0), at(c1, r0), at(c0, r1), at(c1, r1)};
    const float topx = a.first + (b.first - a.first) * tx;
    const float topy = a.second + (b.second - a.second) * tx;
    const float botx = cc.first + (d.first - cc.first) * tx;
    const float boty = cc.second + (d.second - cc.second) * tx;
    dx = topx + (botx - topx) * ty;
    dy = topy + (boty - topy) * ty;
}

}  // namespace

bool WarpMesh::identity() const {
    for (float v : offsets)
        if (std::abs(v) > 1e-4f) return false;
    return true;
}

WarpMesh make_warp_mesh(std::uint32_t w, std::uint32_t h) {
    WarpMesh m;
    m.left = 0;
    m.top = 0;
    m.right = static_cast<int>(w);
    m.bottom = static_cast<int>(h);
    m.cols = static_cast<std::uint32_t>(std::ceil(w / kWarpCell)) + 1;
    m.rows = static_cast<std::uint32_t>(std::ceil(h / kWarpCell)) + 1;
    m.offsets.assign(static_cast<std::size_t>(m.cols) * m.rows * 2, 0.0f);
    return m;
}

void warp_mesh_sample(const WarpMesh& m, float x, float y, float& dx,
                      float& dy) {
    sample_offsets(m.offsets.data(), m.cols, m.rows, m.left, m.top, x, y, dx,
                   dy);
}

WarpSubgrid warp_subgrid(const WarpMesh& m, int x0, int y0, int x1, int y1) {
    WarpSubgrid g;
    if (m.cols < 2 || m.rows < 2) return g;
    clamp_region(x0, y0, x1, y1, m.left, m.top, m.right, m.bottom);
    if (x0 >= x1 || y0 >= y1) return g;

    // Every result pixel samples the four vertices around its centre, so the
    // slice spans the vertices under the region's first and last columns/rows,
    // plus one. Integer column k sits at x = left + k*kWarpCell, so the +1
    // vertex of the last pixel is (last - 0.5 - left)/cell + 1.
    const double cell = kWarpCell;
    const long long cmax = static_cast<long long>(m.cols) - 1;
    const long long rmax = static_cast<long long>(m.rows) - 1;
    const auto floor_ll = [](double v) {
        return static_cast<long long>(std::floor(v));
    };
    const long long c0 = std::clamp(
        floor_ll((static_cast<double>(x0) + 0.5 - m.left) / cell), 0LL, cmax);
    const long long c1 = std::clamp(
        floor_ll((static_cast<double>(x1) - 0.5 - m.left) / cell) + 1, 0LL,
        cmax);
    const long long r0 = std::clamp(
        floor_ll((static_cast<double>(y0) + 0.5 - m.top) / cell), 0LL, rmax);
    const long long r1 = std::clamp(
        floor_ll((static_cast<double>(y1) - 0.5 - m.top) / cell) + 1, 0LL,
        rmax);
    if (c0 > c1 || r0 > r1) return g;

    g.left = m.left + static_cast<int>(static_cast<double>(c0) * kWarpCell);
    g.top = m.top + static_cast<int>(static_cast<double>(r0) * kWarpCell);
    g.cols = static_cast<std::uint32_t>(c1 - c0 + 1);
    g.rows = static_cast<std::uint32_t>(r1 - r0 + 1);
    g.offsets.resize(static_cast<std::size_t>(g.cols) * g.rows * 2);
    for (std::uint32_t r = 0; r < g.rows; ++r) {
        for (std::uint32_t c = 0; c < g.cols; ++c) {
            const std::size_t s =
                (static_cast<std::size_t>(r0 + r) * m.cols + (c0 + c)) * 2;
            const std::size_t d =
                (static_cast<std::size_t>(r) * g.cols + c) * 2;
            g.offsets[d] = m.offsets[s];
            g.offsets[d + 1] = m.offsets[s + 1];
        }
    }
    return g;
}

void warp_subgrid_sample(const WarpSubgrid& g, float x, float y, float& dx,
                         float& dy) {
    sample_offsets(g.offsets.data(), g.cols, g.rows, g.left, g.top, x, y, dx,
                   dy);
}

void warp_dab_rect(const WarpMesh& m, float cx, float cy, float radius,
                   int& x0, int& y0, int& x1, int& y1) {
    const float reach = radius + kWarpCell;
    x0 = static_cast<int>(std::floor(cx - reach));
    y0 = static_cast<int>(std::floor(cy - reach));
    x1 = static_cast<int>(std::ceil(cx + reach)) + 1;
    y1 = static_cast<int>(std::ceil(cy + reach)) + 1;
    clamp_region(x0, y0, x1, y1, m.left, m.top, m.right, m.bottom);
    if (x0 >= x1 || y0 >= y1) x0 = x1 = y0 = y1 = 0;
}

void warp_mesh_relax(WarpMesh& m, float cx, float cy, float radius,
                     float amount) {
    warp_mesh_for_each_near(m, cx, cy, radius,
                            [amount](float& dx, float& dy, float w, float,
                                     float) {
        const float k = 1.0f - std::clamp(w * amount, 0.0f, 1.0f);
        dx *= k;
        dy *= k;
    });
}

void warp_mesh_smooth(WarpMesh& m, float cx, float cy, float radius,
                      float amount) {
    if (radius <= 0.0f || amount <= 0.0f || m.cols == 0 || m.rows == 0) return;
    struct Tap {
        std::size_t i;
        float w;
    };
    std::vector<Tap> taps;
    warp_mesh_for_each_near(m, cx, cy, radius,
                            [&](float&, float&, float w, float rx,
                                float ry) {
                                const int c = static_cast<int>(std::round(
                                    (rx + cx - m.left) / kWarpCell));
                                const int r = static_cast<int>(std::round(
                                    (ry + cy - m.top) / kWarpCell));
                                if (c < 0 || r < 0 ||
                                    c >= static_cast<int>(m.cols) ||
                                    r >= static_cast<int>(m.rows))
                                    return;
                                taps.push_back(Tap{(static_cast<std::size_t>(r) *
                                                    m.cols +
                                                    static_cast<std::size_t>(c)) *
                                                       2,
                                                   w});
                            });
    if (taps.empty()) return;
    double ax = 0.0, ay = 0.0, sw = 0.0;
    for (const Tap& t : taps) {
        ax += m.offsets[t.i] * t.w;
        ay += m.offsets[t.i + 1] * t.w;
        sw += t.w;
    }
    if (sw <= 0.0) return;
    ax /= sw;
    ay /= sw;
    for (const Tap& t : taps) {
        const float k = std::clamp(t.w * amount, 0.0f, 1.0f);
        m.offsets[t.i] += static_cast<float>(ax - m.offsets[t.i]) * k;
        m.offsets[t.i + 1] += static_cast<float>(ay - m.offsets[t.i + 1]) * k;
    }
}

void warp_mesh_clone(WarpMesh& m, float sx, float sy, float dx, float dy,
                     float radius, float strength) {
    if (radius <= 0.0f || strength <= 0.0f || m.cols == 0 || m.rows == 0)
        return;
    const std::vector<float> snap = m.offsets;
    const auto sample = [&snap, &m](float x, float y) {
        float ox = 0.0f, oy = 0.0f;
        const float fx =
            std::clamp((x - m.left) / kWarpCell, 0.0f,
                       static_cast<float>(m.cols - 1));
        const float fy =
            std::clamp((y - m.top) / kWarpCell, 0.0f,
                       static_cast<float>(m.rows - 1));
        const auto c0 = static_cast<unsigned>(fx);
        const auto r0 = static_cast<unsigned>(fy);
        const auto c1 = std::min(c0 + 1, m.cols - 1);
        const auto r1 = std::min(r0 + 1, m.rows - 1);
        const float tx = fx - c0;
        const float ty = fy - r0;
        const auto at = [&snap, &m](unsigned c, unsigned r) {
            const std::size_t i =
                (static_cast<std::size_t>(r) * m.cols + c) * 2;
            return std::pair<float, float>{snap[i], snap[i + 1]};
        };
        const auto a = at(c0, r0);
        const auto b = at(c1, r0);
        const auto c = at(c0, r1);
        const auto d = at(c1, r1);
        ox = a.first + (b.first - a.first) * tx +
             ((c.first + (d.first - c.first) * tx) -
              (a.first + (b.first - a.first) * tx)) *
                 ty;
        oy = a.second + (b.second - a.second) * tx +
             ((c.second + (d.second - c.second) * tx) -
              (a.second + (b.second - a.second) * tx)) *
                 ty;
        return std::pair<float, float>{ox, oy};
    };
    warp_mesh_for_each_near(m, dx, dy, radius,
                            [&](float& ox, float& oy, float w, float rx,
                                float ry) {
                                const auto s = sample(sx + rx, sy + ry);
                                const float k =
                                    std::clamp(w * strength, 0.0f, 1.0f);
                                ox += (s.first - ox) * k;
                                oy += (s.second - oy) * k;
                            });
}

bool warp_mesh_write(const std::string& path, const WarpMesh& m) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    const char magic[6] = {'I', 'F', 'L', 'Q', '0', '2'};
    out.write(magic, 6);
    const std::int32_t dims[4] = {m.left, m.top, m.right, m.bottom};
    out.write(reinterpret_cast<const char*>(dims), sizeof(dims));
    const std::uint32_t shape[2] = {m.cols, m.rows};
    out.write(reinterpret_cast<const char*>(shape), sizeof(shape));
    out.write(reinterpret_cast<const char*>(m.offsets.data()),
              static_cast<std::streamsize>(m.offsets.size() * sizeof(float)));
    return static_cast<bool>(out);
}

bool warp_mesh_read(const std::string& path, WarpMesh& m) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    char magic[6] = {};
    in.read(magic, 6);
    if (!in || magic[0] != 'I' || magic[1] != 'F' || magic[2] != 'L' ||
        magic[3] != 'Q' || magic[4] != '0' || magic[5] != '2')
        return false;
    std::int32_t dims[4] = {};
    std::uint32_t shape[2] = {};
    in.read(reinterpret_cast<char*>(dims), sizeof(dims));
    in.read(reinterpret_cast<char*>(shape), sizeof(shape));
    if (!in || shape[0] < 2 || shape[1] < 2 || shape[0] > 65536 ||
        shape[1] > 65536)
        return false;
    const std::size_t n =
        static_cast<std::size_t>(shape[0]) * shape[1] * 2;
    if (n > 200000000) return false;
    WarpMesh tmp;
    tmp.left = dims[0];
    tmp.top = dims[1];
    tmp.right = dims[2];
    tmp.bottom = dims[3];
    tmp.cols = shape[0];
    tmp.rows = shape[1];
    tmp.offsets.resize(n);
    in.read(reinterpret_cast<char*>(tmp.offsets.data()),
            static_cast<std::streamsize>(n * sizeof(float)));
    if (!in) return false;
    for (float v : tmp.offsets) {
        if (!std::isfinite(v)) return false;
    }
    m = std::move(tmp);
    return true;
}

namespace {

// Source pixel in layer coords; transparent outside the layer.
inline RGBAf src_pixel(const RGBAf* src, std::uint32_t w, std::uint32_t h,
                       int x, int y) {
    if (x < 0 || y < 0 || x >= static_cast<int>(w) ||
        y >= static_cast<int>(h))
        return RGBAf{0, 0, 0, 0};
    return src[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)];
}

// Bilinear fetch on premultiplied alpha, returning straight alpha. The
// subpixel displacement is kept separate from the integer pixel step so large
// coordinates do not lose fractional bits in the sum.
inline RGBAf fetch(const RGBAf* src, std::uint32_t w, std::uint32_t h, int x,
                   int y, float dx, float dy) {
    const float ox = std::floor(dx);
    const float oy = std::floor(dy);
    const float tx = dx - ox;
    const float ty = dy - oy;
    const int x0 = x + static_cast<int>(ox);
    const int y0 = y + static_cast<int>(oy);

    float ar = 0.0f, ag = 0.0f, ab = 0.0f, aa = 0.0f;
    struct Tap { int dx, dy; float w; };
    const Tap taps[4] = {{0, 0, (1.0f - tx) * (1.0f - ty)},
                         {1, 0, tx * (1.0f - ty)},
                         {0, 1, (1.0f - tx) * ty},
                         {1, 1, tx * ty}};
    for (const Tap& t : taps) {
        if (t.w <= 0.0f) continue;
        const RGBAf p = src_pixel(src, w, h, x0 + t.dx, y0 + t.dy);
        ar += p.r * p.a * t.w;
        ag += p.g * p.a * t.w;
        ab += p.b * p.a * t.w;
        aa += p.a * t.w;
    }
    if (aa <= 1e-6f) return RGBAf{0, 0, 0, 0};
    return RGBAf{ar / aa, ag / aa, ab / aa, aa};
}

}  // namespace

void warp_into_host(RGBAf* dst, const RGBAf* src, std::uint32_t w,
                    std::uint32_t h, const WarpMesh& mesh, int x0, int y0,
                    int x1, int y1) {
    if (!dst || !src || w == 0 || h == 0) return;
    clamp_region(x0, y0, x1, y1, mesh.left, mesh.top, mesh.right, mesh.bottom);
    if (x0 >= x1 || y0 >= y1 || mesh.cols < 2 || mesh.rows < 2) return;
    warp_into_host(dst, src, w, h, warp_subgrid(mesh, x0, y0, x1, y1), x0, y0,
                   x1, y1);
}

void warp_into_host(RGBAf* dst, const RGBAf* src, std::uint32_t w,
                    std::uint32_t h, const WarpSubgrid& grid, int x0, int y0,
                    int x1, int y1) {
    if (!dst || !src || w == 0 || h == 0 || grid.empty()) return;
    x0 = std::max(0, x0);
    y0 = std::max(0, y0);
    x1 = std::min(x1, static_cast<int>(w));
    y1 = std::min(y1, static_cast<int>(h));
    if (x0 >= x1 || y0 >= y1) return;

    // Rows independent (the seam a threaded backend splits on):
    // bit-identical threaded.
    const std::uint32_t rows = static_cast<std::uint32_t>(y1 - y0);
    pittore::core::parallel_rows(rows, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t yy = lo; yy < hi; ++yy) {
            const int y = y0 + static_cast<int>(yy);
            RGBAf* row = dst + static_cast<std::size_t>(y) * w;
            for (int x = x0; x < x1; ++x) {
                // Sample the mesh at the pixel centre, then fetch from `src` at
                // the displaced position.
                float dx = 0.0f, dy = 0.0f;
                warp_subgrid_sample(grid, x + 0.5f, y + 0.5f, dx, dy);
                row[x] = fetch(src, w, h, x, y, dx, dy);
            }
        }
    });
}

}  // namespace pittore::compute
