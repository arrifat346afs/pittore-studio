#pragma once
// Shared window-shell internals: helpers with more than one consumer live
// here so no window/*.cpp depends on another.
#include <QImage>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>

#include "ui/app_state.h"

class QCloseEvent;

namespace pittore::ui {

// R21: a tool letter held longer than this becomes a spring-loaded temporary
// switch that reverts on key release. Tapped, it is a permanent switch.
constexpr qint64 kSpringDwellMs = 220;

QString shortcutTextFor(char key);

// A panel shown as its own top-level window instead of a docked tab. The
// Character and Paragraph panels behave like palettes: closing the window just
// hides it and unchecks the menu action.
class FloatingPanelWindow final : public QWidget {
  public:
    FloatingPanelWindow(const QString& title, QWidget* content, QWidget* parent)
        : QWidget(parent, Qt::Window) {
        setWindowTitle(title);
        setAttribute(Qt::WA_DeleteOnClose, true);
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        column->setSpacing(0);
        column->addWidget(content);
        resize(300, 620);
        if (parent) {
            const QRect frame = parent->frameGeometry();
            move(qMax(frame.left() + 20, frame.right() - width() - 60),
                 frame.top() + 80);
        }
    }

    std::function<void()> onClose;

  protected:
    void closeEvent(QCloseEvent* event) override {
        QWidget::closeEvent(event);
        if (onClose) onClose();
    }
};

// Render exactly one layer over transparency ("export this layer").
QImage renderLayerForExport(DocumentItem* doc, int index);

}  // namespace pittore::ui
