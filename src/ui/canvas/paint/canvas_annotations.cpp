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
// Annotation overlays (Color Sampler / Note / Count / Ruler)
// ---------------------------------------------------------------------------
int CanvasView::samplePinAtView(const QPointF& viewPos) const {
    DocumentItem* d = doc();
    if (!d) return -1;
    const QTransform t = documentTransform();
    for (int i = 0; i < d->colorSamples.size(); ++i)
        if (QLineF(t.map(d->colorSamples[i]), viewPos).length() <= 7.0) return i;
    return -1;
}


int CanvasView::countMarkerAtView(const QPointF& viewPos) const {
    DocumentItem* d = doc();
    if (!d) return -1;
    const QTransform t = documentTransform();
    for (int i = d->countMarkers.size() - 1; i >= 0; --i)
        if (QLineF(t.map(d->countMarkers[i].pos), viewPos).length() <= 9.0) return i;
    return -1;
}


int CanvasView::noteAtView(const QPointF& viewPos) const {
    DocumentItem* d = doc();
    if (!d) return -1;
    const QTransform t = documentTransform();
    for (int i = d->notes.size() - 1; i >= 0; --i)
        if (QLineF(t.map(d->notes[i].pos), viewPos).length() <= 10.0) return i;
    return -1;
}


int CanvasView::sliceAtView(const QPointF& viewPos) const {
    DocumentItem* d = doc();
    if (!d) return -1;
    const QTransform t = documentTransform();
    int best = -1;
    double bestArea = 0.0;
    for (int i = 0; i < d->slices.size(); ++i) {
        const QRectF vr = t.mapRect(QRectF(d->slices[i]));
        if (!vr.adjusted(-4, -4, 4, 4).contains(viewPos)) continue;
        // Nested slices resolve to the smallest (topmost) region.
        const double area = vr.width() * vr.height();
        if (best < 0 || area < bestArea) {
            best = i;
            bestArea = area;
        }
    }
    return best;
}


void CanvasView::paintAnnotationOverlay(QPainter& p, const QTransform& t) {
    DocumentItem* d = doc();
    if (!d) return;
    const ThemeColors c = colorsFor(state_->theme());

    // Color Sampler pins: a crosshair, the sampled colour as a numbered dot.
    for (int i = 0; i < d->colorSamples.size(); ++i) {
        const QPointF v = t.map(d->colorSamples[i]);
        const int sizeIdx =
            state_->option(ToolId::ColorSampler, QStringLiteral("sample_size")).toInt();
        const QColor sample = sampleCompositeColor(*d, d->colorSamples[i], sizeIdx);
        const QColor swatch = sample.isValid() ? sample : QColor(0x80, 0x80, 0x80);
        p.setPen(QPen(Qt::white, 3));
        p.drawLine(v + QPointF(-8, 0), v + QPointF(8, 0));
        p.drawLine(v + QPointF(0, -8), v + QPointF(0, 8));
        p.setPen(QPen(Qt::black, 1));
        p.drawLine(v + QPointF(-8, 0), v + QPointF(8, 0));
        p.drawLine(v + QPointF(0, -8), v + QPointF(0, 8));
        p.setBrush(swatch);
        p.setPen(QPen(Qt::black, 1));
        p.drawEllipse(v + QPointF(10, -10), 7, 7);
        QFont f = p.font();
        f.setPixelSize(9);
        p.setFont(f);
        p.setPen(Qt::black);
        p.drawText(QRectF(v.x() + 3, v.y() - 16, 14, 12), Qt::AlignCenter,
                   QString::number(i + 1));
    }

    // Count markers, numbered within their group.
    const QColor countColor =
        state_->option(ToolId::Count, QStringLiteral("countcolor")).value<QColor>();
    const double markerR =
        state_->option(ToolId::Count, QStringLiteral("marker")).toDouble();
    const int labelPx =
        state_->option(ToolId::Count, QStringLiteral("labelsize")).toInt();
    for (int i = 0; i < d->countMarkers.size(); ++i) {
        const CountMarker& m = d->countMarkers[i];
        const QPointF v = t.map(m.pos);
        const double r = qBound(4.0, markerR * 2.0, 12.0);
        int n = 0;
        for (int j = 0; j <= i; ++j)
            if (d->countMarkers[j].group == m.group) ++n;
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(countColor.isValid() ? countColor : c.accent, 2));
        p.drawEllipse(v, r, r);
        QFont f = p.font();
        f.setPixelSize(qBound(8, labelPx, 72));
        f.setBold(true);
        p.setFont(f);
        const QRectF box(v.x() - r, v.y() - r, 2 * r, 2 * r);
        p.setPen(QPen(Qt::white, 3));
        p.drawText(box, Qt::AlignCenter, QString::number(n));
        p.setPen(QPen(countColor.isValid() ? countColor : c.accent, 1));
        p.drawText(box, Qt::AlignCenter, QString::number(n));
    }

    // Notes: a folded-corner page marker, highlighted while hovered.
    for (int i = 0; i < d->notes.size(); ++i) {
        const DocNote& note = d->notes[i];
        const QPointF v = t.map(note.pos);
        const QColor col = note.color.isValid() ? note.color : QColor(0xff, 0xd2, 0x2b);
        const bool hot = (i == noteHover_);
        QPainterPath page;
        page.moveTo(v + QPointF(-7, -8));
        page.lineTo(v + QPointF(3, -8));
        page.lineTo(v + QPointF(7, -4));
        page.lineTo(v + QPointF(7, 8));
        page.lineTo(v + QPointF(-7, 8));
        page.closeSubpath();
        p.setBrush(col);
        p.setPen(QPen(hot ? c.accent : QColor(0, 0, 0, 160), hot ? 2 : 1));
        p.drawPath(page);
        p.setBrush(QColor(255, 255, 255, 140));
        p.setPen(Qt::NoPen);
        QPainterPath fold;
        fold.moveTo(v + QPointF(3, -8));
        fold.lineTo(v + QPointF(3, -4));
        fold.lineTo(v + QPointF(7, -4));
        fold.closeSubpath();
        p.drawPath(fold);
    }

    // Ruler measurement: dashed segment with endpoint handles and a readout.
    if (d->rulerHasMeasurement) {
        const QPointF a = t.map(d->rulerStart);
        const QPointF b = t.map(d->rulerEnd);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 3));
        p.drawLine(a, b);
        p.setPen(QPen(Qt::black, 1, Qt::DashLine));
        p.drawLine(a, b);
        for (const QPointF& h : {a, b}) {
            p.setPen(QPen(Qt::white, 2));
            p.drawLine(h + QPointF(-5, 0), h + QPointF(5, 0));
            p.drawLine(h + QPointF(0, -5), h + QPointF(0, 5));
            p.setPen(QPen(Qt::black, 1));
            p.drawRect(QRectF(h.x() - 3, h.y() - 3, 6, 6));
        }
        const double dx = d->rulerEnd.x() - d->rulerStart.x();
        const double dy = d->rulerEnd.y() - d->rulerStart.y();
        const double len = std::hypot(dx, dy);
        const double ang = std::atan2(dy, dx) * 180.0 / 3.14159265358979323846;
        const QString label = QStringLiteral("%1 px   %2°")
                                  .arg(len, 0, 'f', 1)
                                  .arg(ang, 0, 'f', 1);
        const QPointF mid = (a + b) * 0.5;
        QFont f = p.font();
        f.setPixelSize(11);
        p.setFont(f);
        const QRectF box(mid.x() + 8, mid.y() - 24, 150, 18);
        p.fillRect(box, QColor(0, 0, 0, 170));
        p.setPen(QColor(255, 255, 255, 235));
        p.drawText(box.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, label);
    }

    // Area measurement: dashed rectangle with corner handles and a readout.
    if (d->areaHasMeasurement) {
        const QRectF vr = t.mapRect(d->areaRect);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(Qt::white, 3));
        p.drawRect(vr);
        p.setPen(QPen(Qt::black, 1, Qt::DashLine));
        p.drawRect(vr);
        for (const QPointF& h :
             {vr.topLeft(), vr.topRight(), vr.bottomLeft(), vr.bottomRight()}) {
            p.setPen(QPen(Qt::white, 2));
            p.drawRect(QRectF(h.x() - 3, h.y() - 3, 6, 6));
        }
        const double w = d->areaRect.width();
        const double h = d->areaRect.height();
        const QString label = QStringLiteral("%1 × %2 px  =  %3 px²")
                                  .arg(w, 0, 'f', 1)
                                  .arg(h, 0, 'f', 1)
                                  .arg(w * h, 0, 'f', 1);
        QFont f = p.font();
        f.setPixelSize(11);
        p.setFont(f);
        const QRectF box(vr.left(), vr.bottom() + 6, 230, 18);
        p.fillRect(box, QColor(0, 0, 0, 170));
        p.setPen(QColor(255, 255, 255, 235));
        p.drawText(box.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   label);
    }

    // Slices (R19): numbered export regions. They persist like guides, so they
    // draw regardless of the active tool; the selected one highlights.
    const ToolId activeTool = state_->activeTool();
    for (int i = 0; i < d->slices.size(); ++i) {
        const QRectF vr = t.mapRect(QRectF(d->slices[i]));
        const bool selected =
            i == d->selectedSlice &&
            (activeTool == ToolId::Slice || activeTool == ToolId::SliceSelect);
        p.setBrush(QColor(0x40, 0x90, 0xff, 28));
        p.setPen(QPen(selected ? QColor(0xff, 0xb0, 0x40) : QColor(0x40, 0x90, 0xff),
                      selected ? 2.0 : 1.0));
        p.drawRect(vr);
        // Number tags (Settings > Canvas > Show): the rectangles persist
        // regardless, matching a separate slice-number toggle.
        if (state_->settings().showSliceNumbers) {
            QFont f = p.font();
            f.setPixelSize(11);
            p.setFont(f);
            const QRectF tag(vr.left() + 3, vr.top() + 3, 20, 15);
            p.fillRect(tag, QColor(0x20, 0x60, 0xc0, 215));
            p.setPen(Qt::white);
            p.drawText(tag, Qt::AlignCenter, QString::number(i + 1));
        }
    }
    if (sliceDragging_) {
        const QRectF vr =
            t.mapRect(QRectF(sliceDragAnchorDoc_, sliceDragCurrentDoc_).normalized());
        p.setBrush(QColor(0x40, 0x90, 0xff, 28));
        p.setPen(QPen(QColor(0x40, 0x90, 0xff), 1, Qt::DashLine));
        p.drawRect(vr);
    }
}

}  // namespace pittore::ui
