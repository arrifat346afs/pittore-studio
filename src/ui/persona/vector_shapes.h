#pragma once
// Live shape factory (ui/persona): parametric vector shapes for the canvas
// creation flow. Builders emit engine anchors in a LOCAL rect; makeShapeArt
// assembles them into a retained ArtNode via the battle-tested
// artNodeFromShape converter (same semantics as the SVG and .af importers).
// Geometry only — placement lives in the layer's offset/scale, like imports.
#include <QColor>
#include <QRectF>
#include <QString>

#include <memory>

#include "ui/tools/ids/tool_ids.h"

namespace pittore::vector {
struct ArtNode;
}

namespace pittore::ui {

struct ShapeStyle {
    bool hasFill = true;
    QColor fill;
    bool hasStroke = false;
    QColor stroke;
    double strokeWidth = 1.0;
    int cap = 0;   // ArtPaint: 0 butt, 1 round, 2 square
    int join = 0;  // ArtPaint: 0 miter, 1 round, 2 bevel
    // Corner treatments (rect family): per-corner radii win over radius;
    // cornerType 0 rounded, 1 chamfer, 2 concave, 3 cutout.
    double radius = 0.0;
    double rtl = -1.0, rtr = -1.0, rbr = -1.0, rbl = -1.0;
    int cornerType = 0;
    int sides = 5;
    double indent = 50.0;  // star inner %, 1..100
    double hole = 25.0;    // donut/cog hole %, 0..100
    double startDeg = 0.0, endDeg = 90.0;
    double skew = 25.0;  // parallelogram top shift, % of width
    // Line arrowheads: % of the stroke width (Arrow W/L options).
    bool startArrow = false;
    bool endArrow = false;
    double arrowWPct = 500.0;
    double arrowLPct = 1000.0;
    // QR Code tool options.
    QString qrContent;
    int qrSize = 256;
    int qrEcc = 0;
    // Custom Shape gallery index (customshape option).
    int customshape = 0;
};

class AppState;

// Read a tool's shape style from its options-bar values (fill defaults to the
// foreground, stroke to transparent — mirroring the options bar wells).
ShapeStyle shapeStyleFor(const AppState* state, ToolId tool);

// Build the shape's ArtNode in LOCAL coords (0,0,w,h), matrix identity — the
// creation flow anchors it with the layer's offset, exactly like an import.
// Null when the tool has no geometry (QRCode needs an encoder) or the rect is
// degenerate.
std::shared_ptr<pittore::vector::ArtNode> makeShapeArt(ToolId tool,
                                                        const QRectF& local,
                                                        const ShapeStyle& style);

}  // namespace pittore::ui
