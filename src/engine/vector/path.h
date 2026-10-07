#pragma once
// Path construction and antialiased rasterization.
//
// A path is built from lines and cubic Béziers and flattened to polylines on
// demand; filling runs a scanline rasterizer that supersamples vertically and
// accumulates exact horizontal span coverage, so edges are clean in both axes
// without a tessellation dependency. The output is an 8-bit coverage mask,
// which both the shape decoder and the text engine paint through.

#include <cstdint>
#include <utility>
#include <vector>

namespace pittore::vector {

// Axis-aligned rectangle in document pixel space, half-open: x in
// [left, right) and y in [top, bottom). Coordinates may be negative, because
// layers can extend past the canvas.
struct IRect {
    int left = 0, top = 0, right = 0, bottom = 0;

    static IRect fromSize(int width, int height) {
        return IRect{0, 0, width, height};
    }
    static IRect fromXYWH(int x, int y, int w, int h) {
        return IRect{x, y, x + w, y + h};
    }

    // Saturating: a rect can be built from file-supplied coordinates whose
    // span overflows int, and an overflowing rect must stay impossibly large
    // so the callers' sanity checks reject it rather than see a small one.
    int width() const {
        return right < left ? 0 : (right - left);
    }
    int height() const {
        return bottom < top ? 0 : (bottom - top);
    }
    bool isEmpty() const { return right <= left || bottom <= top; }
    bool contains(int x, int y) const {
        return x >= left && x < right && y >= top && y < bottom;
    }
    IRect intersect(const IRect& o) const {
        const IRect r{left > o.left ? left : o.left, top > o.top ? top : o.top,
                      right < o.right ? right : o.right,
                      bottom < o.bottom ? bottom : o.bottom};
        return r.isEmpty() ? IRect{} : r;
    }
    IRect unionWith(const IRect& o) const {
        if (isEmpty()) return o;
        if (o.isEmpty()) return *this;
        return IRect{left < o.left ? left : o.left, top < o.top ? top : o.top,
                     right > o.right ? right : o.right,
                     bottom > o.bottom ? bottom : o.bottom};
    }
    IRect inflated(int d) const {
        return IRect{left - d, top - d, right + d, bottom + d};
    }
    bool operator==(const IRect& o) const {
        return left == o.left && top == o.top && right == o.right &&
               bottom == o.bottom;
    }
    bool operator!=(const IRect& o) const { return !(*this == o); }
};

enum class FillRule {
    EvenOdd,  // overlapping subpaths punch holes
    NonZero,  // overlapping subpaths merge
};

// A path flattened to polylines, in document coordinates.
struct Path {
    std::vector<std::vector<std::pair<float, float>>> subpaths;
    // Whether each subpath was explicitly closed. Only stroking reads this --
    // filling always treats a subpath as closed -- so a hand-assembled Path
    // fills exactly as it always did. Entries past the end count as open.
    std::vector<bool> closed;

    bool isEmpty() const {
        for (const auto& s : subpaths)
            if (s.size() >= 3) return false;
        return true;
    }
    bool isClosed(std::size_t i) const {
        return i < closed.size() ? closed[i] : false;
    }
    void pushOpen(std::vector<std::pair<float, float>> points) {
        closed.resize(subpaths.size(), false);
        subpaths.push_back(std::move(points));
        closed.push_back(false);
    }
    void pushClosed(std::vector<std::pair<float, float>> points) {
        closed.resize(subpaths.size(), false);
        subpaths.push_back(std::move(points));
        closed.push_back(true);
    }
    // Bounding box, rounded outwards.
    IRect bounds() const;
};

// One drawing command, kept unflattened so paths stay editable.
struct Segment {
    enum class Kind { MoveTo, LineTo, CubicTo, Close };
    Kind kind = Kind::MoveTo;
    float x = 0.0f, y = 0.0f;                  // MoveTo/LineTo endpoint
    float c1x = 0.0f, c1y = 0.0f;              // CubicTo first control
    float c2x = 0.0f, c2y = 0.0f;              // CubicTo second control
};

// Builds a Path from segments, flattening curves on demand.
struct PathBuilder {
    std::vector<Segment> segments;

    PathBuilder& moveTo(float x, float y);
    PathBuilder& lineTo(float x, float y);
    PathBuilder& cubicTo(float c1x, float c1y, float c2x, float c2y, float x,
                         float y);
    PathBuilder& close();
    PathBuilder& rect(const IRect& r);
    PathBuilder& ellipse(const IRect& r);
    PathBuilder& polygon(const IRect& r, std::uint32_t sides);

    // Flatten to polylines. `tolerance` is the maximum deviation in pixels.
    Path build(float tolerance) const;
};

// Flatten raw segments (same semantics as PathBuilder::build).
Path flattenSegments(const std::vector<Segment>& segs, float tolerance);

// How a stroke terminates at the free end of an open subpath.
enum class LineCap { Butt, Round, Square };

// How a stroke turns a corner.
enum class LineJoin { Round, Miter, Bevel };

struct StrokeStyle {
    float width = 1.0f;
    LineCap cap = LineCap::Butt;
    LineJoin join = LineJoin::Round;
    // Miter joins longer than this multiple of the width fall back to bevel.
    float miter_limit = 4.0f;

    StrokeStyle() = default;
    explicit StrokeStyle(float w) : width(w) {}
};

// A variable-width profile: multipliers over normalized arclength (per
// subpath). Empty (or hasProfile=false upstream) draws uniform.
struct WidthProfile {
    std::vector<float> pos;    // ascending, 0..1
    std::vector<float> scale;  // >= 0, same size as pos
};

// Multiplier at `t` (linear interpolation, clamped ends). 1.0 when empty.
float widthProfileAt(const WidthProfile& prof, float t);

// Rasterize `path` into an 8-bit coverage mask covering `rect`
// (rect.width() * rect.height() bytes, row-major).
std::vector<std::uint8_t> rasterize(const Path& path, const IRect& rect,
                                    FillRule rule);

// Expand a path's outline into a fillable path with round caps and joins.
Path strokeToPath(const Path& path, float width);

// Expand a path's outline into a path that can be filled with NonZero to draw
// the stroke. Every emitted piece is wound the same way, because under the
// nonzero rule opposite windings cancel and punch holes where pieces overlap.
Path strokePath(const Path& path, const StrokeStyle& style);

// Expand with per-point widths: `widths[i]` matches `path.subpaths[i]`
// point-for-point (mismatched entries fall back to style.width). `dash` is
// an on/off length list in the same units as the coordinates (empty =
// solid); dash pieces take the pen cap. Pieces are wound for nonzero fill,
// exactly like strokePath.
Path strokeVariable(const Path& path,
                    const std::vector<std::vector<float>>& widths,
                    const StrokeStyle& style, const std::vector<float>& dash,
                    float dashOffset);

}  // namespace pittore::vector
