#pragma once
// Shared curve surface for Curves: destructive dialog + live adjustment layers.
// Points are normalized (x in, y out, 0..1, y up); fed to buildCurveLUT.
// `onChanged_` fires after every edit.
#include <QPointF>
#include <QVector>
#include <QWidget>

#include <functional>

namespace pittore::ui {

class CurveEditor final : public QWidget {
  public:
    explicit CurveEditor(QWidget* parent = nullptr);

    QVector<QPointF> points() const { return points_; }
    void setPoints(QVector<QPointF> pts);
    void reset();

    std::function<void()> onChanged_;

    QSize sizeHint() const override { return {280, 280}; }

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    QRectF plotRect() const;
    QPointF toNorm(const QPointF& w, const QRectF& plot) const;
    QPointF toWidget(const QPointF& n, const QRectF& plot) const;
    int pointAt(const QPointF& w, const QRectF& plot) const;
    void changed();

    QVector<QPointF> points_;
    int dragIndex_ = -1;
};

}  // namespace pittore::ui
