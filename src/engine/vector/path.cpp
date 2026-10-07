#include "engine/vector/path.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pittore::vector {
namespace {

constexpr float kTau = 6.283185307179586f;
constexpr float kHalfPi = 1.5707963267948966f;

// Adaptive-ish cubic flattening: subdivision count from the control polygon's
// deviation from a straight line.
void flattenCubic(std::pair<float, float> p0, std::pair<float, float> p1,
                  std::pair<float, float> p2, std::pair<float, float> p3,
                  float tolerance,
                  std::vector<std::pair<float, float>>& out) {
    const float d1 = std::hypot(p1.first - p0.first, p1.second - p0.second);
    const float d2 = std::hypot(p2.first - p1.first, p2.second - p1.second);
    const float d3 = std::hypot(p3.first - p2.first, p3.second - p2.second);
    const float len = d1 + d2 + d3;
    const float raw = std::sqrt(len / tolerance);
    long steps = static_cast<long>(std::ceil(raw));
    steps = std::clamp<long>(steps, 1, 256);
    for (long i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float mt = 1.0f - t;
        const float a = mt * mt * mt;
        const float b = 3.0f * mt * mt * t;
        const float c = 3.0f * mt * t * t;
        const float d = t * t * t;
        out.emplace_back(a * p0.first + b * p1.first + c * p2.first + d * p3.first,
                         a * p0.second + b * p1.second + c * p2.second +
                             d * p3.second);
    }
}

// Vertical supersampling factor of the rasterizer.
constexpr int kSub = 5;

// Credit one scanline sample's worth of fill to the pixels between x0 and x1:
// a pixel at either end counts only for the fraction of it the span covers.
void addCoverage(float* row, float x0, float x1, float weight, int w) {
    x0 = std::max(0.0f, x0);
    x1 = std::min(static_cast<float>(w), x1);
    if (x1 <= x0) return;
    const int first = static_cast<int>(std::floor(x0));
    const int last = std::min(static_cast<int>(std::ceil(x1)), w);
    for (int px = first; px < last; ++px) {
        const float left = std::max(static_cast<float>(px), x0);
        const float right = std::min(static_cast<float>(px + 1), x1);
        if (right > left) row[px] += (right - left) * weight;
    }
}

// Drop consecutive duplicate points: a zero-length segment has no normal, and
// stroking one would divide by zero.
std::vector<std::pair<float, float>> dedup(
    const std::vector<std::pair<float, float>>& pts) {
    std::vector<std::pair<float, float>> out;
    out.reserve(pts.size());
    for (const auto& p : pts) {
        if (out.empty() || std::abs(out.back().first - p.first) > 1e-6f ||
            std::abs(out.back().second - p.second) > 1e-6f)
            out.push_back(p);
    }
    // A closed ring comes back around to where it started: one copy of that
    // point is enough.
    if (out.size() > 1) {
        const auto f = out.front();
        const auto l = out.back();
        if (std::abs(f.first - l.first) <= 1e-6f &&
            std::abs(f.second - l.second) <= 1e-6f)
            out.pop_back();
    }
    return out;
}

// Signed area, positive for one winding direction and negative for the other.
float signedArea(const std::vector<std::pair<float, float>>& poly) {
    const std::size_t n = poly.size();
    float acc = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        const auto [x0, y0] = poly[i];
        const auto [x1, y1] = poly[(i + 1) % n];
        acc += x0 * y1 - x1 * y0;
    }
    return acc / 2.0f;
}

// Push a polygon, flipping it if needed so every piece of a stroke shares one
// winding direction. Mixed windings cancel under the nonzero rule and leave
// holes where pieces overlap.
void pushWound(Path& out, std::vector<std::pair<float, float>> poly) {
    if (signedArea(poly) < 0.0f) std::reverse(poly.begin(), poly.end());
    out.subpaths.push_back(std::move(poly));
}

std::vector<std::pair<float, float>> disc(float cx, float cy, float r) {
    long steps = static_cast<long>(r * 2.0f);
    steps = std::clamp<long>(steps, 8, 48);
    std::vector<std::pair<float, float>> out;
    out.reserve(static_cast<std::size_t>(steps));
    for (long i = 0; i < steps; ++i) {
        const float a = static_cast<float>(i) * kTau / static_cast<float>(steps);
        out.emplace_back(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    return out;
}

bool unit(float x, float y, float* ux, float* uy) {
    const float len = std::hypot(x, y);
    if (len <= 1e-6f) return false;
    *ux = x / len;
    *uy = y / len;
    return true;
}

// Fill in the corner a stroke leaves where the leg `prev`→`here` turns into
// `here`→`next`. Round joins drop a disc; the flat ones grow a small patch on
// the outside of the turn, which a miter join may stretch to a point.
void pushJoin(Path& out, std::pair<float, float> prev,
              std::pair<float, float> here, std::pair<float, float> next,
              float hw, const StrokeStyle& style) {
    if (style.join == LineJoin::Round) {
        pushWound(out, disc(here.first, here.second, hw));
        return;
    }
    float u0x, u0y, u1x, u1y;
    if (!unit(here.first - prev.first, here.second - prev.second, &u0x, &u0y))
        return;
    if (!unit(next.first - here.first, next.second - here.second, &u1x, &u1y))
        return;
    // Each leg pushed aside by half the width, on the side away from the turn;
    // the cross product of the legs says which side that is.
    const float turn = u0x * u1y - u0y * u1x;
    if (std::abs(turn) < 1e-6f) return;  // straight on: nothing to fill in
    const float s = turn > 0.0f ? -1.0f : 1.0f;
    const float e0x = -u0y * hw * s, e0y = u0x * hw * s;  // outgoing side
    const float e1x = -u1y * hw * s, e1y = u1x * hw * s;  // incoming side
    std::vector<std::pair<float, float>> patch{
        here, {here.first + e0x, here.second + e0y},
        {here.first + e1x, here.second + e1y}};
    if (style.join == LineJoin::Miter) {
        // Pull the patch out to where the two widened legs cross, unless that
        // point runs away into a spike beyond the miter limit.
        const float half = std::sqrt(
            std::max((1.0f + (u0x * u1x + u0y * u1y)) / 2.0f, 1e-6f));
        if (1.0f / half <= style.miter_limit) {
            float mx, my;
            if (!unit(e0x + e1x, e0y + e1y, &mx, &my)) return;
            patch = {here,
                     {here.first + e0x, here.second + e0y},
                     {here.first + mx * hw / half, here.second + my * hw / half},
                     {here.first + e1x, here.second + e1y}};
        }
    }
    pushWound(out, std::move(patch));
}

// Close the free end sitting at `end`, with `from` the point just inside the
// stroke. Butt leaves the segment rectangles alone, round drops a disc, square
// adds a box that hangs half a width past the end.
void pushCap(Path& out, std::pair<float, float> from,
             std::pair<float, float> end, float hw, LineCap cap) {
    if (cap == LineCap::Butt) return;
    if (cap == LineCap::Round) {
        pushWound(out, disc(end.first, end.second, hw));
        return;
    }
    float dx, dy;
    if (!unit(end.first - from.first, end.second - from.second, &dx, &dy)) return;
    const float sx = dx * hw, sy = dy * hw;   // along the stroke, past the end
    const float px = -dy * hw, py = dx * hw;  // across the stroke
    pushWound(out, {{end.first + px, end.second + py},
                    {end.first + px + sx, end.second + py + sy},
                    {end.first - px + sx, end.second - py + sy},
                    {end.first - px, end.second - py}});
}

}  // namespace

IRect Path::bounds() const {
    IRect out;
    for (const auto& sub : subpaths) {
        for (const auto& [x, y] : sub) {
            const IRect r{static_cast<int>(std::floor(x)),
                          static_cast<int>(std::floor(y)),
                          static_cast<int>(std::ceil(x)) + 1,
                          static_cast<int>(std::ceil(y)) + 1};
            out = out.unionWith(r);
        }
    }
    return out;
}

PathBuilder& PathBuilder::moveTo(float x, float y) {
    Segment s;
    s.kind = Segment::Kind::MoveTo;
    s.x = x;
    s.y = y;
    segments.push_back(s);
    return *this;
}

PathBuilder& PathBuilder::lineTo(float x, float y) {
    Segment s;
    s.kind = Segment::Kind::LineTo;
    s.x = x;
    s.y = y;
    segments.push_back(s);
    return *this;
}

PathBuilder& PathBuilder::cubicTo(float c1x, float c1y, float c2x, float c2y,
                                  float x, float y) {
    Segment s;
    s.kind = Segment::Kind::CubicTo;
    s.c1x = c1x;
    s.c1y = c1y;
    s.c2x = c2x;
    s.c2y = c2y;
    s.x = x;
    s.y = y;
    segments.push_back(s);
    return *this;
}

PathBuilder& PathBuilder::close() {
    Segment s;
    s.kind = Segment::Kind::Close;
    segments.push_back(s);
    return *this;
}

PathBuilder& PathBuilder::rect(const IRect& r) {
    moveTo(static_cast<float>(r.left), static_cast<float>(r.top))
        .lineTo(static_cast<float>(r.right), static_cast<float>(r.top))
        .lineTo(static_cast<float>(r.right), static_cast<float>(r.bottom))
        .lineTo(static_cast<float>(r.left), static_cast<float>(r.bottom))
        .close();
    return *this;
}

PathBuilder& PathBuilder::ellipse(const IRect& r) {
    constexpr float k = 0.5522848f;  // circle-to-cubic magic constant
    const float cx = (r.left + r.right) * 0.5f;
    const float cy = (r.top + r.bottom) * 0.5f;
    const float rx = r.width() * 0.5f;
    const float ry = r.height() * 0.5f;
    const float ox = rx * k, oy = ry * k;
    moveTo(cx - rx, cy)
        .cubicTo(cx - rx, cy - oy, cx - ox, cy - ry, cx, cy - ry)
        .cubicTo(cx + ox, cy - ry, cx + rx, cy - oy, cx + rx, cy)
        .cubicTo(cx + rx, cy + oy, cx + ox, cy + ry, cx, cy + ry)
        .cubicTo(cx - ox, cy + ry, cx - rx, cy + oy, cx - rx, cy)
        .close();
    return *this;
}

PathBuilder& PathBuilder::polygon(const IRect& r, std::uint32_t sides) {
    sides = std::max<std::uint32_t>(sides, 3);
    const float cx = (r.left + r.right) * 0.5f;
    const float cy = (r.top + r.bottom) * 0.5f;
    const float rx = r.width() * 0.5f;
    const float ry = r.height() * 0.5f;
    for (std::uint32_t i = 0; i < sides; ++i) {
        const float a = -kHalfPi + static_cast<float>(i) * kTau /
                                       static_cast<float>(sides);
        const float x = cx + rx * std::cos(a);
        const float y = cy + ry * std::sin(a);
        if (i == 0)
            moveTo(x, y);
        else
            lineTo(x, y);
    }
    close();
    return *this;
}

Path PathBuilder::build(float tolerance) const {
    const float tol = std::max(tolerance, 0.01f);
    Path out;
    std::vector<std::pair<float, float>> current;
    std::pair<float, float> cursor{0.0f, 0.0f};
    for (const Segment& seg : segments) {
        switch (seg.kind) {
            case Segment::Kind::MoveTo:
                if (current.size() >= 2) {
                    out.pushOpen(std::move(current));
                    current.clear();
                } else {
                    current.clear();
                }
                cursor = {seg.x, seg.y};
                current.push_back(cursor);
                break;
            case Segment::Kind::LineTo:
                cursor = {seg.x, seg.y};
                current.push_back(cursor);
                break;
            case Segment::Kind::CubicTo:
                flattenCubic(cursor, {seg.c1x, seg.c1y}, {seg.c2x, seg.c2y},
                             {seg.x, seg.y}, tol, current);
                cursor = {seg.x, seg.y};
                break;
            case Segment::Kind::Close:
                if (current.size() >= 2) {
                    out.pushClosed(std::move(current));
                    current.clear();
                } else {
                    current.clear();
                }
                break;
        }
    }
    if (current.size() >= 2) out.pushOpen(std::move(current));
    return out;
}

std::vector<std::uint8_t> rasterize(const Path& path, const IRect& rect,
                                    FillRule rule) {
    const int w = std::max(0, rect.width());
    const int h = std::max(0, rect.height());
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(w) * h, 0);
    if (w == 0 || h == 0 || path.isEmpty()) return mask;

    // One piece of outline per non-horizontal edge, tagged with the direction
    // it runs in so the winding rule has something to count.
    struct Piece {
        float x0, y0, x1, y1;
        int dir;
    };
    std::vector<Piece> pieces;
    for (const auto& sub : path.subpaths) {
        if (sub.size() < 3) continue;
        for (std::size_t i = 0; i < sub.size(); ++i) {
            const auto [x0, y0] = sub[i];
            const auto [x1, y1] = sub[(i + 1) % sub.size()];
            if (std::abs(y0 - y1) < std::numeric_limits<float>::epsilon())
                continue;  // flat: it never leaves its own scanline
            pieces.push_back({x0, y0, x1, y1, y1 > y0 ? 1 : -1});
        }
    }
    if (pieces.empty()) return mask;

    // Note down which pixel rows each piece can reach, so a scanline only
    // walks the geometry crossing it instead of the whole outline.
    const float top = static_cast<float>(rect.top);
    std::vector<std::vector<int>> perRow(static_cast<std::size_t>(h));
    for (int i = 0; i < static_cast<int>(pieces.size()); ++i) {
        const Piece& p = pieces[static_cast<std::size_t>(i)];
        const float lo = std::min(p.y0, p.y1) - top;
        const float hi = std::max(p.y0, p.y1) - top;
        const int first = std::clamp(static_cast<int>(std::floor(lo)), 0, h);
        const int last = std::clamp(static_cast<int>(std::ceil(hi)), 0, h);
        for (int r = first; r < last; ++r)
            perRow[static_cast<std::size_t>(r)].push_back(i);
    }

    std::vector<float> rowCov(static_cast<std::size_t>(w), 0.0f);
    std::vector<std::pair<float, int>> crossings;
    crossings.reserve(pieces.size());
    for (int row = 0; row < h; ++row) {
        const std::vector<int>& reach = perRow[static_cast<std::size_t>(row)];
        if (reach.empty()) continue;
        std::fill(rowCov.begin(), rowCov.end(), 0.0f);
        const float py = top + static_cast<float>(row);
        for (int s = 0; s < kSub; ++s) {
            const float sy =
                py + (static_cast<float>(s) + 0.5f) / static_cast<float>(kSub);
            crossings.clear();
            for (int idx : reach) {
                const Piece& p = pieces[static_cast<std::size_t>(idx)];
                const float ymin = std::min(p.y0, p.y1);
                const float ymax = std::max(p.y0, p.y1);
                if (sy < ymin || sy >= ymax) continue;
                const float t = (sy - p.y0) / (p.y1 - p.y0);
                crossings.emplace_back(p.x0 + t * (p.x1 - p.x0), p.dir);
            }
            if (crossings.size() < 2) continue;
            std::stable_sort(crossings.begin(), crossings.end(),
                             [](const auto& a, const auto& b) {
                                 return a.first < b.first;
                             });

            // Sweep left to right: each crossing opens or closes a region, and
            // the rule decides whether the region just opened is filled.
            int winding = 0;
            for (std::size_t i = 0; i + 1 < crossings.size(); ++i) {
                winding += rule == FillRule::NonZero ? crossings[i].second : 1;
                const bool inside = rule == FillRule::NonZero
                                        ? winding != 0
                                        : winding % 2 != 0;
                if (inside) {
                    addCoverage(rowCov.data(),
                                crossings[i].first - static_cast<float>(rect.left),
                                crossings[i + 1].first - static_cast<float>(rect.left),
                                1.0f / static_cast<float>(kSub), w);
                }
            }
        }
        std::uint8_t* out = &mask[static_cast<std::size_t>(row) * w];
        for (int x = 0; x < w; ++x) {
            const float c = std::clamp(rowCov[static_cast<std::size_t>(x)], 0.0f, 1.0f);
            out[x] = static_cast<std::uint8_t>(c * 255.0f + 0.5f);
        }
    }
    return mask;
}

Path strokeToPath(const Path& path, float width) {
    StrokeStyle style(width);
    style.cap = LineCap::Round;
    style.join = LineJoin::Round;
    return strokePath(path, style);
}

Path strokePath(const Path& path, const StrokeStyle& style) {
    const float hw = std::max(style.width / 2.0f, 0.05f);
    Path out;
    for (std::size_t i = 0; i < path.subpaths.size(); ++i) {
        const auto pts = dedup(path.subpaths[i]);
        if (pts.size() < 2) {
            // Too short to carry a direction: only a cap with area of its own
            // is left to draw, so a round one becomes a dot.
            if (pts.size() == 1 && style.cap == LineCap::Round)
                pushWound(out, disc(pts[0].first, pts[0].second, hw));
            continue;
        }
        const bool closed = path.isClosed(i) && pts.size() >= 3;
        const std::size_t n = pts.size();

        // Body: one rectangle per leg, pushed aside by half the width.
        const std::size_t legs = closed ? n : n - 1;
        for (std::size_t k = 0; k < legs; ++k) {
            const auto [x0, y0] = pts[k];
            const auto [x1, y1] = pts[(k + 1) % n];
            const float dx = x1 - x0, dy = y1 - y0;
            const float len = std::hypot(dx, dy);
            if (len < 1e-6f) continue;
            const float nx = -dy / len * hw, ny = dx / len * hw;
            pushWound(out, {{x0 + nx, y0 + ny},
                            {x1 + nx, y1 + ny},
                            {x1 - nx, y1 - ny},
                            {x0 - nx, y0 - ny}});
        }

        // Corners: every vertex where two legs meet. A closed ring has one at
        // the seam as well; an open run has none at either free end.
        const std::size_t corners = closed ? n : n - 2;
        for (std::size_t j = 0; j < corners; ++j) {
            const std::size_t v = closed ? j : j + 1;
            pushJoin(out, pts[(v + n - 1) % n], pts[v], pts[(v + 1) % n], hw,
                     style);
        }

        if (!closed) {
            pushCap(out, pts[1], pts[0], hw, style.cap);
            pushCap(out, pts[n - 2], pts[n - 1], hw, style.cap);
        }
    }
    return out;
}

float widthProfileAt(const WidthProfile& prof, float t) {
    if (prof.pos.size() < 2 || prof.scale.size() != prof.pos.size()) {
        if (!prof.scale.empty()) return std::max(0.0f, prof.scale.front());
        return 1.0f;
    }
    const float tc = std::clamp(t, 0.0f, 1.0f);
    std::size_t k = 0;
    while (k + 1 < prof.pos.size() && prof.pos[k + 1] < tc) ++k;
    const std::size_t k2 = std::min(k + 1, prof.pos.size() - 1);
    const float p0 = prof.pos[k], p1 = prof.pos[k2];
    const float f = (p1 > p0) ? (tc - p0) / (p1 - p0) : 0.0f;
    return std::max(
        0.0f, prof.scale[k] + (prof.scale[k2] - prof.scale[k]) * f);
}

Path flattenSegments(const std::vector<Segment>& segs, float tolerance) {
    PathBuilder b;
    b.segments = segs;
    return b.build(tolerance);
}

namespace {

// One dash-on run over a polyline: points plus per-point widths, with both
// ends interpolated (position and width) onto dash boundaries.
struct DashSpan {
    std::vector<std::pair<float, float>> pts;
    std::vector<float> widths;
};

// Split a polyline (with per-point widths) into dash-on spans. Lengths are
// absolute (same units as the points). Zero-length on-runs become dots
// (single-point spans); butt/square caps skip them, round keeps them.
std::vector<DashSpan> dashSplit(const std::vector<std::pair<float, float>>& pts,
                                const std::vector<float>& ws, bool closed,
                                const std::vector<float>& dash,
                                float dashOffset) {
    std::vector<DashSpan> spans;
    if (pts.size() < 2) return spans;
    // Normalize: keep zeros (dots), drop negatives; odd counts repeat.
    std::vector<float> pat;
    for (float v : dash)
        if (v >= 0.0f) pat.push_back(v);
    bool anyPositive = false;
    for (float v : pat)
        if (v > 0.0f) anyPositive = true;
    if (pat.empty() || !anyPositive) return spans;  // solid: caller skips us
    if (pat.size() % 2 == 1) {
        const std::size_t n = pat.size();
        for (std::size_t i = 0; i < n; ++i) pat.push_back(pat[i]);
    }
    float total = 0.0f;
    for (float v : pat) total += v;
    if (!(total > 0.0f)) return spans;
    // Arclength table.
    std::vector<float> cum(pts.size(), 0.0f);
    for (std::size_t i = 1; i < pts.size(); ++i)
        cum[i] = cum[i - 1] +
                 std::hypot(pts[i].first - pts[i - 1].first,
                            pts[i].second - pts[i - 1].second);
    float loopLen = cum.back();
    if (closed && pts.size() >= 2)
        loopLen += std::hypot(pts.front().first - pts.back().first,
                              pts.front().second - pts.back().second);
    if (!(loopLen > 1e-9f)) return spans;
    auto at = [&](float d, std::pair<float, float>* p, float* w) {
        float dd = closed ? std::fmod(d, loopLen) : std::clamp(d, 0.0f, loopLen);
        if (dd < 0.0f) dd += loopLen;
        std::size_t i = 1;
        while (i + 1 < cum.size() && cum[i] < dd) ++i;
        // For the closing edge of a loop, interpolate toward the front.
        float x1 = pts[i].first, y1 = pts[i].second, w1 = ws[i];
        if (closed && i + 1 >= cum.size() && pts.size() >= 2) {
            x1 = pts.front().first;
            y1 = pts.front().second;
            w1 = ws.front();
        }
        const float segLen = dd - cum[i - 1];
        const float span =
            (closed && i + 1 >= cum.size())
                ? loopLen - cum[i - 1]
                : cum[i] - cum[i - 1];
        const float f = (span > 1e-9f) ? segLen / span : 0.0f;
        const float fc = std::clamp(f, 0.0f, 1.0f);
        *p = {pts[i - 1].first + (x1 - pts[i - 1].first) * fc,
              pts[i - 1].second + (y1 - pts[i - 1].second) * fc};
        *w = ws[i - 1] + (w1 - ws[i - 1]) * fc;
    };
    // Lay pattern repeats along [0, loopLen]: distance d carries pattern
    // coordinate pc(d) = d + offset (mod total), so a zero-length even
    // entry lands a dot exactly on its boundary instead of being skipped.
    const float shift = std::fmod(dashOffset, total);
    const float rMin = std::floor((0.0f - shift) / total);
    const float rMax = std::ceil((loopLen - shift) / total);
    for (float r = rMin; r <= rMax; ++r) {
        float pos = r * total - shift;
        for (std::size_t k = 0; k < pat.size(); ++k) {
            const float len = pat[k];
            const float s0 = std::max(pos, 0.0f);
            const float s1 = std::min(pos + len, loopLen);
            if ((k % 2) == 0) {
                if (s1 > s0 + 1e-9f) {
                    // Positive on-run: endpoints plus interior vertices.
                    DashSpan span;
                    std::pair<float, float> p0;
                    float w0 = 0.0f;
                    at(s0, &p0, &w0);
                    span.pts.push_back(p0);
                    span.widths.push_back(w0);
                    for (std::size_t i = 0; i < pts.size(); ++i) {
                        if (cum[i] > s0 + 1e-9f && cum[i] < s1 - 1e-9f) {
                            span.pts.push_back(pts[i]);
                            span.widths.push_back(ws[i]);
                        }
                    }
                    std::pair<float, float> p1;
                    float w1 = 0.0f;
                    at(s1, &p1, &w1);
                    span.pts.push_back(p1);
                    span.widths.push_back(w1);
                    spans.push_back(std::move(span));
                } else if (len <= 1e-9f && s0 >= 0.0f && s0 <= loopLen) {
                    // Zero-length on-run: a dot on the boundary.
                    std::pair<float, float> p;
                    float w = 0.0f;
                    at(s0, &p, &w);
                    spans.push_back(DashSpan{{p}, {w}});
                }
            }
            pos += len;
        }
    }
    return spans;
}

// Expand one centerline span with per-point widths into wound pieces.
void expandSpan(Path& out, const std::vector<std::pair<float, float>>& pts,
                const std::vector<float>& ws, bool closed,
                const StrokeStyle& style) {
    if (pts.empty()) return;
    if (pts.size() == 1) {
        const float hw = (ws.empty() ? style.width : ws.front()) * 0.5f;
        if (style.cap == LineCap::Round && hw > 1e-6f)
            pushWound(out, disc(pts[0].first, pts[0].second, hw));
        return;
    }
    const auto hwAt = [&](std::size_t i) {
        const float w = (i < ws.size()) ? ws[i] : style.width;
        return std::max(w * 0.5f, 0.0f);
    };
    const std::size_t n = pts.size();
    const std::size_t last = closed ? n : n - 1;
    for (std::size_t k = 0; k < last; ++k) {
        const auto [x0, y0] = pts[k];
        const auto [x1, y1] = pts[(k + 1) % n];
        const float dx = x1 - x0, dy = y1 - y0;
        const float len = std::hypot(dx, dy);
        if (len < 1e-6f) continue;
        const float h0 = hwAt(k), h1 = hwAt((k + 1) % n);
        const float nx = -dy / len, ny = dx / len;
        pushWound(out, {{x0 + nx * h0, y0 + ny * h0},
                        {x1 + nx * h1, y1 + ny * h1},
                        {x1 - nx * h1, y1 - ny * h1},
                        {x0 - nx * h0, y0 - ny * h0}});
    }
    if (closed) {
        for (std::size_t j = 0; j < n; ++j)
            pushJoin(out, pts[(j + n - 1) % n], pts[j], pts[(j + 1) % n],
                     hwAt(j), style);
    } else {
        for (std::size_t j = 1; j + 1 < n; ++j)
            pushJoin(out, pts[j - 1], pts[j], pts[j + 1], hwAt(j), style);
    }
    if (!closed) {
        pushCap(out, pts[1], pts[0], hwAt(0), style.cap);
        pushCap(out, pts[n - 2], pts[n - 1], hwAt(n - 1), style.cap);
    }
}

}  // namespace

Path strokeVariable(const Path& path,
                    const std::vector<std::vector<float>>& widths,
                    const StrokeStyle& style, const std::vector<float>& dash,
                    float dashOffset) {
    Path out;
    const bool dashed =
        !dash.empty() &&
        std::any_of(dash.begin(), dash.end(),
                    [](float v) { return v > 0.0f; });
    for (std::size_t i = 0; i < path.subpaths.size(); ++i) {
        const auto pts = dedup(path.subpaths[i]);
        if (pts.size() < 2) {
            if (pts.size() == 1) {
                float w = style.width;
                if (i < widths.size() && !widths[i].empty()) w = widths[i].front();
                expandSpan(out, pts, std::vector<float>{w}, false, style);
            }
            continue;
        }
        std::vector<float> ws(pts.size(), style.width);
        if (i < widths.size() && widths[i].size() == pts.size()) ws = widths[i];
        const bool closed = path.isClosed(i) && pts.size() >= 3;
        if (!dashed) {
            expandSpan(out, pts, ws, closed, style);
            continue;
        }
        for (const DashSpan& span :
             dashSplit(pts, ws, closed, dash, dashOffset))
            expandSpan(out, span.pts, span.widths, false, style);
    }
    return out;
}

}  // namespace pittore::vector
