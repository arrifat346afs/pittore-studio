#include "ui/curve_editor.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

#include <algorithm>

namespace pittore::ui {

CurveEditor::CurveEditor(QWidget* parent) : QWidget(parent) {
    setMinimumSize(280, 280);
    points_ = {{0.0, 0.0}, {1.0, 1.0}};
}

void CurveEditor::setPoints(QVector<QPointF> pts) {
    points_ = std::move(pts);
    update();
    if (onChanged_) onChanged_();
}

void CurveEditor::reset() { setPoints({{0.0, 0.0}, {1.0, 1.0}}); }

void CurveEditor::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(0x26, 0x26, 0x26));
    const double m = 14.0;
    const QRectF plot(m, m, width() - 2 * m, height() - 2 * m);

    // Quarter grid.
    p.setPen(QPen(QColor(0x3a, 0x3a, 0x3a), 1));
    for (int i = 1; i < 4; ++i) {
        const double x = plot.left() + plot.width() * i / 4.0;
        const double y = plot.top() + plot.height() * i / 4.0;
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    // Border + diagonal.
    p.setPen(QPen(QColor(0x55, 0x55, 0x55), 1));
    p.drawRect(plot);
    p.drawLine(plot.topLeft(), plot.bottomRight());

    // Curve through the points.
    p.setPen(QPen(QColor(0x8f, 0xc8, 0xff), 2));
    QPolygonF poly;
    for (const QPointF& pt : points_) poly << toWidget(pt, plot);
    p.drawPolyline(poly);

    // Handles.
    for (int i = 0; i < points_.size(); ++i) {
        const QPointF c = toWidget(points_[i], plot);
        p.setPen(QPen(Qt::black, 1));
        p.setBrush(i == dragIndex_ ? QColor(0xff, 0xe0, 0x80) : Qt::white);
        p.drawEllipse(c, 4.5, 4.5);
    }
}

void CurveEditor::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const QRectF plot = plotRect();
    dragIndex_ = pointAt(event->pos(), plot);
    if (dragIndex_ < 0) {
        if (points_.size() >= 16) return;
        const QPointF n = toNorm(event->pos(), plot);
        points_.push_back(n);
        std::sort(points_.begin(), points_.end(),
                  [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
        dragIndex_ = pointAt(event->pos(), plot);
        changed();
    }
    update();
}

void CurveEditor::mouseMoveEvent(QMouseEvent* event) {
    if (dragIndex_ < 0) return;
    const QRectF plot = plotRect();
    const QPointF n = toNorm(event->pos(), plot);
    points_[dragIndex_] = n;
    // Keep x-order so the curve stays a function.
    if (dragIndex_ > 0) points_[dragIndex_].setX(std::max(points_[dragIndex_].x(),
                                                          points_[dragIndex_ - 1].x()));
    if (dragIndex_ + 1 < points_.size())
        points_[dragIndex_].setX(std::min(points_[dragIndex_].x(),
                                          points_[dragIndex_ + 1].x()));
    changed();
    update();
}

void CurveEditor::mouseReleaseEvent(QMouseEvent* event) {
    (void)event;
    dragIndex_ = -1;
    update();
}

void CurveEditor::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const QRectF plot = plotRect();
    const int idx = pointAt(event->pos(), plot);
    if (idx >= 0 && points_.size() > 2) {
        points_.removeAt(idx);
        dragIndex_ = -1;
        changed();
        update();
    }
}

QRectF CurveEditor::plotRect() const {
    const double m = 14.0;
    return QRectF(m, m, width() - 2 * m, height() - 2 * m);
}

QPointF CurveEditor::toNorm(const QPointF& w, const QRectF& plot) const {
    return {std::clamp((w.x() - plot.left()) / plot.width(), 0.0, 1.0),
            std::clamp(1.0 - (w.y() - plot.top()) / plot.height(), 0.0, 1.0)};
}

QPointF CurveEditor::toWidget(const QPointF& n, const QRectF& plot) const {
    return {plot.left() + n.x() * plot.width(),
            plot.bottom() - n.y() * plot.height()};
}

int CurveEditor::pointAt(const QPointF& w, const QRectF& plot) const {
    for (int i = 0; i < points_.size(); ++i) {
        const QPointF c = toWidget(points_[i], plot);
        if ((c - w).manhattanLength() < 9.0) return i;
    }
    return -1;
}

void CurveEditor::changed() {
    if (onChanged_) onChanged_();
}

}  // namespace pittore::ui
