#pragma once
#include <QDialog>
#include <QImage>
#include <QPointF>
#include <QTimer>
#include <QTransform>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "engine/compute/warp.h"

class QCheckBox;
class QComboBox;
class QContextMenuEvent;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QToolBar;

namespace pittore::ui {

class AppState;

struct LiquifyToolDef {
    const char *name = "";
    const char *shortcut = "";
    int mode = 0;
};

const LiquifyToolDef *liquifyTools(int &count);

class LiquifyCanvas final : public QWidget {
    Q_OBJECT
public:
    struct ViewOptions {
        bool showImage = true;
        bool showMesh = true;
        int meshSize = 1;
        QColor meshColor{160, 160, 160};
        int meshOpacity = 40;
        int divisions = 16;
        bool showMask = true;
        QColor maskColor{255, 0, 0};
        bool showBackdrop = false;
        int backdropSource = 0;
        int backdropMode = 0;
        int backdropOpacity = 50;
    };

    struct BrushOptions {
        int tool = 0;
        int size = 150;
        int density = 50;
        int pressure = 50;
        int rate = 50;
        int jitter = 50;
        int opacity = 100;
        int speed = 50;
        int hardness = 80;
        int ramp = 0;
        bool stylus = false;
        bool pinEdges = false;
        int reconstructMode = 0;
    };

    explicit LiquifyCanvas(AppState *state, QWidget *parent = nullptr);

    BrushOptions &brush() { return brush_; }
    ViewOptions &view() { return view_; }
    double zoom() const { return zoom_; }
    void setZoom(double z, QPointF anchor = QPointF(-1, -1));
    void zoomIn();
    void zoomOut();
    void zoomToFit();
    void zoomToWidth();
    void beginSessionUndoPoint();
    bool undoStroke();
    void pushStrokeUndoPoint();
    QPointF layerToDoc(QPointF lp) const;

    void refreshImages();
    void refreshBackdrop();
    void captureBackdrop();
    void bumpMaskVersion() { ++maskVersion_; }
    bool sessionMoved() const { return sessionMoved_; }
    void showEvent(QShowEvent *event) override;

signals:
    void brushSizeChanged(int size);
    void statusChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void tabletEvent(QTabletEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    float falloffExp() const;
    float dabStrength(bool radial) const;
    float brushRadiusLayer() const;
    QPointF viewToDoc(QPointF vp) const;
    QPointF docToView(QPointF dp) const;
    QPointF viewToLayer(QPointF vp) const;
    bool ensureSession();
    void applyDab(QPointF docPos, float dxLayer, float dyLayer);
    void applyMirrorDab(QPointF docPos);
    void applyMaskDab(QPointF docPos, bool freeze);
    void finishStroke(bool moved);
    void updateHoldTimer();
    QImage layerImage() const;
    void drawChecker(QPainter &p, QRectF r) const;
    void drawMesh(QPainter &p) const;
    void drawMask(QPainter &p) const;

    AppState *state_;
    BrushOptions brush_;
    ViewOptions view_;
    double zoom_ = 1.0;
    QPointF pan_{0, 0};
    bool pressed_ = false;
    bool panning_ = false;
    bool spaceHeld_ = false;
    bool draggingStroke_ = false;
    QPoint pressView_;
    QPointF pressDoc_;
    QPointF lastDoc_;
    QPointF lastDragDir_{1, 0};
    QPointF mirrorAxis_{1, 0};
    bool mirrorAxisValid_ = false;
    double tabletPressure_ = 1.0;
    QTimer holdTimer_;
    std::uint64_t dabSeed_ = 1;
    bool strokeMoved_ = false;
    bool sessionMoved_ = false;
    struct StrokeUndo {
        std::vector<float> mesh;
        std::vector<float> mask;
    };
    std::vector<StrokeUndo> undoStack_;
    QImage backdrop_;
    QImage backdropComposite_;
    QImage frozenLayer_;
    QColor bg_{43, 43, 43};
    mutable QImage maskCache_;
    QImage layerCache_;
    QRect lastDirtyRect_;
    mutable std::uint64_t maskCacheVersion_ = 0;
    std::uint64_t maskVersion_ = 1;
    bool sessionOk_ = false;
    QPointF cursorView_{-1, -1};
};

class LiquifyDialog final : public QDialog {
    Q_OBJECT
public:
    explicit LiquifyDialog(AppState *state, QWidget *parent = nullptr);
    ~LiquifyDialog() override;
    void accept() override;
    void reject() override;

private:
    void buildToolbar();
    void buildProperties();
    void buildBottomBar();
    void applyDialogTheme();
    void setTool(int tool);
    void applyMaskSource(int op);
    void updatePreview();

    AppState *state_;
    LiquifyCanvas *canvas_ = nullptr;
    QToolBar *toolbar_ = nullptr;
    QWidget *props_ = nullptr;
    QWidget *bottom_ = nullptr;
    QLabel *zoomLabel_ = nullptr;
    QComboBox *zoomCombo_ = nullptr;
    QCheckBox *previewBox_ = nullptr;
    QSpinBox *sizeSpin_ = nullptr;
    QComboBox *rampCombo_ = nullptr;
    QCheckBox *stylusBox_ = nullptr;
    QCheckBox *pinBox_ = nullptr;
    QComboBox *reconCombo_ = nullptr;
    QComboBox *maskSourceCombo_ = nullptr;
    QCheckBox *showImageBox_ = nullptr;
    QCheckBox *showMeshBox_ = nullptr;
    QComboBox *meshSizeCombo_ = nullptr;
    QComboBox *meshColorCombo_ = nullptr;
    QCheckBox *showMaskBox_ = nullptr;
    QComboBox *maskColorCombo_ = nullptr;
    QCheckBox *showBackdropBox_ = nullptr;
    QComboBox *backdropUseCombo_ = nullptr;
    QComboBox *backdropModeCombo_ = nullptr;
    QSpinBox *reconStrengthSpin_ = nullptr;
    void reconstructAll();
    void resetMesh();
    void applyMeshStrength();
    void loadMesh(bool last);
    void saveMesh();
    void updateZoomLabel();
    void syncSizeWidgets(int size);
    QWidget *makeSliderRow(const QString &label, int lo, int hi, int value,
                           const QString &suffix, std::function<void(int)> onChange,
                           QSpinBox **outSpin = nullptr);
    QComboBox *colorCombo(QColor current);
    std::vector<float> buildMaskSource();
    std::vector<QAction *> toolActions_;
    int currentTool_ = 0;
    bool previewOn_ = true;
    double lastMeshStrength_ = 100.0;
    pittore::compute::WarpMesh previewSaved_;
    bool previewHasSaved_ = false;
    QString lastMeshPath_;
    bool firstShow_ = true;
};

}
