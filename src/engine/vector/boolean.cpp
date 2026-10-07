#include "engine/vector/boolean.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace pittore::vector {
namespace {

constexpr double kEps = 1e-9;
constexpr double kPi = 3.14159265358979323846;

struct Pt {
    double x = 0.0, y = 0.0;
};

Pt operator-(const Pt& a, const Pt& b) { return {a.x - b.x, a.y - b.y}; }
Pt operator*(const Pt& a, double s) { return {a.x * s, a.y * s}; }
double cross(const Pt& a, const Pt& b) { return a.x * b.y - a.y * b.x; }
double dot(const Pt& a, const Pt& b) { return a.x * b.x + a.y * b.y; }
double norm(const Pt& a) { return std::hypot(a.x, a.y); }

double boolRingAreaPt(const std::vector<Pt>& ring) {
    double a = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const Pt& p = ring[i];
        const Pt& q = ring[(i + 1) % ring.size()];
        a += p.x * q.y - q.x * p.y;
    }
    return a * 0.5;
}

double pointSegDist(const Pt& p, const Pt& a, const Pt& b) {
    const Pt ab = b - a;
    const double len = norm(ab);
    if (len <= kEps) return norm(p - a);
    return std::abs(cross(p - a, ab)) / len;
}

// Winding-ray point-in-polygon over explicit rings.
bool pointInRings(const Pt& p, const std::vector<std::vector<Pt>>& rings,
                  BoolFill fill) {
    long winding = 0;
    long parity = 0;
    for (const auto& ring : rings) {
        const std::size_t n = ring.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Pt& a = ring[i];
            const Pt& b = ring[(i + 1) % n];
            // Upward crossing of the +x ray (y-strict low end).
            if ((a.y <= p.y && b.y > p.y) || (b.y <= p.y && a.y > p.y)) {
                const double t = (p.y - a.y) / (b.y - a.y);
                const double x = a.x + t * (b.x - a.x);
                if (x > p.x) {
                    ++parity;
                    winding += (b.y > a.y) ? 1 : -1;
                }
            }
        }
    }
    return fill == BoolFill::EvenOdd ? (parity & 1) != 0 : winding != 0;
}

struct Edge {
    Pt a, b;
    int poly = 0;  // 0 subject, 1 clip
};

struct SubEdge {
    Pt a, b;
    int poly = 0;
};

struct Coincident {
    std::size_t s = 0, c = 0;  // subedge indices (subject, clip)
    bool sameDir = true;
};

std::int64_t qcoord(double v) {
    return static_cast<std::int64_t>(std::llround(v / kEps));
}

struct Key {
    std::int64_t x = 0, y = 0;
    bool operator==(const Key& o) const { return x == o.x && y == o.y; }
};
struct KeyHash {
    std::size_t operator()(const Key& k) const {
        return static_cast<std::size_t>(
            (static_cast<std::uint64_t>(k.x) * 73856093u) ^
            (static_cast<std::uint64_t>(k.y) * 19349663u));
    }
};
Key keyOf(const Pt& p) { return {qcoord(p.x), qcoord(p.y)}; }

double normAngle(double a) {
    while (a < 0.0) a += 2.0 * kPi;
    while (a >= 2.0 * kPi) a -= 2.0 * kPi;
    return a;
}

// Even-odd containment of a point in a single loop.
bool pointInLoop(const Pt& p, const BoolRing& ring) {
    bool inside = false;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const double ax = ring[i].first, ay = ring[i].second;
        const double bx = ring[(i + 1) % ring.size()].first;
        const double by = ring[(i + 1) % ring.size()].second;
        if ((ay <= p.y && by > p.y) || (by <= p.y && ay > p.y)) {
            const double x = ax + (p.y - ay) / (by - ay) * (bx - ax);
            if (x > p.x) inside = !inside;
        }
    }
    return inside;
}

}  // namespace

double boolRingArea(const BoolRing& ring) {
    double a = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const auto& p = ring[i];
        const auto& q = ring[(i + 1) % ring.size()];
        a += p.first * q.second - q.first * p.second;
    }
    return a * 0.5;
}

std::vector<BoolRing> booleanOp(const std::vector<BoolRing>& subjectIn,
                                const std::vector<BoolRing>& clipIn, BoolOp op,
                                BoolFill fill) {
    // 1. Sanitize: drop degenerate rings, consecutive duplicates, closing
    // duplicate. Work in Pt vectors from here on.
    std::vector<std::vector<Pt>> polys[2];
    const std::vector<BoolRing>* ins[2] = {&subjectIn, &clipIn};
    for (int k = 0; k < 2; ++k) {
        for (const BoolRing& r : *ins[k]) {
            std::vector<Pt> pts;
            for (const auto& p : r) {
                const Pt q{p.first, p.second};
                if (!pts.empty() && norm(q - pts.back()) <= kEps) continue;
                pts.push_back(q);
            }
            if (pts.size() >= 2 && norm(pts.front() - pts.back()) <= kEps)
                pts.pop_back();
            if (pts.size() >= 3) polys[k].push_back(std::move(pts));
        }
    }
    std::vector<Edge> edges;
    for (int k = 0; k < 2; ++k)
        for (const auto& ring : polys[k])
            for (std::size_t i = 0; i < ring.size(); ++i)
                edges.push_back(Edge{ring[i], ring[(i + 1) % ring.size()], k});

    // 2. Intersections: per-edge split parameters. Overlaps are found
    // again geometrically after subdivision (step 4), so only the split
    // parameters are recorded here.
    std::vector<std::vector<double>> splits(edges.size());
    const std::size_t ne = edges.size();
    for (std::size_t i = 0; i < ne; ++i) {
        for (std::size_t j = i + 1; j < ne; ++j) {
            const Edge& e = edges[i];
            const Edge& f = edges[j];
            // Bounding-box reject.
            if (std::max(e.a.x, e.b.x) + kEps < std::min(f.a.x, f.b.x) ||
                std::max(f.a.x, f.b.x) + kEps < std::min(e.a.x, e.b.x) ||
                std::max(e.a.y, e.b.y) + kEps < std::min(f.a.y, f.b.y) ||
                std::max(f.a.y, f.b.y) + kEps < std::min(e.a.y, e.b.y))
                continue;
            const Pt d1 = e.b - e.a, d2 = f.b - f.a;
            const double denom = cross(d1, d2);
            if (std::abs(denom) > kEps) {
                // Proper crossing or endpoint touch.
                const Pt r = f.a - e.a;
                const double t = cross(r, d2) / denom;
                const double u = cross(r, d1) / denom;
                const bool tIn = t > kEps && t < 1.0 - kEps;
                const bool uIn = u > kEps && u < 1.0 - kEps;
                const bool tEnd = !tIn && (t <= kEps || t >= 1.0 - kEps) &&
                                  t >= -kEps && t <= 1.0 + kEps;
                const bool uEnd = !uIn && (u <= kEps || u >= 1.0 + kEps) &&
                                  u >= -kEps && u <= 1.0 + kEps;
                if (tIn && uIn) {
                    splits[i].push_back(t);
                    splits[j].push_back(u);
                } else if (tIn && uEnd) {
                    splits[i].push_back(t);  // f's endpoint pierces e
                } else if (uIn && tEnd) {
                    splits[j].push_back(u);  // e's endpoint pierces f
                }
                // Else: shared endpoints only — already matching vertices.
            } else if (pointSegDist(e.a, f.a, f.b) <= kEps ||
                       pointSegDist(e.b, f.a, f.b) <= kEps ||
                       pointSegDist(f.a, e.a, e.b) <= kEps ||
                       pointSegDist(f.b, e.a, e.b) <= kEps) {
                // Collinear overlap: project f onto e's dominant axis.
                const double l1 = norm(d1);
                if (l1 <= kEps) continue;
                const Pt u1 = d1 * (1.0 / l1);
                auto proj = [&](const Pt& p) { return dot(p - e.a, u1) / l1; };
                double te0 = proj(f.a), te1 = proj(f.b);
                const double lo = std::max(0.0, std::min(te0, te1));
                const double hi = std::min(1.0, std::max(te0, te1));
                if (hi - lo > kEps) {
                    // Split both at the interval ends.
                    if (lo > kEps && lo < 1.0 - kEps) splits[i].push_back(lo);
                    if (hi > kEps && hi < 1.0 - kEps) splits[i].push_back(hi);
                    const double l2 = norm(d2);
                    if (l2 > kEps) {
                        const auto paramOnF = [&](const Pt& p) {
                            return dot(p - f.a, d2) / (l2 * l2);
                        };
                        const Pt pLo{e.a.x + u1.x * lo * l1,
                                     e.a.y + u1.y * lo * l1};
                        const Pt pHi{e.a.x + u1.x * hi * l1,
                                     e.a.y + u1.y * hi * l1};
                        const double uf = paramOnF(pLo);
                        const double ug = paramOnF(pHi);
                        if (uf > kEps && uf < 1.0 - kEps)
                            splits[j].push_back(uf);
                        if (ug > kEps && ug < 1.0 - kEps)
                            splits[j].push_back(ug);
                    }
                } else {
                    // Touching at endpoints/crossing at a point: split the
                    // pierced edge (endpoint-on-edge both ways).
                    for (int pass = 0; pass < 2; ++pass) {
                        const Edge& src = pass == 0 ? e : f;
                        const Edge& dst = pass == 0 ? f : e;
                        for (const Pt& p : {src.a, src.b}) {
                            if (pointSegDist(p, dst.a, dst.b) > kEps) continue;
                            const Pt dd = dst.b - dst.a;
                            const double l = norm(dd);
                            if (l <= kEps) continue;
                            const double t = dot(p - dst.a, dd) / (l * l);
                            if (t > kEps && t < 1.0 - kEps)
                                splits[pass == 0 ? j : i].push_back(t);
                        }
                    }
                }
            }
        }
    }

    // 3. Subdivide at sorted unique parameters.
    std::vector<SubEdge> subs;
    for (std::size_t i = 0; i < ne; ++i) {
        auto& ts = splits[i];
        ts.push_back(0.0);
        ts.push_back(1.0);
        std::sort(ts.begin(), ts.end());
        std::vector<double> uniq;
        for (double t : ts) {
            if (uniq.empty() || t - uniq.back() > kEps) uniq.push_back(t);
        }
        const Edge& e = edges[i];
        for (std::size_t k = 0; k + 1 < uniq.size(); ++k) {
            const Pt a{e.a.x + (e.b.x - e.a.x) * uniq[k],
                       e.a.y + (e.b.y - e.a.y) * uniq[k]};
            const Pt b{e.a.x + (e.b.x - e.a.x) * uniq[k + 1],
                       e.a.y + (e.b.y - e.a.y) * uniq[k + 1]};
            if (norm(b - a) <= kEps) continue;
            subs.push_back(SubEdge{a, b, e.poly});
        }
    }

    // 4. Coincident cross-poly subedges (shared geometry after splitting).
    struct EndPair {
        Key a, b;  // ordered (min, max)
        bool operator==(const EndPair& o) const {
            return a == o.a && b == o.b;
        }
    };
    struct EndPairHash {
        std::size_t operator()(const EndPair& k) const {
            KeyHash h;
            return h(k.a) * 1315423911u + h(k.b);
        }
    };
    std::unordered_map<EndPair, std::vector<std::size_t>, EndPairHash> byEnds;
    for (std::size_t i = 0; i < subs.size(); ++i) {
        const Key ka = keyOf(subs[i].a), kb = keyOf(subs[i].b);
        const bool ab = ka.x < kb.x || (ka.x == kb.x && ka.y < kb.y);
        byEnds[EndPair{ab ? ka : kb, ab ? kb : ka}].push_back(i);
    }
    std::vector<int> coincWith(subs.size(), -1);
    std::vector<char> coincSame(subs.size(), 0);
    for (const auto& entry : byEnds) {
        const std::vector<std::size_t>& v = entry.second;
        for (std::size_t a = 0; a < v.size(); ++a) {
            for (std::size_t b = a + 1; b < v.size(); ++b) {
                const std::size_t i = v[a], j = v[b];
                if (subs[i].poly == subs[j].poly) continue;
                if (coincWith[i] >= 0 || coincWith[j] >= 0) continue;
                coincWith[i] = (int)j;
                coincWith[j] = (int)i;
                const bool fwd = norm(subs[i].a - subs[j].a) <= kEps &&
                                 norm(subs[i].b - subs[j].b) <= kEps;
                coincSame[i] = coincSame[j] = fwd ? 1 : 0;
            }
        }
    }
    const double sampEps = 1e-7;
    auto sidedness = [&](const SubEdge& s) {
        // Returns +1 when the interiors on both sides match the pattern of
        // overlapping (same-side) boundaries, -1 for external touch.
        const Pt d = s.b - s.a;
        const double len = norm(d);
        if (len <= kEps) return 0;
        const Pt n{-d.y / len, d.x / len};
        const Pt mid{(s.a.x + s.b.x) * 0.5, (s.a.y + s.b.y) * 0.5};
        const Pt L{mid.x + n.x * sampEps, mid.y + n.y * sampEps};
        const Pt R{mid.x - n.x * sampEps, mid.y - n.y * sampEps};
        const bool lS = pointInRings(L, polys[0], fill);
        const bool lC = pointInRings(L, polys[1], fill);
        const bool rS = pointInRings(R, polys[0], fill);
        const bool rC = pointInRings(R, polys[1], fill);
        // Overlap-type: one side is inside BOTH (or outside both... no:
        // for a shared boundary of overlapping solids, one side is in
        // both, the other in neither).
        if ((lS && lC && !rS && !rC) || (!lS && !lC && rS && rC)) return 1;
        return -1;
    };

    // 5. Classify non-coincident subedges by midpoint.
    std::vector<char> inOther(subs.size(), 0);
    for (std::size_t i = 0; i < subs.size(); ++i) {
        if (coincWith[i] >= 0) continue;
        const Pt mid{(subs[i].a.x + subs[i].b.x) * 0.5,
                     (subs[i].a.y + subs[i].b.y) * 0.5};
        inOther[i] =
            pointInRings(mid, polys[subs[i].poly == 0 ? 1 : 0], fill) ? 1 : 0;
    }

    // 6. Select directed result edges.
    struct DEdge {
        Pt a, b;
    };
    std::vector<DEdge> sel;
    std::vector<char> consumed(subs.size(), 0);
    auto emit = [&](const Pt& a, const Pt& b) {
        if (norm(b - a) > kEps) sel.push_back(DEdge{a, b});
    };
    for (std::size_t i = 0; i < subs.size(); ++i) {
        if (consumed[i]) continue;
        const int j = coincWith[i];
        if (j >= 0) {
            consumed[i] = 1;
            if (consumed[j]) continue;
            consumed[j] = 1;
            // Representatives: the subject copy, plus the clip copy for
            // external touches (each belongs to its own loop). Genuine
            // overlaps keep a single copy.
            const SubEdge& s =
                subs[i].poly == 0 ? subs[i] : subs[(std::size_t)j];
            const SubEdge& c =
                subs[i].poly == 1 ? subs[i] : subs[(std::size_t)j];
            const int side = sidedness(s);
            const bool overlapType = side > 0;
            switch (op) {
                case BoolOp::Union:
                    emit(s.a, s.b);
                    if (!overlapType) emit(c.a, c.b);
                    break;
                case BoolOp::Intersection:
                    if (overlapType) emit(s.a, s.b);
                    break;
                case BoolOp::Difference:
                    if (!overlapType && s.poly == 0) emit(s.a, s.b);
                    break;
                case BoolOp::Xor:
                    if (!overlapType) {
                        emit(s.a, s.b);
                        emit(c.a, c.b);
                    }
                    break;
            }
            continue;
        }
        const SubEdge& s = subs[i];
        const bool inside = inOther[i] != 0;
        switch (op) {
            case BoolOp::Union:
                if (!inside) emit(s.a, s.b);
                break;
            case BoolOp::Intersection:
                if (inside) emit(s.a, s.b);
                break;
            case BoolOp::Difference:
                if (s.poly == 0) {
                    if (!inside) emit(s.a, s.b);
                } else if (inside) {
                    emit(s.b, s.a);  // hole boundary, reversed
                }
                break;
            case BoolOp::Xor:
                // Both the outer hull and the removed overlap's boundary
                // survive: every non-coincident edge bounds kept material
                // on exactly one side.
                emit(s.a, s.b);
                break;
        }
    }

    // 7. Trace loops by smallest left turn.
    std::unordered_map<Key, std::vector<std::size_t>, KeyHash> starts;
    for (std::size_t i = 0; i < sel.size(); ++i) starts[keyOf(sel[i].a)].push_back(i);
    std::vector<char> used(sel.size(), 0);
    std::vector<BoolRing> out;
    for (std::size_t s = 0; s < sel.size(); ++s) {
        if (used[s]) continue;
        used[s] = 1;
        std::vector<Pt> loop{sel[s].a, sel[s].b};
        const Key home = keyOf(sel[s].a);
        while (true) {
            const Pt& tip = loop.back();
            if (keyOf(tip) == home) break;
            auto it = starts.find(keyOf(tip));
            if (it == starts.end()) break;
            const Pt& prev = loop[loop.size() - 2];
            Pt din = tip - prev;
            const double lin = norm(din);
            if (lin <= kEps) break;
            din = din * (1.0 / lin);
            const double ref = std::atan2(-din.y, -din.x);
            double bestTurn = 1e300;
            std::size_t best = sel.size();
            for (std::size_t j : it->second) {
                if (used[j]) continue;
                Pt dout = sel[j].b - sel[j].a;
                const double lout = norm(dout);
                if (lout <= kEps) continue;
                const double turn =
                    normAngle(std::atan2(dout.y, dout.x) - ref);
                if (turn < bestTurn) {
                    bestTurn = turn;
                    best = j;
                }
            }
            if (best == sel.size()) break;  // dangling: drop the chain
            used[best] = 1;
            loop.push_back(sel[best].b);
            if (loop.size() > sel.size() + 2) break;  // safety fuse
        }
        if (loop.size() >= 4 && keyOf(loop.back()) == home) {
            loop.pop_back();
            if (loop.size() >= 3 &&
                std::abs(boolRingAreaPt(loop)) > kEps) {
                BoolRing ring;
                for (const Pt& p : loop) ring.emplace_back(p.x, p.y);
                out.push_back(std::move(ring));
            }
        }
    }
    // Canonical orientation: outer loops counter-clockwise, holes
    // clockwise, by nesting depth — callers can sum signed areas and fill
    // with either rule. Probes are edge midpoints (vertices may touch).
    for (std::size_t i = 0; i < out.size(); ++i) {
        int depth = 0;
        const Pt probe{(out[i][0].first + out[i][1].first) * 0.5,
                       (out[i][0].second + out[i][1].second) * 0.5};
        for (std::size_t k = 0; k < out.size(); ++k) {
            if (k != i && pointInLoop(probe, out[k])) ++depth;
        }
        const bool ccw = boolRingArea(out[i]) > 0.0;
        if (ccw == ((depth & 1) != 0))
            std::reverse(out[i].begin(), out[i].end());
    }
    return out;
}

}  // namespace pittore::vector
