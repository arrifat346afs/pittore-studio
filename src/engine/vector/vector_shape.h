#pragma once
// Anchor-based vector paths and the live shapes the decoder rebuilds from a
// document. A path stores each point with its two Bezier handles as offsets
// from the point, so moving an anchor carries its curvature with it. Filling
// and stroking flatten to the low-level Path in path.h and run through the
// scanline rasterizer there.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {

// One point with its incoming and outgoing Bezier handles, as offsets.
struct Anchor {
    float px = 0.0f, py = 0.0f;
    float hix = 0.0f, hiy = 0.0f;  // handle arriving at this anchor
    float hox = 0.0f, hoy = 0.0f;  // handle leaving it

    static Anchor corner(float x, float y) { return Anchor{x, y, 0, 0, 0, 0}; }
    static Anchor smooth(float x, float y, float hx, float hy) {
        return Anchor{x, y, -hx, -hy, hx, hy};
    }
};

struct SubPath {
    std::vector<Anchor> anchors;
    bool closed = false;
};

// A named path: one or more subpaths of anchors.
struct VectorPath {
    std::string name;
    std::vector<SubPath> subpaths;

    bool isEmpty() const {
        for (const auto& s : subpaths)
            if (!s.anchors.empty()) return false;
        return true;
    }
    // Anchors and their handles, rounded outwards.
    IRect bounds() const;
};

// A live shape: outline plus the paint that turns it into pixels.
struct VectorShape {
    VectorPath path;
    std::array<std::uint8_t, 4> fill{0, 0, 0, 0};
    bool hasStroke = false;
    std::array<std::uint8_t, 4> stroke{0, 0, 0, 0};
    float strokeWidth = 0.0f;
    // Even-odd rather than nonzero, so counter-subpaths punch holes.
    bool evenOdd = false;
};

struct GradientStop {
    float pos = 0.0f;
    std::array<std::uint8_t, 4> color{0, 0, 0, 0};
};

// A gradient fill: colour stops plus the axis (or centre and radius) it runs
// along, in the same space as the path.
struct GradientFill {
    std::vector<GradientStop> stops;
    double startX = 0.0, startY = 0.0;
    double endX = 0.0, endY = 0.0;
    bool radial = false;

    std::array<std::uint8_t, 4> colorAt(float t) const;
};

// A rendered shape: straight RGBA8 over `rect`, row-major.
struct ShapeImage {
    IRect rect;
    std::vector<std::uint8_t> rgba;
};

// Fill (solid or gradient-shaded), then stroke composited over it. Nullopt
// when the shape has no area or exceeds the pixel cap.
std::optional<ShapeImage> rasterizeShape(const VectorShape& shape,
                                         const GradientFill* gradient);

// Assemble a subpath from 18-byte path records. The marker pair classifies
// each record: (1,0)/(2,0) are on-curve points, (0,1) is the previous point's
// outgoing control and (0,2) the next point's incoming control. A closed
// path's trailing controls belong to the segment joining back to the start.
struct PathRecord {
    float x = 0.0f, y = 0.0f;
    std::uint8_t m0 = 0, m1 = 0;
};
std::optional<SubPath> subpathFromRecords(const std::vector<PathRecord>& records,
                                          bool closed);

// Circle-to-Bezier handle length as a fraction of the radius.
inline constexpr float kKappa = 0.5522848f;

// Geometry builders shared by the shape classes. All produce anchors in the
// caller's coordinate space.
std::vector<Anchor> ellipseAnchors(float x0, float y0, float x1, float y1);
std::vector<Anchor> roundedRectAnchors(float x0, float y0, float x1, float y1,
                                       const std::array<float, 4>& radii);
// Per-corner treatments: 0 rounded, 1 straight chamfer, 2 concave, 3 cutout.
std::vector<Anchor> corneredRectAnchors(float x0, float y0, float x1, float y1,
                                        const std::array<float, 4>& radii,
                                        const std::array<std::uint16_t, 4>& types);
std::vector<Anchor> arcAnchors(float cx, float cy, float rx, float ry, float t0,
                               float t1);
Anchor unitAnchor(float ux, float uy, float x0, float y0, float x1, float y1);
std::vector<Anchor> squareStarAnchors(std::uint32_t sides, float cut, float x0,
                                      float y0, float x1, float y1);
std::vector<Anchor> cloudAnchors(std::uint32_t bubbles, float meet, float x0,
                                 float y0, float x1, float y1);
std::optional<std::pair<std::pair<float, float>, float>> circleThrough(
    std::pair<float, float> p0, std::pair<float, float> p1,
    std::pair<float, float> p2);
std::vector<Anchor> heartAnchors(float x0, float y0, float x1, float y1,
                                 float spread);
// A circular arc in unit space from the top tip to the bottom tip, bowing
// sideways with sagitta `bow`/2; negative bows left.
std::vector<Anchor> bowArcUnit(float bow, bool downward);

// Flatten a path's anchors to the low-level curve form.
Path flattenPath(const VectorPath& path, float tolerance);

}  // namespace pittore::vector
