#pragma once
// Shared vector-path commands (ui/persona).
//
// One implementation behind the Paths-panel footer, the Path/ShapeLayer
// task-bar buttons and MainWindow::runCommand, so the three surfaces can never
// disagree. Work-path storage and boolean combine ops do not exist yet — those
// ids get an honest status hint instead of silent no-ops.
#include <QColor>
#include <QString>

namespace pittore::ui {

class AppState;

// Load the editable art layer's outline as a selection (alpha → coverage,
// one undo step inside).
bool vectorPathToSelection(AppState* state);
// Paint the editable art layer's fill/stroke with `color`, one undo step each.
bool vectorFillActiveShape(AppState* state, const QColor& color,
                           const QString& undoName);
bool vectorStrokeActiveShape(AppState* state, const QColor& color,
                             const QString& undoName);
// Honest "planned" hint for ops with no engine yet (work paths, combine).
void vectorPlannedHint(AppState* state, const QString& what);

// Measure display: document pixels → real-world units. `units` is the
// measure_units option (0 px, 1 pt, 2 mm, 3 cm, 4 in); `dpi` the document
// resolution; `scale` document pixels per unit (draw_scale option, 1 = 1:1).
double vectorMeasureDisplay(double docPx, int units, int dpi, double scale);

}  // namespace pittore::ui
