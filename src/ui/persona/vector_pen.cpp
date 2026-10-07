#include "ui/persona/vector_pen.h"

#include <QRectF>

#include <cmath>

namespace pittore::ui {
namespace {

using pittore::vector::Segment;

// Distance from p to the line through a-b (0 for a == b).
double pointLineDist(const QPointF& p, const QPointF& a, const QPointF& b) {
    const double dx = b.x() - a.x(), dy = b.y() - a.y();
    const double len = std::hypot(dx, dy);
    if (len <= 1e-12) return std::hypot(p.x() - a.x(), p.y() - a.y());
    return std::abs((p.x() - a.x()) * dy - (p.y() - a.y()) * dx) / len;
}

}  // namespace

void PenPath::clear() {
    points_.clear();
    closed_ = false;
}

void PenPath::addCorner(const QPointF& at) {
    PenPoint p;
    p.anchor = at;
    points_.push_back(p);
}

void PenPath::addSmooth(const QPointF& at, const QPointF& dragVec) {
    PenPoint p;
    p.anchor = at;
    p.outHandle = at + dragVec;
    p.inHandle = at - dragVec;
    p.hasOut = p.hasIn = true;
    points_.push_back(p);
}

void PenPath::setLastHandles(const QPointF& anchor, const QPointF& dragVec) {
    if (points_.empty()) return;
    PenPoint& p = points_.back();
    p.anchor = anchor;
    p.outHandle = anchor + dragVec;
    p.inHandle = anchor - dragVec;
    p.hasOut = p.hasIn = true;
}

void PenPath::demoteLastToCorner(double minHandleDoc) {
    if (points_.empty()) return;
    PenPoint& p = points_.back();
    const double outLen =
        p.hasOut ? std::hypot(p.outHandle.x() - p.anchor.x(),
                              p.outHandle.y() - p.anchor.y())
                 : 0.0;
    const double inLen =
        p.hasIn ? std::hypot(p.inHandle.x() - p.anchor.x(),
                             p.inHandle.y() - p.anchor.y())
                : 0.0;
    if (std::max(outLen, inLen) < minHandleDoc) {
        p.hasIn = p.hasOut = false;
    }
}

void PenPath::addFreehand(const QPointF& at, double minSpacingDoc) {
    if (!points_.empty()) {
        const QPointF& last = points_.back().anchor;
        if (std::hypot(at.x() - last.x(), at.y() - last.y()) < minSpacingDoc)
            return;
    }
    addCorner(at);
}

void PenPath::simplifyFreehand(double tolDoc) {
    // Greedy collinear collapse: drop middle points that sit on the line.
    // Runs before flattening so long straight runs stay one span.
    if (points_.size() < 3) return;
    std::vector<PenPoint> kept;
    kept.reserve(points_.size());
    kept.push_back(points_.front());
    for (std::size_t i = 1; i + 1 < points_.size(); ++i) {
        if (pointLineDist(points_[i].anchor, kept.back().anchor,
                          points_[i + 1].anchor) > tolDoc)
            kept.push_back(points_[i]);
    }
    kept.push_back(points_.back());
    points_.swap(kept);
}

bool PenPath::closeHit(const QPointF& docPos, double tolDoc) const {
    if (closed_ || points_.size() < 2) return false;
    const QPointF& first = points_.front().anchor;
    return std::hypot(docPos.x() - first.x(), docPos.y() - first.y()) <= tolDoc;
}

QRectF PenPath::bounds() const {
    QRectF box;
    for (const PenPoint& p : points_) {
        box |= QRectF(p.anchor, p.anchor);
        if (p.hasIn) box |= QRectF(p.inHandle, p.inHandle);
        if (p.hasOut) box |= QRectF(p.outHandle, p.outHandle);
    }
    return box;
}

std::vector<Segment> PenPath::toSegments(PenMode mode) const {
    std::vector<Segment> out;
    if (points_.empty()) return out;
    const std::size_t n = points_.size();

    auto move = [&](const QPointF& p) {
        Segment s;
        s.kind = Segment::Kind::MoveTo;
        s.x = static_cast<float>(p.x());
        s.y = static_cast<float>(p.y());
        out.push_back(s);
    };
    auto line = [&](const QPointF& p) {
        Segment s;
        s.kind = Segment::Kind::LineTo;
        s.x = static_cast<float>(p.x());
        s.y = static_cast<float>(p.y());
        out.push_back(s);
    };
    auto cubic = [&](const QPointF& c1, const QPointF& c2, const QPointF& p) {
        Segment s;
        s.kind = Segment::Kind::CubicTo;
        s.c1x = static_cast<float>(c1.x());
        s.c1y = static_cast<float>(c1.y());
        s.c2x = static_cast<float>(c2.x());
        s.c2y = static_cast<float>(c2.y());
        s.x = static_cast<float>(p.x());
        s.y = static_cast<float>(p.y());
        out.push_back(s);
    };

    if (mode == PenMode::Curvature && n >= 2) {
        // Catmull-Rom through the anchors, one cubic per span. Endpoints
        // duplicate (open) or wrap (closed) so the curve meets its anchors.
        const auto at = [&](std::size_t i) -> QPointF {
            if (closed_) return points_[i % n].anchor;
            if (i >= n) return points_[n - 1].anchor;
            return points_[i].anchor;
        };
        move(points_.front().anchor);
        const std::size_t spans = closed_ ? n : n - 1;
        for (std::size_t i = 0; i < spans; ++i) {
            const QPointF p0 = (i == 0) ? at(0) : at(i - 1);
            const QPointF p1 = at(i), p2 = at(i + 1);
            const QPointF p3 = at(i + 2);
            cubic(QPointF(p1.x() + (p2.x() - p0.x()) / 6.0,
                          p1.y() + (p2.y() - p0.y()) / 6.0),
                  QPointF(p2.x() - (p3.x() - p1.x()) / 6.0,
                          p2.y() - (p3.y() - p1.y()) / 6.0),
                  p2);
        }
    } else {
        // Bezier (per-point handles) and Freehand (bare polyline) share the
        // span walk: a span is cubic when either side offers a handle.
        move(points_.front().anchor);
        const std::size_t spans = closed_ ? n : n - 1;
        for (std::size_t i = 0; i < spans; ++i) {
            const PenPoint& a = points_[i];
            const PenPoint& b = points_[(i + 1) % n];
            const QPointF c1 = a.hasOut ? a.outHandle : a.anchor;
            const QPointF c2 = b.hasIn ? b.inHandle : b.anchor;
            if (a.hasOut || b.hasIn)
                cubic(c1, c2, b.anchor);
            else
                line(b.anchor);
        }
    }
    if (closed_) {
        Segment s;
        s.kind = Segment::Kind::Close;
        out.push_back(s);
    }
    return out;
}

void BrushStroke::clear() {
    dabs_.clear();
}

double brushPressureWidth(double baseWidth, double pressure) {
    if (!(baseWidth > 0.0)) return 8.0;
    const double p = std::clamp(pressure, 0.0, 1.0);
    return baseWidth * (0.1 + 0.9 * p);
}

void BrushStroke::addDab(const QPointF& pos, double width) {
    // Spacing gate (~1.5 doc px): move events fire faster than pixels.
    if (!dabs_.empty()) {
        const QPointF& last = dabs_.back().pos;
        if (std::hypot(pos.x() - last.x(), pos.y() - last.y()) < 1.5) {
            dabs_.back().width = width;
            return;
        }
    }
    dabs_.push_back(BrushDab{pos, std::max(0.1, width)});
}

std::vector<Segment> BrushStroke::buildRibbon(double ratio, double angleDeg,
                                              bool square) const {
    using Kind = Segment::Kind;
    std::vector<Segment> out;
    if (dabs_.empty()) return out;
    const double rat = std::clamp(ratio, 0.01, 1.0);
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const double cosA = std::cos(rad), sinA = std::sin(rad);
    auto move = [&](const QPointF& p) {
        Segment s;
        s.kind = Kind::MoveTo;
        s.x = static_cast<float>(p.x());
        s.y = static_cast<float>(p.y());
        out.push_back(s);
    };
    auto cubic = [&](const QPointF& c1, const QPointF& c2, const QPointF& p) {
        Segment s;
        s.kind = Kind::CubicTo;
        s.c1x = static_cast<float>(c1.x());
        s.c1y = static_cast<float>(c1.y());
        s.c2x = static_cast<float>(c2.x());
        s.c2y = static_cast<float>(c2.y());
        s.x = static_cast<float>(p.x());
        s.y = static_cast<float>(p.y());
        out.push_back(s);
    };
    auto close = [&] {
        Segment s;
        s.kind = Kind::Close;
        out.push_back(s);
    };
    // Nib support: half-extent across unit direction u, nib half-size h.
    // Ellipse mirrors the dab kernel (semi-axes h and h*ratio); square
    // uses the rotated Chebyshev support, corner-exact at 45 degrees.
    auto nibExtent = [&](double ux, double uy, double h) {
        const double ex = ux * cosA - uy * sinA;
        const double ey = ux * sinA + uy * cosA;
        if (square)
            return h * (std::fabs(ex) + rat * std::fabs(ey));
        return h * std::sqrt(ex * ex + rat * rat * ey * ey);
    };
    if (dabs_.size() == 1) {
        // Single dab: rotated nib outline (ellipse arcs, or a rotated
        // square), centered on the dab.
        const QPointF& c = dabs_.front().pos;
        const double h = std::max(0.05, dabs_.front().width * 0.5);
        if (square) {
            const QPointF ex(cosA, sinA), ey(-sinA, cosA);
            const QPointF p0 = c - ex * h - ey * h * rat;
            move(p0);
            auto line = [&](const QPointF& p) {
                Segment s;
                s.kind = Kind::LineTo;
                s.x = static_cast<float>(p.x());
                s.y = static_cast<float>(p.y());
                out.push_back(s);
            };
            line(c + ex * h - ey * h * rat);
            line(c + ex * h + ey * h * rat);
            line(c - ex * h + ey * h * rat);
            close();
            return out;
        }
        // Kappa circle in nib space, mapped through the nib frame.
        constexpr double kKappa = 0.5522847498308316;
        const QPointF ex(cosA, sinA), ey(-sinA, cosA);
        const QPointF p0 = c + ex * h;
        move(p0);
        const QPointF pts[4] = {c + ey * h * rat, c - ex * h,
                                c - ey * h * rat, p0};
        const QPointF prev[4] = {p0, pts[0], pts[1], pts[2]};
        for (int i = 0; i < 4; ++i) {
            // Quarter arc from prev[i] to pts[i] around c. Tangents run
            // unit-length so the kappa handles scale with the radius (not
            // its square); the last arc lands back on p0, closing the dot
            // instead of flying to the document origin.
            const QPointF a = prev[i] - c, b = pts[i] - c;
            const double la = std::max(1e-9, std::hypot(a.x(), a.y()));
            const double lb = std::max(1e-9, std::hypot(b.x(), b.y()));
            const QPointF t1(-a.y() / la, a.x() / la);
            const QPointF t2(-b.y() / lb, b.x() / lb);
            // Orient tangents along the arc direction.
            const double s1 = (t1.x() * (b.x() - a.x()) + t1.y() * (b.y() - a.y())) >= 0.0 ? 1.0 : -1.0;
            const double s2 = (t2.x() * (b.x() - a.x()) + t2.y() * (b.y() - a.y())) >= 0.0 ? 1.0 : -1.0;
            cubic(prev[i] + t1 * (s1 * kKappa * la),
                  pts[i] + t2 * (s2 * kKappa * lb),
                  pts[i]);
        }
        close();
        return out;
    }
    // Chaikin-smooth the centerline (widths ride along) so raw sample
    // jitter never reaches the outline; endpoints stay exact.
    struct Node {
        QPointF p;
        double w = 0.1;
    };
    std::vector<Node> spine;
    spine.reserve(dabs_.size());
    for (const auto& d : dabs_)
        spine.push_back({d.pos, std::max(0.1, d.width)});
    for (int pass = 0; pass < 2; ++pass) {
        if (spine.size() < 2) break;
        std::vector<Node> next;
        next.reserve(spine.size() * 2);
        next.push_back(spine.front());
        for (std::size_t i = 0; i + 1 < spine.size(); ++i) {
            const Node& a = spine[i];
            const Node& b = spine[i + 1];
            next.push_back({{a.p.x() * 0.75 + b.p.x() * 0.25,
                             a.p.y() * 0.75 + b.p.y() * 0.25},
                            a.w * 0.75 + b.w * 0.25});
            next.push_back({{a.p.x() * 0.25 + b.p.x() * 0.75,
                             a.p.y() * 0.25 + b.p.y() * 0.75},
                            a.w * 0.25 + b.w * 0.75});
        }
        next.push_back(spine.back());
        spine.swap(next);
    }
    const std::size_t n = spine.size();
    // Unit tangents (central differences); doubled points reuse the last
    // good tangent so the ribbon never twists.
    std::vector<QPointF> tangents(n, QPointF(1, 0));
    for (std::size_t i = 0; i < n; ++i) {
        const QPointF& a = spine[i == 0 ? 0 : i - 1].p;
        const QPointF& b = spine[i + 1 >= n ? n - 1 : i + 1].p;
        QPointF d(b.x() - a.x(), b.y() - a.y());
        const double len = std::hypot(d.x(), d.y());
        if (len > 1e-9)
            tangents[i] = QPointF(d.x() / len, d.y() / len);
        else if (i > 0)
            tangents[i] = tangents[i - 1];
    }
    // Offset points on both sides via the nib support across each normal.
    std::vector<QPointF> left(n), right(n);
    for (std::size_t i = 0; i < n; ++i) {
        const QPointF& t = tangents[i];
        const QPointF m(-t.y(), t.x());
        const double h = std::max(0.05, spine[i].w * 0.5);
        const double e = nibExtent(m.x(), m.y(), h);
        left[i] = spine[i].p + m * e;
        right[i] = spine[i].p - m * e;
    }
    // Catmull-Rom through each offset polyline, emitted as Beziers.
    auto edgeCubics = [&](const std::vector<QPointF>& pts, bool forward) {
        if (forward) {
            move(pts.front());
            for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
                const QPointF& p0 = pts[i == 0 ? 0 : i - 1];
                const QPointF& p1 = pts[i];
                const QPointF& p2 = pts[i + 1];
                const QPointF& p3 = pts[i + 2 >= pts.size() ? pts.size() - 1 : i + 2];
                cubic(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
            }
        } else {
            for (std::size_t k = 0; k + 1 < pts.size(); ++k) {
                const std::size_t i = pts.size() - 1 - k;
                const QPointF& p0 = pts[i + 1 >= pts.size() ? pts.size() - 1 : i + 1];
                const QPointF& p1 = pts[i];
                const QPointF& p2 = pts[i == 0 ? 0 : i - 1];
                const QPointF& p3 =
                    pts[i < 2 ? 0 : i - 2];
                cubic(p1 + (p2 - p0) / 6.0, p2 - (p3 - p1) / 6.0, p2);
            }
        }
    };
    // Round caps: kappa semicircle of the end half-extent around each
    // tip, bulging away from the stroke. Start/end points coincide with
    // the edge endpoints, so the outline stays continuous.
    auto cap = [&](const QPointF& tip, const QPointF& outward, double r) {
        constexpr double kKappa = 0.5522847498308316;
        QPointF d = outward;
        if (!(std::hypot(d.x(), d.y()) > 1e-9)) d = QPointF(1, 0);
        d /= std::max(1e-9, std::hypot(d.x(), d.y()));
        const QPointF n(-d.y(), d.x());
        const QPointF a = tip + n * r, b = tip - n * r, m = tip + d * r;
        cubic(a + d * (kKappa * r), m + n * (kKappa * r), m);
        cubic(m - n * (kKappa * r), b + d * (kKappa * r), b);
    };
    edgeCubics(left, true);
    {
        const std::size_t m = n - 1;
        const double h = std::max(0.05, spine[m].w * 0.5);
        const QPointF& t = tangents[m];
        const QPointF mm(-t.y(), t.x());
        cap(spine[m].p, t, nibExtent(mm.x(), mm.y(), h));
    }
    edgeCubics(right, false);
    {
        const double h = std::max(0.05, spine.front().w * 0.5);
        const QPointF& t = tangents.front();
        cap(spine.front().p, QPointF(-t.x(), -t.y()),
            nibExtent(-t.y(), t.x(), h));
    }
    close();
    return out;
}

}  // namespace pittore::ui
