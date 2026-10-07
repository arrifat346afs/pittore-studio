#pragma once
#include <QPoint>
#include <QWidget>

#include "ui/app_state.h"

class QHBoxLayout;

namespace pittore::ui {

// Floating strip that follows (tool + selection + layer). Lives on the
// canvas, drags by its grip, pins, resets to bottom-centre. Never drawn
// into the document.
class ContextualTaskBar final : public QWidget {
    Q_OBJECT

  public:
    explicit ContextualTaskBar(AppState* state, QWidget* parent);

    void resetPosition();
    bool pinned() const { return pinned_; }
    void setPinned(bool on);

    // Keeps the bar near bottom-centre when the canvas resizes (unpinned).
    void reflow();

  signals:
    void commandTriggered(const QString& id);

  protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

  private:
    void rebuild();
    void addCommand(const QString& id, const QString& label, const QString& iconKey = {},
                    bool primary = false);
    void addDivider();

    AppState* state_;
    QHBoxLayout* row_ = nullptr;
    QPoint dragOffset_;
    bool dragging_ = false;
    bool pinned_ = false;
    QPoint pinnedPos_;
};

}  // namespace pittore::ui
