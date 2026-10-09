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
// The standard zoom ladder. Clicks with the Zoom tool and Ctrl+/Ctrl- step
// through these while inside the range, then continue geometrically (x2)
// past the ends; wheel/pinch multiply freely. Inline: a static local costs
// nothing per call and keeps this table reachable from translation units
// that link only a subset of the UI sources (e.g. headless tests).
inline const QVector<double>& zoomSteps() {
    static const QVector<double> steps = {
        0.000833, 0.00125, 0.0025, 0.005, 0.0067, 0.01, 0.0125, 0.01667, 0.025, 0.03333,
        0.05, 0.0667, 0.0833, 0.125, 0.1667, 0.25, 0.3333, 0.50, 0.6667, 1.0,
        1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 11.0, 16.0, 22.0, 32.0};
    return steps;
}
// Infinite-zoom rails: the ladder above covers everyday work; wheel, pinch
// and keys continue geometrically past its ends until these sanity rails.
// Twelve orders of magnitude each way keeps every downstream double (view
// coords stay below 2^53 for any real document) and guarded int conversion
// exact. setZoom rejects anything non-positive or non-finite outright.
constexpr double kMinZoom = 1e-9;
constexpr double kMaxZoom = 1e9;
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
