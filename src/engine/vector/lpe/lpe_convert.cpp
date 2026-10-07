// Convert-family LPEs with real geometry.
// Effect ids: lpe-bool, lpe-slice, lpe-bounding-box, lpe-measure-segments,
// lpe-powerclip/powermask, lpe-circle_3pts/ellipse_5pts/pts2ellipse,
// lpe-line_segment/parallel/perp_bisector/angle_bisector/tangent_to_curve,
// lpe-transform_2pts, lpe-knot, lpe-curvestitch, lpe-fill-between-*,
// lpe-attach-path, lpe-path_length, lpe-recursiveskeleton, lpe-dynastroke,
// lpe-embrodery-stitch, lpe-lattice. Canvas-only effects (ruler,
// show-handles, text-label, clone-original) record params for round-trip.
#include "engine/vector/lpe/lpe.h"

#include <cmath>

#include <algorithm>

#include "engine/vector/boolean.h"

namespace pittore::vector::lpe {
namespace {

using Pt = std::pair<double, double>;

// "x,y" param → point. False when malformed.
bool getPoint(const Params& p, const std::string& key, Pt& out) {
    std::string s = p.getString(key, "");
    size_t c = s.find(',');
    if (c == std::string::npos) return false;
    try {
        out = {std::stod(s.substr(0, c)), std::stod(s.substr(c + 1))};
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<Pt> flattenAll(const std::vector<Segment>& src, float tol = 0.5f) {
    Path p = flattenSegments(src, tol);
    std::vector<Pt> pts;
    for (auto& sp : p.subpaths)
        for (auto [x, y] : sp) pts.emplace_back(x, y);
    return pts;
}

std::vector<BoolRing> ringsOf(const std::vector<Segment>& src, float tol = 0.5f) {
    Path p = flattenSegments(src, tol);
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

// Params rect "x,y,w,h" (fallback: subject bbox) as one ring.
BoolRing paramRect(const Params& p, const std::vector<BoolRing>& subject) {
    double x = 0, y = 0, w = 0, h = 0;
    std::string s = p.getString("rect", "");
    if (!s.empty()) {
        try {
            size_t c1 = s.find(','), c2 = s.find(',', c1 + 1), c3 = s.find(',', c2 + 1);
            x = std::stod(s.substr(0, c1));
            y = std::stod(s.substr(c1 + 1, c2 - c1 - 1));
            w = std::stod(s.substr(c2 + 1, c3 - c2 - 1));
            h = std::stod(s.substr(c3 + 1));
        } catch (...) {
            w = h = 0;
        }
    }
    if (!(w > 0 && h > 0) && !subject.empty()) {
        double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
        for (auto& r : subject)
            for (auto [px, py] : r) {
                x0 = std::min(x0, px);
                y0 = std::min(y0, py);
                x1 = std::max(x1, px);
                y1 = std::max(y1, py);
            }
        x = x0;
        y = y0;
        w = (x1 - x0) / 2;
        h = y1 - y0;
    }
    return {{{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}};
}

std::vector<Segment> circleAt(double cx, double cy, double r) {
    if (!(r > 1e-9)) return {};
    const double k = 0.5522848;
    std::vector<Segment> out{Segment{Segment::Kind::MoveTo, (float)(cx + r), (float)cy}};
    out.push_back(Segment{Segment::Kind::CubicTo, (float)(cx + r), (float)(cy + r * k),
                          (float)(cx + r * k), (float)(cy + r), (float)cx, (float)(cy + r)});
    out.push_back(Segment{Segment::Kind::CubicTo, (float)(cx - r * k), (float)(cy + r),
                          (float)(cx - r), (float)(cy + r * k), (float)(cx - r), (float)cy});
    out.push_back(Segment{Segment::Kind::CubicTo, (float)(cx - r), (float)(cy - r * k),
                          (float)(cx - r * k), (float)(cy - r), (float)cx, (float)(cy - r)});
    out.push_back(Segment{Segment::Kind::CubicTo, (float)(cx + r * k), (float)(cy - r),
                          (float)(cx + r), (float)(cy - r * k), (float)(cx + r), (float)cy});
    out.push_back(Segment{Segment::Kind::Close});
    return out;
}

// Moments-based ellipse fit (centroid + covariance axes): stable closed form
// for the pts2ellipse/ellipse_5pts constructors.
std::vector<Segment> ellipseFit(const std::vector<Pt>& pts) {
    if (pts.size() < 5) return {};
    double mx = 0, my = 0;
    for (auto [x, y] : pts) {
        mx += x;
        my += y;
    }
    mx /= pts.size();
    my /= pts.size();
    double sxx = 0, sxy = 0, syy = 0;
    for (auto [x, y] : pts) {
        sxx += (x - mx) * (x - mx);
        sxy += (x - mx) * (y - my);
        syy += (y - my) * (y - my);
    }
    sxx /= pts.size();
    sxy /= pts.size();
    syy /= pts.size();
    double tr = sxx + syy, det = sxx * syy - sxy * sxy;
    if (det <= 1e-12) return {};
    double disc = std::sqrt(std::max(0.0, tr * tr / 4 - det));
    double l1 = tr / 2 + disc, l2 = tr / 2 - disc;
    double a1 = std::atan2(l1 - sxx, sxy);
    double rx = 2 * std::sqrt(std::max(l1, 1e-9)), ry = 2 * std::sqrt(std::max(l2, 1e-9));
    // Four cubic quarters from axis endpoints, rotated by the eigen angle.
    const double k = 0.5522848;
    double ca = std::cos(a1), sa = std::sin(a1);
    std::vector<Segment> out;
    std::vector<Pt> e{{rx, 0}, {0, ry}, {-rx, 0}, {0, -ry}};
    auto rot = [&](Pt q) {
        return Pt{mx + q.first * ca - q.second * sa, my + q.first * sa + q.second * ca};
    };
    auto [s0x, s0y] = rot(e[0]);
    out.push_back(Segment{Segment::Kind::MoveTo, (float)s0x, (float)s0y});
    for (int q = 0; q < 4; q++) {
        // Direct kappa construction per quadrant (explicit, no index math).
        double c1x = 0, c1y = 0, c2x = 0, c2y = 0, ex = 0, ey = 0;
        if (q == 0) {
            c1x = rx;
            c1y = ry * k;
            c2x = rx * k;
            c2y = ry;
            ex = 0;
            ey = ry;
        } else if (q == 1) {
            c1x = -rx * k;
            c1y = ry;
            c2x = -rx;
            c2y = ry * k;
            ex = -rx;
            ey = 0;
        } else if (q == 2) {
            c1x = -rx;
            c1y = -ry * k;
            c2x = -rx * k;
            c2y = -ry;
            ex = 0;
            ey = -ry;
        } else {
            c1x = rx * k;
            c1y = -ry;
            c2x = rx;
            c2y = -ry * k;
            ex = rx;
            ey = 0;
        }
        auto R = [&](double vx, double vy) {
            return Pt{mx + vx * ca - vy * sa, my + vx * sa + vy * ca};
        };
        auto [q1x, q1y] = R(c1x, c1y);
        auto [q2x, q2y] = R(c2x, c2y);
        auto [qex, qey] = R(ex, ey);
        out.push_back(Segment{Segment::Kind::CubicTo, (float)q1x, (float)q1y, (float)q2x,
                              (float)q2y, (float)qex, (float)qey});
    }
    out.push_back(Segment{Segment::Kind::Close});
    return out;
}

struct ConvertEffect : Effect {
    ConvertEffect(EffectType t, const Params& p) {
        type = t;
        params = p;
    }
    std::vector<Segment> apply(const std::vector<Segment>& src) const override {
        if (src.empty()) return src;
        switch (type) {
            case EffectType::BoundingBox: {
                auto pts = flattenAll(src);
                if (pts.empty()) return src;
                double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
                for (auto [x, y] : pts) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
                if (!(x1 > x0 && y1 > y0)) return src;
                return {Segment{Segment::Kind::MoveTo, (float)x0, (float)y0},
                        Segment{Segment::Kind::LineTo, (float)x1, (float)y0},
                        Segment{Segment::Kind::LineTo, (float)x1, (float)y1},
                        Segment{Segment::Kind::LineTo, (float)x0, (float)y1},
                        Segment{Segment::Kind::Close}};
            }
            case EffectType::Circle3Pts:
            case EffectType::Pts2Ellipse:
            case EffectType::Ellipse5Pts: {
                auto pts = flattenAll(src);
                if (type == EffectType::Circle3Pts) {
                    if (pts.size() < 3) return src;
                    auto [x0, y0] = pts[0];
                    auto [x1, y1] = pts[1];
                    auto [x2, y2] = pts[2];
                    double d = 2 * (x0 * (y1 - y2) + x1 * (y2 - y0) + x2 * (y0 - y1));
                    if (std::abs(d) < 1e-9) return src;
                    double s0 = x0 * x0 + y0 * y0, s1 = x1 * x1 + y1 * y1,
                           s2 = x2 * x2 + y2 * y2;
                    double cx = (s0 * (y1 - y2) + s1 * (y2 - y0) + s2 * (y0 - y1)) / d;
                    double cy = (s0 * (x2 - x1) + s1 * (x0 - x2) + s2 * (x1 - x0)) / d;
                    return circleAt(cx, cy, std::hypot(x0 - cx, y0 - cy));
                }
                auto fit = ellipseFit(pts);
                return fit.empty() ? src : fit;
            }
            case EffectType::CircleWithRadius: {
                Pt c{0, 0};
                getPoint(params, "center", c);
                return circleAt(c.first, c.second, params.getDouble("radius", 20.0));
            }
            case EffectType::LineSegment: {
                Pt a{0, 0}, b{10, 10};
                getPoint(params, "a", a);
                getPoint(params, "b", b);
                return {Segment{Segment::Kind::MoveTo, (float)a.first, (float)a.second},
                        Segment{Segment::Kind::LineTo, (float)b.first, (float)b.second}};
            }
            case EffectType::Parallel: {
                double dist = params.getDouble("distance", 10.0);
                Path p = flattenSegments(src, 0.5f);
                std::vector<Segment> out;
                for (auto& sp : p.subpaths) {
                    bool first = true;
                    for (size_t i = 0; i < sp.size(); i++) {
                        size_t j = (i + 1) % sp.size(), k = (i + sp.size() - 1) % sp.size();
                        double dx = sp[j].first - sp[k].first;
                        double dy = sp[j].second - sp[k].second;
                        double l = std::hypot(dx, dy);
                        double nx = 0, ny = 0;
                        if (l > 1e-9) {
                            nx = -dy / l;
                            ny = dx / l;
                        }
                        float X = sp[i].first + (float)(nx * dist);
                        float Y = sp[i].second + (float)(ny * dist);
                        out.push_back(Segment{first ? Segment::Kind::MoveTo
                                                   : Segment::Kind::LineTo,
                                              X, Y});
                        first = false;
                    }
                }
                return out.empty() ? src : out;
            }
            case EffectType::PerpBisector:
            case EffectType::AngleBisector: {
                Pt a{0, 0}, b{10, 0}, c{10, 10};
                getPoint(params, "a", a);
                getPoint(params, "b", b);
                getPoint(params, "c", c);
                double ang;
                Pt mid;
                if (type == EffectType::PerpBisector) {
                    mid = {(a.first + b.first) / 2, (a.second + b.second) / 2};
                    ang = std::atan2(b.second - a.second, b.first - a.first) +
                          3.14159265358979 / 2;
                } else {
                    mid = b;
                    double a1 = std::atan2(a.second - b.second, a.first - b.first);
                    double a2 = std::atan2(c.second - b.second, c.first - b.first);
                    double m = (a1 + a2) / 2;
                    // Take the interior bisector (pointing between the rays).
                    double fx = std::cos(m), fy = std::sin(m);
                    double dot = fx * (std::cos(a1) + std::cos(a2)) +
                                 fy * (std::sin(a1) + std::sin(a2));
                    if (dot < 0) m += 3.14159265358979;
                    ang = m;
                }
                double L = params.getDouble("length", 40.0);
                return {Segment{Segment::Kind::MoveTo,
                                (float)(mid.first - L * std::cos(ang)),
                                (float)(mid.second - L * std::sin(ang))},
                        Segment{Segment::Kind::LineTo,
                                (float)(mid.first + L * std::cos(ang)),
                                (float)(mid.second + L * std::sin(ang))}};
            }
            case EffectType::TangentToCurve: {
                Pt ext{0, 0};
                getPoint(params, "from", ext);
                auto pts = flattenAll(src);
                if (pts.size() < 2) return src;
                // Sample tangent points: radius ⟂ curve tangent.
                double best1 = 1e100, best2 = 1e100;
                Pt t1 = pts[0], t2 = pts[0];
                for (size_t i = 1; i + 1 < pts.size(); i++) {
                    double tx = pts[i + 1].first - pts[i - 1].first;
                    double ty = pts[i + 1].second - pts[i - 1].second;
                    double rx = pts[i].first - ext.first, ry = pts[i].second - ext.second;
                    double cross = std::abs(tx * ry - ty * rx) /
                                   (std::hypot(tx, ty) * std::hypot(rx, ry) + 1e-12);
                    if (cross < best1) {
                        best2 = best1;
                        t2 = t1;
                        best1 = cross;
                        t1 = pts[i];
                    } else if (cross < best2) {
                        best2 = cross;
                        t2 = pts[i];
                    }
                }
                return {Segment{Segment::Kind::MoveTo, (float)ext.first,
                                (float)ext.second},
                        Segment{Segment::Kind::LineTo, (float)t1.first, (float)t1.second},
                        Segment{Segment::Kind::MoveTo, (float)ext.first,
                                (float)ext.second},
                        Segment{Segment::Kind::LineTo, (float)t2.first, (float)t2.second}};
            }
            case EffectType::Transform2Pts: {
                Pt p0{0, 0}, p1{10, 0}, q0{0, 0}, q1{10, 0};
                getPoint(params, "p0", p0);
                getPoint(params, "p1", p1);
                getPoint(params, "q0", q0);
                getPoint(params, "q1", q1);
                double dx = p1.first - p0.first, dy = p1.second - p0.second;
                double ex = q1.first - q0.first, ey = q1.second - q0.second;
                double sl = std::hypot(dx, dy), dl = std::hypot(ex, ey);
                double sc = sl > 1e-9 ? dl / sl : 1.0;
                double sa = std::atan2(dy, dx), da = std::atan2(ey, ex);
                double rot = da - sa;
                double cr = std::cos(rot) * sc, sr = std::sin(rot) * sc;
                Path p = flattenSegments(src, 0.5f);
                std::vector<Segment> out;
                for (auto& sp : p.subpaths) {
                    bool first = true;
                    for (auto [x, y] : sp) {
                        double lx = x - p0.first, ly = y - p0.second;
                        float X = (float)(q0.first + lx * cr - ly * sr);
                        float Y = (float)(q0.second + lx * sr + ly * cr);
                        out.push_back(Segment{first ? Segment::Kind::MoveTo
                                                   : Segment::Kind::LineTo,
                                              X, Y});
                        first = false;
                    }
                }
                return out.empty() ? src : out;
            }
            case EffectType::BoolOp: {
                auto subject = ringsOf(src);
                if (subject.empty()) return src;
                BoolRing clip{paramRect(params, subject)};
                std::string op = params.getString("op", "union");
                BoolOp bop = BoolOp::Union;
                if (op == "intersection") bop = BoolOp::Intersection;
                else if (op == "difference") bop = BoolOp::Difference;
                else if (op == "xor") bop = BoolOp::Xor;
                auto r = booleanOp(subject, {clip}, bop);
                return r.empty() ? src : ringsToSegments(r);
            }
            case EffectType::Slice: {
                auto subject = ringsOf(src);
                if (subject.empty()) return src;
                double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
                for (auto& r : subject)
                    for (auto [x, y] : r) {
                        x0 = std::min(x0, x);
                        y0 = std::min(y0, y);
                        x1 = std::max(x1, x);
                        y1 = std::max(y1, y);
                    }
                bool horiz = params.getString("axis", "x") != "y";
                double pos = params.getDouble(
                    "pos", horiz ? (x0 + x1) / 2 : (y0 + y1) / 2);
                double pad = 1e6;
                BoolRing keepA = horiz ? BoolRing{{{x0 - pad, y0 - pad},
                                                   {pos, y0 - pad},
                                                   {pos, y1 + pad},
                                                   {x0 - pad, y1 + pad}}}
                                       : BoolRing{{{x0 - pad, y0 - pad},
                                                   {x1 + pad, y0 - pad},
                                                   {x1 + pad, pos},
                                                   {x0 - pad, pos}}};
                BoolRing keepB = horiz ? BoolRing{{{pos, y0 - pad},
                                                   {x1 + pad, y0 - pad},
                                                   {x1 + pad, y1 + pad},
                                                   {pos, y1 + pad}}}
                                       : BoolRing{{{x0 - pad, pos},
                                                   {x1 + pad, pos},
                                                   {x1 + pad, y1 + pad},
                                                   {x0 - pad, y1 + pad}}};
                auto ra = booleanOp(subject, {keepA}, BoolOp::Intersection);
                auto rb = booleanOp(subject, {keepB}, BoolOp::Intersection);
                ra.insert(ra.end(), rb.begin(), rb.end());
                return ra.empty() ? src : ringsToSegments(ra);
            }
            case EffectType::Powerclip:
            case EffectType::Powermask: {
                auto subject = ringsOf(src);
                if (subject.empty()) return src;
                BoolRing clip{paramRect(params, subject)};
                auto r = booleanOp(subject, {clip}, BoolOp::Intersection);
                return r.empty() ? src : ringsToSegments(r);
            }
            case EffectType::Knot: {
                double gap = params.getDouble("gap", 4.0);
                Path p = flattenSegments(src, 0.25f);
                std::vector<Segment> out;
                for (auto& sp : p.subpaths) {
                    if (sp.size() < 8) {
                        if (!sp.empty()) {
                            out.push_back(Segment{Segment::Kind::MoveTo, sp[0].first,
                                                  sp[0].second});
                            for (size_t i = 1; i < sp.size(); i++)
                                out.push_back(Segment{Segment::Kind::LineTo, sp[i].first,
                                                      sp[i].second});
                        }
                        continue;
                    }
                    // Self-intersections (segment pairs): cut gaps around them.
                    std::vector<double> cuts;  // arclength positions
                    std::vector<double> cum{0};
                    for (size_t i = 1; i < sp.size(); i++)
                        cum.push_back(cum.back() +
                                      std::hypot(sp[i].first - sp[i - 1].first,
                                                 sp[i].second - sp[i - 1].second));
                    for (size_t i = 0; i + 1 < sp.size(); i++)
                        for (size_t j = i + 3; j + 1 < sp.size(); j++) {
                            double d = (sp[i + 1].first - sp[i].first) *
                                           (sp[j + 1].second - sp[j].second) -
                                       (sp[i + 1].second - sp[i].second) *
                                           (sp[j + 1].first - sp[j].first);
                            if (std::abs(d) < 1e-9) continue;
                            double t = ((sp[j].first - sp[i].first) *
                                            (sp[j + 1].second - sp[j].second) -
                                        (sp[j].second - sp[i].second) *
                                            (sp[j + 1].first - sp[j].first)) /
                                       d;
                            double u = ((sp[j].first - sp[i].first) *
                                            (sp[i + 1].second - sp[i].second) -
                                        (sp[j].second - sp[i].second) *
                                            (sp[i + 1].first - sp[i].first)) /
                                       d;
                            if (t > 0 && t < 1 && u > 0 && u < 1)
                                cuts.push_back(cum[i] + t * (cum[i + 1] - cum[i]));
                        }
                    std::sort(cuts.begin(), cuts.end());
                    // Emit runs, skipping alternating gap windows (over/under).
                    double total = cum.back();
                    size_t ci = 0;
                    bool skip = false;
                    double runStart = 0;
                    auto emitRun = [&](double a, double b) {
                        if (b - a < 1e-6) return;
                        bool first = true;
                        for (size_t i = 0; i < sp.size(); i++) {
                            if (cum[i] < a - 1e-9 || cum[i] > b + 1e-9) continue;
                            out.push_back(Segment{first ? Segment::Kind::MoveTo
                                                       : Segment::Kind::LineTo,
                                                  sp[i].first, sp[i].second});
                            first = false;
                        }
                    };
                    for (double c : cuts) {
                        double ga = std::max(0.0, c - gap / 2),
                               gb = std::min(total, c + gap / 2);
                        if (skip) {
                            runStart = gb;
                        } else {
                            emitRun(runStart, ga);
                            runStart = gb;
                        }
                        skip = !skip;
                        ci++;
                        (void)ci;
                    }
                    emitRun(runStart, total);
                }
                return out.empty() ? src : out;
            }
            case EffectType::CurveStitch: {
                Path p = flattenSegments(src, 0.5f);
                if (p.subpaths.size() < 2) {
                    // Single spine: fence across halves.
                    auto pts = flattenAll(src);
                    if (pts.size() < 4) return src;
                    size_t h = pts.size() / 2;
                    std::vector<Segment> out;
                    for (size_t i = 0; i < h && i + h < pts.size(); i++) {
                        out.push_back(Segment{Segment::Kind::MoveTo, (float)pts[i].first,
                                              (float)pts[i].second});
                        out.push_back(Segment{Segment::Kind::LineTo,
                                              (float)pts[i + h].first,
                                              (float)pts[i + h].second});
                    }
                    return out.empty() ? src : out;
                }
                auto& A = p.subpaths[0];
                auto& B = p.subpaths[1];
                std::vector<Segment> out;
                size_t n = std::min(A.size(), B.size());
                for (size_t i = 0; i < n; i++) {
                    out.push_back(Segment{Segment::Kind::MoveTo, A[i].first, A[i].second});
                    out.push_back(Segment{Segment::Kind::LineTo, B[i].first, B[i].second});
                }
                return out.empty() ? src : out;
            }
            case EffectType::FillBetweenMany:
            case EffectType::FillBetweenStrokes: {
                Path p = flattenSegments(src, 0.5f);
                if (p.subpaths.size() < 2) return src;
                auto& A = p.subpaths.front();
                auto& B = p.subpaths.back();
                std::vector<Segment> out{
                    Segment{Segment::Kind::MoveTo, A.front().first, A.front().second}};
                for (size_t i = 1; i < A.size(); i++)
                    out.push_back(
                        Segment{Segment::Kind::LineTo, A[i].first, A[i].second});
                for (size_t i = B.size(); i-- > 0;)
                    out.push_back(
                        Segment{Segment::Kind::LineTo, B[i].first, B[i].second});
                out.push_back(Segment{Segment::Kind::Close});
                return out;
            }
            case EffectType::AttachPath: {
                Pt anchor{0, 0};
                getPoint(params, "anchor", anchor);
                auto pts = flattenAll(src);
                if (pts.empty()) return src;
                double dx = anchor.first - pts[0].first;
                double dy = anchor.second - pts[0].second;
                Path p = flattenSegments(src, 0.5f);
                std::vector<Segment> out;
                for (auto& sp : p.subpaths) {
                    bool first = true;
                    for (auto [x, y] : sp) {
                        out.push_back(Segment{first ? Segment::Kind::MoveTo
                                                   : Segment::Kind::LineTo,
                                              (float)(x + dx), (float)(y + dy)});
                        first = false;
                    }
                }
                return out.empty() ? src : out;
            }
            case EffectType::MeasureSegments:
            case EffectType::PathLength: {
                double step = params.getDouble(
                    "step", type == EffectType::PathLength ? 20.0 : 10.0);
                double tick = params.getDouble("tick", 3.0);
                Path p = flattenSegments(src, 0.5f);
                std::vector<Segment> out;
                for (auto& sp : p.subpaths) {
                    if (sp.size() < 2) continue;
                    out.push_back(
                        Segment{Segment::Kind::MoveTo, sp[0].first, sp[0].second});
                    for (size_t i = 1; i < sp.size(); i++)
                        out.push_back(
                            Segment{Segment::Kind::LineTo, sp[i].first, sp[i].second});
                    // Ticks every `step` along arclength.
                    std::vector<double> cum{0};
                    for (size_t i = 1; i < sp.size(); i++)
                        cum.push_back(cum.back() +
                                      std::hypot(sp[i].first - sp[i - 1].first,
                                                 sp[i].second - sp[i - 1].second));
                    for (double s = step; s < cum.back(); s += step) {
                        size_t i = 1;
                        while (i < cum.size() && cum[i] < s) i++;
                        if (i >= sp.size()) break;
                        double t = (s - cum[i - 1]) / (cum[i] - cum[i - 1] + 1e-12);
                        double mx = sp[i - 1].first + (sp[i].first - sp[i - 1].first) * t;
                        double my = sp[i - 1].second + (sp[i].second - sp[i - 1].second) * t;
                        double dx = sp[i].first - sp[i - 1].first;
                        double dy = sp[i].second - sp[i - 1].second;
                        double l = std::hypot(dx, dy) + 1e-12;
                        out.push_back(Segment{Segment::Kind::MoveTo,
                                              (float)(mx - dy / l * tick),
                                              (float)(my + dx / l * tick)});
                        out.push_back(Segment{Segment::Kind::LineTo,
                                              (float)(mx + dy / l * tick),
                                              (float)(my - dx / l * tick)});
                    }
                }
                return out.empty() ? src : out;
            }
            case EffectType::RecursiveSkeleton: {
                // Chord-midpoint skeleton: pair opposite samples of each
                // closed loop (or thirds of an open spine) into a centerline.
                Path p = flattenSegments(src, 0.5f);
                std::vector<Segment> out;
                for (auto& sp : p.subpaths) {
                    if (sp.size() < 6) continue;
                    size_t h = sp.size() / 2;
                    bool first = true;
                    for (size_t i = 0; i < h; i++) {
                        float X = (sp[i].first + sp[(i + h) % sp.size()].first) / 2;
                        float Y = (sp[i].second + sp[(i + h) % sp.size()].second) / 2;
                        out.push_back(Segment{first ? Segment::Kind::MoveTo
                                                   : Segment::Kind::LineTo,
                                              X, Y});
                        first = false;
                    }
                }
                return out.empty() ? src : out;
            }
            case EffectType::DynaStroke: {
                // Pressure-simulated widths (sine swell), expanded outline.
                Path p = flattenSegments(src, 0.5f);
                float base = (float)params.getDouble("width", 4.0);
                float swell = (float)params.getDouble("swell", 0.6);
                std::vector<std::vector<float>> widths;
                for (auto& sp : p.subpaths) {
                    std::vector<float> w;
                    for (size_t i = 0; i < sp.size(); i++) {
                        double t = sp.size() > 1 ? (double)i / (sp.size() - 1) : 0;
                        w.push_back(base * (1 + swell * std::sin(t * 6.2831)));
                    }
                    widths.push_back(w);
                }
                StrokeStyle st(base);
                st.cap = LineCap::Round;
                st.join = LineJoin::Round;
                Path stroked = strokeVariable(p, widths, st, {}, 0);
                std::vector<Segment> out;
                for (auto& sp : stroked.subpaths) {
                    if (sp.empty()) continue;
                    out.push_back(
                        Segment{Segment::Kind::MoveTo, sp[0].first, sp[0].second});
                    for (size_t i = 1; i < sp.size(); i++)
                        out.push_back(
                            Segment{Segment::Kind::LineTo, sp[i].first, sp[i].second});
                    out.push_back(Segment{Segment::Kind::Close});
                }
                return out.empty() ? src : out;
            }
            case EffectType::EmbroideryStitch: {
                // Satin zigzag along the spine + straight underlay.
                double stitch = params.getDouble("stitch", 4.0);
                double width = params.getDouble("width", 6.0);
                auto pts = flattenAll(src);
                if (pts.size() < 2) return src;
                std::vector<double> cum{0};
                for (size_t i = 1; i < pts.size(); i++)
                    cum.push_back(cum.back() + std::hypot(pts[i].first - pts[i - 1].first,
                                                          pts[i].second - pts[i - 1].second));
                std::vector<Segment> out{
                    Segment{Segment::Kind::MoveTo, (float)pts[0].first,
                            (float)pts[0].second}};
                for (size_t i = 1; i < pts.size(); i++)
                    out.push_back(Segment{Segment::Kind::LineTo, (float)pts[i].first,
                                          (float)pts[i].second});
                bool side = false;
                for (double s = 0; s < cum.back(); s += stitch) {
                    size_t i = 1;
                    while (i < cum.size() && cum[i] < s) i++;
                    if (i >= pts.size()) break;
                    double t = (s - cum[i - 1]) / (cum[i] - cum[i - 1] + 1e-12);
                    double mx = pts[i - 1].first + (pts[i].first - pts[i - 1].first) * t;
                    double my = pts[i - 1].second + (pts[i].second - pts[i - 1].second) * t;
                    double dx = pts[i].first - pts[i - 1].first;
                    double dy = pts[i].second - pts[i - 1].second;
                    double l = std::hypot(dx, dy) + 1e-12;
                    double sgn = side ? 1 : -1;
                    side = !side;
                    out.push_back(Segment{Segment::Kind::MoveTo, (float)mx, (float)my});
                    out.push_back(Segment{Segment::Kind::LineTo,
                                          (float)(mx - dy / l * width * sgn),
                                          (float)(my + dx / l * width * sgn)});
                }
                return out;
            }
            case EffectType::Lattice: {
                // Legacy lattice: same sine warp as lattice2 (shared params).
                double amt = params.getDouble("amount", 0.3);
                double freq = params.getDouble("freq", 3.0);
                auto pts = flattenAll(src);
                if (pts.empty()) return src;
                double x0 = 1e100, y0 = 1e100, x1 = -1e100, y1 = -1e100;
                for (auto [x, y] : pts) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
                double w = std::max(1.0, x1 - x0), h = std::max(1.0, y1 - y0);
                std::vector<Segment> out{Segment{Segment::Kind::MoveTo, 0, 0}};
                out.clear();
                bool first = true;
                for (auto [x, y] : pts) {
                    double u = (x - x0) / w, v = (y - y0) / h;
                    float X = (float)(x + std::sin(v * freq * 6.2831) * amt * w * 0.1);
                    float Y = (float)(y + std::sin(u * freq * 6.2831) * amt * h * 0.1);
                    out.push_back(Segment{first ? Segment::Kind::MoveTo
                                               : Segment::Kind::LineTo,
                                          X, Y});
                    first = false;
                }
                return out.empty() ? src : out;
            }
            default:
                // Ruler, ShowHandles, TextLabel, CloneOriginal: canvas or
                // document context required — record-only for SVG round-trip.
                return src;
        }
    }
};

}  // namespace

std::shared_ptr<Effect> makeConvertEffect(EffectType t, const Params& p) {
    return std::make_shared<ConvertEffect>(t, p);
}

}  // namespace pittore::vector::lpe
