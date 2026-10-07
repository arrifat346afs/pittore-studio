#pragma once
#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QString>
#include <QVector>
#include <vector>

class QCheckBox;
class QComboBox;

namespace pittore::ui {

class AppState;

class FilterPreviewCanvas final : public QWidget {
    Q_OBJECT
public:
    explicit FilterPreviewCanvas(AppState *state, QWidget *parent = nullptr);
    void setTool(int tool);
    void refresh();
    void setPreviewImage(const QImage &image);
    void clearPreviewImage();
    bool hasPreviewImage() const { return !preview_.isNull(); }
    double zoom() const;
    void setZoomIndex(int index);
    int zoomIndex() const { return zoomIndex_; }

signals:
    void zoomChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void applyZoom(double z, QPointF anchor);
    void zoomToFit();
    void zoomToWidth();
    QPointF viewToImage(QPointF vp) const;
    QPointF imageToView(QPointF ip) const;

    AppState *state_;
    QImage image_;
    QImage preview_;
    double imageScale_ = 1.0;
    int tool_ = 0;
    int zoomIndex_ = 4;
    double zoom_ = 1.0;
    QPointF pan_;
    bool panning_ = false;
    bool spaceHeld_ = false;
    QPoint pressPos_;
    QPointF pressPan_;
};

class FilterDialog final : public QDialog {
    Q_OBJECT
public:
    explicit FilterDialog(AppState *state, const QString &filterId, QWidget *parent = nullptr);
    QString filterId() const;
    std::vector<double> appliedParams() const;
    bool showingProxyPreview() const;
    void accept() override;
    void reject() override;

private:
    void preview();
    class Impl;
    Impl *impl_;
};

class FilterGalleryDialog final : public QDialog {
    Q_OBJECT
public:
    explicit FilterGalleryDialog(AppState *state, const QString &category = QString(), QWidget *parent = nullptr);
    void setCategory(const QString &category);

private:
    class Impl;
    Impl *impl_;
};

QVector<QPair<QString, QString>> galleryFilterEntries(const QString &category);

}
