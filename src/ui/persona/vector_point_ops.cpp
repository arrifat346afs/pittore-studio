#include "ui/persona/vector_point_ops.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

#include "engine/vector/vector_art.h"

namespace pittore::ui {
namespace {

using pittore::vector::Path;
using pittore::vector::PathBuilder;
using pittore::vector::Segment;
using pittore::vector::StrokeStyle;

constexpr double kPi = 3.14159265358979323846;

bool isAnchor(const Segment& s) {
    return s.kind == Segment::Kind::MoveTo || s.kind == Segment::Kind::LineTo ||
           s.kind == Segment::Kind::CubicTo;
}

QPointF segEnd(const Segment& s) { return QPointF(s.x, s.y); }

// Cubic evaluation (p0 start, c1, c2, p3 end).
QPointF cubicAt(const QPointF& p0, const QPointF& c1, const QPointF& c2,
                const QPointF& p3, double t) {
    const double u = 1.0 - t;
    return QPointF(u * u * u * p0.x() + 3 * u * u * t * c1.x() +
                       3 * u * t * t * c2.x() + t * t * t * p3.x(),
                   u * u * u * p0.y() + 3 * u * u * t * c1.y() +
                       3 * u * t * t * c2.y() + t * t * t * p3.y());
}

// de Casteljau split: first/second halves as (p0, c1, c2, p3) tuples.
void splitCubic(const QPointF& p0, const QPointF& c1, const QPointF& c2,
                const QPointF& p3, double t, QPointF* a1, QPointF* a2,
                QPointF* a3, QPointF* b1, QPointF* b2, QPointF* b3) {
    const QPointF q0 = p0 + (c1 - p0) * t;
    const QPointF q1 = c1 + (c2 - c1) * t;
    const QPointF q2 = c2 + (p3 - c2) * t;
    const QPointF r0 = q0 + (q1 - q0) * t;
    const QPointF r1 = q1 + (q2 - q1) * t;
    const QPointF m = r0 + (r1 - r0) * t;
    *a1 = q0;
    *a2 = r0;
    *a3 = m;
    *b1 = r1;
    *b2 = q2;
    *b3 = m;  // b3 unused by callers (end is p3); kept symmetric
    (void)b3;
}

double polyLength(const std::vector<QPointF>& pts) {
    double len = 0.0;
    for (std::size_t i = 1; i < pts.size(); ++i)
        len += std::hypot(pts[i].x() - pts[i - 1].x(),
                          pts[i].y() - pts[i - 1].y());
    return len;
}

std::vector<QPointF> flattenCubic(const QPointF& p0, const QPointF& c1,
                                  const QPointF& c2, const QPointF& p3,
                                  double tol) {
    // Adaptive subdivision stack (depth-capped).
    std::vector<QPointF> out{p0};
    struct Item {
        QPointF a, b, c, d;
        int depth;
    };
    std::vector<Item> stack{{p0, c1, c2, p3, 0}};
    while (!stack.empty()) {
        Item it = stack.back();
        stack.pop_back();
        const QPointF m = cubicAt(it.a, it.b, it.c, it.d, 0.5);
        const QPointF chord = (it.a + it.d) * 0.5;
        if (it.depth >= 10 ||
            (std::hypot(m.x() - chord.x(), m.y() - chord.y()) <= tol &&
             it.depth > 0)) {
            out.push_back(it.d);
            continue;
        }
        QPointF a1, a2, a3, b1, b2, b3;
        splitCubic(it.a, it.b, it.c, it.d, 0.5, &a1, &a2, &a3, &b1, &b2, &b3);
        stack.push_back({a3, b1, b2, it.d, it.depth + 1});
        stack.push_back({it.a, a1, a2, a3, it.depth + 1});
    }
    return out;
}

bool segIntersect(const QPointF& p, const QPointF& q, const QPointF& a,
                  const QPointF& b, QPointF* hit) {
    const double d = (q.x() - p.x()) * (b.y() - a.y()) -
                     (q.y() - p.y()) * (b.x() - a.x());
    if (std::abs(d) <= 1e-12) return false;
    const double t = ((a.x() - p.x()) * (b.y() - a.y()) -
                      (a.y() - p.y()) * (b.x() - a.x())) /
                     d;
    const double u = ((a.x() - p.x()) * (q.y() - p.y()) -
                      (a.y() - p.y()) * (q.x() - p.x())) /
                     d;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return false;
    if (hit) *hit = QPointF(p.x() + t * (q.x() - p.x()),
                            p.y() + t * (q.y() - p.y()));
    return true;
}

}  // namespace

QPointF nodeAnchorCentroid(const pittore::vector::ArtNode& node) {
    QPointF sum;
    int n = 0;
    for (const Segment& s : node.segments) {
        if (!isAnchor(s)) continue;
        sum += QPointF(s.x, s.y);
        ++n;
    }
    return n > 0 ? sum / n : QPointF();
}

void transformNodePoints(pittore::vector::ArtNode& node,
                         const QPointF& centre, double factor,
                         double degrees) {
    const double a = degrees * kPi / 180.0;
    const double c = std::cos(a) * factor, s = std::sin(a) * factor;
    auto xform = [&](float x, float y, float* ox, float* oy) {
        const double dx = x - centre.x(), dy = y - centre.y();
        *ox = static_cast<float>(centre.x() + dx * c - dy * s);
        *oy = static_cast<float>(centre.y() + dx * s + dy * c);
    };
    for (Segment& sg : node.segments) {
        if (sg.kind == Segment::Kind::CubicTo) {
            xform(sg.c1x, sg.c1y, &sg.c1x, &sg.c1y);
            xform(sg.c2x, sg.c2y, &sg.c2x, &sg.c2y);
        }
        if (isAnchor(sg)) xform(sg.x, sg.y, &sg.x, &sg.y);
    }
}

bool roundNodeCorner(pittore::vector::ArtNode& node, int seg, double radius) {
    if (!(radius > 0.0) || seg < 0 ||
        seg >= static_cast<int>(node.segments.size()))
        return false;
    if (!isAnchor(node.segments[static_cast<std::size_t>(seg)])) return false;
    const bool isClosed =
        !node.segments.empty() &&
        node.segments.back().kind == Segment::Kind::Close;
    int lastAnchor = -1;
    for (int i = 0; i < static_cast<int>(node.segments.size()); ++i)
        if (isAnchor(node.segments[static_cast<std::size_t>(i)])) lastAnchor = i;
    // Closed loops have an implicit last→first edge: make it explicit so
    // the first/last corners trim like any other (the extra span overdraws
    // the same edge — raster-identical). Shapes built by artNodeFromShape
    // already close explicitly (last anchor == first): no extra span then,
    // just point the incoming edge at the closing cubic; a press on the
    // duplicate closing anchor rounds as the first corner.
    int inSpanIdx = seg;
    if (isClosed && (seg == 0 || seg == lastAnchor)) {
        const QPointF start = segEnd(node.segments.front());
        const QPointF lastPt =
            segEnd(node.segments[static_cast<std::size_t>(lastAnchor)]);
        const bool alreadyClosed =
            std::hypot(lastPt.x() - start.x(), lastPt.y() - start.y()) < 1e-9;
        if (alreadyClosed) {
            if (seg == lastAnchor) seg = 0;
            inSpanIdx = lastAnchor;
        } else {
            node.segments.pop_back();  // trailing Close
            Segment e;
            e.kind = Segment::Kind::LineTo;
            e.x = static_cast<float>(start.x());
            e.y = static_cast<float>(start.y());
            node.segments.push_back(e);
            Segment c;
            c.kind = Segment::Kind::Close;
            node.segments.push_back(c);
            lastAnchor = static_cast<int>(node.segments.size()) - 2;
            if (seg == 0) inSpanIdx = lastAnchor;
        }
    }
    const QPointF anchor = segEnd(node.segments[static_cast<std::size_t>(seg)]);
    // Neighbour anchors (Close carries no point). The next anchor wraps on
    // closed loops; the previous comes from the incoming span (inSpanIdx),
    // which for seg==0 is the explicit closing edge made above.
    QPointF prev, next;
    bool hasPrev = false, hasNext = false;
    for (int i = inSpanIdx - 1; i >= 0; --i) {
        if (isAnchor(node.segments[static_cast<std::size_t>(i)])) {
            prev = segEnd(node.segments[static_cast<std::size_t>(i)]);
            hasPrev = true;
            break;
        }
    }
    int nextSeg = -1;
    for (int i = seg + 1; i < static_cast<int>(node.segments.size()); ++i) {
        if (isAnchor(node.segments[static_cast<std::size_t>(i)])) {
            next = segEnd(node.segments[static_cast<std::size_t>(i)]);
            nextSeg = i;
            hasNext = true;
            break;
        }
    }
    if (!hasNext && isClosed) {
        next = segEnd(node.segments.front());
        nextSeg = 0;
        hasNext = true;
    }
    if (!hasPrev || !hasNext) return false;
    // Sample both adjacent spans (flattened: lines and cubics share one
    // code path). inPts runs prev→anchor, outPts anchor→next.
    const Segment& inSpan = node.segments[static_cast<std::size_t>(inSpanIdx)];
    std::vector<QPointF> inPts{prev}, outPts{anchor};
    if (inSpan.kind == Segment::Kind::CubicTo) {
        auto flat =
            flattenCubic(prev, QPointF(inSpan.c1x, inSpan.c1y),
                         QPointF(inSpan.c2x, inSpan.c2y), anchor, 0.25);
        inPts.insert(inPts.end(), flat.begin() + 1, flat.end());
    } else if (inSpan.kind == Segment::Kind::LineTo) {
        inPts.push_back(anchor);
    } else {
        return false;  // MoveTo carries no incoming span
    }
    // The outgoing edge is segments[nextSeg], except the wrapped closed-loop
    // edge (nextSeg==0: pen position after the fillet draws it implicitly).
    const bool implicitOut = (nextSeg == 0);
    if (!implicitOut) {
        const Segment& outSpan =
            node.segments[static_cast<std::size_t>(nextSeg)];
        if (outSpan.kind == Segment::Kind::CubicTo) {
            auto flat = flattenCubic(anchor, QPointF(outSpan.c1x, outSpan.c1y),
                                     QPointF(outSpan.c2x, outSpan.c2y), next,
                                     0.25);
            outPts.insert(outPts.end(), flat.begin() + 1, flat.end());
        } else if (outSpan.kind == Segment::Kind::LineTo) {
            outPts.push_back(next);
        } else {
            return false;
        }
    } else {
        outPts.push_back(next);
    }
    const double inLen = polyLength(inPts), outLen = polyLength(outPts);
    const double r = std::min(radius, 0.45 * std::min(inLen, outLen));
    if (!(r > 1e-9)) return false;
    auto backAlong = [&](const std::vector<QPointF>& pts, double dist) {
        double left = dist;
        for (std::size_t i = pts.size() - 1; i > 0; --i) {
            const double l = std::hypot(pts[i].x() - pts[i - 1].x(),
                                        pts[i].y() - pts[i - 1].y());
            if (l >= left) {
                const double t = (l - left) / std::max(1e-12, l);
                return pts[i - 1] + (pts[i] - pts[i - 1]) * t;
            }
            left -= l;
        }
        return pts.front();
    };
    const QPointF cutIn = backAlong(inPts, r);
    std::vector<QPointF> revOut(outPts.rbegin(), outPts.rend());
    const QPointF cutOut = backAlong(revOut, r);
    auto spanFraction = [&](const std::vector<QPointF>& pts,
                            const QPointF& cut) {
        const double total = polyLength(pts);
        if (!(total > 1e-12)) return 0.0;
        double best = 0.0, bestD = 1e300, run = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            if (i > 0)
                run += std::hypot(pts[i].x() - pts[i - 1].x(),
                                  pts[i].y() - pts[i - 1].y());
            const double dd = std::hypot(pts[i].x() - cut.x(),
                                         pts[i].y() - cut.y());
            if (dd < bestD) {
                bestD = dd;
                best = run / total;
            }
        }
        return std::clamp(best, 0.0, 1.0);
    };
    const double tIn = spanFraction(inPts, cutIn);
    const double tOut = spanFraction(outPts, cutOut);
    // Trim the incoming span's end (keep [0, tIn]).
    {
        Segment& s = node.segments[static_cast<std::size_t>(inSpanIdx)];
        if (s.kind == Segment::Kind::CubicTo) {
            QPointF a1, a2, a3, b1, b2, b3;
            splitCubic(prev, QPointF(s.c1x, s.c1y), QPointF(s.c2x, s.c2y),
                       anchor, tIn, &a1, &a2, &a3, &b1, &b2, &b3);
            s.c1x = static_cast<float>(a1.x());
            s.c1y = static_cast<float>(a1.y());
            s.c2x = static_cast<float>(a2.x());
            s.c2y = static_cast<float>(a2.y());
            s.x = static_cast<float>(a3.x());
            s.y = static_cast<float>(a3.y());
        } else {
            s.x = static_cast<float>(cutIn.x());
            s.y = static_cast<float>(cutIn.y());
        }
    }
    // Trim the outgoing span's start (keep [tOut, 1]); the implicit closing
    // edge needs no trim (the pen draws it from the fillet's end).
    if (!implicitOut) {
        Segment& s = node.segments[static_cast<std::size_t>(nextSeg)];
        if (s.kind == Segment::Kind::CubicTo) {
            QPointF a1, a2, a3, b1, b2, b3;
            splitCubic(anchor, QPointF(s.c1x, s.c1y), QPointF(s.c2x, s.c2y),
                       next, tOut, &a1, &a2, &a3, &b1, &b2, &b3);
            s.c1x = static_cast<float>(b1.x());
            s.c1y = static_cast<float>(b1.y());
            s.c2x = static_cast<float>(b2.x());
            s.c2y = static_cast<float>(b2.y());
        } else {
            // A line starting at the anchor becomes the straight line
            // cutOut→next, written as an exact cubic.
            s.kind = Segment::Kind::CubicTo;
            s.c1x = static_cast<float>(cutOut.x() + (next.x() - cutOut.x()) / 3.0);
            s.c1y = static_cast<float>(cutOut.y() + (next.y() - cutOut.y()) / 3.0);
            s.c2x = static_cast<float>(cutOut.x() + 2.0 * (next.x() - cutOut.x()) / 3.0);
            s.c2y = static_cast<float>(cutOut.y() + 2.0 * (next.y() - cutOut.y()) / 3.0);
        }
    }
    // Fillet: cubic cutIn → cutOut with both controls at the old anchor
    // (tangent to both spans at the cuts, the standard round-corner
    // approximation). It follows the trimmed incoming span.
    Segment f;
    f.kind = Segment::Kind::CubicTo;
    f.c1x = static_cast<float>(anchor.x());
    f.c1y = static_cast<float>(anchor.y());
    f.c2x = static_cast<float>(anchor.x());
    f.c2y = static_cast<float>(anchor.y());
    f.x = static_cast<float>(cutOut.x());
    f.y = static_cast<float>(cutOut.y());
    node.segments.insert(node.segments.begin() + inSpanIdx + 1, f);
    return true;
}

bool contourNodePath(pittore::vector::ArtNode& node, double radius,
                     int join) {
    if (!(std::abs(radius) > 1e-9)) return false;
    // Closed outlines only: an open stroke has no inside to offset.
    bool anyClose = false;
    for (const Segment& s : node.segments)
        if (s.kind == Segment::Kind::Close) {
            anyClose = true;
            break;
        }
    if (!anyClose) return false;
    // Flatten, then offset each closed loop directly (edge normals +
    // miter/round/bevel joins). The stroke engine's expansion comes out as
    // fragments, not loops, so offsetting here is the honest primitive.
    PathBuilder b;
    bool open = true;
    for (const Segment& s : node.segments) {
        switch (s.kind) {
            case Segment::Kind::MoveTo:
                b.moveTo(s.x, s.y);
                open = false;
                break;
            case Segment::Kind::LineTo:
                if (open) {
                    b.moveTo(s.x, s.y);
                    open = false;
                } else {
                    b.lineTo(s.x, s.y);
                }
                break;
            case Segment::Kind::CubicTo:
                if (open) {
                    b.moveTo(s.c1x, s.c1y);
                    open = false;
                }
                b.cubicTo(s.c1x, s.c1y, s.c2x, s.c2y, s.x, s.y);
                break;
            case Segment::Kind::Close:
                b.close();
                open = true;
                break;
        }
    }
    const Path flat = b.build(0.25f);
    const double r = radius;
    // Round joins arc around the original vertex — correct only on the
    // outer side. Inward, convex corners meet at sharp points, so round
    // degrades to miter there (concave intrusions stay near-exact).
    const int effJoin = (r < 0.0 && join == 1) ? 0 : join;
    std::vector<Segment> out;
    bool anyLoop = false;
    for (std::size_t si = 0; si < flat.subpaths.size(); ++si) {
        if (!flat.isClosed(si)) continue;
        const auto& poly = flat.subpaths[si];
        if (poly.size() < 3) continue;
        std::vector<QPointF> pts;
        pts.reserve(poly.size());
        for (const auto& p : poly)
            pts.emplace_back((double)p.first, (double)p.second);
        // Drop a duplicated closing vertex for the wrap walk.
        if ((pts.front() - pts.back()).manhattanLength() < 1e-9) pts.pop_back();
        if (pts.size() < 3) continue;
        // Signed area: CCW loops offset outward to the right of each edge.
        double area = 0.0;
        for (std::size_t i = 0; i < pts.size(); ++i) {
            const QPointF& p = pts[i];
            const QPointF& q = pts[(i + 1) % pts.size()];
            area += p.x() * q.y() - q.x() * p.y();
        }
        const double side = (area >= 0.0 ? 1.0 : -1.0) * (r >= 0.0 ? 1.0 : -1.0);
        const double dist = std::abs(r);
        const std::size_t n = pts.size();
        // Offset lines per edge.
        struct OLine {
            QPointF p, d;  // point + unit direction
        };
        std::vector<OLine> off(n);
        for (std::size_t i = 0; i < n; ++i) {
            const QPointF& p = pts[i];
            const QPointF& q = pts[(i + 1) % n];
            QPointF d = q - p;
            const double len = std::hypot(d.x(), d.y());
            if (!(len > 1e-12)) {
                off[i] = {p, QPointF(1, 0)};
                continue;
            }
            d /= len;
            // Right normal (outward for CCW) times side.
            off[i] = {p + QPointF(d.y(), -d.x()) * side * dist, d};
        }
        auto intersect = [](const OLine& l, const OLine& m, QPointF* hit) {
            // l.p + t*l.d == m.p + u*m.d (2D cross-product form).
            const double den = l.d.x() * m.d.y() - l.d.y() * m.d.x();
            if (std::abs(den) <= 1e-12) return false;
            const double t = ((m.p.x() - l.p.x()) * m.d.y() -
                              (m.p.y() - l.p.y()) * m.d.x()) /
                             den;
            if (hit) *hit = QPointF(l.p.x() + t * l.d.x(),
                                    l.p.y() + t * l.d.y());
            return true;
        };
        std::vector<QPointF> loop;
        for (std::size_t i = 0; i < n; ++i) {
            const OLine& prev = off[(i + n - 1) % n];
            const OLine& cur = off[i];
            QPointF miter;
            bool ok = intersect(prev, cur, &miter);
            // Miter clamp: fall back to bevel past 10× the radius.
            if (ok && (miter - pts[i]).manhattanLength() >
                          10.0 * dist + std::abs(dist)) {
                ok = false;
            }
            if (effJoin == 0 && ok) {
                loop.push_back(miter);
            } else if (effJoin == 1) {
                // Round: arc around the vertex from the previous offset
                // end to the current offset start.
                auto proj = [](const OLine& l, const QPointF& v) {
                    const double t = (v.x() - l.p.x()) * l.d.x() +
                                     (v.y() - l.p.y()) * l.d.y();
                    return QPointF(l.p.x() + t * l.d.x(),
                                   l.p.y() + t * l.d.y());
                };
                const QPointF p0 = proj(prev, pts[i]);
                const QPointF p1 = proj(cur, pts[i]);
                const double a0 = std::atan2(p0.y() - pts[i].y(),
                                             p0.x() - pts[i].x());
                double a1 = std::atan2(p1.y() - pts[i].y(),
                                       p1.x() - pts[i].x());
                // Sweep the short way through the outside.
                while (a1 - a0 > kPi) a1 -= 2.0 * kPi;
                while (a1 - a0 < -kPi) a1 += 2.0 * kPi;
                const int steps =
                    std::max(2, (int)std::ceil(std::abs(a1 - a0) / (kPi / 12.0)));
                for (int k = 0; k <= steps; ++k) {
                    const double a = a0 + (a1 - a0) * k / steps;
                    loop.emplace_back(pts[i].x() + dist * std::cos(a),
                                      pts[i].y() + dist * std::sin(a));
                }
            } else {
                // Bevel (or clamped miter): straight cut across.
                auto proj = [](const OLine& l, const QPointF& v) {
                    const double t = (v.x() - l.p.x()) * l.d.x() +
                                     (v.y() - l.p.y()) * l.d.y();
                    return QPointF(l.p.x() + t * l.d.x(),
                                   l.p.y() + t * l.d.y());
                };
                loop.push_back(proj(prev, pts[i]));
                loop.push_back(proj(cur, pts[i]));
            }
        }
        if (loop.size() < 3) continue;
        for (std::size_t i = 0; i < loop.size(); ++i) {
            Segment s;
            s.kind = (i == 0) ? Segment::Kind::MoveTo : Segment::Kind::LineTo;
            s.x = static_cast<float>(loop[i].x());
            s.y = static_cast<float>(loop[i].y());
            out.push_back(s);
        }
        Segment c;
        c.kind = Segment::Kind::Close;
        out.push_back(c);
        anyLoop = true;
    }
    if (!anyLoop) return false;
    node.segments = std::move(out);
    return true;
}

int knifeCutNode(pittore::vector::ArtNode& node, const QPointF& a,
                 const QPointF& b, double tolerance, int closeMode) {
    if (node.segments.empty()) return 0;
    const double tol = std::max(0.05, tolerance);
    // Cut parameters per span (fraction along the span).
    std::vector<std::vector<double>> cuts(node.segments.size());
    int total = 0;
    QPointF runStart;
    bool haveRun = false;
    for (std::size_t i = 0; i < node.segments.size(); ++i) {
        const Segment& s = node.segments[i];
        if (s.kind == Segment::Kind::MoveTo) {
            runStart = segEnd(s);
            haveRun = true;
            continue;
        }
        if (s.kind == Segment::Kind::Close) {
            haveRun = false;
            continue;
        }
        if (!haveRun) {
            runStart = segEnd(s);
            haveRun = true;
            continue;
        }
        const QPointF end = segEnd(s);
        if (s.kind == Segment::Kind::LineTo) {
            QPointF hit;
            if (segIntersect(runStart, end, a, b, &hit)) {
                const double l = std::hypot(end.x() - runStart.x(),
                                            end.y() - runStart.y());
                if (l > 1e-9) {
                    const double t = std::hypot(hit.x() - runStart.x(),
                                                hit.y() - runStart.y()) /
                                     l;
                    if (t > 1e-3 && t < 1 - 1e-3) {
                        cuts[i].push_back(t);
                        ++total;
                    }
                }
            }
        } else if (s.kind == Segment::Kind::CubicTo) {
            const QPointF c1(s.c1x, s.c1y), c2(s.c2x, s.c2y);
            auto flat = flattenCubic(runStart, c1, c2, end, tol);
            double run = 0.0;
            const double full = polyLength(flat);
            for (std::size_t k = 1; k < flat.size(); ++k) {
                QPointF hit;
                if (segIntersect(flat[k - 1], flat[k], a, b, &hit) &&
                    full > 1e-9) {
                    run += std::hypot(hit.x() - flat[k - 1].x(),
                                      hit.y() - flat[k - 1].y());
                    const double t = run / full;
                    if (t > 1e-3 && t < 1 - 1e-3) {
                        cuts[i].push_back(t);
                        ++total;
                    }
                    run += std::hypot(flat[k].x() - hit.x(),
                                      flat[k].y() - hit.y());
                } else {
                    run += std::hypot(flat[k].x() - flat[k - 1].x(),
                                      flat[k].y() - flat[k - 1].y());
                }
            }
        }
        runStart = end;
    }
    if (total == 0) return 0;
    // Rebuild: split spans at sorted params; every cut boundary breaks the
    // subpath (MoveTo), so one closed outline becomes open pieces.
    std::vector<Segment> out;
    const double closeTol =
        closeMode == 3 ? 1e300 : (closeMode == 2 ? 32.0 : (closeMode == 1 ? 8.0 : -1.0));
    auto maybeClose = [&](std::vector<Segment>& dst) {
        // Close the trailing subpath per closeMode (needs 2+ anchors).
        int anchors = 0;
        QPointF first, last;
        bool have = false;
        for (auto it = dst.rbegin(); it != dst.rend(); ++it) {
            if (it->kind == Segment::Kind::MoveTo) {
                first = segEnd(*it);
                have = true;
                break;
            }
            if (isAnchor(*it)) {
                if (anchors == 0) last = segEnd(*it);
                ++anchors;
            }
        }
        if (have && anchors >= 2 &&
            std::hypot(last.x() - first.x(), last.y() - first.y()) <= closeTol) {
            Segment c;
            c.kind = Segment::Kind::Close;
            dst.push_back(c);
        }
    };
    QPointF cur;
    bool haveCur = false;
    for (std::size_t i = 0; i < node.segments.size(); ++i) {
        const Segment& s = node.segments[i];
        if (s.kind == Segment::Kind::MoveTo) {
            maybeClose(out);
            out.push_back(s);
            cur = segEnd(s);
            haveCur = true;
            continue;
        }
        if (s.kind == Segment::Kind::Close) {
            maybeClose(out);
            continue;  // cuts open the path; re-close only per closeMode
        }
        if (!haveCur) {
            out.push_back(s);
            cur = segEnd(s);
            haveCur = true;
            continue;
        }
        const std::vector<double>& ts = cuts[i];
        if (ts.empty()) {
            out.push_back(s);
        } else {
            std::vector<double> sorted = ts;
            std::sort(sorted.begin(), sorted.end());
            if (s.kind == Segment::Kind::LineTo) {
                for (double t : sorted) {
                    // Cut points interpolate the ORIGINAL span; the MoveTo
                    // between them breaks the subpath there.
                    const QPointF q(cur.x() + (segEnd(s).x() - cur.x()) * t,
                                    cur.y() + (segEnd(s).y() - cur.y()) * t);
                    Segment l;
                    l.kind = Segment::Kind::LineTo;
                    l.x = static_cast<float>(q.x());
                    l.y = static_cast<float>(q.y());
                    out.push_back(l);
                    Segment m;
                    m.kind = Segment::Kind::MoveTo;
                    m.x = l.x;
                    m.y = l.y;
                    out.push_back(m);
                }
            } else {
                const QPointF c1(s.c1x, s.c1y), c2(s.c2x, s.c2y);
                const QPointF p3 = segEnd(s);
                double prev = 0.0;
                QPointF segStart = cur;
                QPointF sc1 = c1, sc2 = c2, se = p3;
                for (double t : sorted) {
                    // Split the REMAINDER at the renormalized parameter.
                    const double rt = (t - prev) / (1.0 - prev);
                    QPointF a1, a2, a3, b1, b2, b3;
                    splitCubic(segStart, sc1, sc2, se, rt, &a1, &a2, &a3, &b1,
                               &b2, &b3);
                    Segment h;
                    h.kind = Segment::Kind::CubicTo;
                    h.c1x = static_cast<float>(a1.x());
                    h.c1y = static_cast<float>(a1.y());
                    h.c2x = static_cast<float>(a2.x());
                    h.c2y = static_cast<float>(a2.y());
                    h.x = static_cast<float>(a3.x());
                    h.y = static_cast<float>(a3.y());
                    out.push_back(h);
                    Segment m;
                    m.kind = Segment::Kind::MoveTo;
                    m.x = h.x;
                    m.y = h.y;
                    out.push_back(m);
                    segStart = a3;
                    sc1 = b1;
                    sc2 = b2;
                    se = p3;
                    prev = t;
                }
                Segment h;
                h.kind = Segment::Kind::CubicTo;
                h.c1x = static_cast<float>(sc1.x());
                h.c1y = static_cast<float>(sc1.y());
                h.c2x = static_cast<float>(sc2.x());
                h.c2y = static_cast<float>(sc2.y());
                h.x = static_cast<float>(se.x());
                h.y = static_cast<float>(se.y());
                out.push_back(h);
            }
        }
        cur = segEnd(s);
        haveCur = true;
    }
    maybeClose(out);
    node.segments = std::move(out);
    return total;
}

bool isSmoothAnchor(const pittore::vector::ArtNode& node, int seg) {
    using Kind = pittore::vector::Segment::Kind;
    if (seg < 0 || seg >= static_cast<int>(node.segments.size())) return false;
    if (!isAnchor(node.segments[static_cast<std::size_t>(seg)])) return false;
    auto liveCubic = [&](const Segment& s, const QPointF& from) {
        if (s.kind != Kind::CubicTo) return false;
        const QPointF end = segEnd(s);
        const QPointF c1(s.c1x, s.c1y), c2(s.c2x, s.c2y);
        return (c1 - from).manhattanLength() > 1e-9 ||
               (c2 - end).manhattanLength() > 1e-9;
    };
    // Own span runs prev→anchor; the next span runs anchor→next.
    const Segment& own = node.segments[static_cast<std::size_t>(seg)];
    QPointF prev = segEnd(own);
    for (int i = seg - 1; i >= 0; --i) {
        if (isAnchor(node.segments[static_cast<std::size_t>(i)])) {
            prev = segEnd(node.segments[static_cast<std::size_t>(i)]);
            break;
        }
    }
    if (liveCubic(own, prev)) return true;
    const int nx = seg + 1;
    if (nx < static_cast<int>(node.segments.size())) {
        const Segment& s = node.segments[static_cast<std::size_t>(nx)];
        if (s.kind != Kind::Close &&
            liveCubic(s, segEnd(own)))
            return true;
    }
    return false;
}

bool insertAnchorPoint(pittore::vector::ArtNode& node, const QPointF& nodePos,
                       double tol, int* segOut) {
    using Kind = pittore::vector::Segment::Kind;
    if (!(tol > 0.0)) return false;
    int bestSpan = -1;
    double bestD = tol;
    double bestT = 0.0;
    QPointF runStart;
    bool haveRun = false;
    for (int i = 0; i < static_cast<int>(node.segments.size()); ++i) {
        const Segment& s = node.segments[static_cast<std::size_t>(i)];
        if (s.kind == Kind::MoveTo) {
            runStart = segEnd(s);
            haveRun = true;
            continue;
        }
        if (s.kind == Kind::Close || !haveRun) {
            if (s.kind == Kind::Close) haveRun = false;
            continue;
        }
        const QPointF end = segEnd(s);
        if (s.kind == Kind::LineTo) {
            const QPointF ab = end - runStart;
            const double len2 = QPointF::dotProduct(ab, ab);
            double t = 0.0;
            if (len2 > 1e-12)
                t = std::clamp(
                    QPointF::dotProduct(nodePos - runStart, ab) / len2, 0.0,
                    1.0);
            const QPointF q = runStart + ab * t;
            // Endpoints belong to the anchor tools, not the insert: keep a
            // few pixels of margin so a near-corner click still grabs the
            // corner.
            if ((q - runStart).manhattanLength() < 4.0 ||
                (q - end).manhattanLength() < 4.0)
                continue;
            if (t > 1e-3 && t < 1.0 - 1e-3) {
                const double d = std::hypot(q.x() - nodePos.x(),
                                            q.y() - nodePos.y());
                if (d < bestD) {
                    bestD = d;
                    bestSpan = i;
                    bestT = t;
                }
            }
        } else if (s.kind == Kind::CubicTo) {
            auto flat = flattenCubic(runStart, QPointF(s.c1x, s.c1y),
                                     QPointF(s.c2x, s.c2y), end, 0.25);
            double run = 0.0, total = 0.0;
            for (std::size_t k = 1; k < flat.size(); ++k)
                total += std::hypot(flat[k].x() - flat[k - 1].x(),
                                    flat[k].y() - flat[k - 1].y());
            if (!(total > 1e-12)) {
                runStart = end;
                continue;
            }
            double spanBest = 1e300, spanT = 0.0;
            for (std::size_t k = 1; k < flat.size(); ++k) {
                run += std::hypot(flat[k].x() - flat[k - 1].x(),
                                  flat[k].y() - flat[k - 1].y());
                const double d = std::hypot(flat[k].x() - nodePos.x(),
                                            flat[k].y() - nodePos.y());
                if (d < spanBest) {
                    spanBest = d;
                    spanT = run / total;
                }
            }
            if (spanT > 1e-3 && spanT < 1.0 - 1e-3 && spanBest < bestD) {
                // Same endpoint margin as lines (absolute units): the
                // nearest flat sample stands in for the cut point.
                const QPointF cut =
                    runStart + (end - runStart) * spanT;
                if ((cut - runStart).manhattanLength() < 4.0 ||
                    (cut - end).manhattanLength() < 4.0)
                    continue;
                bestD = spanBest;
                bestSpan = i;
                bestT = spanT;
            }
        }
        runStart = end;
    }
    if (bestSpan < 0) return false;
    Segment& s = node.segments[static_cast<std::size_t>(bestSpan)];
    // The winning span's start anchor.
    QPointF prev;
    {
        bool hr = false;
        for (int i = bestSpan - 1; i >= 0; --i) {
            if (isAnchor(node.segments[static_cast<std::size_t>(i)])) {
                prev = segEnd(node.segments[static_cast<std::size_t>(i)]);
                hr = true;
                break;
            }
        }
        if (!hr) return false;
    }
    if (s.kind == Kind::LineTo) {
        Segment n;
        n.kind = Kind::LineTo;
        n.x = static_cast<float>(prev.x() + (segEnd(s).x() - prev.x()) * bestT);
        n.y = static_cast<float>(prev.y() + (segEnd(s).y() - prev.y()) * bestT);
        node.segments.insert(node.segments.begin() + bestSpan, n);
        if (segOut) *segOut = bestSpan;
        return true;
    }
    // Cubic: split at bestT; the halves keep the original curvature.
    const QPointF origEnd = segEnd(s);
    QPointF a1, a2, a3, b1, b2, b3;
    splitCubic(prev, QPointF(s.c1x, s.c1y), QPointF(s.c2x, s.c2y), origEnd,
               std::clamp(bestT, 0.0, 1.0), &a1, &a2, &a3, &b1, &b2, &b3);
    s.c1x = static_cast<float>(a1.x());
    s.c1y = static_cast<float>(a1.y());
    s.c2x = static_cast<float>(a2.x());
    s.c2y = static_cast<float>(a2.y());
    s.x = static_cast<float>(a3.x());
    s.y = static_cast<float>(a3.y());
    Segment n;
    n.kind = Kind::CubicTo;
    n.c1x = static_cast<float>(b1.x());
    n.c1y = static_cast<float>(b1.y());
    n.c2x = static_cast<float>(b2.x());
    n.c2y = static_cast<float>(b2.y());
    n.x = static_cast<float>(origEnd.x());
    n.y = static_cast<float>(origEnd.y());
    node.segments.insert(node.segments.begin() + bestSpan + 1, n);
    if (segOut) *segOut = bestSpan + 1;
    return true;
}

bool deleteAnchorPoint(pittore::vector::ArtNode& node, int seg) {
    using Kind = pittore::vector::Segment::Kind;
    if (seg < 0 || seg >= static_cast<int>(node.segments.size()))
        return false;
    if (!isAnchor(node.segments[static_cast<std::size_t>(seg)])) return false;
    const bool isClosed =
        !node.segments.empty() &&
        node.segments.back().kind == Segment::Kind::Close;
    int lastAnchor = -1, nAnchors = 0;
    for (int i = 0; i < static_cast<int>(node.segments.size()); ++i)
        if (isAnchor(node.segments[static_cast<std::size_t>(i)])) {
            lastAnchor = i;
            ++nAnchors;
        }
    if (seg == 0) return false;  // MoveTo start: pick another anchor
    if (isClosed && seg == lastAnchor) {
        // Refuse the duplicate closing anchor (it is the start point);
        // deleting it would just reopen the loop at the same spot.
        const QPointF first = segEnd(node.segments.front());
        const QPointF last =
            segEnd(node.segments[static_cast<std::size_t>(lastAnchor)]);
        if ((first - last).manhattanLength() < 1e-9) return false;
    }
    // Closed loops need 3 distinct corners after the cut; open paths need
    // 2 anchors. (Explicitly closed shapes duplicate the start point, so
    // distinct == nAnchors - 1 there.)
    bool dupClose = false;
    if (isClosed && lastAnchor > 0) {
        const QPointF first = segEnd(node.segments.front());
        const QPointF last =
            segEnd(node.segments[static_cast<std::size_t>(lastAnchor)]);
        dupClose = (first - last).manhattanLength() < 1e-9;
    }
    const int distinct = nAnchors - (dupClose ? 1 : 0);
    if (isClosed ? (distinct <= 3) : (nAnchors <= 2)) return false;
    node.segments.erase(node.segments.begin() + seg);
    // The span now sitting at `seg` bridges the gap: straighten it so the
    // join is a clean corner (curved bridges are the Node tool's job).
    if (seg < static_cast<int>(node.segments.size())) {
        Segment& s = node.segments[static_cast<std::size_t>(seg)];
        if (s.kind == Kind::CubicTo) {
            s.kind = Kind::LineTo;
            s.c1x = s.c1y = s.c2x = s.c2y = 0.0f;
        }
    }
    return true;
}

}  // namespace pittore::ui
