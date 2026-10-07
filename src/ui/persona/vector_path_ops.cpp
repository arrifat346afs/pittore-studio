#include "ui/persona/vector_path_ops.h"

#include <QtGlobal>

#include "ui/app_state.h"
#include "ui/persona/vector_edit.h"

namespace pittore::ui {

bool vectorPathToSelection(AppState* state) {
    if (!state) return false;
    const int index = vectorEditableLayer(state);
    if (index < 0) {
        // selectFromLayerAlpha would refuse with its own hint; name the need.
        state->setStatusHint(
            QObject::tr("Select a vector shape layer to load its outline."));
        return false;
    }
    // Art layers already render their outline into pixels, so the alpha
    // channel IS the path outline (soft edges kept). Undo is handled inside.
    return state->selectFromLayerAlpha(index);
}

bool vectorFillActiveShape(AppState* state, const QColor& color,
                           const QString& undoName) {
    if (!state) return false;
    DocumentItem* d = state->activeDocument();
    const int index = vectorEditableLayer(state);
    if (!d || index < 0 || !d->layers[index].art) {
        state->setStatusHint(
            QObject::tr("Select a vector shape layer to fill."));
        return false;
    }
    auto paint = d->layers[index].art->paint;
    paint.hasFill = true;
    paint.fill[0] = static_cast<std::uint8_t>(color.red());
    paint.fill[1] = static_cast<std::uint8_t>(color.green());
    paint.fill[2] = static_cast<std::uint8_t>(color.blue());
    paint.fill[3] = static_cast<std::uint8_t>(color.alpha());
    return state->applyVectorPaint(index, paint, -1.0, undoName);
}

bool vectorStrokeActiveShape(AppState* state, const QColor& color,
                             const QString& undoName) {
    if (!state) return false;
    DocumentItem* d = state->activeDocument();
    const int index = vectorEditableLayer(state);
    if (!d || index < 0 || !d->layers[index].art) {
        state->setStatusHint(
            QObject::tr("Select a vector shape layer to stroke."));
        return false;
    }
    auto paint = d->layers[index].art->paint;
    paint.hasStroke = true;
    paint.stroke[0] = static_cast<std::uint8_t>(color.red());
    paint.stroke[1] = static_cast<std::uint8_t>(color.green());
    paint.stroke[2] = static_cast<std::uint8_t>(color.blue());
    paint.stroke[3] = static_cast<std::uint8_t>(color.alpha());
    return state->applyVectorPaint(index, paint, -1.0, undoName);
}

void vectorPlannedHint(AppState* state, const QString& what) {
    if (!state) return;
    state->setStatusHint(
        QObject::tr("%1 needs stored work paths — planned.").arg(what));
}

double vectorMeasureDisplay(double docPx, int units, int dpi, double scale) {
    if (!(dpi > 0)) dpi = 96;
    if (!(scale > 0.0) || !qIsFinite(scale)) scale = 1.0;
    double perUnit = 1.0;  // document pixels per display unit at 1:1
    switch (units) {
        case 1:
            perUnit = dpi / 72.0;  // pt
            break;
        case 2:
            perUnit = dpi / 25.4;  // mm
            break;
        case 3:
            perUnit = dpi / 2.54;  // cm
            break;
        case 4:
            perUnit = static_cast<double>(dpi);  // in
            break;
        default:
            break;  // 0 px and anything unknown: raw pixels
    }
    if (!(perUnit > 0.0)) perUnit = 1.0;
    return docPx / perUnit * scale;
}

}  // namespace pittore::ui
