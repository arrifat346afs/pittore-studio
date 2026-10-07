#include "ui/canvas_view.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPainterPath>
#include <QScrollBar>
#include <QDateTime>
#include <QTimer>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ui/ai_models.h"
#include "ui/contextual_task_bar.h"
#include "ui/icons.h"
#include "ui/selection_mask.h"
#include "ui/svg_parts.h"
#include "engine/ai/bg_remove.h"
#include "engine/compute/paint.h"
#include "engine/compute/warp.h"
#include "engine/core/log.h"

#include "ui/canvas/shared/canvas_helpers.h"

namespace pittore::ui {


// ---------------------------------------------------------------------------
// Liquify (displacement warp)
// ---------------------------------------------------------------------------
void CanvasView::beginLiquifyStroke(const QPointF& docPos) {
    liquifyActive_ = false;
    liquifyMoved_ = false;
    if (!state_->beginLiquifyStroke()) return;   // no warppable active layer
    const LayerItem* l = state_->activeLayer();
    if (!l || !l->pixels) {
        state_->cancelLiquifyStroke();
        return;
    }
    liquifyActive_ = true;
    liquifyLastDoc_ = docPos;
    liquifyMesh_ =
        pittore::compute::make_warp_mesh(l->pixels->width(), l->pixels->height());
    // A click with no drag still applies the radial brushes (Twirl/Pucker/
    // Bloat); Forward Warp and Push Left need movement.
    liquifyDabPoint(docPos, 0.0f, 0.0f);
}


void CanvasView::liquifyDabAt(const QPointF& docPos) {
    if (!liquifyActive_) return;
    const LayerItem* l = state_->activeLayer();
    if (!l || !l->pixels) return;
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    const QPointF delta = docPos - liquifyLastDoc_;
    const float dx = static_cast<float>(delta.x() / lsx);
    const float dy = static_cast<float>(delta.y() / lsy);
    // Pointer samples closer than one layer pixel only add noise to the mesh.
    if (std::hypot(dx, dy) < 1.0f) return;
    liquifyDabPoint(docPos, dx, dy);
    liquifyLastDoc_ = docPos;
}


void CanvasView::liquifyDabPoint(const QPointF& docPos, float dxLayer,
                                 float dyLayer) {
    LayerItem* l = state_->activeLayer();
    if (!l || !l->pixels || liquifyMesh_.cols < 2) return;
    const double lsx = std::max(l->scaleX, 1e-6);
    const double lsy = std::max(l->scaleY, 1e-6);
    const float cx = static_cast<float>((docPos.x() - l->offset.x()) / lsx);
    const float cy = static_cast<float>((docPos.y() - l->offset.y()) / lsy);
    // The radius uses the minor axis so the dab covers the whole document-space
    // circle, like paintDab.
    const float radius =
        static_cast<float>(strokeRadius() / std::min(lsx, lsy));
    if (radius <= 0.0f) return;

    const int mode =
        state_->option(ToolId::Liquify, QStringLiteral("tool")).toInt();
    const double pressure =
        state_->option(ToolId::Liquify, QStringLiteral("pressure")).toInt();
    const float strength =
        static_cast<float>(qBound(0.0, pressure / 100.0, 1.0));

    if (!applyLiquifyBrush(liquifyMesh_, mode, cx, cy, radius, strength, dxLayer,
                           dyLayer))
        return;

    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    pittore::compute::warp_dab_rect(liquifyMesh_, cx, cy, radius, x0, y0, x1,
                                     y1);
    if (x0 >= x1 || y0 >= y1) return;
    if (state_->liquifyDab(liquifyMesh_, x0, y0, x1, y1)) {
        liquifyMoved_ = true;
        state_->flushPaint();
    }
}


void CanvasView::finishLiquifyStroke(bool commit) {
    if (!liquifyActive_) return;
    liquifyActive_ = false;
    // The stroke can end before the pointer is released (a tool shortcut, the
    // menu). Drop the drag so the eventual release does not run the marquee
    // fallback and push a spurious history entry.
    dragging_ = false;
    if (commit && liquifyMoved_)
        state_->commitUndoStep(toolName(ToolId::Liquify),
                               QString::fromUtf8(toolDef(ToolId::Liquify).iconKey));
    else
        state_->discardUndoStep();   // click with no deformation: no history
    state_->endLiquifyStroke();
    liquifyMoved_ = false;
    refresh();
}


void CanvasView::cancelLiquify() {
    if (!liquifyActive_) return;
    liquifyActive_ = false;
    liquifyMoved_ = false;
    dragging_ = false;
    state_->cancelLiquifyStroke();
    refresh();
}


void CanvasView::commitLiquifyStrokeIfActive() {
    if (liquifyActive_) finishLiquifyStroke(true);
}


void CanvasView::wheelEvent(QWheelEvent* event) {
    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool swap = state_->settings().zoomWithScroll;
    // Alt+wheel always zooms at the cursor (conventional behaviour), regardless of
    // the scroll/zoom preference swap.
    if (modifiers.testFlag(Qt::AltModifier)) {
        const double factor = std::pow(1.0015, event->angleDelta().y());
        setZoom(zoom() * factor, event->position());
        event->accept();
        return;
    }
    const bool zoomKey = modifiers.testFlag(Qt::ControlModifier);
    if (swap ? !zoomKey : zoomKey) {
        if (!swap && !zoomKey) {
            QAbstractScrollArea::wheelEvent(event);
            return;
        }
        if (swap && zoomKey) {
            QAbstractScrollArea::wheelEvent(event);
            return;
        }
        const double factor = std::pow(1.0015, event->angleDelta().y());
        setZoom(zoom() * factor, event->position());
        event->accept();
        return;
    }
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() -
                                        event->angleDelta().y());
        event->accept();
        return;
    }
    QAbstractScrollArea::wheelEvent(event);
}

}  // namespace pittore::ui
