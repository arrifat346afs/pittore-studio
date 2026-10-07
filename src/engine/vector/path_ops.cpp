// Path verbs built on flattenSegments + booleanOp.
#include "engine/vector/path_ops.h"

#include <cmath>
#include <algorithm>

#include "engine/vector/boolean.h"

namespace pittore::vector {
namespace {

std::vector<BoolRing> ringsOf(const std::vector<Segment>& segs, float tol) {
    Path p = flattenSegments(segs, tol);
    std::vector<BoolRing> rings;
    for (auto& sp : p.subpaths) {
        BoolRing r;
        for (auto [x, y] : sp) r.emplace_back(x, y);
        if (r.size() >= 3) rings.push_back(r);
    }
    return rings;
}

std::vector<Segment> ringsToSegments(const std::vector<BoolRing>& rings) {
    std::vector<Segment> out;
    for (auto& r : rings) {
        if (r.empty()) continue;
        out.push_back(Segment{Segment::Kind::MoveTo, (float)r[0].first, (float)r[0].second});
        for (size_t i = 1; i < r.size(); i++)
            out.push_back(
                Segment{Segment::Kind::LineTo, (float)r[i].first, (float)r[i].second});
        out.push_back(Segment{Segment::Kind::Close});
    }
    return out;
}

double perpDist(double px, double py, double x0, double y0, double x1, double y1) {
    double dx = x1 - x0, dy = y1 - y0;
    double len = std::hypot(dx, dy);
    if (len < 1e-12) return std::hypot(px - x0, py - y0);
    return std::abs((py - y0) * dx - (px - x0) * dy) / len;
}

void rdp(const std::vector<std::pair<float, float>>& pts, size_t a, size_t b,
         double eps, std::vector<bool>& keep) {
    if (b <= a + 1) return;
    double best = -1;
    size_t idx = a;
    for (size_t i = a + 1; i < b; i++) {
        double d = perpDist(pts[i].first, pts[i].second, pts[a].first, pts[a].second,
                            pts[b].first, pts[b].second);
        if (d > best) {
            best = d;
            idx = i;
        }
    }
    if (best > eps) {
        keep[idx] = true;
        rdp(pts, a, idx, eps, keep);
        rdp(pts, idx, b, eps, keep);
    }
}

}  // namespace

std::vector<Segment> combinePaths(const std::vector<std::vector<Segment>>& parts) {
    std::vector<Segment> out;
    for (auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

std::vector<std::vector<Segment>> breakApart(const std::vector<Segment>& segs) {
    std::vector<std::vector<Segment>> out;
    std::vector<Segment> cur;
    for (auto& s : segs) {
        if (s.kind == Segment::Kind::MoveTo && !cur.empty()) {
            out.push_back(cur);
            cur.clear();
        }
        cur.push_back(s);
        if (s.kind == Segment::Kind::Close) {
            out.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

DivideResult dividePath(const std::vector<Segment>& subject,
                        const std::vector<Segment>& clip, float tolerance) {
    auto s = ringsOf(subject, tolerance), c = ringsOf(clip, tolerance);
    DivideResult r;
    r.inside = breakApart(ringsToSegments(booleanOp(s, c, BoolOp::Intersection)));
    r.outside = breakApart(ringsToSegments(booleanOp(s, c, BoolOp::Difference)));
    return r;
}

std::vector<std::vector<Segment>> cutPath(const std::vector<Segment>& subject,
                                           const std::vector<Segment>& cutter,
                                           float tolerance) {
    // Approximate cut by differencing a thin ribbon around the cutter.
    Path cp = flattenSegments(cutter, tolerance);
    std::vector<BoolRing> ribbon;
    const double w = 0.75;
    for (auto& sp : cp.subpaths)
        for (size_t i = 0; i + 1 < sp.size(); i++) {
            double x0 = sp[i].first, y0 = sp[i].second;
            double x1 = sp[i + 1].first, y1 = sp[i + 1].second;
            double dx = x1 - x0, dy = y1 - y0, l = std::hypot(dx, dy);
            if (l < 1e-9) continue;
            double nx = -dy / l * w, ny = dx / l * w;
            ribbon.push_back({{x0 + nx, y0 + ny}, {x1 + nx, y1 + ny},
                              {x1 - nx, y1 - ny}, {x0 - nx, y0 - ny}});
        }
    std::vector<BoolRing> acc = ringsOf(subject, tolerance);
    for (auto& band : ribbon) {
        std::vector<BoolRing> next;
        for (auto& ring : acc) {
            auto parts = booleanOp({ring}, {band}, BoolOp::Difference);
            next.insert(next.end(), parts.begin(), parts.end());
        }
        acc = std::move(next);
        if (acc.empty()) break;
    }
    return breakApart(ringsToSegments(acc));
}

std::vector<Segment> strokeToSegments(const std::vector<Segment>& segs,
                                      const StrokeStyle& style, float tolerance) {
    Path p = flattenSegments(segs, tolerance);
    Path st = strokePath(p, style);
    std::vector<Segment> out;
    for (size_t k = 0; k < st.subpaths.size(); k++) {
        auto& sp = st.subpaths[k];
        if (sp.empty()) continue;
        out.push_back(Segment{Segment::Kind::MoveTo, sp[0].first, sp[0].second});
        for (size_t i = 1; i < sp.size(); i++)
            out.push_back(Segment{Segment::Kind::LineTo, sp[i].first, sp[i].second});
        out.push_back(Segment{Segment::Kind::Close});
    }
    return out;
}

std::vector<Segment> offsetPath(const std::vector<Segment>& segs, double amount,
                                float tolerance) {
    Path p = flattenSegments(segs, tolerance);
    std::vector<BoolRing> rings;
    for (auto& sp : p.subpaths) {
        BoolRing r;
        for (auto [x, y] : sp) r.emplace_back(x, y);
        if (r.size() >= 3) rings.push_back(r);
    }
    if (rings.empty() || std::abs(amount) < 1e-9) return segs;
    // Outset via stroker trick: stroke the ring with width 2*|amount| and
    // union/difference it. Correct for rects/circles; approximate elsewhere
    // (the fast path, ahead of an exact-offset fallback).
    StrokeStyle st;
    st.width = (float)(std::abs(amount) * 2);
    st.join = LineJoin::Miter;
    Path halo = strokePath(p, st);
    std::vector<BoolRing> haloRings;
    for (auto& sp : halo.subpaths) {
        BoolRing r;
        for (auto [x, y] : sp) r.emplace_back(x, y);
        if (r.size() >= 3) haloRings.push_back(r);
    }
    auto res = amount > 0 ? booleanOp(rings, haloRings, BoolOp::Union)
                          : booleanOp(rings, haloRings, BoolOp::Difference);
    return ringsToSegments(res);
}

std::vector<Segment> dynamicOffset(const std::vector<Segment>& segs, double radius,
                                   float tolerance) {
    return offsetPath(segs, radius, tolerance);
}

std::vector<Segment> simplifyPath(const std::vector<Segment>& segs, double threshold,
                                  bool smooth, float tolerance) {
    (void)smooth;
    Path p = flattenSegments(segs, tolerance);
    std::vector<Segment> out;
    // Simplify per flattened subpath, then re-emit as lines (smooth refit is
    // the caller's VectorPath smoothing pass; lines keep topology exact).
    for (auto& sp : p.subpaths) {
        if (sp.size() < 3) {
            for (auto [x, y] : sp) out.push_back(Segment{Segment::Kind::LineTo, x, y});
            continue;
        }
        std::vector<bool> keep(sp.size(), false);
        keep.front() = keep.back() = true;
        rdp(sp, 0, sp.size() - 1, threshold, keep);
        bool first = true;
        for (size_t i = 0; i < sp.size(); i++) {
            if (!keep[i]) continue;
            out.push_back(Segment{first ? Segment::Kind::MoveTo : Segment::Kind::LineTo,
                                  sp[i].first, sp[i].second});
            first = false;
        }
    }
    return out;
}

std::vector<Segment> filletPath(const std::vector<Segment>& segs, double r, bool chamfer) {
    if (r <= 0) return segs;
    Path p = flattenSegments(segs, 0.5f);
    std::vector<Segment> out;
    for (auto& sp : p.subpaths) {
        if (sp.size() < 3) continue;
        out.push_back(Segment{Segment::Kind::MoveTo, sp[0].first, sp[0].second});
        for (size_t i = 1; i + 1 < sp.size(); i++) {
            double x0 = sp[i - 1].first, y0 = sp[i - 1].second;
            double x1 = sp[i].first, y1 = sp[i].second;
            double x2 = sp[i + 1].first, y2 = sp[i + 1].second;
            double d1 = std::hypot(x1 - x0, y1 - y0), d2 = std::hypot(x2 - x1, y2 - y1);
            double t = std::min({r / (d1 > 0 ? d1 : 1), r / (d2 > 0 ? d2 : 1), 0.5});
            double ax = x1 + (x0 - x1) * t, ay = y1 + (y0 - y1) * t;
            double bx = x1 + (x2 - x1) * t, by = y1 + (y2 - y1) * t;
            out.push_back(Segment{Segment::Kind::LineTo, (float)ax, (float)ay});
            if (chamfer) {
                out.push_back(Segment{Segment::Kind::LineTo, (float)bx, (float)by});
            } else {
                // Quadratic approx via cubic with both controls at the corner.
                out.push_back(Segment{Segment::Kind::CubicTo, (float)x1, (float)y1,
                                      (float)x1, (float)y1, (float)bx, (float)by});
            }
        }
        out.push_back(
            Segment{Segment::Kind::LineTo, sp.back().first, sp.back().second});
    }
    return out;
}

}  // namespace pittore::vector
