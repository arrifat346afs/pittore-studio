#pragma once
#include <QFrame>
#include <QHash>
#include <QSet>
#include <QVector>
#include <QWidget>

#include "ui/app_state.h"
#include "ui/tool_registry.h"

class QGridLayout;
class QTimer;
class QToolButton;

namespace pittore::ui {

class AppState;

// Foreground/background wells from the toolbar bottom: two swatches,
// swap (X) and reset (D). The active well is where Color-panel edits go.
class ColorWells final : public QWidget {
    Q_OBJECT

  public:
    explicit ColorWells(AppState* state, QWidget* parent = nullptr);

    QSize sizeHint() const override;

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

  private:
    QRectF foregroundRect() const;
    QRectF backgroundRect() const;
    QRectF swapRect() const;
    QRectF resetRect() const;

    AppState* state_;
};

// The vertical tools strip: selection, flyouts, Shift+letter cycling,
// one/two-column layout, plus quick mask / screen mode at the bottom.
class ToolsPanel final : public QFrame {
    Q_OBJECT

  public:
    explicit ToolsPanel(AppState* state, QWidget* parent = nullptr);

    bool twoColumn() const { return twoColumn_; }
    void setTwoColumn(bool on);

    // Hidden via Edit Toolbar… — lives under "…" instead of the strip.
    QStringList hiddenTools() const;
    void setHiddenTools(const QStringList& ids);

  signals:
    void editToolbarRequested();
    void screenModeCycleRequested();

  private:
    void rebuild();
    void showFlyout(const ToolGroup& group, QToolButton* anchor);
    // Fire-time group resolution for button handlers (see makeToolButton).
    void showFlyoutFor(QToolButton* anchor);
    QToolButton* makeToolButton(const ToolGroup& group);
    void applyTheme();
    // Strip metrics follow tablet mode (pen-sized tap targets).
    int buttonSize() const;
    int iconSize() const;

    AppState* state_;
    QGridLayout* grid_ = nullptr;
    QWidget* toolArea_ = nullptr;
    ColorWells* wells_ = nullptr;
    QToolButton* quickMaskButton_ = nullptr;
    QToolButton* screenModeButton_ = nullptr;
    QHash<int, QToolButton*> buttons_;   // group leader id -> button
    QHash<int, ToolId> groupSelection_;  // group leader id -> last used member
    QSet<int> hidden_;
    bool twoColumn_ = false;
};

}  // namespace pittore::ui
