#pragma once
// Single integration point for the persona feature: creates the manager + bar
// and inserts the bar above the options bar. Everything else lives in this
// folder. Call once from MainWindow's constructor, after buildDocks() (the
// ToolsPanel must exist for findChild to succeed).
class QMainWindow;

namespace pittore::ui {

class AppState;

void wirePersona(QMainWindow* window, AppState* state);

}  // namespace pittore::ui
