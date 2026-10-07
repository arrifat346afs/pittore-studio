#include "ui/canvas_view.h"

#include <QMouseEvent>
#include <QTabletEvent>

#include <algorithm>
#include <cmath>

#include "ui/canvas/shared/canvas_helpers.h"
#include "ui/persona/vector_node.h"

namespace pittore::ui {

// Snap a document point to a nearby art endpoint (Freehand magnetic mode,
// Pen clicks): returns true and writes the snapped point when an endpoint of
// any art layer lands within tolDoc.
bool CanvasView::snapPenToArt(const QPointF& docPos, double tolDoc,
                              QPointF* out) {
    DocumentItem* d = doc();
    if (!d || !out) return false;
    double bestD = tolDoc;
    QPointF best;
    bool found = false;
    for (const LayerItem& l : d->layers) {
        if (!l.art || l.art->isEmpty() || l.scaleX <= 0.0 || l.scaleY <= 0.0)
            continue;
        const QTransform docT =
            QTransform(l.art->matrix[0], l.art->matrix[1], l.art->matrix[2],
                       l.art->matrix[3], l.art->matrix[4], l.art->matrix[5]) *
            QTransform().scale(l.scaleX, l.scaleY) *
            QTransform().translate(l.offset.x(), l.offset.y());
        for (const QPointF& np : nodeEndpoints(*l.art)) {
            const QPointF dp = docT.map(np);
            const double dist =
                std::hypot(dp.x() - docPos.x(), dp.y() - docPos.y());
            if (dist <= bestD) {
                bestD = dist;
                best = dp;
                found = true;
            }
        }
    }
    if (found) *out = best;
    return found;
}

void CanvasView::cancelPenPath() {
    penActive_ = false;
    penPressed_ = false;
    penShaping_ = false;
    penPath_.clear();
    viewport()->update();
}

// Returns the pre-eraser tool after an eraser-end stroke, but only when the
// Eraser is still active (a manual switch mid-stroke wins and is kept).
// An eraser end that flipped erase blend instead clears the toggle when it
// set it (a manually enabled toggle is left alone).
void CanvasView::restoreStylusTool() {
    if (stylusEraseBlend_) {
        stylusEraseBlend_ = false;
        const ToolId active = state_->activeTool();
        if (active == ToolId::Brush || active == ToolId::Pencil)
            state_->setOption(active, QStringLiteral("brush_erase_blend"),
                              false);
        return;
    }
    if (!stylusSwitched_) return;
    stylusSwitched_ = false;
    if (state_->activeTool() == ToolId::Eraser)
        state_->setActiveTool(stylusReturnTool_);
}

double CanvasView::vbrushWidthAt(const QPointF& docPos) const {
    const ToolId tool = ToolId::VectorBrushTool;
    double base =
        state_->option(tool, QStringLiteral("brush_width")).toDouble();
    if (!(base > 0.0)) base = 8.0;
    const int controller =
        state_->option(tool, QStringLiteral("controller")).toInt();
    // Velocity controller: long fast spans draw thinner (geometry-only, so
    // headless and tablet-less runs stay deterministic).
    if (controller == 3 && !vbrushStroke_.dabs().empty()) {
        const QPointF& last = vbrushStroke_.dabs().back().pos;
        const double len =
            std::hypot(docPos.x() - last.x(), docPos.y() - last.y());
        return base * qBound(0.4, 8.0 / (4.0 + len), 1.25);
    }
    // Pressure controller: live stylus pressure scales the width; without
    // a tablet stroke in flight it stays constant.
    if (controller == 2 && tabletDown_ && tabletPressure_ >= 0.0)
        return brushPressureWidth(base, tabletPressure_);
    return base;
}

void CanvasView::vbrushNib(double& ratio, double& angleDeg,
                            bool& square) const {
    const ToolId tool = ToolId::VectorBrushTool;
    const QVariant rv = state_->option(tool, QStringLiteral("brush_roundness"));
    const QVariant av = state_->option(tool, QStringLiteral("brush_angle"));
    const QVariant tv = state_->option(tool, QStringLiteral("brush_tip"));
    ratio = rv.isValid() ? qBound(0.01, rv.toDouble() / 100.0, 1.0) : 1.0;
    angleDeg = av.isValid() ? qBound(-180.0, av.toDouble(), 180.0) : 0.0;
    square = tv.isValid() && tv.toInt() == 1;
}

bool CanvasView::handleTabletEvent(QTabletEvent* event) {
    // The vector brush consumes stylus input for pressure width; pixel
    // paint tools latch the pressure and get the same synthetic gesture
    // (their dabs read the latch). Everything else ignores tablet events
    // so Qt synthesizes the usual mouse gesture instead.
    const ToolId active = state_->activeTool();
    const bool isBrush = active == ToolId::VectorBrushTool;
    if (!isBrush && !isPaintTool(active)) return false;
    const QEvent::Type type = event->type();
    if (type == QEvent::TabletPress) {
        tabletDown_ = true;
        tabletPressure_ = std::clamp(event->pressure(), 0.0, 1.0);
        pixelPressure_ = tabletPressure_;
        tiltX_ = std::clamp(event->xTilt(), -60.0, 60.0);
        tiltY_ = std::clamp(event->yTilt(), -60.0, 60.0);
        barrelRotation_ = std::clamp(event->rotation(), -360.0, 360.0);
        tangentialPressure_ =
            std::clamp(event->tangentialPressure(), 0.0, 1.0);
        // Eraser end: on Brush/Pencil keep the live tip and flip erase
        // blend (restored on release); other paint tools fall back to
        // the pixel Eraser for the stroke as before.
        if (event->pointerType() ==
                QPointingDevice::PointerType::Eraser &&
            active != ToolId::Eraser) {
            if (active == ToolId::Brush || active == ToolId::Pencil) {
                const QVariant cur = state_->option(
                    active, QStringLiteral("brush_erase_blend"));
                if (!cur.isValid() || !cur.toBool()) {
                    state_->setOption(active,
                                      QStringLiteral("brush_erase_blend"),
                                      true);
                    stylusEraseBlend_ = true;
                }
            } else {
                stylusReturnTool_ = active;
                stylusSwitched_ = true;
                state_->setActiveTool(ToolId::Eraser);
            }
        }
    } else if (type == QEvent::TabletLeaveProximity) {
        tabletDown_ = false;
        tabletPressure_ = -1.0;
        pixelPressure_ = 1.0;
        tiltX_ = tiltY_ = 0.0;
        barrelRotation_ = 0.0;
        tangentialPressure_ = 0.0;
        restoreStylusTool();
        return false;
    } else if (!tabletDown_) {
        return false;  // hover: normal mouse synthesis
    } else if (type == QEvent::TabletMove) {
        tabletPressure_ = std::clamp(event->pressure(), 0.0, 1.0);
        pixelPressure_ = tabletPressure_;
        tiltX_ = std::clamp(event->xTilt(), -60.0, 60.0);
        tiltY_ = std::clamp(event->yTilt(), -60.0, 60.0);
        barrelRotation_ = std::clamp(event->rotation(), -360.0, 360.0);
        tangentialPressure_ =
            std::clamp(event->tangentialPressure(), 0.0, 1.0);
    }
    QMouseEvent mouseEvent(
        type == QEvent::TabletPress ? QEvent::MouseButtonPress
        : type == QEvent::TabletRelease ? QEvent::MouseButtonRelease
                                        : QEvent::MouseMove,
        event->position(), event->globalPosition(), Qt::LeftButton,
        event->buttons(), event->modifiers());
    event->accept();
    if (type == QEvent::TabletPress)
        mousePressEvent(&mouseEvent);
    else if (type == QEvent::TabletRelease)
        mouseReleaseEvent(&mouseEvent);
    else
        mouseMoveEvent(&mouseEvent);
    if (type == QEvent::TabletRelease) {
        tabletDown_ = false;
        tabletPressure_ = -1.0;
        pixelPressure_ = 1.0;
        tiltX_ = tiltY_ = 0.0;
        barrelRotation_ = 0.0;
        tangentialPressure_ = 0.0;
        restoreStylusTool();
    }
    return true;
}

bool CanvasView::docToArtNode(int layerIndex, const QPointF& docPos,
                              QPointF* nodePos) {
    const DocumentItem* d = doc();
    if (!d || !nodePos || layerIndex < 0 || layerIndex >= d->layers.size())
        return false;
    const LayerItem& l = d->layers[layerIndex];
    if (!l.art || l.art->isEmpty() || l.scaleX <= 0.0 || l.scaleY <= 0.0)
        return false;
    const QPointF src((docPos.x() - l.offset.x()) / l.scaleX,
                      (docPos.y() - l.offset.y()) / l.scaleY);
    const QTransform inv =
        QTransform(l.art->matrix[0], l.art->matrix[1], l.art->matrix[2],
                   l.art->matrix[3], l.art->matrix[4], l.art->matrix[5])
            .inverted();
    *nodePos = inv.map(src);
    return true;
}

bool CanvasView::finishPenPath() {
    if (!penActive_ || penPath_.isEmpty()) return false;
    const PenMode mode = penModeFor(penTool_);
    if (!penPath_.closed() && penPath_.size() < 2) {
        state_->setStatusHint(tr("A path needs at least two points."));
        return false;
    }
    if (mode == PenMode::Freehand) {
        double fit = state_->option(penTool_, QStringLiteral("curvefit")).toDouble();
        if (!(fit > 0.0)) fit = 2.0;
        penPath_.simplifyFreehand(fit);
    }
    const std::vector<pittore::vector::Segment> segs =
        penPath_.toSegments(mode);
    const bool ok =
        state_->addVectorPathLayer(segs, penTool_, toolName(penTool_));
    penPath_.clear();
    // Freehand is one path per drag; Pen/Curvature keep drawing.
    if (penTool_ == ToolId::FreeformPen) penActive_ = false;
    viewport()->update();
    return ok;
}

}  // namespace pittore::ui
