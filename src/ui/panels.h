#pragma once
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

#include "ui/app_state.h"

namespace pittore::ui {

// A panel is one tab + one control surface. The Window menu and the icon
// rail both read this table, so adding a panel is one entry here.
struct PanelInfo {
    QString id;
    QString title;
    QString iconKey;
    QString defaultDock;   // "right" | "right-secondary" | "left" | "bottom"
    bool defaultVisible = false;
    std::function<QWidget*(AppState*, QWidget*)> factory;
    // True for popups like Character/Paragraph that float instead of docking.
    bool floating = false;
};

const QVector<PanelInfo>& allPanels();
const PanelInfo* panelInfo(const QString& id);

// Shared footer row (Layers, Channels, Paths, History, …).
QWidget* makePanelFooter(AppState* state, const QVector<QPair<QString, QString>>& buttons,
                         QWidget* parent, std::function<void(const QString&)> onClick);

}  // namespace pittore::ui
