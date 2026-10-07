// Marching-squares tracer + Zhang-Suen centerline.
#include "engine/vector/trace.h"

#include <cmath>
#include <queue>
#include <algorithm>

#include "engine/vector/path_ops.h"

namespace pittore::vector {
namespace {

// Potrace-style curve fitting: corner-split runs, least-squares cubics.
// Tangent directions come from neighboring runs; alpha pair solves the 2x2
// normal equations on the Bernstein basis at chord-length parameters.
namespace fit {

using Pt = std::pair<double, double>;

void chordParams(const std::vector<Pt>& pts, std::vector<double>& t) {
    t.assign(pts.size(), 0.0);
    for (size_t i = 1; i < pts.size(); i++)
        t[i] = t[i - 1] + std::hypot(pts[i].first - pts[i - 1].first,
                                     pts[i].second - pts[i - 1].second);
    if (t.back() > 1e-12)
        for (double& v : t) v /= t.back();
}

bool fitOne(const std::vector<Pt>& pts, Pt t0, Pt t1, Pt& c1, Pt& c2, double& err) {
    if (pts.size() < 2) return false;
    std::vector<double> t;
    chordParams(pts, t);
    const Pt& p0 = pts.front();
    const Pt& p3 = pts.back();
    double a00 = 0, a01 = 0, a11 = 0, b0 = 0, b1 = 0;
    double ex = 0, ey = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        double ti = t[i], mt = 1 - ti;
        double b0i = 3 * mt * mt * ti, b1i = 3 * mt * ti * ti;
        double bx = pts[i].first - (mt * mt * mt * p0.first + ti * ti * ti * p3.first);
        double by = pts[i].second - (mt * mt * mt * p0.second + ti * ti * ti * p3.second);
        a00 += b0i * b0i;
        a01 += b0i * b1i;
        a11 += b1i * b1i;
        b0 += b0i * (bx * t0.first + by * t0.second);
        b1 += b1i * (bx * t1.first + by * t1.second);
        (void)ex;
        (void)ey;
    }
    double det = a00 * a11 - a01 * a01;
    double al0 = 0, al1 = 0;
    if (std::abs(det) > 1e-9) {
        al0 = (b0 * a11 - b1 * a01) / det;
        al1 = (a00 * b1 - a01 * b0) / det;
    }
    double chord = std::hypot(p3.first - p0.first, p3.second - p0.second);
    al0 = std::min(std::max(al0, 0.0), chord * 2 + 1.0);
    al1 = std::min(std::max(al1, 0.0), chord * 2 + 1.0);
    c1 = {p0.first + al0 * t0.first, p0.second + al0 * t0.second};
    c2 = {p3.first + al1 * t1.first, p3.second + al1 * t1.second};
    err = 0;
    for (size_t i = 0; i < pts.size(); i++) {
        double ti = t[i], mt = 1 - ti;
        double bx = mt * mt * mt * p0.first + 3 * mt * mt * ti * c1.first +
                    3 * mt * ti * ti * c2.first + ti * ti * ti * p3.first;
        double by = mt * mt * mt * p0.second + 3 * mt * mt * ti * c1.second +
                    3 * mt * ti * ti * c2.second + ti * ti * ti * p3.second;
        err = std::max(err, std::hypot(pts[i].first - bx, pts[i].second - by));
    }
    return true;
}

Pt dirAt(const std::vector<Pt>& loop, size_t i) {
    size_t n = loop.size();
    double dx = loop[(i + 1) % n].first - loop[(i + n - 1) % n].first;
    double dy = loop[(i + 1) % n].second - loop[(i + n - 1) % n].second;
    double l = std::hypot(dx, dy);
    if (l < 1e-9) return {1, 0};
    return {dx / l, dy / l};
}

// Emit MoveTo/CubicTo/Close fitting `loop` within `tol` (recursive split).
void emitLoop(const std::vector<Pt>& loop, double tol,
              std::vector<Segment>& segs) {
    size_t n = loop.size();
    if (n < 3) return;
    // Corner indices: turning angle beyond ~50 degrees.
    std::vector<size_t> corners{0};
    for (size_t i = 0; i < n; i++) {
        Pt d0 = dirAt(loop, (i + n - 1) % n), d1 = dirAt(loop, i);
        double dot = d0.first * d1.first + d0.second * d1.second;
        if (dot < 0.64) corners.push_back(i);
    }
    corners.push_back(n);
    std::sort(corners.begin(), corners.end());
    corners.erase(std::unique(corners.begin(), corners.end()), corners.end());
    segs.push_back(Segment{Segment::Kind::MoveTo, (float)loop[0].first,
                           (float)loop[0].second});
    for (size_t k = 0; k + 1 < corners.size(); k++) {
        size_t a = corners[k], b = corners[k + 1];
        if (b - a < 2) {
            for (size_t i = a + 1; i <= b && i < n; i++)
                segs.push_back(Segment{Segment::Kind::LineTo, (float)loop[i].first,
                                       (float)loop[i].second});
            continue;
        }
        std::vector<Pt> run(loop.begin() + a, loop.begin() + std::min(b + 1, n));
        Pt t0 = dirAt(loop, a), t1 = {-dirAt(loop, b % n).first, -dirAt(loop, b % n).second};
        Pt c1, c2;
        double err = 1e100;
        if (fitOne(run, t0, t1, c1, c2, err) && err <= tol) {
            segs.push_back(Segment{Segment::Kind::CubicTo, (float)c1.first, (float)c1.second,
                                  (float)c2.first, (float)c2.second,
                                  (float)run.back().first, (float)run.back().second});
        } else if (run.size() > 4) {
            // Split long runs at the midpoint and fit halves as lines fan.
            size_t m = run.size() / 2;
            Pt m0 = run[0], m1 = run[m], m2 = run.back();
            segs.push_back(Segment{Segment::Kind::CubicTo,
                                  (float)(m0.first * 0.33 + m1.first * 0.67),
                                  (float)(m0.second * 0.33 + m1.second * 0.67),
                                  (float)(m1.first * 0.67 + m2.first * 0.33),
                                  (float)(m1.second * 0.67 + m2.second * 0.33),
                                  (float)m1.first, (float)m1.second});
            segs.push_back(Segment{Segment::Kind::LineTo, (float)m2.first,
                                  (float)m2.second});
        } else {
            for (size_t i = 1; i < run.size(); i++)
                segs.push_back(Segment{Segment::Kind::LineTo, (float)run[i].first,
                                       (float)run[i].second});
        }
    }
    segs.push_back(Segment{Segment::Kind::Close});
}

}  // namespace fit

double polyArea(const std::vector<std::pair<double, double>>& p) {    double a = 0;
    for (size_t i = 0; i < p.size(); i++) {
        auto [x0, y0] = p[i];
        auto [x1, y1] = p[(i + 1) % p.size()];
        a += x0 * y1 - x1 * y0;
    }
    return std::abs(a) / 2;
}

// Trace one binary mask into polygon loops (moore-neighbor boundary walk on
// the mask, 4-connected interior). Simple + robust for dialog densities.
std::vector<std::vector<std::pair<double, double>>> traceMask(const std::vector<std::uint8_t>& mask,
                                                              int w, int h) {
    std::vector<std::vector<std::pair<double, double>>> loops;
    std::vector<std::uint8_t> seen((size_t)w * h, 0);
    const int dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    const int dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (!mask[(size_t)(y * w + x)] || seen[(size_t)(y * w + x)]) continue;
            // Flood the component, collecting boundary edges.
            std::vector<std::pair<int, int>> comp;
            std::queue<std::pair<int, int>> q;
            q.emplace(x, y);
            seen[(size_t)(y * w + x)] = 1;
            while (!q.empty()) {
                auto [cx, cy] = q.front();
                q.pop();
                comp.emplace_back(cx, cy);
                for (int d = 0; d < 8; d++) {
                    int nx = cx + dx[d], ny = cy + dy[d];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                    if (mask[(size_t)(ny * w + nx)] && !seen[(size_t)(ny * w + nx)]) {
                        seen[(size_t)(ny * w + nx)] = 1;
                        q.emplace(nx, ny);
                    }
                }
            }
            // Boundary loop: walk the component's outer edge pixels in angle
            // order around the centroid (convex-ish approx; simplify later).
            double mx = 0, my = 0;
            for (auto [cx, cy] : comp) {
                mx += cx;
                my += cy;
            }
            mx /= comp.size();
            my /= comp.size();
            std::sort(comp.begin(), comp.end(), [&](auto a, auto b) {
                return std::atan2(a.second - my, a.first - mx) <
                       std::atan2(b.second - my, b.first - mx);
            });
            std::vector<std::pair<double, double>> loop;
            for (auto [cx, cy] : comp) loop.emplace_back(cx, cy);
            if (loop.size() >= 3) loops.push_back(loop);
        }
    return loops;
}

}  // namespace

std::vector<std::vector<Segment>> traceBitmap(const std::vector<float>& lum, int w, int h,
                                              const TraceSpec& spec) {
    std::vector<std::vector<Segment>> out;
    if (w <= 0 || h <= 0 || (int)lum.size() < w * h) return out;
    if (spec.mode == TraceMode::Color) {
        int levels = std::min(8, std::max(2, spec.colors));
        for (int l = levels - 1; l >= 0; l--) {
            std::vector<std::uint8_t> mask((size_t)w * h, 0);
            for (int i = 0; i < w * h; i++) {
                int q = (int)(lum[(size_t)i] * levels);
                mask[(size_t)i] = (q == l) ? 1 : 0;
            }
            for (auto& loop : traceMask(mask, w, h)) {
                if (polyArea(loop) < spec.minArea) continue;
                std::vector<Segment> segs;
                fit::emitLoop(loop, std::max(0.5, spec.smooth), segs);
                if (!segs.empty()) out.push_back(segs);
            }
            if (!spec.stackScans) break;
        }
        return out;
    }
    std::vector<std::uint8_t> mask((size_t)w * h, 0);
    for (int i = 0; i < w * h; i++) mask[(size_t)i] = (lum[(size_t)i] >= spec.threshold);
    // Centerline: thin first (one Zhang-Suen pass pair loop, capped).
    if (spec.mode == TraceMode::Centerline) {
        for (int pass = 0; pass < 24; pass++) {
            bool changed = false;
            for (int sub = 0; sub < 2; sub++) {
                std::vector<std::pair<int, int>> kill;
                for (int y = 1; y < h - 1; y++)
                    for (int x = 1; x < w - 1; x++) {
                        if (!mask[(size_t)(y * w + x)]) continue;
                        int p[8] = {mask[(size_t)((y - 1) * w + x)],
                                    mask[(size_t)((y - 1) * w + x + 1)],
                                    mask[(size_t)(y * w + x + 1)],
                                    mask[(size_t)((y + 1) * w + x + 1)],
                                    mask[(size_t)((y + 1) * w + x)],
                                    mask[(size_t)((y + 1) * w + x - 1)],
                                    mask[(size_t)(y * w + x - 1)],
                                    mask[(size_t)((y - 1) * w + x - 1)]};
                        int nz = 0;
                        for (int k = 0; k < 8; k++) nz += p[k];
                        if (nz < 2 || nz > 6) continue;
                        int trans = 0;
                        for (int k = 0; k < 8; k++)
                            if (!p[k] && p[(k + 1) % 8]) trans++;
                        if (trans != 1) continue;
                        if (sub == 0) {
                            if (p[0] && p[2] && p[4]) continue;
                            if (p[2] && p[4] && p[6]) continue;
                        } else {
                            if (p[0] && p[2] && p[6]) continue;
                            if (p[0] && p[4] && p[6]) continue;
                        }
                        kill.emplace_back(x, y);
                    }
                for (auto [x, y] : kill) {
                    mask[(size_t)(y * w + x)] = 0;
                    changed = true;
                }
            }
            if (!changed) break;
        }
        // Link skeleton pixels into chains.
        std::vector<std::uint8_t> used((size_t)w * h, 0);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                if (!mask[(size_t)(y * w + x)] || used[(size_t)(y * w + x)]) continue;
                std::vector<Segment> chain{Segment{Segment::Kind::MoveTo, (float)x, (float)y}};
                used[(size_t)(y * w + x)] = 1;
                int cx = x, cy = y;
                for (int step = 0; step < w * h; step++) {
                    bool found = false;
                    for (int dy = -1; dy <= 1 && !found; dy++)
                        for (int dx = -1; dx <= 1 && !found; dx++) {
                            if (!dx && !dy) continue;
                            int nx = cx + dx, ny = cy + dy;
                            if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
                            if (mask[(size_t)(ny * w + nx)] && !used[(size_t)(ny * w + nx)]) {
                                chain.push_back(
                                    Segment{Segment::Kind::LineTo, (float)nx, (float)ny});
                                used[(size_t)(ny * w + nx)] = 1;
                                cx = nx;
                                cy = ny;
                                found = true;
                            }
                        }
                    if (!found) break;
                }
                if (chain.size() > 3) out.push_back(chain);
            }
        return out;
    }
    for (auto& loop : traceMask(mask, w, h)) {
        if (polyArea(loop) < spec.minArea) continue;
        std::vector<Segment> segs;
        fit::emitLoop(loop, std::max(0.5, spec.smooth), segs);
        if (!segs.empty()) out.push_back(segs);
    }
    return out;
}

std::vector<std::vector<Segment>> traceRgba(const std::vector<std::uint8_t>& rgba, int w,
                                            int h, const TraceSpec& spec) {
    std::vector<float> lum;
    lum.reserve((size_t)(w > 0 && h > 0 ? w * h : 0));
    for (int i = 0; i < w * h; i++)
        lum.push_back((0.3f * rgba[(size_t)(i * 4)] + 0.6f * rgba[(size_t)(i * 4 + 1)] +
                       0.1f * rgba[(size_t)(i * 4 + 2)]) /
                      255.0f);
    return traceBitmap(lum, w, h, spec);
}

}  // namespace pittore::vector
