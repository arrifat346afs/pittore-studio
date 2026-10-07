#pragma once
// Shared pixel-math helpers for every filter category.
// Split from engine/filter/filters.h.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

#include "engine/core/image.h"
#include "engine/core/parallel.h"
#include "engine/filter/core/filter_types.h"

namespace pittore::filter {

namespace detail {

inline double c01(double v) {
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

inline double strengthOf(const std::vector<double>& par) {
    if (par.empty()) return 1.0;
    const double v = par[0] / 100.0;
    return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
}

inline void blendInto(Image& img, const Image& orig, double m) {
    if (m >= 1.0) return;
    const float k = static_cast<float>(m);
    RGBAf* px = img.data();
    const RGBAf* q = orig.data();
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    // Rows disjoint: bit-identical.
    pittore::core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            const std::size_t base = static_cast<std::size_t>(y) * w;
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t i = base + x;
                px[i].r = q[i].r + (px[i].r - q[i].r) * k;
                px[i].g = q[i].g + (px[i].g - q[i].g) * k;
                px[i].b = q[i].b + (px[i].b - q[i].b) * k;
                px[i].a = q[i].a + (px[i].a - q[i].a) * k;
            }
        }
    });
}

inline std::uint32_t cw(const Image &img, int x) {
    if (x < 0) return 0;
    const int w = static_cast<int>(img.width());
    return static_cast<std::uint32_t>(x >= w ? w - 1 : x);
}

inline std::uint32_t ch(const Image &img, int y) {
    if (y < 0) return 0;
    const int h = static_cast<int>(img.height());
    return static_cast<std::uint32_t>(y >= h ? h - 1 : y);
}

inline RGBAf bilin(const Image &img, double x, double y) {
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const double fx = x - x0;
    const double fy = y - y0;
    const RGBAf &a = img.at(cw(img, x0), ch(img, y0));
    const RGBAf &b = img.at(cw(img, x0 + 1), ch(img, y0));
    const RGBAf &c = img.at(cw(img, x0), ch(img, y0 + 1));
    const RGBAf &d = img.at(cw(img, x0 + 1), ch(img, y0 + 1));
    RGBAf o;
    o.r = static_cast<float>(a.r * (1 - fx) * (1 - fy) + b.r * fx * (1 - fy) + c.r * (1 - fx) * fy + d.r * fx * fy);
    o.g = static_cast<float>(a.g * (1 - fx) * (1 - fy) + b.g * fx * (1 - fy) + c.g * (1 - fx) * fy + d.g * fx * fy);
    o.b = static_cast<float>(a.b * (1 - fx) * (1 - fy) + b.b * fx * (1 - fy) + c.b * (1 - fx) * fy + d.b * fx * fy);
    o.a = static_cast<float>(a.a * (1 - fx) * (1 - fy) + b.a * fx * (1 - fy) + c.a * (1 - fx) * fy + d.a * fx * fy);
    return o;
}

// Scratch slot for the host blur chain. A 4K box pass used to `Image tmp(w,h)`
// (133MB zero-fill + first-touch faults ≈ 4.8ms) on every call — 4 of them per
// gaussian. Reused instead: growth is the only time we pay, and the slots stay
// hot (resident, no re-fault) between calls. Thread-local by construction:
// only the calling thread blurs, never the parallel_rows workers.
inline RGBAf* blurScratch(int slot, std::size_t n) {
    static thread_local std::vector<RGBAf> s0;
    static thread_local std::vector<RGBAf> s1;
    std::vector<RGBAf>& v = slot == 0 ? s0 : s1;
    if (v.size() < n) v.resize(n);
    return v.data();
}

// Separable box blur, three distinct buffers: `src` -> `mid` (H pass) ->
// `dst` (V pass). Identical taps, channel order and double-accumulator
// rounding to the Image overload — only the intermediate's owner changes, so
// callers can ping-pong the chain without allocating or copying.
inline void boxBlurRaw(const RGBAf* src, RGBAf* mid, RGBAf* dst,
                       std::uint32_t w, std::uint32_t h, int r) {
    if (w == 0 || h == 0) return;
    const int rr = r < 1 ? 1 : r;
    const int iw = static_cast<int>(w);
    const int ih = static_cast<int>(h);
    // Sliding clamped-edge window: identical taps to the naive box
    // (each of the 2rr+1 taps clamped into range), but O(1) per pixel
    // instead of O(rr). Double accumulators keep the rounding error at the
    // naive sum's level; channel order and alpha handling are unchanged.
    const double n = static_cast<double>(2 * rr + 1);
    RGBAf* t = mid;
    // H-pass rows are independent sliding windows: bit-identical threaded.
    // (V-pass keeps its column running sums: serial by design.)
    pittore::core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            const RGBAf* row = src + static_cast<std::size_t>(y) * w;
            RGBAf* trow = t + static_cast<std::size_t>(y) * w;
            double ar = 0.0, ag = 0.0, ab = 0.0;
            for (int dx = -rr; dx <= rr; ++dx) {
                const RGBAf& p = row[std::clamp(dx, 0, iw - 1)];
                ar += p.r;
                ag += p.g;
                ab += p.b;
            }
            for (std::uint32_t x = 0; x < w; ++x) {
                const int xi = static_cast<int>(x);
                RGBAf& o = trow[x];
                o.r = static_cast<float>(ar / n);
                o.g = static_cast<float>(ag / n);
                o.b = static_cast<float>(ab / n);
                o.a = row[x].a;
                const RGBAf& out =
                    row[std::clamp(xi - rr, 0, iw - 1)];
                const RGBAf& in =
                    row[std::clamp(xi + rr + 1, 0, iw - 1)];
                ar += static_cast<double>(in.r) - out.r;
                ag += static_cast<double>(in.g) - out.g;
                ab += static_cast<double>(in.b) - out.b;
            }
        }
    });
    RGBAf* d = dst;
    // Row-major sweep with one running sum per column (L2-resident), so both
    // passes stream. Init covers rows [-rr, rr] clamped, then each step down
    // drops clamp(y-rr-1) ... precisely: window rows [y-rr, y+rr] clamped.
    // Column strips are independent (each column has its own running sums),
    // so the sweep parallelizes bit-identically over column ranges.
    pittore::core::parallel_cols(w, [&](std::uint32_t x0, std::uint32_t x1) {
        const std::size_t strip = static_cast<std::size_t>(x1 - x0);
        std::vector<double> ar(strip, 0.0);
        std::vector<double> ag(strip, 0.0);
        std::vector<double> ab(strip, 0.0);
        for (int dy = -rr; dy <= rr; ++dy) {
            const RGBAf* row =
                t + static_cast<std::size_t>(std::clamp(dy, 0, ih - 1)) * w + x0;
            for (std::uint32_t x = 0; x < strip; ++x) {
                ar[x] += row[x].r;
                ag[x] += row[x].g;
                ab[x] += row[x].b;
            }
        }
        for (std::uint32_t y = 0; y < h; ++y) {
            const int yi = static_cast<int>(y);
            RGBAf* orow = d + static_cast<std::size_t>(y) * w + x0;
            const RGBAf* srow = src + static_cast<std::size_t>(y) * w + x0;
            for (std::uint32_t x = 0; x < strip; ++x) {
                orow[x].r = static_cast<float>(ar[x] / n);
                orow[x].g = static_cast<float>(ag[x] / n);
                orow[x].b = static_cast<float>(ab[x] / n);
                orow[x].a = srow[x].a;
            }
            const RGBAf* drop =
                t + static_cast<std::size_t>(std::clamp(yi - rr, 0, ih - 1)) * w + x0;
            const RGBAf* add =
                t + static_cast<std::size_t>(std::clamp(yi + rr + 1, 0, ih - 1)) *
                        w + x0;
            for (std::uint32_t x = 0; x < strip; ++x) {
                ar[x] += static_cast<double>(add[x].r) - drop[x].r;
                ag[x] += static_cast<double>(add[x].g) - drop[x].g;
                ab[x] += static_cast<double>(add[x].b) - drop[x].b;
            }
        }
    });
}

inline void boxBlurInto(const Image &src, Image &dst, int r) {
    const std::uint32_t w = src.width();
    const std::uint32_t h = src.height();
    if (w == 0 || h == 0) return;
    boxBlurRaw(src.data(), detail::blurScratch(1, static_cast<std::size_t>(w) * h),
               dst.data(), w, h, r);
}

inline void gaussBlur(Image &img, double sigma) {
    if (sigma <= 0.05) return;
    const int r = std::max(1, static_cast<int>(std::round(sigma)));
    const std::uint32_t w = img.width(), h = img.height();
    const std::size_t n = static_cast<std::size_t>(w) * h;
    RGBAf* a = detail::blurScratch(0, n);
    RGBAf* m = detail::blurScratch(1, n);
    RGBAf* s = img.data();
    // Three passes, ping-ponged across {img, a, m}: img->a->m->img lands the
    // result back in the caller's buffer, so the old per-pass 133MB memcpy
    // (3 per gaussian) is gone. img doubles as pass 1's intermediate — it is
    // free the moment pass 0 has read it. Every pass still reads exactly the
    // same bytes in the same order: bit-identical output.
    boxBlurRaw(s, m, a, w, h, r);   // pass 0: src=img, mid=s1, dst=s0
    boxBlurRaw(a, s, m, w, h, r);   // pass 1: src=s0,  mid=img, dst=s1
    boxBlurRaw(m, a, s, w, h, r);   // pass 2: src=s1,  mid=s0, dst=img
}

// Blur `src` into `dst` without first copying src into dst (the caller's
// whole-frame memcpy of a 4K image). sigma <= 0.05 still delivers dst = src,
// so this is a drop-in for "memcpy then gaussBlur".
inline void gaussBlurFrom(const Image &src, Image &dst, double sigma) {
    const std::uint32_t w = src.width(), h = src.height();
    const std::size_t n = static_cast<std::size_t>(w) * h;
    if (sigma <= 0.05) {
        std::memcpy(dst.data(), src.data(), sizeof(RGBAf) * n);
        return;
    }
    const int r = std::max(1, static_cast<int>(std::round(sigma)));
    RGBAf* a = detail::blurScratch(0, n);
    RGBAf* m = detail::blurScratch(1, n);
    boxBlurRaw(src.data(), m, a, w, h, r);  // pass 0: src->s0 via s1
    boxBlurRaw(a, dst.data(), m, w, h, r);  // pass 1: s0->s1 via dst
    boxBlurRaw(m, a, dst.data(), w, h, r);  // pass 2: s1->dst via s0
}

inline void medianR(Image &img, int r) {
    if (r < 1) return;
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    Image out(w, h);
    const std::size_t k = static_cast<std::size_t>(2 * r + 1) * (2 * r + 1);
    // Rows disjoint, out separate, per-worker scratch: bit-identical.
    // (Manual split rather than parallel_rows: the scratch vectors must be
    // per-worker, not per-call.)
    const std::uint32_t rows = h;
    unsigned hw = std::thread::hardware_concurrency();
    if (hw == 0) hw = 4;
    unsigned threads = std::min<unsigned>(hw, (rows + 63) / 64);
    // Exact median of one window: insertion sort for small k (lower constant
    // than nth_element), nth_element above. k is always odd here, so both
    // return the same ranked element: bit-identical either way.
    auto median_rows = [&](std::uint32_t y0, std::uint32_t y1) {
        std::vector<float> w0, w1, w2;
        w0.reserve(k);
        w1.reserve(k);
        w2.reserve(k);
        const bool small = k <= 32;
        for (std::uint32_t yy = y0; yy < y1; ++yy) {
            for (std::uint32_t x = 0; x < w; ++x) {
                // Single window walk feeds all three channels (was: one full
                // walk per channel = 3x memory traffic for identical taps).
                w0.clear();
                w1.clear();
                w2.clear();
                for (int dy = -r; dy <= r; ++dy) {
                    const std::uint32_t sy = ch(img, static_cast<int>(yy) + dy);
                    for (int dx = -r; dx <= r; ++dx) {
                        const RGBAf& t =
                            img.at(cw(img, static_cast<int>(x) + dx), sy);
                        w0.push_back(t.r);
                        w1.push_back(t.g);
                        w2.push_back(t.b);
                    }
                }
                RGBAf acc{0, 0, 0, 1.0f};
                float* wins[3] = {w0.data(), w1.data(), w2.data()};
                for (int c = 0; c < 3; ++c) {
                    float* v = wins[c];
                    if (small) {
                        for (std::size_t i = 1; i < k; ++i) {
                            const float key = v[i];
                            std::size_t j = i;
                            while (j > 0 && v[j - 1] > key) {
                                v[j] = v[j - 1];
                                --j;
                            }
                            v[j] = key;
                        }
                    } else {
                        std::nth_element(v, v + k / 2, v + k);
                    }
                    acc[c] = v[k / 2];
                }
                acc.a = img.at(x, yy).a;
                out.at(x, yy) = acc;
            }
        }
    };
    if (threads <= 1) {
        median_rows(0, rows);
    } else {
        std::vector<std::thread> workers;
        workers.reserve(threads - 1);
        const std::uint32_t base = rows / threads;
        const std::uint32_t rest = rows % threads;
        std::uint32_t y = 0;
        for (unsigned t = 0; t + 1 < threads; ++t) {
            const std::uint32_t n = base + (t < rest ? 1 : 0);
            workers.emplace_back([&, y, n] { median_rows(y, y + n); });
            y += n;
        }
        median_rows(y, rows);
        for (auto &th : workers) th.join();
    }
    std::memcpy(img.data(), out.data(), sizeof(RGBAf) * img.pixel_count());
}

inline void unsharpF(Image &img, double amount, int radius, double threshold) {
    const int r = std::max(1, radius);
    Image blur(img.width(), img.height());
    boxBlurInto(img, blur, r);
    const double amt = std::clamp(amount, 0.0, 5.0);
    const double thr = std::clamp(threshold, 0.0, 1.0);
    RGBAf *px = img.data();
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    const RGBAf *b = blur.data();
    // Rows disjoint, read-then-write per pixel: bit-identical.
    pittore::core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            const std::size_t base = static_cast<std::size_t>(y) * w;
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t i = base + x;
                for (int c = 0; c < 3; ++c) {
                    const double diff = static_cast<double>(px[i][c]) - b[i][c];
                    if (std::fabs(diff) <= thr) continue;
                    px[i][c] = static_cast<float>(c01(static_cast<double>(px[i][c]) + amt * diff));
                }
            }
        }
    });
}

inline float lumaF(const RGBAf &p) {
    return 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
}

inline std::uint64_t hash2i(int x, int y, std::uint64_t s) {
    std::uint64_t z = static_cast<std::uint64_t>(x * 374761393 + y * 668265263) + s * 144665;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

inline double vnoise(double x, double y, std::uint64_t s) {
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const double fx = x - xi;
    const double fy = y - yi;
    const double sx = fx * fx * (3.0 - 2.0 * fx);
    const double sy = fy * fy * (3.0 - 2.0 * fy);
    const double a = (hash2i(xi, yi, s) % 100000) / 100000.0;
    const double b = (hash2i(xi + 1, yi, s) % 100000) / 100000.0;
    const double c = (hash2i(xi, yi + 1, s) % 100000) / 100000.0;
    const double d = (hash2i(xi + 1, yi + 1, s) % 100000) / 100000.0;
    return a * (1 - sx) * (1 - sy) + b * sx * (1 - sy) + c * (1 - sx) * sy + d * sx * sy;
}

inline double fbm(double x, double y, int oct, std::uint64_t s) {
    double v = 0, amp = 0.5, f = 1.0;
    for (int i = 0; i < oct; ++i) {
        v += amp * vnoise(x * f, y * f, s + static_cast<std::uint64_t>(i) * 7919);
        amp *= 0.5;
        f *= 2.0;
    }
    return v;
}

inline void toHsl(float r, float g, float b, float &h, float &s, float &l) {
    const float mx = std::max({r, g, b});
    const float mn = std::min({r, g, b});
    l = (mx + mn) * 0.5f;
    if (mx == mn) {
        h = 0;
        s = 0;
        return;
    }
    const float d = mx - mn;
    s = l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
    if (mx == r)
        h = (g - b) / d + (g < b ? 6.0f : 0.0f);
    else if (mx == g)
        h = (b - r) / d + 2.0f;
    else
        h = (r - g) / d + 4.0f;
    h /= 6.0f;
}

inline void fromHsl(float h, float s, float l, float &r, float &g, float &b) {
    if (s == 0) {
        r = g = b = l;
        return;
    }
    auto hue = [](float p, float q, float t) {
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
        if (t < 0.5f) return q;
        if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
        return p;
    };
    const float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
    const float p = 2 * l - q;
    r = hue(p, q, h + 1.0f / 3.0f);
    g = hue(p, q, h);
    b = hue(p, q, h - 1.0f / 3.0f);
}

inline void posterF(Image &img, int levels) {
    const int lv = std::max(2, levels);
    RGBAf *px = img.data();
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    // Rows disjoint: bit-identical.
    pittore::core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            const std::size_t base = static_cast<std::size_t>(y) * w;
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::size_t i = base + x;
                for (int c = 0; c < 3; ++c) {
                    const double v = c01(px[i][c]);
                    px[i][c] = static_cast<float>(std::floor(v * (lv - 1) + 0.5) / (lv - 1));
                }
            }
        }
    });
}

inline double edgeMag(const Image &img, std::uint32_t x, std::uint32_t y) {
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    const std::uint32_t xm = x == 0 ? 0 : x - 1;
    const std::uint32_t xp = x + 1 >= w ? w - 1 : x + 1;
    const std::uint32_t ym = y == 0 ? 0 : y - 1;
    const std::uint32_t yp = y + 1 >= h ? h - 1 : y + 1;
    const double gx = lumaF(img.at(xp, y)) - lumaF(img.at(xm, y));
    const double gy = lumaF(img.at(x, yp)) - lumaF(img.at(x, ym));
    return std::sqrt(gx * gx + gy * gy);
}

inline double polyScale(double theta, int sides) {
    const double seg = 6.283185307179586 / sides;
    double a = std::fmod(theta, seg);
    if (a < 0) a += seg;
    return std::cos(3.141592653589793 / sides) / std::cos(a - seg * 0.5);
}

inline bool footprintPt(int dx, int dy, int r, int kind) {
    const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
    if (dist > r || r <= 0) return dist <= 0.5 && r <= 0;
    switch (kind) {
        case 0:
            return true;
        case 1:
            return std::abs(dx) + std::abs(dy) <= r;
        case 2: {
            const double th = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
            return dist <= r * polyScale(th, 6);
        }
        case 3:
            return std::abs(dx) * 3 <= r || std::abs(dy) * 3 <= r;
        case 4:
            return dist >= r * 0.55;
        case 5: {
            const double th = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
            return dist <= r * (0.45 + 0.55 * std::fabs(std::cos(th * 2.5)));
        }
        default: {
            const int sides = kind - 10;
            if (sides < 3) return true;
            const double th = std::atan2(static_cast<double>(dy), static_cast<double>(dx));
            return dist <= r * polyScale(th, sides);
        }
    }
}

inline void shapedBlurInto(const Image &src, Image &dst, int r, int kind) {
    const std::uint32_t w = src.width();
    const std::uint32_t h = src.height();
    const int rr = std::max(1, r);
    // Rows disjoint, dst separate: bit-identical (footprintPt is pure).
    pittore::core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                double ar = 0, ag = 0, ab = 0;
                int n = 0;
                for (int dy = -rr; dy <= rr; ++dy) {
                    for (int dx = -rr; dx <= rr; ++dx) {
                        if (!footprintPt(dx, dy, rr, kind)) continue;
                        const RGBAf &p = src.at(cw(src, static_cast<int>(x) + dx), ch(src, static_cast<int>(y) + dy));
                        ar += p.r;
                        ag += p.g;
                        ab += p.b;
                        ++n;
                    }
                }
                n = std::max(1, n);
                RGBAf &o = dst.at(x, y);
                o.r = static_cast<float>(ar / n);
                o.g = static_cast<float>(ag / n);
                o.b = static_cast<float>(ab / n);
                o.a = src.at(x, y).a;
            }
        }
    });
}

inline void remapWarp(Image &img, Image &scratch,
                      const std::function<std::pair<double, double>(double, double, double, double)> &fn) {
    const std::uint32_t w = img.width();
    const std::uint32_t h = img.height();
    // Rows disjoint, scratch separate (fn must be pure): bit-identical.
    pittore::core::parallel_rows(h, [&](std::uint32_t lo, std::uint32_t hi) {
        for (std::uint32_t y = lo; y < hi; ++y) {
            for (std::uint32_t x = 0; x < w; ++x) {
                auto pr = fn(static_cast<double>(x), static_cast<double>(y),
                             static_cast<double>(w), static_cast<double>(h));
                scratch.at(x, y) = bilin(img, pr.first, pr.second);
            }
        }
    });
    std::memcpy(img.data(), scratch.data(), sizeof(RGBAf) * img.pixel_count());
}

}

}  // namespace pittore::filter
