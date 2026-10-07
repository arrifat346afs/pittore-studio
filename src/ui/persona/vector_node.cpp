#include "ui/persona/vector_node.h"

#include <cmath>

#include "engine/vector/vector_art.h"

namespace pittore::ui {
namespace {

bool endpointOf(const pittore::vector::Segment& s, QPointF* out) {
    using Kind = pittore::vector::Segment::Kind;
    if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
        s.kind == Kind::CubicTo) {
        *out = QPointF(s.x, s.y);
        return true;
    }
    return false;
}

}  // namespace

std::vector<QPointF> nodeEndpoints(const pittore::vector::ArtNode& node) {
    std::vector<QPointF> out;
    for (const auto& s : node.segments) {
        QPointF p;
        if (endpointOf(s, &p)) out.push_back(p);
    }
    return out;
}

int nodeEndpointAt(const pittore::vector::ArtNode& node, const QPointF& nodePos,
                   double tol, int* segOut) {
    int best = -1;
    double bestD = tol;
    for (int i = 0; i < static_cast<int>(node.segments.size()); ++i) {
        QPointF p;
        if (!endpointOf(node.segments[static_cast<std::size_t>(i)], &p)) continue;
        const double d = std::hypot(p.x() - nodePos.x(), p.y() - nodePos.y());
        if (d <= bestD) {
            bestD = d;
            best = i;
        }
    }
    if (segOut) *segOut = best;
    return best;
}

void moveNodePoint(pittore::vector::ArtNode& node, int seg,
                   const QPointF& nodePos) {
    using Kind = pittore::vector::Segment::Kind;
    if (seg < 0 || seg >= static_cast<int>(node.segments.size())) return;
    pittore::vector::Segment& s = node.segments[static_cast<std::size_t>(seg)];
    QPointF old;
    if (!endpointOf(s, &old)) return;
    const double dx = nodePos.x() - old.x();
    const double dy = nodePos.y() - old.y();
    if (s.kind == Kind::CubicTo) {
        s.c1x = static_cast<float>(s.c1x + dx);
        s.c1y = static_cast<float>(s.c1y + dy);
        s.c2x = static_cast<float>(s.c2x + dx);
        s.c2y = static_cast<float>(s.c2y + dy);
    }
    s.x = static_cast<float>(nodePos.x());
    s.y = static_cast<float>(nodePos.y());
}

// The in-handle of anchor `seg` (c2 of its own cubic), if any.
bool inHandleOf(const pittore::vector::ArtNode& node, int seg, QPointF* out) {
    using Kind = pittore::vector::Segment::Kind;
    if (seg < 0 || seg >= static_cast<int>(node.segments.size())) return false;
    const pittore::vector::Segment& s =
        node.segments[static_cast<std::size_t>(seg)];
    if (s.kind != Kind::CubicTo) return false;
    *out = QPointF(s.c2x, s.c2y);
    return true;
}

// The out-handle of anchor `seg` (c1 of the next cubic), if any.
bool outHandleOf(const pittore::vector::ArtNode& node, int seg, QPointF* out) {
    using Kind = pittore::vector::Segment::Kind;
    const int next = seg + 1;
    if (next >= static_cast<int>(node.segments.size())) return false;
    const pittore::vector::Segment& s =
        node.segments[static_cast<std::size_t>(next)];
    if (s.kind != Kind::CubicTo) return false;
    *out = QPointF(s.c1x, s.c1y);
    return true;
}

std::vector<NodeHandle> nodeHandles(const pittore::vector::ArtNode& node) {
    std::vector<NodeHandle> out;
    for (int i = 0; i < static_cast<int>(node.segments.size()); ++i) {
        QPointF anchor;
        if (!endpointOf(node.segments[static_cast<std::size_t>(i)], &anchor))
            continue;
        QPointF p;
        // Zero-length handles (tip on the anchor — e.g. a straight arrival)
        // are not grabbable: the anchor itself wins the hit-test.
        if (inHandleOf(node, i, &p) &&
            std::hypot(p.x() - anchor.x(), p.y() - anchor.y()) > 1e-6)
            out.push_back(NodeHandle{i, NodeHandleSide::In, p});
        if (outHandleOf(node, i, &p) &&
            std::hypot(p.x() - anchor.x(), p.y() - anchor.y()) > 1e-6)
            out.push_back(NodeHandle{i, NodeHandleSide::Out, p});
    }
    return out;
}

NodeHandle nodeHandleAt(const pittore::vector::ArtNode& node,
                        const QPointF& nodePos, double tol) {
    NodeHandle best;
    double bestD = tol;
    for (const NodeHandle& h : nodeHandles(node)) {
        const double d =
            std::hypot(h.pos.x() - nodePos.x(), h.pos.y() - nodePos.y());
        if (d <= bestD) {
            bestD = d;
            best = h;
        }
    }
    return best;
}

bool nodeHandlesMirrored(const pittore::vector::ArtNode& node, int anchorSeg,
                         double tol) {
    if (anchorSeg < 0 || anchorSeg >= static_cast<int>(node.segments.size()))
        return false;
    QPointF in, out, anchor;
    if (!endpointOf(node.segments[static_cast<std::size_t>(anchorSeg)], &anchor))
        return false;
    if (!inHandleOf(node, anchorSeg, &in)) return false;
    if (!outHandleOf(node, anchorSeg, &out)) return false;
    // Mirrored when in + out == 2 * anchor (within tol).
    return std::hypot(in.x() + out.x() - 2.0 * anchor.x(),
                      in.y() + out.y() - 2.0 * anchor.y()) <= tol;
}

void moveNodeHandle(pittore::vector::ArtNode& node, int anchorSeg,
                    NodeHandleSide side, const QPointF& nodePos, bool mirror) {
    using Kind = pittore::vector::Segment::Kind;
    if (anchorSeg < 0 || anchorSeg >= static_cast<int>(node.segments.size()))
        return;
    QPointF anchor;
    if (!endpointOf(node.segments[static_cast<std::size_t>(anchorSeg)], &anchor))
        return;
    auto setIn = [&](const QPointF& p) {
        pittore::vector::Segment& s =
            node.segments[static_cast<std::size_t>(anchorSeg)];
        if (s.kind != Kind::CubicTo) return;
        s.c2x = static_cast<float>(p.x());
        s.c2y = static_cast<float>(p.y());
    };
    auto setOut = [&](const QPointF& p) {
        const int next = anchorSeg + 1;
        if (next >= static_cast<int>(node.segments.size())) return;
        pittore::vector::Segment& s = node.segments[static_cast<std::size_t>(next)];
        if (s.kind != Kind::CubicTo) return;
        s.c1x = static_cast<float>(p.x());
        s.c1y = static_cast<float>(p.y());
    };
    if (side == NodeHandleSide::In)
        setIn(nodePos);
    else
        setOut(nodePos);
    if (mirror) {
        const QPointF mirrored(2.0 * anchor.x() - nodePos.x(),
                               2.0 * anchor.y() - nodePos.y());
        if (side == NodeHandleSide::In)
            setOut(mirrored);
        else
            setIn(mirrored);
    }
}

void convertNodePoint(pittore::vector::ArtNode& node, int anchorSeg,
                      bool smooth) {
    using Kind = pittore::vector::Segment::Kind;
    if (anchorSeg < 0 || anchorSeg >= static_cast<int>(node.segments.size()))
        return;
    QPointF anchor;
    if (!endpointOf(node.segments[static_cast<std::size_t>(anchorSeg)], &anchor))
        return;
    if (!smooth) {
        // Corner: retract both adjacent spans to straight lines. The spans
        // keep their endpoints, so neighbours are untouched.
        pittore::vector::Segment& own =
            node.segments[static_cast<std::size_t>(anchorSeg)];
        if (own.kind == Kind::CubicTo) {
            own.kind = Kind::LineTo;
            own.c1x = own.c1y = own.c2x = own.c2y = 0.0f;
        }
        const int next = anchorSeg + 1;
        if (next < static_cast<int>(node.segments.size())) {
            pittore::vector::Segment& nx =
                node.segments[static_cast<std::size_t>(next)];
            if (nx.kind == Kind::CubicTo) {
                nx.kind = Kind::LineTo;
                nx.c1x = nx.c1y = nx.c2x = nx.c2y = 0.0f;
            }
        }
        return;
    }
    // Smooth: mirrored handles along the neighbour direction, at a third of
    // the shorter neighbour span (the Catmull-Rom-ish default).
    QPointF prev = anchor, next = anchor;
    bool hasPrev = false, hasNext = false;
    for (int i = anchorSeg - 1; i >= 0; --i) {
        if (endpointOf(node.segments[static_cast<std::size_t>(i)], &prev)) {
            hasPrev = true;
            break;
        }
    }
    for (int i = anchorSeg + 1; i < static_cast<int>(node.segments.size()); ++i) {
        if (endpointOf(node.segments[static_cast<std::size_t>(i)], &next)) {
            hasNext = true;
            break;
        }
    }
    if (!hasPrev && !hasNext) return;
    QPointF dir;
    if (hasPrev && hasNext)
        dir = QPointF(next.x() - prev.x(), next.y() - prev.y());
    else if (hasNext)
        dir = QPointF(next.x() - anchor.x(), next.y() - anchor.y());
    else
        dir = QPointF(anchor.x() - prev.x(), anchor.y() - prev.y());
    const double len = std::hypot(dir.x(), dir.y());
    if (!(len > 1e-9)) return;
    const double dPrev =
        hasPrev ? std::hypot(anchor.x() - prev.x(), anchor.y() - prev.y()) : len;
    const double dNext =
        hasNext ? std::hypot(next.x() - anchor.x(), next.y() - anchor.y()) : len;
    const double h = std::min(dPrev, dNext) / 3.0;
    const QPointF in(anchor.x() - dir.x() / len * h,
                     anchor.y() - dir.y() / len * h);
    const QPointF out(anchor.x() + dir.x() / len * h,
                      anchor.y() + dir.y() / len * h);
    // Promote adjacent line spans to cubics so the handles have a home.
    // Fresh controls initialize to the span endpoints (straight), so the
    // curve only bends where a handle actually pulls — a bare promotion
    // must never leave a (0,0) control behind.
    pittore::vector::Segment& own =
        node.segments[static_cast<std::size_t>(anchorSeg)];
    if (own.kind == Kind::LineTo) {
        own.kind = Kind::CubicTo;
        own.c1x = static_cast<float>(prev.x());
        own.c1y = static_cast<float>(prev.y());
    }
    const int nx = anchorSeg + 1;
    if (nx < static_cast<int>(node.segments.size())) {
        pittore::vector::Segment& s = node.segments[static_cast<std::size_t>(nx)];
        if (s.kind == Kind::LineTo) {
            s.kind = Kind::CubicTo;
            QPointF nxAnchor;
            if (endpointOf(s, &nxAnchor)) {
                s.c2x = static_cast<float>(nxAnchor.x());
                s.c2y = static_cast<float>(nxAnchor.y());
            }
        }
    }
    if (own.kind == Kind::CubicTo) {
        own.c2x = static_cast<float>(in.x());
        own.c2y = static_cast<float>(in.y());
    }
    if (nx < static_cast<int>(node.segments.size())) {
        pittore::vector::Segment& s = node.segments[static_cast<std::size_t>(nx)];
        if (s.kind == Kind::CubicTo) {
            s.c1x = static_cast<float>(out.x());
            s.c1y = static_cast<float>(out.y());
        }
    }
}

bool closeNodePath(pittore::vector::ArtNode& node) {
    using Kind = pittore::vector::Segment::Kind;
    if (node.segments.empty()) return false;
    if (node.segments.back().kind == Kind::Close) return false;
    int anchors = 0;
    for (const auto& s : node.segments) {
        if (s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
            s.kind == Kind::CubicTo)
            ++anchors;
    }
    if (anchors < 2) return false;
    pittore::vector::Segment s;
    s.kind = Kind::Close;
    node.segments.push_back(s);
    return true;
}

namespace {

// Anchor test local to the subpath ops below.
bool isNodeAnchor(const pittore::vector::Segment& s) {
    using Kind = pittore::vector::Segment::Kind;
    return s.kind == Kind::MoveTo || s.kind == Kind::LineTo ||
           s.kind == Kind::CubicTo;
}

// Subpath span holding anchor `seg`: [start, end] segment indices, where
// start is the MoveTo (or 0) and end is the last anchor before Close, the
// next MoveTo, or the list end. False when `seg` is not an anchor.
bool nodeSubpathSpan(const pittore::vector::ArtNode& node, int seg, int* start,
                     int* end) {
    using Kind = pittore::vector::Segment::Kind;
    if (seg < 0 || seg >= static_cast<int>(node.segments.size())) return false;
    if (!isNodeAnchor(node.segments[static_cast<std::size_t>(seg)]))
        return false;
    int s = seg;
    while (s > 0 &&
           node.segments[static_cast<std::size_t>(s - 1)].kind != Kind::Close &&
           node.segments[static_cast<std::size_t>(s)].kind != Kind::MoveTo)
        --s;
    // s now sits on the subpath's MoveTo (or 0 for a malformed lead).
    int e = seg;
    const int n = static_cast<int>(node.segments.size());
    while (e + 1 < n &&
           node.segments[static_cast<std::size_t>(e + 1)].kind != Kind::Close &&
           node.segments[static_cast<std::size_t>(e + 1)].kind != Kind::MoveTo)
        ++e;
    if (start) *start = s;
    if (end) *end = e;
    return true;
}

QPointF segEndPt(const pittore::vector::Segment& s) {
    return QPointF(s.x, s.y);
}

}  // namespace

bool splitNodePath(pittore::vector::ArtNode& node, int seg) {
    using Kind = pittore::vector::Segment::Kind;
    int start = -1, end = -1;
    if (!nodeSubpathSpan(node, seg, &start, &end)) return false;
    const int n = static_cast<int>(node.segments.size());
    // A closed loop opens at the anchor: drop its trailing Close. When the
    // anchor is already a loop end (first or last), that alone is the split.
    if (end + 1 < n &&
        node.segments[static_cast<std::size_t>(end + 1)].kind == Kind::Close) {
        node.segments.erase(node.segments.begin() + end + 1);
        if (seg == start || seg == end) return true;
        // Indices above end shifted down by one; seg <= end is unaffected.
    }
    if (seg == start || seg == end) return false;  // already on a break
    // Duplicate the anchor as a fresh MoveTo right after it: the span into
    // it keeps its in-handle, the span out keeps its out-handle, and both
    // cubic controls stay valid because the pen sits on the anchor again.
    const QPointF at = segEndPt(node.segments[static_cast<std::size_t>(seg)]);
    pittore::vector::Segment m;
    m.kind = Kind::MoveTo;
    m.x = static_cast<float>(at.x());
    m.y = static_cast<float>(at.y());
    node.segments.insert(node.segments.begin() + seg + 1, m);
    return true;
}

bool joinNodePath(pittore::vector::ArtNode& node, int seg) {
    using Kind = pittore::vector::Segment::Kind;
    int start = -1, end = -1;
    if (!nodeSubpathSpan(node, seg, &start, &end)) return false;
    const int n = static_cast<int>(node.segments.size());
    // `seg` must be an open end: a subpath start, or an end with no Close
    // following it.
    const bool isStart = (seg == start);
    const bool isEnd =
        (seg == end) &&
        !(end + 1 < n &&
          node.segments[static_cast<std::size_t>(end + 1)].kind == Kind::Close);
    if (!isStart && !isEnd) return false;
    // Joining out of a closed loop is split's job (it opens the loop there);
    // joining into one would silently break it. Both refuse here.
    if (end + 1 < n &&
        node.segments[static_cast<std::size_t>(end + 1)].kind == Kind::Close)
        return false;
    const QPointF here = segEndPt(node.segments[static_cast<std::size_t>(seg)]);
    // Nearest other open end (starts and unclosed ends across all subpaths).
    int best = -1;
    bool bestIsStart = false;
    double bestD = 1e300;
    for (int i = 0; i < n; ++i) {
        if (!isNodeAnchor(node.segments[static_cast<std::size_t>(i)])) continue;
        int s = -1, e = -1;
        if (!nodeSubpathSpan(node, i, &s, &e)) continue;
        for (int cand : {s, e}) {
            if (cand == seg) continue;
            // An "end" followed by Close is loop-closed, not open.
            if (cand == e && e + 1 < n &&
                node.segments[static_cast<std::size_t>(e + 1)].kind == Kind::Close)
                continue;
            const QPointF p =
                segEndPt(node.segments[static_cast<std::size_t>(cand)]);
            const double d =
                std::hypot(p.x() - here.x(), p.y() - here.y());
            if (d < bestD) {
                bestD = d;
                best = cand;
                bestIsStart = (cand == s);
            }
        }
    }
    if (best < 0) return false;
    const QPointF there =
        segEndPt(node.segments[static_cast<std::size_t>(best)]);
    // Same-subpath ends join by closing the loop (deleting the MoveTo here
    // would strand the opening span without a pen position).
    int bs = -1, be = -1;
    if (nodeSubpathSpan(node, best, &bs, &be) && bs == start) {
        // Same-subpath ends join by closing the loop (deleting the MoveTo
        // here would strand the opening span without a pen position). A
        // zero-length connector collapses to just the Close.
        if (std::hypot(there.x() - here.x(), there.y() - here.y()) > 1e-9) {
            pittore::vector::Segment l;
            l.kind = Kind::LineTo;
            l.x = static_cast<float>(there.x());
            l.y = static_cast<float>(there.y());
            node.segments.insert(node.segments.begin() + end + 1, l);
            pittore::vector::Segment c;
            c.kind = Kind::Close;
            node.segments.insert(node.segments.begin() + end + 2, c);
        } else {
            pittore::vector::Segment c;
            c.kind = Kind::Close;
            node.segments.insert(node.segments.begin() + end + 1, c);
        }
        return true;
    }
    if (bestIsStart) {
        // Merge: connector after `seg`, then drop the obsolete MoveTo. The
        // absorbed spans keep drawing from the connector's endpoint.
        pittore::vector::Segment l;
        l.kind = Kind::LineTo;
        l.x = static_cast<float>(there.x());
        l.y = static_cast<float>(there.y());
        node.segments.insert(node.segments.begin() + seg + 1, l);
        // `best` shifted right by the insert when it sits after `seg`.
        const int dropAt = (best > seg) ? best + 1 : best;
        node.segments.erase(node.segments.begin() + dropAt);
        return true;
    }
    // End-to-end: reverse the other subpath first so it starts at the
    // target, then merge as above.
    if (!reverseNodeSubpath(node, best)) return false;
    // Reversal preserves anchor positions but moves segments: relocate both
    // ends (the selected anchor may have shifted too).
    int s2 = -1, e2 = -1;
    for (int i = 0; i < static_cast<int>(node.segments.size()); ++i) {
        if (!isNodeAnchor(node.segments[static_cast<std::size_t>(i)])) continue;
        const QPointF p = segEndPt(node.segments[static_cast<std::size_t>(i)]);
        if (std::hypot(p.x() - here.x(), p.y() - here.y()) <= 1e-6) {
            if (!nodeSubpathSpan(node, i, &s2, &e2)) return false;
            seg = i;
            break;
        }
    }
    if (s2 < 0) return false;
    // After reversal the target subpath starts where it ended: find its new
    // MoveTo by position match on `there`.
    int newStart = -1;
    for (int i = 0; i < static_cast<int>(node.segments.size()); ++i) {
        if (node.segments[static_cast<std::size_t>(i)].kind != Kind::MoveTo)
            continue;
        const QPointF p = segEndPt(node.segments[static_cast<std::size_t>(i)]);
        if (std::hypot(p.x() - there.x(), p.y() - there.y()) <= 1e-6) {
            newStart = i;
            break;
        }
    }
    if (newStart < 0) return false;
    pittore::vector::Segment l;
    l.kind = Kind::LineTo;
    l.x = static_cast<float>(there.x());
    l.y = static_cast<float>(there.y());
    node.segments.insert(node.segments.begin() + seg + 1, l);
    const int dropAt = (newStart > seg) ? newStart + 1 : newStart;
    node.segments.erase(node.segments.begin() + dropAt);
    return true;
}

bool reverseNodeSubpath(pittore::vector::ArtNode& node, int seg) {
    using Kind = pittore::vector::Segment::Kind;
    int start = -1, end = -1;
    if (!nodeSubpathSpan(node, seg, &start, &end)) return false;
    if (end <= start) return false;  // lone anchor: nothing to reverse
    // Endpoints from last to first; the reversed subpath starts where the
    // original ended.
    std::vector<pittore::vector::Segment> rev;
    pittore::vector::Segment m;
    m.kind = Kind::MoveTo;
    {
        const QPointF p0 =
            segEndPt(node.segments[static_cast<std::size_t>(end)]);
        m.x = static_cast<float>(p0.x());
        m.y = static_cast<float>(p0.y());
    }
    rev.push_back(m);
    // Original span i (start < i <= end) ran P(i-1) → P(i); reversed it runs
    // P(i) → P(i-1) with swapped controls.
    for (int i = end; i > start; --i) {
        const pittore::vector::Segment& s =
            node.segments[static_cast<std::size_t>(i)];
        if (s.kind == Kind::LineTo) {
            pittore::vector::Segment l;
            l.kind = Kind::LineTo;
            const QPointF p =
                segEndPt(node.segments[static_cast<std::size_t>(i - 1)]);
            l.x = static_cast<float>(p.x());
            l.y = static_cast<float>(p.y());
            rev.push_back(l);
        } else if (s.kind == Kind::CubicTo) {
            pittore::vector::Segment c;
            c.kind = Kind::CubicTo;
            c.c1x = s.c2x;
            c.c1y = s.c2y;
            c.c2x = s.c1x;
            c.c2y = s.c1y;
            const QPointF p =
                segEndPt(node.segments[static_cast<std::size_t>(i - 1)]);
            c.x = static_cast<float>(p.x());
            c.y = static_cast<float>(p.y());
            rev.push_back(c);
        } else {
            return false;  // start anchor reached early: malformed span
        }
    }
    // Splice [start, end] (a trailing Close stays put, so closed loops keep
    // looping in the new direction).
    node.segments.erase(node.segments.begin() + start,
                        node.segments.begin() + end + 1);
    node.segments.insert(node.segments.begin() + start, rev.begin(),
                         rev.end());
    return true;
}

}  // namespace pittore::ui
