// Vector paint end-to-end: SVG import → Appearance/Stroke panels →
// applyVectorPaint re-raster → undo restores. Runs headless (offscreen).
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

#include "test_util.h"
#include "ui/app_state.h"
#include "ui/icons.h"
#include "ui/persona/appearance_panel.h"
#include "ui/persona/stroke_panel.h"
#include "ui/persona/vector_edit.h"
#include "ui/persona/vector_node.h"
#include "ui/persona/vector_gradient.h"
#include "ui/persona/vector_build.h"
#include "ui/persona/vector_path_ops.h"
#include "ui/persona/vector_point_ops.h"
#include "ui/persona/vector_qr.h"
#include "ui/persona/vector_pen.h"
#include "ui/persona/vector_raster.h"
#include "ui/persona/vector_shapes.h"
#include "ui/persona/vector_view.h"
#include "ui/svg_parts.h"
#include "ui/tools/options/tool_options.h"

using namespace pittore::ui;

namespace {

const char* kSvg =
    "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"100\">"
    "<rect x=\"10\" y=\"10\" width=\"40\" height=\"20\" fill=\"#ff0000\"/>"
    "</svg>";

bool importRect(AppState& state) {
    SvgImportResult result;
    int dpi = 96;
    QString error;
    if (!svgPartsImport(QByteArray(kSvg), &result, &dpi, &error)) return false;
    QString openError;
    return state.openSvgParts(QStringLiteral("rect.svg"), result, dpi, &openError);
}

QColor compositeAt(AppState& state, int x, int y) {
    DocumentItem* d = state.activeDocument();
    if (!d || d->composite.isNull()) return QColor();
    return d->composite.pixelColor(x, y);
}

// Explicit bounds over segment endpoints (QRectF point-union ignores null
// rects, and its setters misbehave on them — plain doubles throughout).
QRectF segBounds(const std::vector<pittore::vector::Segment>& segs) {
    double x0 = 0.0, y0 = 0.0, x1 = 0.0, y1 = 0.0;
    bool first = true;
    for (const auto& s : segs) {
        if (s.kind == pittore::vector::Segment::Kind::Close) continue;
        if (first) {
            x0 = x1 = s.x;
            y0 = y1 = s.y;
            first = false;
        } else {
            x0 = std::min(x0, (double)s.x);
            y0 = std::min(y0, (double)s.y);
            x1 = std::max(x1, (double)s.x);
            y1 = std::max(y1, (double)s.y);
        }
    }
    return QRectF(QPointF(x0, y0), QPointF(x1, y1));
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppState state;

    // Every new vector tool resolves: options schema present, icon paints.
    {
        const QColor normal(200, 200, 200), active(255, 255, 255);
        for (ToolId id :
             {ToolId::NodeTool, ToolId::PointTransformTool, ToolId::CornerTool,
              ToolId::ContourTool, ToolId::StrokeWidthTool, ToolId::KnifeTool,
              ToolId::VectorBrushTool, ToolId::VectorFloodFillTool,
              ToolId::ShapeBuilderTool, ToolId::TransparencyTool,
              ToolId::StylePickerTool, ToolId::MeasureTool, ToolId::AreaTool,
              ToolId::VectorCropTool, ToolId::PlaceTool, ToolId::RoundedRectangle,
              ToolId::Diamond, ToolId::Trapezoid, ToolId::DoubleStar,
              ToolId::SquareStar, ToolId::Arrow, ToolId::Donut, ToolId::Pie,
              ToolId::Segment, ToolId::Crescent, ToolId::Cog, ToolId::Cloud,
              ToolId::CalloutRect, ToolId::CalloutEllipse, ToolId::Tear,
              ToolId::Heart, ToolId::Spiral, ToolId::QRCode, ToolId::Cat,
              ToolId::Hexagon, ToolId::Octagon, ToolId::Cross,
              ToolId::RightTriangle, ToolId::Parallelogram, ToolId::Chevron,
              ToolId::DoubleArrow, ToolId::CircularArrow, ToolId::Sparkle,
              ToolId::Shield, ToolId::Ticket, ToolId::Sun}) {
            CHECK(!optionsFor(id).empty());
            CHECK(!toolIcon(id, normal, active).isNull());
        }
    }

    // No document: nothing editable, panels show the hint without crashing.
    CHECK(vectorEditableLayer(&state) == -1);
    AppearancePanel appearance(&state);
    StrokePanel stroke(&state);

    // Import: one red-rect shape layer with retained geometry.
    CHECK(importRect(state));
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);
    CHECK(d->layers.size() == 1);
    CHECK(d->layers[0].art && !d->layers[0].art->isEmpty());
    CHECK(vectorEditableLayer(&state) == 0);
    CHECK(compositeAt(state, 30, 20).red() > 200);

    // Recolor through the same path the panels use: one undo step.
    {
        auto paint = d->layers[0].art->paint;
        paint.fill[0] = 0;
        paint.fill[2] = 255;
        CHECK(state.applyVectorPaint(0, paint, -1.0, QStringLiteral("Recolor")));
        CHECK(compositeAt(state, 30, 20).blue() > 200);
        CHECK(state.canUndo());
        state.undo();
        CHECK(compositeAt(state, 30, 20).red() > 200);
        CHECK(vectorEditableLayer(&state) == 0);
    }

    // Refusals leave the document untouched.
    {
        auto paint = d->layers[0].art->paint;
        CHECK(!state.applyVectorPaint(-1, paint, -1.0, QStringLiteral("Bad")));
        CHECK(!state.applyVectorPaint(99, paint, -1.0, QStringLiteral("Bad")));
    }

    // Shared path ops (Paths footer, task bar, runCommand): same apply path.
    {
        CHECK(vectorFillActiveShape(&state, QColor(0, 255, 0),
                                    QStringLiteral("Fill")));
        CHECK(compositeAt(state, 30, 20).green() > 200);
        state.undo();
        CHECK(compositeAt(state, 30, 20).red() > 200);

        CHECK(vectorStrokeActiveShape(&state, QColor(0, 0, 0),
                                      QStringLiteral("Stroke")));
        CHECK(d->layers[0].art->paint.hasStroke);
        state.undo();

        CHECK(vectorPathToSelection(&state));
        CHECK(state.activeDocument()->selectionIsMask ||
              !state.activeDocument()->selection.isEmpty());
        state.clearSelection();

        vectorPlannedHint(&state, QStringLiteral("Work paths"));
        CHECK(state.statusHint().contains(QStringLiteral("planned")));
    }

    // Node geometry: hit-test, translate (controls travel), outline path.
    {
        pittore::vector::ArtNode node;
        auto seg = [](pittore::vector::Segment::Kind kind, float x, float y) {
            pittore::vector::Segment s;
            s.kind = kind;
            s.x = x;
            s.y = y;
            return s;
        };
        node.segments = {seg(pittore::vector::Segment::Kind::MoveTo, 10, 10),
                         seg(pittore::vector::Segment::Kind::LineTo, 50, 10),
                         seg(pittore::vector::Segment::Kind::LineTo, 50, 30),
                         seg(pittore::vector::Segment::Kind::LineTo, 10, 30),
                         seg(pittore::vector::Segment::Kind::Close, 0, 0)};
        CHECK(nodeEndpoints(node).size() == 4);
        int hit = -1;
        CHECK(nodeEndpointAt(node, QPointF(10.2, 9.8), 1.0, &hit) == 0);
        CHECK(hit == 0);
        CHECK(nodeEndpointAt(node, QPointF(30, 20), 1.0) == -1);
        moveNodePoint(node, 1, QPointF(60, 10));
        CHECK(node.segments[1].x == 60.0f);

        // Cubic controls travel with their endpoint.
        pittore::vector::Segment c;
        c.kind = pittore::vector::Segment::Kind::CubicTo;
        c.c1x = 1;
        c.c1y = 2;
        c.c2x = 3;
        c.c2y = 4;
        c.x = 5;
        c.y = 6;
        pittore::vector::ArtNode curve;
        curve.segments = {c};
        moveNodePoint(curve, 0, QPointF(15, 16));
        CHECK(curve.segments[0].x == 15.0f);
        CHECK(curve.segments[0].c1x == 11.0f);
        CHECK(curve.segments[0].c2y == 14.0f);
        moveNodePoint(curve, 99, QPointF(0, 0));  // out of range: no-op

        const QPainterPath path = artNodePath(node);
        CHECK(!path.isEmpty());
        CHECK(path.boundingRect().width() == 50.0);
    }

    // Node commit end-to-end: shift the rect +10 x through applyVectorNode.
    {
        auto node = *d->layers[0].art;
        for (auto& s : node.segments) {
            if (s.kind != pittore::vector::Segment::Kind::Close) s.x += 10.0f;
        }
        CHECK(state.applyVectorNode(0, node, QStringLiteral("Node")));
        CHECK(compositeAt(state, 15, 20).alpha() < 16);
        CHECK(compositeAt(state, 55, 20).red() > 200);
        state.undo();
        CHECK(compositeAt(state, 30, 20).red() > 200);
    }

    // Measure math: units, dpi, drawing scale.
    {
        CHECK(vectorMeasureDisplay(100, 0, 96, 1.0) == 100.0);
        CHECK_NEAR(vectorMeasureDisplay(96, 2, 96, 1.0), 25.4, 0.01);
        CHECK_NEAR(vectorMeasureDisplay(96, 4, 96, 1.0), 1.0, 0.001);
        CHECK_NEAR(vectorMeasureDisplay(96, 2, 96, 2.0), 50.8, 0.01);
        CHECK(vectorMeasureDisplay(100, 9, 0, -1.0) == 100.0);
    }

    // Vector Crop composition: rect selection + reveal mask hides outside.
    {
        state.setSelection(QRectF(20, 15, 20, 10), false);
        CHECK(state.maskRevealSelection());
        CHECK(d->layers[0].hasMask);
        CHECK(compositeAt(state, 30, 20).red() > 200);
        CHECK(compositeAt(state, 12, 12).alpha() < 16);
        state.undo();
        CHECK(!d->layers[0].hasMask);
        CHECK(compositeAt(state, 12, 12).red() > 200);
        state.clearSelection();
    }

    // Shape builders: every tool yields geometry (or an honest null).
    {
        ShapeStyle st;
        st.fill = QColor(255, 0, 0);
        st.stroke = QColor(0, 0, 0);
        const QRectF box(0, 0, 40, 20);
        for (ToolId id :
             {ToolId::Rectangle, ToolId::Ellipse, ToolId::RoundedRectangle,
              ToolId::Triangle, ToolId::Diamond, ToolId::Trapezoid,
              ToolId::Polygon, ToolId::Hexagon, ToolId::Octagon, ToolId::Star,
              ToolId::Sparkle, ToolId::DoubleStar, ToolId::SquareStar,
              ToolId::Arrow, ToolId::DoubleArrow, ToolId::Donut, ToolId::Pie,
              ToolId::Segment, ToolId::Crescent, ToolId::Cog, ToolId::Cloud,
              ToolId::CalloutRect, ToolId::CalloutEllipse, ToolId::Tear,
              ToolId::Heart, ToolId::CustomShape, ToolId::Spiral, ToolId::Line,
              ToolId::Cross, ToolId::RightTriangle, ToolId::Parallelogram,
              ToolId::Chevron, ToolId::CircularArrow, ToolId::Shield,
              ToolId::Ticket, ToolId::Sun, ToolId::Cat}) {
            auto node = makeShapeArt(id, box, st);
            CHECK(node && !node->segments.empty());
        }
        CHECK(!makeShapeArt(ToolId::QRCode, box, st));
        CHECK(!makeShapeArt(ToolId::Rectangle, QRectF(), st));
        auto donut = makeShapeArt(ToolId::Donut, box, st);
        CHECK(donut && donut->evenOdd);
        auto rect = makeShapeArt(ToolId::Rectangle, box, st);
        CHECK(rect && !rect->evenOdd);
    }

    // Shape creation end-to-end: blue rect layer, undo removes it.
    {
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(60, 60, 20, 20),
                                        QStringLiteral("Rectangle")));
        CHECK(d->layers.size() == 2);
        CHECK(d->layers[0].art && !d->layers[0].art->isEmpty());
        CHECK(compositeAt(state, 70, 70).blue() > 200);
        state.undo();
        CHECK(d->layers.size() == 1);
        CHECK(compositeAt(state, 70, 70).alpha() < 16);
    }

    // Fresh-option defaults: open-path tools fall back to foreground stroke
    // (Line options call it "weight"), closed shapes to fill-only.
    {
        AppState fresh;
        const ShapeStyle line = shapeStyleFor(&fresh, ToolId::Line);
        CHECK(line.hasStroke);
        CHECK(line.strokeWidth > 0.0);
        CHECK(line.stroke == fresh.foreground());
        const ShapeStyle rect = shapeStyleFor(&fresh, ToolId::Rectangle);
        CHECK(rect.hasFill);
        CHECK(!rect.hasStroke);
        CHECK(rect.fill == fresh.foreground());
    }

    // Vector-direct placement: 4x upscale re-renders edges crisp from
    // geometry. A resampled bake would stretch the AA ramp ~4x wider, so the
    // texels just outside/inside the edge discriminate the two. Sized to stay
    // inside the 100 px test doc (edges must not clip for the ramp check).
    {
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(20, 20, 15, 15),
                                        QStringLiteral("RT")));
        DocumentItem* dd = state.activeDocument();
        state.beginUndoStep();
        CHECK(state.setActiveLayerPlacement(QPointF(20, 20), 4.0, 4.0));
        CHECK(state.refreshVectorArt());
        state.commitUndoStep(QStringLiteral("Big"), QStringLiteral("move"));
        // Edge-relative: trim margins move the exact edge, so read it off
        // the layer bounds (a resampled bake would smear ~4x wider here).
        {
            const QRectF b = layerBounds(*dd, dd->layers[0]);
            const int ex = qRound(b.left());
            const int ey = qRound(b.center().y());
            CHECK(compositeAt(state, ex - 2, ey).alpha() < 16);
            CHECK(compositeAt(state, ex + 4, ey).blue() > 200);
        }
        state.undo();
        CHECK(compositeAt(state, 27, 27).blue() > 200);
        state.undo();
    }

    // Dense display bakes: zoom-coupled supersampling from geometry.
    {
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(20, 20, 15, 15),
                                        QStringLiteral("DZ")));        DocumentItem* dd = state.activeDocument();
        // At 1:1 no dense bake exists (pixels path is already exact).
        CHECK(!dd->layers[0].styled);
        // Zoom in and re-bake: supersampled display data appears.
        dd->zoom = 4.0;
        CHECK(state.rezoomVectorArt());
        CHECK(dd->layers[0].styled);
        CHECK(dd->layers[0].styledResample == 4.0);
        CHECK(dd->layers[0].styledValid);
        CHECK(compositeAt(state, 27, 27).blue() > 200);
        // Zoom back out: the dense bake is dropped, pixels serve again.
        dd->zoom = 1.0;
        CHECK(state.rezoomVectorArt());
        CHECK(!dd->layers[0].styled);
        CHECK(compositeAt(state, 27, 27).blue() > 200);
        // Extended bucket: deep zoom keeps supersampling (cap-bounded).
        dd->zoom = 16.0;
        CHECK(state.rezoomVectorArt());
        CHECK(dd->layers[0].styledResample == 16.0);
        dd->zoom = 1.0;
        CHECK(state.rezoomVectorArt());
        // Undo hook: a stale dense bake is re-evaluated on undo.
        dd->zoom = 4.0;
        CHECK(state.rezoomVectorArt());
        CHECK(dd->layers[0].styledResample == 4.0);
        state.beginUndoStep();
        CHECK(state.setActiveLayerPlacement(QPointF(25, 25), 1.0, 1.0));
        state.commitUndoStep(QStringLiteral("Nudge"), QStringLiteral("move"));
        // Undo restores the pre-move snapshot atomically (pixels, art AND
        // display bake travel together), so the dense bake survives intact.
        state.undo();
        CHECK(dd->layers[0].styled);
        CHECK(dd->layers[0].styledResample == 4.0);
        CHECK(compositeAt(state, 27, 27).blue() > 200);
        state.undo();  // remove the shape; later blocks assume red-only doc
    }

    // View-order predicate: simple art is drawable, everything fancy bakes.
    {
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(10, 10, 20, 20),
                                        QStringLiteral("VO")));
        DocumentItem* dd = state.activeDocument();
        CHECK(vectorViewOrder(*dd).contains(0));
        // Hidden art is skipped, not drawn.
        dd->layers[0].visible = false;
        CHECK(!vectorViewOrder(*dd).contains(0));
        dd->layers[0].visible = true;
        // Non-normal blend forces the raster path.
        dd->layers[0].blendMode = QStringLiteral("Multiply");
        CHECK(!vectorViewOrder(*dd).contains(0));
        dd->layers[0].blendMode = QStringLiteral("Normal");
        CHECK(vectorViewOrder(*dd).contains(0));
        // A mask forces the raster path.
        dd->layers[0].hasMask = true;
        CHECK(!vectorViewOrder(*dd).contains(0));
        dd->layers[0].hasMask = false;
        // An adjustment above keeps vectors raster; below leaves them drawable.
        CHECK(state.addAdjustmentLayer(1));  // BrightnessContrast
        int blueAt = -1;
        for (int i = 0; i < dd->layers.size(); ++i) {
            if (dd->layers[i].art && !dd->layers[i].art->isEmpty()) blueAt = i;
        }
        CHECK(blueAt >= 0);
        CHECK(!vectorViewOrder(*dd).contains(blueAt));
        state.undo();  // drop the adjustment; red+blue docs undisturbed
        blueAt = -1;
        for (int i = 0; i < dd->layers.size(); ++i) {
            if (dd->layers[i].art && !dd->layers[i].art->isEmpty()) blueAt = i;
        }
        CHECK(blueAt >= 0);
        CHECK(vectorViewOrder(*dd).contains(blueAt));
        state.undo();  // remove the VO shape
    }

    // Pen path model: corners, smooth drags, curvature, freehand, close.
    {
        using Kind = pittore::vector::Segment::Kind;
        PenPath path;
        path.addCorner(QPointF(10, 10));
        path.addCorner(QPointF(30, 10));
        path.addCorner(QPointF(30, 30));
        const auto segs = path.toSegments(PenMode::Bezier);
        CHECK(segs.size() == 3);
        CHECK(segs[0].kind == Kind::MoveTo);
        CHECK(segs[1].kind == Kind::LineTo);
        CHECK(segs[2].kind == Kind::LineTo);
        CHECK(path.closeHit(QPointF(11, 11), 8.0));
        CHECK(!path.closeHit(QPointF(60, 60), 8.0));
        path.setClosed(true);
        const auto closed = path.toSegments(PenMode::Bezier);
        CHECK(closed.back().kind == Kind::Close);

        // A press-drag smooth point makes its spans cubic.
        PenPath smooth;
        smooth.addCorner(QPointF(10, 10));
        smooth.addSmooth(QPointF(30, 10), QPointF(10, 0));
        const auto ssegs = smooth.toSegments(PenMode::Bezier);
        CHECK(ssegs.size() == 2);
        CHECK(ssegs[1].kind == Kind::CubicTo);
        // A near-zero drag demotes back to a corner.
        smooth.demoteLastToCorner(100.0);
        CHECK(smooth.toSegments(PenMode::Bezier)[1].kind ==
              Kind::LineTo);

        // Curvature fits one cubic per span through the anchors.
        PenPath curve;
        curve.addCorner(QPointF(0, 0));
        curve.addCorner(QPointF(10, 0));
        curve.addCorner(QPointF(20, 10));
        curve.addCorner(QPointF(30, 10));
        const auto csegs = curve.toSegments(PenMode::Curvature);
        CHECK(csegs.size() == 4);
        CHECK(csegs[0].kind == Kind::MoveTo);
        for (std::size_t i = 1; i < csegs.size(); ++i)
            CHECK(csegs[i].kind == Kind::CubicTo);

        // Freehand: spacing gate plus collinear collapse.
        PenPath free;
        free.addFreehand(QPointF(0, 0), 2.0);
        free.addFreehand(QPointF(0.5, 0), 2.0);
        CHECK(free.size() == 1);
        free.addFreehand(QPointF(5, 0), 2.0);
        free.addFreehand(QPointF(10, 0.1), 2.0);
        free.addFreehand(QPointF(15, 0), 2.0);
        free.simplifyFreehand(1.0);
        CHECK(free.size() == 2);
        const auto fsegs = free.toSegments(PenMode::Freehand);
        CHECK(fsegs.size() == 2);
        CHECK(fsegs[1].kind == Kind::LineTo);
    }

    // Pen commit end-to-end: open stroke-only, closed keeps fill, undo.
    {
        using Kind = pittore::vector::Segment::Kind;
        state.addDocument(QStringLiteral("pen"), QSize(100, 100), 300);
        state.setForeground(QColor(255, 0, 0));
        DocumentItem* dd = state.activeDocument();
        const int n0 = dd ? (int)dd->layers.size() : -1;
        CHECK(n0 >= 1);
        // Open Pen path: stroke-only paint on its own layer.
        PenPath open;
        open.addCorner(QPointF(20, 20));
        open.addSmooth(QPointF(60, 20), QPointF(10, -10));
        CHECK(state.addVectorPathLayer(open.toSegments(PenMode::Bezier),
                                       ToolId::Pen, QStringLiteral("Pen")));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 + 1);
        const LayerItem* art = nullptr;
        for (const LayerItem& l : dd->layers)
            if (l.art && !l.art->isEmpty()) art = &l;
        CHECK(art != nullptr);
        CHECK(art->art->paint.hasStroke);
        CHECK(!art->art->paint.hasFill);
        // Freehand: thick foreground stream, solid composite mark.
        PenPath stream;
        stream.addFreehand(QPointF(20, 40), 0.0);
        stream.addFreehand(QPointF(40, 40), 0.0);
        stream.addFreehand(QPointF(60, 40), 0.0);
        CHECK(state.addVectorPathLayer(stream.toSegments(PenMode::Freehand),
                                       ToolId::FreeformPen,
                                       QStringLiteral("Freehand")));
        CHECK(compositeAt(state, 40, 40).red() > 150 &&
              compositeAt(state, 40, 40).green() < 100);
        // Closed Curvature path: keeps the bar fill, paints its interior.
        PenPath tri;
        tri.addCorner(QPointF(20, 60));
        tri.addCorner(QPointF(60, 60));
        tri.addCorner(QPointF(40, 84));
        tri.setClosed(true);
        CHECK(state.addVectorPathLayer(tri.toSegments(PenMode::Curvature),
                                       ToolId::CurvaturePen,
                                       QStringLiteral("Curve")));
        dd = state.activeDocument();
        const LayerItem* triArt = nullptr;
        for (const LayerItem& l : dd->layers)
            if (l.art && !l.art->isEmpty() &&
                !l.art->segments.empty() &&
                l.art->segments.back().kind == Kind::Close)
                triArt = &l;
        CHECK(triArt != nullptr);
        CHECK(triArt->art->paint.hasFill);
        CHECK(compositeAt(state, 40, 68).red() > 150 &&
              compositeAt(state, 40, 68).green() < 100);
        // Degenerate (single anchor) refuses with no mutation.
        PenPath dot;
        dot.addCorner(QPointF(5, 5));
        const int n1 = (int)dd->layers.size();
        CHECK(!state.addVectorPathLayer(dot.toSegments(PenMode::Bezier),
                                        ToolId::Pen, QStringLiteral("Pen")));
        CHECK((int)state.activeDocument()->layers.size() == n1);
        state.undo();  // drop the triangle
        CHECK((int)state.activeDocument()->layers.size() == n1 - 1);
    }

    // Node Bezier handles: hit-test, mirrored drag, solo drag, convert,
    // close. Anchors: (10,10) move, cubic to (40,10), line to (60,10).
    {
        using Kind = pittore::vector::Segment::Kind;
        pittore::vector::ArtNode node;
        auto move = [&](float x, float y) {
            pittore::vector::Segment s;
            s.kind = Kind::MoveTo;
            s.x = x;
            s.y = y;
            node.segments.push_back(s);
        };
        auto cubic = [&](float c1x, float c1y, float c2x, float c2y, float x,
                         float y) {
            pittore::vector::Segment s;
            s.kind = Kind::CubicTo;
            s.c1x = c1x;
            s.c1y = c1y;
            s.c2x = c2x;
            s.c2y = c2y;
            s.x = x;
            s.y = y;
            node.segments.push_back(s);
        };
        auto line = [&](float x, float y) {
            pittore::vector::Segment s;
            s.kind = Kind::LineTo;
            s.x = x;
            s.y = y;
            node.segments.push_back(s);
        };
        move(10, 10);
        cubic(20, 10, 30, 10, 40, 10);
        line(60, 10);
        // Two handles: anchor0 out (seg1 c1) and anchor1 in (seg1 c2).
        const auto handles = nodeHandles(node);
        CHECK(handles.size() == 2);
        const NodeHandle hit =
            nodeHandleAt(node, QPointF(30, 11), 8.0);
        CHECK(hit.anchorSeg == 1);
        CHECK(hit.side == NodeHandleSide::In);
        CHECK(nodeHandleAt(node, QPointF(0, 90), 8.0).anchorSeg == -1);
        // Anchor1 has in but no out: not mirrored.
        CHECK(!nodeHandlesMirrored(node, 1, 1e-6));
        // Smooth convert rebuilds mirrored handles along the neighbours
        // (anchor0's out-handle rides along untouched: 3 tips now).
        convertNodePoint(node, 1, true);
        CHECK(nodeHandles(node).size() == 3);
        CHECK(nodeHandlesMirrored(node, 1, 1e-3));
        // Mirrored drag: the opposite handle follows through the anchor.
        NodeHandle out = nodeHandleAt(node, QPointF(50, 10), 20.0);
        CHECK(out.anchorSeg == 1 && out.side == NodeHandleSide::Out);
        const QPointF inBefore = nodeHandleAt(node, QPointF(30, 10), 20.0).pos;
        moveNodeHandle(node, 1, NodeHandleSide::Out, QPointF(52, 16), true);
        const QPointF inAfter = nodeHandleAt(node, QPointF(28, 4), 20.0).pos;
        CHECK(std::abs((inAfter.x() - 40.0) + (52.0 - 40.0)) < 0.01);
        CHECK(std::abs((inAfter.y() - 10.0) + (16.0 - 10.0)) < 0.01);
        CHECK(inBefore != inAfter);
        // Solo drag (Alt): the opposite handle stays put.
        const QPointF pinned = nodeHandleAt(node, QPointF(28, 4), 20.0).pos;
        moveNodeHandle(node, 1, NodeHandleSide::Out, QPointF(54, 18), false);
        CHECK(nodeHandleAt(node, QPointF(28, 4), 20.0).pos == pinned);
        CHECK(!nodeHandlesMirrored(node, 1, 1e-6));
        // Corner convert retracts both spans to lines: handles gone.
        convertNodePoint(node, 1, false);
        CHECK(nodeHandles(node).empty());
        CHECK(node.segments[1].kind == Kind::LineTo);
        CHECK(node.segments[2].kind == Kind::LineTo);
        // Close appends Close once, then refuses.
        CHECK(closeNodePath(node));
        CHECK(node.segments.back().kind == Kind::Close);
        CHECK(!closeNodePath(node));
    }

    // Point ops: corner rounding, contour offset, knife cuts, transforms.
    {
        using Kind = pittore::vector::Segment::Kind;
        auto rectNode = []() {
            pittore::vector::ArtNode n;
            auto move = [&](float x, float y) {
                pittore::vector::Segment s;
                s.kind = Kind::MoveTo;
                s.x = x;
                s.y = y;
                n.segments.push_back(s);
            };
            auto line = [&](float x, float y) {
                pittore::vector::Segment s;
                s.kind = Kind::LineTo;
                s.x = x;
                s.y = y;
                n.segments.push_back(s);
            };
            move(0, 0);
            line(40, 0);
            line(40, 20);
            line(0, 20);
            pittore::vector::Segment c;
            c.kind = Kind::Close;
            n.segments.push_back(c);
            return n;
        };
        // Corner: rounding inserts a fillet cubic, drops the sharp anchor.
        {
            auto n = rectNode();
            CHECK(roundNodeCorner(n, 1, 5.0));
            CHECK(n.segments.size() == 6);
            bool hasCubic = false, sharpLeft = false;
            for (const auto& s : n.segments) {
                if (s.kind == Kind::CubicTo) hasCubic = true;
                if ((s.kind == Kind::LineTo || s.kind == Kind::MoveTo ||
                     s.kind == Kind::CubicTo) &&
                    std::abs(s.x - 40.0) < 0.1 && std::abs(s.y - 0.0) < 0.1)
                    sharpLeft = true;
            }
            CHECK(hasCubic);
            CHECK(!sharpLeft);
            CHECK(!roundNodeCorner(n, 0, 0.0));  // zero radius refuses
        }
        // Contour: outward grows bounds by ~r, inward shrinks.
        {
            auto n = rectNode();
            CHECK(contourNodePath(n, 5.0, 1));
            const QRectF box = segBounds(n.segments);
            CHECK(box.left() < -3.0 && box.top() < -3.0);
            CHECK(box.right() > 43.0 && box.bottom() > 23.0);
            auto m = rectNode();
            CHECK(contourNodePath(m, -5.0, 1));
            const QRectF ibox = segBounds(m.segments);
            CHECK(ibox.left() > 3.0 && ibox.top() > 3.0);
            CHECK(ibox.right() < 37.0 && ibox.bottom() < 17.0);
            // Open paths refuse.
            auto open = rectNode();
            open.segments.pop_back();
            CHECK(!contourNodePath(open, 5.0, 1));
        }
        // Knife: diagonal cut crosses top+bottom (2 cuts, 3 subpaths).
        {
            auto n = rectNode();
            const int cuts = knifeCutNode(n, QPointF(-10, -10),
                                          QPointF(50, 30), 0.25, 0);
            CHECK(cuts == 2);
            int moves = 0;
            for (const auto& s : n.segments)
                if (s.kind == Kind::MoveTo) ++moves;
            CHECK(moves == 3);
            // A miss cuts nothing.
            auto m = rectNode();
            CHECK(knifeCutNode(m, QPointF(100, 100), QPointF(200, 200),
                               0.25, 0) == 0);
        }
        // Transforms: scale doubles about centroid, rotate swaps extents.
        {
            auto n = rectNode();
            const QPointF c = nodeAnchorCentroid(n);
            CHECK(std::abs(c.x() - 20.0) < 1e-9);
            CHECK(std::abs(c.y() - 10.0) < 1e-9);
            transformNodePoints(n, c, 2.0, 0.0);
            double w = -1e300, h = -1e300;
            double x0 = 1e300, y0 = 1e300;
            for (const auto& s : n.segments) {
                if (s.kind == Kind::Close) continue;
                x0 = std::min(x0, (double)s.x);
                y0 = std::min(y0, (double)s.y);
                w = std::max(w, (double)s.x);
                h = std::max(h, (double)s.y);
            }
            CHECK(std::abs((w - x0) - 80.0) < 1e-3);
            CHECK(std::abs((h - y0) - 40.0) < 1e-3);
            auto m = rectNode();
            transformNodePoints(m, nodeAnchorCentroid(m), 1.0, 90.0);
            double w2 = -1e300, h2 = -1e300, x02 = 1e300, y02 = 1e300;
            for (const auto& s : m.segments) {
                if (s.kind == Kind::Close) continue;
                x02 = std::min(x02, (double)s.x);
                y02 = std::min(y02, (double)s.y);
                w2 = std::max(w2, (double)s.x);
                h2 = std::max(h2, (double)s.y);
            }
            CHECK(std::abs((w2 - x02) - 20.0) < 1e-3);
            CHECK(std::abs((h2 - y02) - 40.0) < 1e-3);
        }
    }

    // Brush ribbon + arrowheads + gradient/transparency + dash codec.
    {
        using Kind = pittore::vector::Segment::Kind;
        // Ribbon covers the centerline plus half width and round caps.
        BrushStroke stroke;
        stroke.addDab(QPointF(0, 0), 10.0);
        stroke.addDab(QPointF(20, 0), 10.0);
        const auto ribbon = stroke.buildRibbon();
        CHECK(!ribbon.empty());
        CHECK(ribbon.back().kind == Kind::Close);
        const QRectF box = segBounds(ribbon);
        CHECK(box.left() < -4.0 && box.right() > 24.0);
        CHECK(box.top() < -4.0 && box.bottom() > 4.0);
        // Single dab disc: kappa-circle (move + 4 cubics + close).
        BrushStroke dot;
        dot.addDab(QPointF(5, 5), 8.0);
        const auto disc = dot.buildRibbon();
        CHECK(disc.size() == 6);
        CHECK(disc.back().kind == Kind::Close);
        int cubicCount = 0;
        for (const auto& s : disc)
            if (s.kind == Kind::CubicTo) ++cubicCount;
        CHECK(cubicCount == 4);
        // Multi-dab edges are all cubic (no faceted polylines).
        {
            int lines = 0, cubics = 0;
            for (const auto& s : ribbon) {
                if (s.kind == Kind::LineTo) ++lines;
                if (s.kind == Kind::CubicTo) ++cubics;
            }
            CHECK(lines == 0);
            CHECK(cubics > 4);
        }
        // Flat nib: a horizontal stroke paints thin, a vertical one wide.
        {
            BrushStroke h, v;
            for (int i = 0; i <= 10; ++i) {
                h.addDab(QPointF(i * 4.0, 0), 10.0);
                v.addDab(QPointF(0, i * 4.0), 10.0);
            }
            const QRectF hb = segBounds(h.buildRibbon(0.3, 0.0, false));
            const QRectF vb = segBounds(v.buildRibbon(0.3, 0.0, false));
            CHECK(hb.height() < vb.width());
            CHECK(vb.width() > 8.0);
            // Square nib single dab outlines a rotated square.
            BrushStroke sq;
            sq.addDab(QPointF(0, 0), 10.0);
            const auto sqr = sq.buildRibbon(1.0, 0.0, true);
            CHECK(!sqr.empty());
            CHECK(sqr.back().kind == Kind::Close);
        }
        // Loop-back stroke stays solid: rasterize and probe the doubled
        // region (the old outline slitted here under winding fill).
        {
            BrushStroke straight;
            straight.addDab(QPointF(0, 0), 12.0);
            straight.addDab(QPointF(40, 0), 12.0);
            pittore::vector::ArtNode snode;
            snode.segments = straight.buildRibbon();
            snode.paint.hasFill = true;
            snode.paint.fill[0] = 0;
            snode.paint.fill[1] = 0;
            snode.paint.fill[2] = 0;
            snode.paint.fill[3] = 255;
            snode.evenOdd = false;
            QImage simg;
            QPointF sorigin;
            CHECK(rasterizeArtNode(snode, &simg, &sorigin));
            const QColor sc = simg.pixelColor(int(20 - sorigin.x()),
                                              int(0 - sorigin.y()));
            CHECK(sc.alpha() > 200 && sc.red() < 80);
            BrushStroke loop;
            for (int i = 0; i <= 10; ++i)
                loop.addDab(QPointF(i * 4.0, 0), 12.0);
            for (int i = 9; i >= 0; --i)
                loop.addDab(QPointF(i * 4.0, 3.0), 12.0);
            auto probeSolid = [&](BrushStroke& st, double qx, double qy) {
                pittore::vector::ArtNode n2;
                n2.segments = st.buildRibbon();
                n2.paint.hasFill = true;
                n2.paint.fill[0] = 0;
                n2.paint.fill[1] = 0;
                n2.paint.fill[2] = 0;
                n2.paint.fill[3] = 255;
                n2.evenOdd = false;
                QImage im2;
                QPointF o2;
                if (!rasterizeArtNode(n2, &im2, &o2) || im2.isNull())
                    return QColor();
                return im2.pixelColor(int(qx - o2.x()), int(qy - o2.y()));
            };
            // Hairpin reversals stay solid at every arm gap (winding
            // fill covers the doubled region; odd-even would slit it).
            // The probe sits on the first pass, present in all cases.
            for (double gap : {3.0, 8.0, 14.0}) {
                BrushStroke tst;
                for (int i = 0; i <= 10; ++i)
                    tst.addDab(QPointF(i * 4.0, 0), 12.0);
                for (int i = 9; i >= 0; --i)
                    tst.addDab(QPointF(i * 4.0, gap), 12.0);
                const QColor c = probeSolid(tst, 20, 0);
                CHECK(c.alpha() > 200 && c.red() < 80);
            }
            // Same-direction double pass (overlap, no reversal).
            {
                BrushStroke dbl;
                for (int i = 0; i <= 10; ++i)
                    dbl.addDab(QPointF(i * 4.0, 0), 12.0);
                for (int i = 0; i <= 10; ++i)
                    dbl.addDab(QPointF(i * 4.0, 3.0), 12.0);
                const QColor c = probeSolid(dbl, 20, 1);
                CHECK(c.alpha() > 200 && c.red() < 80);
            }
            // Every emitted coordinate is finite (a NaN would poison the
            // whole fill while leaving the stroked outline drawable).
            {
                const auto segs = loop.buildRibbon();
                for (const auto& s : segs) {
                    CHECK(std::isfinite(s.x));
                    CHECK(std::isfinite(s.y));
                    CHECK(std::isfinite(s.c1x));
                    CHECK(std::isfinite(s.c1y));
                    CHECK(std::isfinite(s.c2x));
                    CHECK(std::isfinite(s.c2y));
                }
            }
            pittore::vector::ArtNode node;
            node.segments = loop.buildRibbon();
            node.paint.hasFill = true;
            node.paint.fill[0] = 0;
            node.paint.fill[1] = 0;
            node.paint.fill[2] = 0;
            node.paint.fill[3] = 255;
            node.evenOdd = false;
            QImage img;
            QPointF origin;
            CHECK(rasterizeArtNode(node, &img, &origin));
            CHECK(!img.isNull());
            // Image pixel = document point minus the returned origin.
            const QColor mid = img.pixelColor(int(20 - origin.x()),
                                              int(0 - origin.y()));
            std::printf("[vbrush] loop mid=(%d,%d,%d,%d) origin=(%.1f,%.1f) "
                        "img=%dx%d\n",
                        mid.red(), mid.green(), mid.blue(), mid.alpha(),
                        origin.x(), origin.y(), img.width(), img.height());
            CHECK(mid.alpha() > 200 && mid.red() < 80);
        }
        // Line arrowheads: two head triangles, stroke-colour fill.
        ShapeStyle ast;
        ast.hasFill = false;
        ast.hasStroke = true;
        ast.stroke = QColor(255, 0, 0);
        ast.strokeWidth = 2.0;
        ast.startArrow = ast.endArrow = true;
        auto arrow = makeShapeArt(ToolId::Line, QRectF(0, 0, 40, 10), ast);
        CHECK(arrow != nullptr);
        int closes = 0;
        for (const auto& s : arrow->segments)
            if (s.kind == Kind::Close) ++closes;
        CHECK(closes == 2);
        CHECK(arrow->paint.hasFill);
        CHECK(arrow->paint.fill[0] == 255);
        // Gradient presets + transparency ramp.
        const auto stops = gradientPresetStops(0, QColor(255, 0, 0),
                                               QColor(0, 0, 255), false);
        CHECK(stops.size() == 2);
        CHECK(stops.front().pos == 0.0f && stops.back().pos == 1.0f);
        const auto rev = gradientPresetStops(0, QColor(255, 0, 0),
                                             QColor(0, 0, 255), true);
        CHECK(rev.size() == 2);
        CHECK(rev.front().pos == 0.0f && rev.back().pos == 1.0f);
        // Reversed: the background colour now leads.
        CHECK(rev.front().rgba[2] == 255 && rev.front().rgba[0] == 0);
        pittore::vector::ArtPaint base;
        const auto grad = paintWithGradient(base, QPointF(0, 0),
                                            QPointF(40, 0), false, stops);
        CHECK(grad.hasGradient && !grad.gradient.radial);
        CHECK(grad.gradient.x2 == 40.0);
        const auto transp = paintWithTransparency(base, QPointF(0, 0),
                                                  QPointF(0, 40), 1, false,
                                                  QColor(0, 255, 0));
        CHECK(transp.hasGradient && transp.hasFill);
        CHECK(transp.gradient.stops.front().rgba[3] == 255);
        CHECK(transp.gradient.stops.back().rgba[3] == 0);
        const auto none = paintWithTransparency(transp, QPointF(0, 0),
                                                QPointF(0, 40), 0, false,
                                                QColor(0, 255, 0));
        CHECK(!none.hasGradient);
        // Dash binary round-trip (tail block).
        pittore::vector::ArtNode dn;
        dn.paint.hasStroke = true;
        dn.paint.hasDash = true;
        dn.paint.dash = {4.0f, 2.0f};
        dn.paint.dashOffset = 1.0f;
        pittore::vector::Segment ms;
        ms.kind = Kind::MoveTo;
        ms.x = 1.0f;
        ms.y = 2.0f;
        dn.segments.push_back(ms);
        const auto bytes = pittore::vector::encodeArtNode(dn);
        const auto back = pittore::vector::decodeArtNode(bytes);
        CHECK(back.has_value());
        CHECK(back->paint.hasDash);
        CHECK(back->paint.dash.size() == 2);
        CHECK(std::abs(back->paint.dash[0] - 4.0f) < 1e-6);
        CHECK(std::abs(back->paint.dashOffset - 1.0f) < 1e-6);
    }

    // Flood fill + Shape Builder end-to-end.
    {
        using Kind = pittore::vector::Segment::Kind;
        // QPainterPath rect converts with a closing segment.
        QPainterPath rect;
        rect.addRect(QRectF(0, 0, 10, 10));
        const auto rsegs = painterPathToSegments(rect);
        CHECK(rsegs.size() >= 5);
        CHECK(rsegs.back().kind == Kind::Close);

        state.addDocument(QStringLiteral("build"), QSize(100, 100), 300);
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(10, 10, 30, 20),
                                        QStringLiteral("R1")));
        state.setForeground(QColor(0, 255, 0));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(30, 20, 30, 20),
                                        QStringLiteral("R2")));
        DocumentItem* dd = state.activeDocument();
        const int n0 = (int)dd->layers.size();
        // Flood inside R1 (away from R2) adds a red fill layer.
        state.setForeground(QColor(255, 0, 0));
        CHECK(state.floodFillVectorArt(QPointF(15, 15)));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 + 1);
        const LayerItem* fill = nullptr;
        for (const LayerItem& l : dd->layers)
            if (l.art && !l.art->isEmpty() && l.art->paint.hasFill &&
                l.art->paint.fill[0] == 255)
                fill = &l;
        CHECK(fill != nullptr);
        // Smart refill repaints the hit art instead of adding.
        state.setOption(ToolId::VectorFloodFillTool, QStringLiteral("fill_mode"), 1);
        state.setForeground(QColor(255, 255, 0));
        CHECK(state.floodFillVectorArt(QPointF(15, 15)));
        CHECK((int)state.activeDocument()->layers.size() == n0 + 1);
        bool yellow = false;
        for (const LayerItem& l : state.activeDocument()->layers)
            if (l.art && !l.art->isEmpty() && l.art->paint.hasFill &&
                l.art->paint.fill[0] == 255 && l.art->paint.fill[1] == 255)
                yellow = true;
        CHECK(yellow);
        state.setOption(ToolId::VectorFloodFillTool, QStringLiteral("fill_mode"), 0);
        // Builder Add combines the three art layers into one.
        CHECK(state.shapeBuilderAt(QRectF(5, 5, 60, 40), 0));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 - 1);
        int moves = 0;
        for (const LayerItem& l : dd->layers)
            if (l.art && !l.art->isEmpty())
                for (const auto& s : l.art->segments)
                    if (s.kind == Kind::MoveTo) ++moves;
        CHECK(moves >= 3);
        // Builder Delete (click) removes it; undo restores everything.
        CHECK(state.shapeBuilderAt(QRectF(20, 20, 1, 1), 1));
        CHECK((int)state.activeDocument()->layers.size() == n0 - 2);
        state.undo();
        CHECK((int)state.activeDocument()->layers.size() == n0 - 1);
        state.undo();
        CHECK((int)state.activeDocument()->layers.size() == n0 + 1);
    }

    // Text on Path: glyph outlines along an open curve, baked vector.
    {
        state.addDocument(QStringLiteral("textpath"), QSize(200, 100), 300);
        state.setForeground(QColor(255, 0, 0));
        PenPath rail;
        rail.addCorner(QPointF(10, 50));
        rail.addCorner(QPointF(190, 50));
        CHECK(state.addVectorPathLayer(rail.toSegments(PenMode::Bezier),
                                       ToolId::Pen, QStringLiteral("Rail")));
        DocumentItem* dd = state.activeDocument();
        const int n0 = (int)dd->layers.size();
        CHECK(state.textOnPath(QStringLiteral("Hi"), QString(), 24.0));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 + 1);
        const LayerItem* glyphs = nullptr;
        for (const LayerItem& l : dd->layers)
            if (l.art && !l.art->isEmpty() && l.art->evenOdd) glyphs = &l;
        CHECK(glyphs != nullptr);
        CHECK(glyphs->art->paint.hasFill);
        // Ink landed near the rail (glyph bodies around y=50).
        bool ink = false;
        for (int y = 20; y < 60 && !ink; ++y)
            for (int x = 10; x < 60 && !ink; ++x)
                if (dd->composite.pixelColor(x, y).red() > 150) ink = true;
        CHECK(ink);
        CHECK(!state.textOnPath(QString(), QString(), 24.0));  // empty refuses
        state.undo();
        CHECK((int)state.activeDocument()->layers.size() == n0);
    }

    // QR Code + Custom Shape gallery.
    {
#ifdef HAVE_QRENCODE
        auto qr = makeQrArt(QStringLiteral("HELLO"), 210, 0);
        CHECK(qr != nullptr);
        CHECK(qr->paint.hasFill && !qr->paint.hasStroke);
        CHECK(qr->segments.size() > 100);  // finder + data modules
        const QRectF box = segBounds(qr->segments);
        CHECK(std::abs(box.width() - 210.0) < 1.0);
        CHECK(std::abs(box.height() - 210.0) < 1.0);
        // Creation end-to-end: drag anchors, option size wins.
        state.addDocument(QStringLiteral("qr"), QSize(300, 300), 300);
        state.setOption(ToolId::QRCode, QStringLiteral("qr_content"),
                        QStringLiteral("HELLO"));
        state.setOption(ToolId::QRCode, QStringLiteral("qr_size"), 210);
        DocumentItem* dd = state.activeDocument();
        const int n0 = (int)dd->layers.size();
        CHECK(state.addVectorShapeLayer(ToolId::QRCode, QRectF(10, 10, 50, 50),
                                        QStringLiteral("QR")));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 + 1);
        const LayerItem* ql = nullptr;
        for (const LayerItem& l : dd->layers)
            if (l.art && !l.art->isEmpty() && l.art->name == "QR Code")
                ql = &l;
        CHECK(ql != nullptr);
        // Dark module ink inside the placed grid.
        CHECK(compositeAt(state, 15, 15).red() < 100);
        state.undo();
        CHECK((int)state.activeDocument()->layers.size() == n0);
#else
        CHECK(makeQrArt(QStringLiteral("HELLO"), 210, 0) == nullptr);
#endif
        // Custom gallery: all six silhouettes build; punched ones are even-odd.
        for (int i = 0; i < 6; ++i) {
            ShapeStyle cst;
            cst.customshape = i;
            auto art = makeShapeArt(ToolId::CustomShape, QRectF(0, 0, 40, 40), cst);
            CHECK(art != nullptr && !art->isEmpty());
        }
        ShapeStyle mst;
        mst.customshape = 2;  // Moon
        auto moon = makeShapeArt(ToolId::CustomShape, QRectF(0, 0, 40, 40), mst);
        CHECK(moon && moon->evenOdd);
    }

    // Boolean fold end-to-end: combine, subtract, create-combine, create.
    {
        using Op = pittore::vector::BoolOp;
        state.addDocument(QStringLiteral("bool"), QSize(100, 100), 300);
        state.setForeground(QColor(0, 0, 255));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(10, 10, 30, 20),
                                        QStringLiteral("B1")));
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(30, 20, 30, 20),
                                        QStringLiteral("B2")));
        DocumentItem* dd = state.activeDocument();
        const int n0 = (int)dd->layers.size();  // bg + 2 rects
        // Union the two rects: one layer, joint footprint. Red discriminates
        // shape blue (red ~0) from the opaque gray background (red 242).
        CHECK(state.booleanFoldLayers(QVector<int>{0, 1}, Op::Union,
                                      nullptr, QStringLiteral("Combine")));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 - 1);
        CHECK(compositeAt(state, 15, 15).blue() > 150 &&   // B1-only area
              compositeAt(state, 15, 15).red() < 100);
        CHECK(compositeAt(state, 50, 30).blue() > 150 &&   // B2-only area
              compositeAt(state, 50, 30).red() < 100);
        CHECK(compositeAt(state, 35, 25).blue() > 150 &&   // overlap
              compositeAt(state, 35, 25).red() < 100);
        state.undo();
        CHECK((int)state.activeDocument()->layers.size() == n0);
        // Subtract B1 from B2 (top-first order: B2 is index 0). The doc
        // background is opaque gray, so "empty" reads red (shape blue has
        // red near zero, gray near 242).
        CHECK(state.booleanFoldLayers(QVector<int>{0, 1}, Op::Difference,
                                      nullptr, QStringLiteral("Subtract")));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 - 1);
        CHECK(compositeAt(state, 15, 15).red() > 200);   // B1 removed
        CHECK(compositeAt(state, 50, 30).blue() > 150 &&  // B2-only kept
              compositeAt(state, 50, 30).red() < 100);
        CHECK(compositeAt(state, 35, 25).red() > 200);   // overlap cut out
        state.undo();
        // Creation-time Combine: new rect merges into the selection.
        state.setOption(ToolId::Rectangle, QStringLiteral("pathop"), 1);
        dd = state.activeDocument();
        dd->selectedLayers = QVector<int>{0, 1};
        CHECK(state.addVectorShapeLayer(ToolId::Rectangle, QRectF(25, 15, 20, 20),
                                        QStringLiteral("B3")));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 - 1);  // folded, not added
        CHECK(compositeAt(state, 30, 25).blue() > 150 &&
              compositeAt(state, 30, 25).red() < 100);
        state.setOption(ToolId::Rectangle, QStringLiteral("pathop"), 0);
        state.undo();
        CHECK((int)state.activeDocument()->layers.size() == n0);
        // Builder Create: the overlap becomes its own layer.
        CHECK(state.shapeBuilderAt(QRectF(5, 5, 60, 40), 2));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0 + 1);
        CHECK(compositeAt(state, 35, 25).blue() > 150 &&
              compositeAt(state, 35, 25).red() < 100);
        state.undo();
        CHECK((int)state.activeDocument()->layers.size() == n0);
    }

    // Node split / join / reverse on plain segment lists.
    {
        using Kind = pittore::vector::Segment::Kind;
        auto move = [&](pittore::vector::ArtNode& n, float x, float y) {
            pittore::vector::Segment s;
            s.kind = Kind::MoveTo;
            s.x = x;
            s.y = y;
            n.segments.push_back(s);
        };
        auto line = [&](pittore::vector::ArtNode& n, float x, float y) {
            pittore::vector::Segment s;
            s.kind = Kind::LineTo;
            s.x = x;
            s.y = y;
            n.segments.push_back(s);
        };
        auto closes = [&](const pittore::vector::ArtNode& n) {
            int c = 0;
            for (const auto& s : n.segments)
                if (s.kind == Kind::Close) ++c;
            return c;
        };
        auto moves = [&](const pittore::vector::ArtNode& n) {
            int c = 0;
            for (const auto& s : n.segments)
                if (s.kind == Kind::MoveTo) ++c;
            return c;
        };
        auto endOf = [&](const pittore::vector::ArtNode& n, int seg) {
            return QPointF(n.segments[std::size_t(seg)].x,
                           n.segments[std::size_t(seg)].y);
        };
        // Split mid-path: the anchor ends one subpath and starts the next.
        {
            pittore::vector::ArtNode n;
            move(n, 0, 0);
            line(n, 40, 0);
            line(n, 40, 30);
            CHECK(splitNodePath(n, 1));
            CHECK(moves(n) == 2);
            CHECK(closes(n) == 0);
            CHECK(endOf(n, 1) == QPointF(40, 0));
            CHECK(n.segments[std::size_t(2)].kind == Kind::MoveTo);
            CHECK(endOf(n, 2) == QPointF(40, 0));
            // Breaks refuse: ends and lone anchors.
            CHECK(!splitNodePath(n, 0));
            pittore::vector::ArtNode dot;
            move(dot, 5, 5);
            CHECK(!splitNodePath(dot, 0));
        }
        // Split opens a closed loop at the anchor.
        {
            pittore::vector::ArtNode n;
            move(n, 0, 0);
            line(n, 40, 0);
            line(n, 40, 30);
            line(n, 0, 30);
            CHECK(closeNodePath(n));
            CHECK(splitNodePath(n, 3));
            CHECK(closes(n) == 0);
            CHECK(moves(n) == 1);  // opened at the loop end, not duplicated
            CHECK(endOf(n, 3) == QPointF(0, 30));
        }
        // Join across subpaths merges into one.
        {
            pittore::vector::ArtNode n;
            move(n, 0, 0);
            line(n, 50, 0);
            move(n, 55, 0);
            line(n, 60, 0);
            CHECK(joinNodePath(n, 1));
            CHECK(moves(n) == 1);
            CHECK(closes(n) == 0);
            CHECK(endOf(n, (int)n.segments.size() - 1) == QPointF(60, 0));
        }
        // Join end-to-end reverses the other subpath, then merges.
        {
            pittore::vector::ArtNode n;
            move(n, 0, 0);
            move(n, 30, 0);
            line(n, 20, 0);
            CHECK(joinNodePath(n, 0));
            CHECK(moves(n) == 1);
            CHECK(n.segments.size() == 3);
            CHECK(endOf(n, 1) == QPointF(20, 0));
            CHECK(endOf(n, 2) == QPointF(30, 0));
        }
        // Join refuses mid-path anchors and closed loops.
        {
            pittore::vector::ArtNode n;
            move(n, 0, 0);
            line(n, 40, 0);
            line(n, 40, 30);
            CHECK(!joinNodePath(n, 1));
            CHECK(closeNodePath(n));
            CHECK(!joinNodePath(n, 0));
        }
        // Reverse swaps endpoints and controls, keeping Close.
        {
            pittore::vector::ArtNode n;
            move(n, 0, 0);
            line(n, 40, 0);
            pittore::vector::Segment c;
            c.kind = Kind::CubicTo;
            c.c1x = 50;
            c.c1y = 0;
            c.c2x = 60;
            c.c2y = 10;
            c.x = 70;
            c.y = 10;
            n.segments.push_back(c);
            CHECK(closeNodePath(n));
            CHECK(reverseNodeSubpath(n, 0));
            CHECK(endOf(n, 0) == QPointF(70, 10));
            CHECK(endOf(n, 3) == QPointF(0, 0));
            CHECK(closes(n) == 1);
            const auto& rc = n.segments[std::size_t(1)];
            CHECK(rc.kind == Kind::CubicTo);
            CHECK(rc.c1x == 60.0f && rc.c1y == 10.0f);
            CHECK(rc.c2x == 50.0f && rc.c2y == 0.0f);
        }
    }

    // Variable-width profiles: commit renders wider, undo restores.
    {
        state.addDocument(QStringLiteral("profile"), QSize(100, 100), 300);
        state.setForeground(QColor(255, 0, 0));
        PenPath line;
        line.addCorner(QPointF(20, 50));
        line.addCorner(QPointF(80, 50));
        CHECK(state.addVectorPathLayer(line.toSegments(PenMode::Bezier),
                                       ToolId::Pen, QStringLiteral("PW")));
        DocumentItem* dd = state.activeDocument();
        const int n0 = (int)dd->layers.size();
        int artAt = -1;
        for (int i = 0; i < dd->layers.size(); ++i)
            if (dd->layers[i].art && !dd->layers[i].art->isEmpty()) artAt = i;
        CHECK(artAt >= 0);
        // Taper 1→3 across the run (strokeWidth 4: 2px at left, 6 at right).
        // Gray background discriminates through green (shape green ~0).
        auto paint = dd->layers[artAt].art->paint;
        paint.strokeWidth = 4.0;
        paint.hasProfile = true;
        paint.profile = {pittore::vector::ArtWidthPoint{0.0f, 0.5f},
                         pittore::vector::ArtWidthPoint{1.0f, 1.5f}};
        state.setActiveLayerIndex(artAt);
        dd->selectedLayers.clear();
        CHECK(state.applyVectorPaint(artAt, paint, -1.0, QStringLiteral("P")));
        dd = state.activeDocument();
        CHECK((int)dd->layers.size() == n0);  // paint edit, no new layer
        CHECK(compositeAt(state, 25, 50).red() > 150);
        CHECK(compositeAt(state, 25, 50).green() < 100);
        CHECK(compositeAt(state, 25, 47).green() > 200);  // narrow: thin
        CHECK(compositeAt(state, 75, 48).red() > 150);    // wide: tall
        CHECK(compositeAt(state, 75, 48).green() < 100);
        // Reset returns to uniform.
        CHECK(state.resetStrokeProfile());
        CHECK(!state.activeDocument()->layers[artAt].art->paint.hasProfile);
        CHECK(!state.resetStrokeProfile());  // already uniform: refuses
        state.undo();  // drop the reset
        CHECK(state.activeDocument()->layers[artAt].art->paint.hasProfile);
        state.undo();  // drop the profile
        CHECK(!state.activeDocument()->layers[artAt].art->paint.hasProfile);
    }

    // Tablet pressure mapping: hairlines at a touch, full width at press,
    // clamped and monotonic in between.
    {
        CHECK_NEAR(brushPressureWidth(20.0, 0.0), 2.0, 1e-9);
        CHECK_NEAR(brushPressureWidth(20.0, 1.0), 20.0, 1e-9);
        CHECK_NEAR(brushPressureWidth(20.0, 0.5), 11.0, 1e-9);
        CHECK(brushPressureWidth(20.0, 0.25) < brushPressureWidth(20.0, 0.75));
        CHECK_NEAR(brushPressureWidth(20.0, -1.0), 2.0, 1e-9);
        CHECK_NEAR(brushPressureWidth(20.0, 2.0), 20.0, 1e-9);
    }

    // Photo layers have no geometry: not editable, apply refuses.
    {
        state.addDocument(QStringLiteral("photo"), QSize(16, 16), 300);
        CHECK(vectorEditableLayer(&state) == -1);
        CHECK(!state.rezoomVectorArt());
        auto paint = d->layers[0].art->paint;  // previous doc's paint, any paint
        CHECK(!state.applyVectorPaint(state.activeDocumentIndex() >= 0
                                          ? state.activeDocument()->activeLayer
                                          : 0,
                                      paint, -1.0, QStringLiteral("Bad")));
    }

    // Committed vector-brush strokes composite SOLID (filled ribbon, not
    // outline): long diagonal like a real gesture, interior must ink.
    {
        // Geometry-vs-bake split: render the COMMITTED art (with its
        // stored matrix) the way paintVectorLayer does, and diff
        // against the baked layer pixels.
        state.addDocument(QStringLiteral("vbrushgeom"), QSize(200, 150),
                          300);
        BrushStroke gst;
        for (int i = 0; i <= 30; ++i)
            gst.addDab(QPointF(20 + i * 4.0, 110 - i * 3.0), 12.0);
        CHECK(state.addVectorBrushLayer(gst.buildRibbon(), QColor(0, 0, 0),
                                        1.0, QString(),
                                        QStringLiteral("Vector Brush")));
        DocumentItem* gd = state.activeDocument();
        const LayerItem& gl = gd->layers[0];
        const double* gm = gl.art->matrix;
        const QTransform docT =
            QTransform(gm[0], gm[1], gm[2], gm[3], gm[4], gm[5]) *
            QTransform().scale(gl.scaleX, gl.scaleY) *
            QTransform().translate(gl.offset.x(), gl.offset.y());
        QImage geo(200, 150, QImage::Format_ARGB32);
        geo.fill(Qt::white);
        {
            QPainter p(&geo);
            p.setRenderHint(QPainter::Antialiasing, true);
            p.setTransform(docT, false);
            p.setPen(Qt::NoPen);
            p.setBrush(Qt::black);
            QPainterPath path = artNodePath(*gl.art);
            path.setFillRule(Qt::WindingFill);
            p.drawPath(path);
        }
        int geoDark = 0, bakeDark = 0, bothDark = 0;
        const std::uint32_t lw = gl.pixels->width();
        const std::uint32_t lh = gl.pixels->height();
        for (int y = 0; y < 150; ++y)
            for (int x = 0; x < 200; ++x) {
                const bool g = qGray(geo.pixel(x, y)) < 128;
                const int lx = x - int(std::round(gl.offset.x()));
                const int ly = y - int(std::round(gl.offset.y()));
                bool b = false;
                if (lx >= 0 && ly >= 0 && lx < int(lw) && ly < int(lh)) {
                    b = gl.pixels->data()[std::size_t(ly) * lw +
                                          std::size_t(lx)]
                            .a > 0.5f;
                }
                if (g) ++geoDark;
                if (b) ++bakeDark;
                if (g && b) ++bothDark;
            }
        std::printf("[vbrush] geo dark=%d bake dark=%d both=%d off=(%.1f,%.1f)\n",
                    geoDark, bakeDark, bothDark, gl.offset.x(),
                    gl.offset.y());
        CHECK(geoDark > 500);
        CHECK(bakeDark > 500);
        CHECK(bothDark * 2 > geoDark);
        state.addDocument(QStringLiteral("vbrushcommit"), QSize(200, 150),
                          300);
        BrushStroke stroke;
        for (int i = 0; i <= 30; ++i)
            stroke.addDab(QPointF(20 + i * 4.0, 110 - i * 3.0), 12.0);
        CHECK(state.addVectorBrushLayer(stroke.buildRibbon(), QColor(0, 0, 0),
                                        1.0, QString(),
                                        QStringLiteral("Vector Brush")));
        DocumentItem* d = state.activeDocument();
        std::printf("[vbrush] commit layers=%d\n", int(d->layers.size()));
        CHECK(d->layers.size() == 2);  // Background + brush
        CHECK(d->layers[0].art && !d->layers[0].art->isEmpty());
        std::printf("[vbrush] brush segs=%d fill=%d\n",
                    int(d->layers[0].art->segments.size()),
                    d->layers[0].art->paint.hasFill ? 1 : 0);
        // Interior of the band (away from edges/caps).
        const QColor mid = compositeAt(state, 80, 65);
        std::printf("[vbrush] commit mid=(%d,%d,%d,%d)\n", mid.red(),
                    mid.green(), mid.blue(), mid.alpha());
        CHECK(mid.alpha() > 200 && mid.red() < 80);
        // Whole-composite census: misplaced (ink elsewhere) vs dropped.
        {
            int dark = 0, dx0 = 200, dx1 = -1, dy0 = 150, dy1 = -1;
            for (int y = 0; y < 150; ++y)
                for (int x = 0; x < 200; ++x) {
                    const QColor c = compositeAt(state, x, y);
                    if (c.alpha() > 128 && c.red() < 80) {
                        ++dark;
                        dx0 = std::min(dx0, x);
                        dx1 = std::max(dx1, x);
                        dy0 = std::min(dy0, y);
                        dy1 = std::max(dy1, y);
                    }
                }
            std::printf("[vbrush] composite census dark=%d bbox=(%d,%d,%d,%d)\n",
                        dark, dx0, dy0, dx1, dy1);
        }
        // Mostly filled: dark-pixel fraction of the band bbox is high
        // (an outline-only render would be a thin fraction).
        int dark = 0, total = 0;
        for (int y = 40; y < 110; ++y)
            for (int x = 20; x < 140; ++x) {
                ++total;
                if (compositeAt(state, x, y).red() < 80) ++dark;
            }
        std::printf("[vbrush] commit fill fraction=%.3f\n",
                    double(dark) / double(total));
        CHECK(double(dark) / double(total) > 0.15);
    }

    // Bare-click dots stay local: a single dab builds a closed nib circle
    // around itself (never a path to the document origin), rasterizes
    // solid, and commits (this once flew to (0,0) with unnormalized
    // kappa handles and failed the bake).
    {
        BrushStroke dot;
        dot.addDab(QPointF(1112, 315), 8.0);
        pittore::vector::ArtNode dnode;
        dnode.segments = dot.buildRibbon();
        CHECK(dnode.segments.size() == 6);
        dnode.paint.hasFill = true;
        dnode.paint.fill[0] = 0;
        dnode.paint.fill[1] = 0;
        dnode.paint.fill[2] = 0;
        dnode.paint.fill[3] = 255;
        dnode.evenOdd = false;
        const QPainterPath dpath = artNodePath(dnode);
        const QRectF db = dpath.boundingRect();
        std::printf("[vbrush] dot bounds=(%.1f,%.1f,%.1f,%.1f)\n", db.x(),
                    db.y(), db.width(), db.height());
        CHECK(db.x() > 1100.0 && db.y() > 300.0);
        CHECK(db.width() < 20.0 && db.height() < 20.0);
        QImage dimg;
        QPointF dorig;
        CHECK(rasterizeArtNode(dnode, &dimg, &dorig));
        CHECK(dimg.width() < 32 && dimg.height() < 32);
        // Centre pixel of the dot is opaque.
        const QColor dc = dimg.pixelColor(dimg.width() / 2, dimg.height() / 2);
        CHECK(dc.alpha() > 200);
        // And the full commit path accepts it.
        state.addDocument(QStringLiteral("vbrushdot"), QSize(200, 150), 300);
        BrushStroke dot2;
        dot2.addDab(QPointF(80, 65), 8.0);
        CHECK(state.addVectorBrushLayer(dot2.buildRibbon(), QColor(0, 0, 0),
                                        1.0, QString(),
                                        QStringLiteral("Vector Brush")));
        CHECK(compositeAt(state, 80, 65).red() < 80);
    }

    // Big-SVG import regressions (skipped when the fixtures are absent):
    // the 39MB Brazil DDD map (5570 same-fill-run paths) must stay under a
    // bounded layer count via the merge post-pass instead of exploding to
    // 6000+ rows, and the 100MB identical-rect stress file stays tiny.
    // Both must still open to a real composite.
    {
        auto fixture = [](const char* rel) -> QString {
#ifdef PITTORE_SOURCE_ROOT
            const QString abs =
                QString::fromLocal8Bit(PITTORE_SOURCE_ROOT) +
                QStringLiteral("/tests/SVG/") + QString::fromLocal8Bit(rel);
            if (QFile::exists(abs)) return abs;
#endif
            const QString a =
                QStringLiteral("tests/SVG/") + QString::fromLocal8Bit(rel);
            if (QFile::exists(a)) return a;
            const QString b =
                QStringLiteral("../../tests/SVG/") + QString::fromLocal8Bit(rel);
            if (QFile::exists(b)) return b;
            return QString();
        };
        const QString brazil =
            fixture("Mapa_do_Brasil_por_código_DDD.svg");
        const QString big = fixture("100mb.svg");
        if (!brazil.isEmpty()) {
            QFile f(brazil);
            CHECK(f.open(QIODevice::ReadOnly));
            if (f.isOpen()) {
                SvgImportResult result;
                int dpi = 96;
                QString error;
                CHECK(svgPartsImport(f.readAll(), &result, &dpi, &error));
                CHECK(result.layers.size() < 1000);
                CHECK(result.docSize == QSize(951, 942));
                AppState st;
                QString openError;
                CHECK(st.openSvgParts(QStringLiteral("brasil"), result, dpi,
                                      &openError));
                DocumentItem* dd = st.activeDocument();
                CHECK(dd != nullptr && !dd->composite.isNull());
                if (dd) CHECK(dd->composite.size() == QSize(951, 942));
            }
        }
        if (!big.isEmpty()) {
            QFile f(big);
            CHECK(f.open(QIODevice::ReadOnly));
            if (f.isOpen()) {
                SvgImportResult result;
                int dpi = 96;
                QString error;
                CHECK(svgPartsImport(f.readAll(), &result, &dpi, &error));
                CHECK(result.layers.size() <= 8);
                CHECK(result.docSize == QSize(1000, 1000));
            }
        }
    }

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
