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
// Input
// ---------------------------------------------------------------------------
void CanvasView::applyToolCursor() {
    // The brush ring belongs to the previous tool: drop the cache and erase
    // the pixels (the next overlay paint redraws it for a brush tool, or
    // leaves it gone for e.g. Hand/Zoom). There is size slack because the ring
    // pen draws 1 px outside its nominal radius.
    if (!brushCursorRect_.isEmpty()) {
        viewport()->update(brushCursorRect_.adjusted(-2, -2, 2, 2).toAlignedRect());
        brushCursorRect_ = QRectF();
    }
    switch (state_->activeTool()) {
        case ToolId::Hand:
            viewport()->setCursor(Qt::OpenHandCursor);
            break;
        case ToolId::Zoom:
            viewport()->setCursor(Qt::CrossCursor);
            break;
        case ToolId::Move:
            viewport()->setCursor(Qt::SizeAllCursor);
            break;
        case ToolId::HorizontalType:
        case ToolId::VerticalType:
            viewport()->setCursor(Qt::IBeamCursor);
            break;
        default:
            // Brush tools (and every other tool that carries a brush tip, from
            // Selection Brush to Liquify) take the configured canvas cursor —
            // blank when the painted ring is the pointer, else a stock
            // arrow/crosshair — with or without a document open. Tools
            // without a brush keep the plain crosshair.
            if (cursorToolActive() ||
                state_->option(state_->activeTool(),
                               QStringLiteral("brush_size")).isValid())
                viewport()->setCursor(canvasCursor());
            else
                viewport()->setCursor(Qt::CrossCursor);
            break;
    }
    // Keep the application-level blank cursor in sync with the tool: switching
    // to a brush tool while the pointer rests over the canvas must push the
    // override without waiting for a mouse move; switching away must pop it.
    syncCursorOverride();
}

}  // namespace pittore::ui
