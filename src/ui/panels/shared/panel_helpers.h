#pragma once
// Shared internals for the per-panel translation units.
// Split from src/ui/panels.cpp — helpers with more than one consumer live here
// so no panel .cpp depends on another panel .cpp.
#include <QString>

class QLabel;
class QComboBox;
class QToolButton;
class QWidget;
class QColor;

namespace pittore::ui {

class AppState;

QToolButton* footerButton(AppState* state, const QString& iconKey, const QString& tip,
                          QWidget* parent);
QLabel* sectionLabel(const QString& text, AppState* state, QWidget* parent);
// Colour well used by the Character panel (defined in shared/panel_helpers.cpp).
void paintColorSwatch(QToolButton* button, const QColor& c);
// Makes a font-family combo searchable while keeping index semantics: editable
// line edit + contains-match popup completer; Enter resolves exact match
// first (case-insensitive), then first contains match, and reverts the text
// on no match. Popup picks flow through currentIndexChanged exactly as
// before, so existing commit handlers are untouched.
void makeFamilyComboSearchable(QComboBox* box);

// Internal drag-and-drop payload for layer rows.
inline const char* kLayerDragMime = "application/x-pittore-layer-rows";

// Live drop-caret state shared between the Layers panel and its rows.
struct LayerDropState {
    enum Mode { None = 0, Above, Below, IntoGroup };
    int row = -1;
    Mode mode = None;
};

}  // namespace pittore::ui
