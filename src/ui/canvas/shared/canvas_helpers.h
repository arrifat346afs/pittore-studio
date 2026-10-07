#pragma once
// Canvas-local helpers shared by the per-concern canvas translation units.
// Everything here had exactly one translation unit's worth of consumers, so
// it lives in exactly one place with external linkage instead of an
// anonymous namespace nobody else could reach.
#include <QCursor>
#include <QImage>
#include <QPoint>
#include <QSize>
#include <QWidget>

#include <cstddef>
#include <string>
#include <vector>

#include "engine/compute/warp.h"
#include "ui/app_state.h"
#include "ui/persona/vector_pen.h"
#include "ui/tool_registry.h"

class QByteArray;
class QMimeData;

namespace pittore::ui {

class CanvasView;

constexpr int kRulerSize = 18;

QCursor brushHiddenCursor();
const QVector<double>& zoomSteps();
bool isSelectionTool(ToolId id);
bool isPaintTool(ToolId id);
bool isTypeTool(ToolId id);
// Shape tools: press-drag draws a live vector shape layer (closed outlines,
// except Line which draws an open stroke path). QRCode is listed but has no
// encoder yet — the canvas refuses it with a hint.
bool isShapeTool(ToolId id);
// Free path tools: press/click builds an editable Bezier path (Pen),
// streams one (Freehand) or clicks smooth points (Curvature).
bool isPenTool(ToolId id);
PenMode penModeFor(ToolId id);
bool applyLiquifyBrush(compute::WarpMesh& m, int mode, float cx, float cy,
                       float radius, float strength, float dx, float dy);

// Ruler strips live in the scroll area's viewport margins rather than inside
// the viewport, so they never scroll with the document and never need to be
// excluded from canvas hit-testing.
class CanvasRuler final : public QWidget {
  public:
    CanvasRuler(CanvasView* view, Qt::Orientation orientation, QWidget* parent);
  protected:
    void paintEvent(QPaintEvent*) override;
    // Drag out of the ruler drops a guide on the canvas (release commits,
    // Escape abandons); the cursor already promises the split direction.
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
  private:
    CanvasView* view_;
    Qt::Orientation orientation_;
};

// Step a UTF-8 byte offset by one character, never splitting a multibyte
// sequence. Both clamp at the string ends.
std::size_t prevCharBoundary(const std::string& s, std::size_t at);
std::size_t nextCharBoundary(const std::string& s, std::size_t at);

// Object-Select support: alpha restriction + component picking + mask union.
QImage layerOpaqueDocMask(const LayerItem* layer, int floor,
                          const QSize& docSize, double& opaqueFraction);
QImage cutoutComponentAt(const QImage& opaque, const QPoint& seed);
QImage selectionComponentAt(const QImage& mask, const QPoint& seed);
void unionMaskInto(QImage& acc, const QImage& m);

bool isCodecImportSuffix(const QByteArray& suffix);
bool dropMimeHasImage(const QMimeData* mime);

}  // namespace pittore::ui
