#include "ui/theme.h"

namespace pittore::ui {
namespace {

// Base chrome tone per theme, close to the four reference swatches.
int baseTone(UiTheme t) {
    switch (t) {
        case UiTheme::Black: return 0x1e;
        case UiTheme::DarkGray: return 0x32;
        case UiTheme::MediumGray: return 0x53;
        case UiTheme::LightGray: return 0xb8;
    }
    return 0x32;
}

QColor shade(int base, int delta) {
    const int v = qBound(0, base + delta, 255);
    return QColor(v, v, v);
}

}  // namespace

ThemeColors colorsFor(UiTheme theme) {
    const int b = baseTone(theme);
    const bool light = theme == UiTheme::LightGray;

    ThemeColors c;
    c.chrome = shade(b, 0);
    c.chromeAlt = shade(b, light ? -10 : 9);
    c.chromeSunken = shade(b, light ? -24 : -11);
    c.surround = shade(b, light ? -40 : -19);
    c.border = shade(b, light ? -48 : -14);
    // Translucent so separators read as soft hairlines, not hard rules.
    c.divider = light ? QColor(0x2a, 0x2a, 0x2a, kDividerAlpha)
                      : QColor(0xff, 0xff, 0xff, kDividerAlpha);
    c.text = light ? QColor(0x1a, 0x1a, 0x1a) : QColor(0xd4, 0xd4, 0xd4);
    c.textDim = light ? QColor(0x6a, 0x6a, 0x6a) : QColor(0x8c, 0x8c, 0x8c);
    c.accent = QColor(0x2d, 0x6f, 0xb5);
    c.accentText = QColor(0xff, 0xff, 0xff);
    c.hover = shade(b, light ? -14 : 16);
    c.pressed = shade(b, light ? -28 : 28);
    return c;
}

QPalette paletteFor(UiTheme theme) {
    const ThemeColors c = colorsFor(theme);
    QPalette p;
    p.setColor(QPalette::Window, c.chrome);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.chromeSunken);
    p.setColor(QPalette::AlternateBase, c.chromeAlt);
    p.setColor(QPalette::ToolTipBase, c.chromeAlt);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.chromeAlt);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, c.accentText);
    p.setColor(QPalette::PlaceholderText, c.textDim);
    // Neutral bevel/frame roles: unset, Qt derives a warm tan tint that
    // shows up as olive borders and toggle pills in the dark themes.
    const int b = baseTone(theme);
    const bool lightTheme = theme == UiTheme::LightGray;
    p.setColor(QPalette::Light, shade(b, lightTheme ? 50 : 44));
    p.setColor(QPalette::Midlight, shade(b, lightTheme ? 30 : 26));
    p.setColor(QPalette::Mid, shade(b, lightTheme ? -30 : 34));
    p.setColor(QPalette::Dark, shade(b, lightTheme ? -60 : -22));
    p.setColor(QPalette::Shadow, shade(b, -40));

    p.setColor(QPalette::Disabled, QPalette::WindowText, c.textDim);
    p.setColor(QPalette::Disabled, QPalette::Text, c.textDim);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, c.textDim);
    p.setColor(QPalette::Disabled, QPalette::Highlight, c.chromeAlt);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, c.textDim);
    return p;
}

QColor canvasSurround(int index, UiTheme theme) {
    switch (index % 3) {
        case 0: return colorsFor(theme).surround;
        case 1: return QColor(0x0a, 0x0a, 0x0a);
        default: return QColor(0x80, 0x80, 0x80);
    }
}

QString styleSheetFor(UiTheme theme) {
    const ThemeColors c = colorsFor(theme);
    const QString chrome = c.chrome.name();
    const QString chromeAlt = c.chromeAlt.name();
    const QString sunken = c.chromeSunken.name();
    const QString border = c.border.name();
    const QString text = c.text.name();
    const QString dim = c.textDim.name();
    const QString accent = c.accent.name();
    const QString hover = c.hover.name();
    const QString pressed = c.pressed.name();
    const QString divider = cssColor(c.divider);

    return QString(R"(
QMainWindow, QDialog { background: %1; }
QWidget { color: %5; font-size: 12px; }

QMenuBar { background: %1; border: none; padding: 1px 4px; }
QMenuBar::item { padding: 5px 10px; background: transparent; border-radius: 3px; }
QMenuBar::item:selected { background: %8; }
QMenuBar::item:pressed { background: %7; }

QMenu { background: %2; border: 1px solid %4; border-radius: 6px; padding: 6px 4px; }
QMenu::item { padding: 6px 30px 6px 26px; border-radius: 4px; margin: 0 2px; }
QMenu::item:selected { background: %7; color: #ffffff; }
QMenu::item:disabled { color: %6; }
QMenu::separator { height: 1px; background: %10; margin: 4px 8px; }
QMenu::indicator { width: 13px; height: 13px; left: 7px; }

QToolTip { background: %2; color: %5; border: 1px solid %4; padding: 3px 6px; }

QDockWidget { titlebar-close-icon: none; titlebar-normal-icon: none; }
QDockWidget::title { background: %2; padding: 8px 12px; border-bottom: 1px solid %10; font-weight: 600; }

QTabBar::tab {
    background: %1; color: %6; border: none; border-bottom: 2px solid transparent;
    padding: 8px 12px; min-width: 40px;
}
QTabBar::tab:selected { background: %2; color: %5; border-bottom: 2px solid %7; }
QTabBar::tab:hover:!selected { color: %5; }

QMainWindow::separator { background: %10; width: 1px; height: 1px; }
QSplitter::handle { background: %10; }
QSplitter::handle:horizontal { width: 1px; }
QSplitter::handle:vertical { height: 1px; }

QScrollBar:vertical { background: %1; width: 11px; margin: 0; border: none; }
QScrollBar:horizontal { background: %1; height: 11px; margin: 0; border: none; }
QScrollBar::handle { background: %8; border-radius: 4px; min-height: 24px; min-width: 24px; margin: 2px; }
QScrollBar::handle:hover { background: %9; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QLineEdit, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit {
    background: %3; border: 1px solid %4; border-radius: 5px;
    padding: 3px 6px; min-height: 20px; selection-background-color: %7;
}
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus { border: 1px solid %7; }

QComboBox {
    background: %3; border: 1px solid %4; border-radius: 5px; padding: 3px 8px;
    min-height: 20px;
}
QComboBox:hover { background: %8; }
QComboBox:focus { border: 1px solid %7; }
QComboBox::drop-down { border: none; width: 16px; }
QComboBox::down-arrow { image: url(:/icons/lucide/chevron-down.svg); width: 12px; height: 12px; }
QComboBox QAbstractItemView {
    background: %2; border: 1px solid %4; selection-background-color: %7;
    outline: none;
}

QPushButton {
    background: %2; border: 1px solid %4; border-radius: 5px; padding: 5px 14px;
}
QPushButton:hover { background: %8; }
QPushButton:pressed, QPushButton:checked { background: %9; }
QPushButton:disabled { color: %6; }
QPushButton:default { background: %7; border: 1px solid %7; color: #ffffff; }

QToolButton { background: transparent; border: none; border-radius: 5px; padding: 4px; }
QToolButton:hover { background: %8; }
QToolButton:pressed, QToolButton:checked { background: %9; }
QToolButton::menu-indicator { image: none; }

QSlider::groove:horizontal { background: %9; height: 4px; border-radius: 2px; }
QSlider::sub-page:horizontal { background: %7; height: 4px; border-radius: 2px; }
QSlider::handle:horizontal {
    background: #eeeeee; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px;
}

QListWidget, QTreeWidget, QTreeView, QListView {
    background: %1; border: none; outline: none;
}
QListWidget::item, QTreeWidget::item { padding: 4px; border: none; border-radius: 4px; }
QListWidget::item:selected, QTreeWidget::item:selected { background: %7; color: #ffffff; }
QListWidget::item:hover:!selected, QTreeWidget::item:hover:!selected { background: %8; }
QHeaderView::section { background: %2; border: none; border-right: 1px solid %10; padding: 3px; }

QGroupBox { border: 1px solid %4; border-radius: 6px; margin-top: 10px; padding-top: 10px; }
QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 4px; color: %6; }

QCheckBox::indicator, QRadioButton::indicator { width: 13px; height: 13px; }
QCheckBox { spacing: 8px; }
QCheckBox::indicator:unchecked { background: %3; border: 1px solid %4; border-radius: 3px; }
QCheckBox::indicator:checked { background: %7; border: 1px solid %7; border-radius: 3px; }

QStatusBar { background: %1; border-top: 1px solid %10; }
QStatusBar::item { border: none; }

QProgressBar { background: %3; border: 1px solid %4; border-radius: 2px; text-align: center; }
QProgressBar::chunk { background: %7; }
)")
        .arg(chrome, chromeAlt, sunken, border, text, dim, accent, hover, pressed, divider);
}

}  // namespace pittore::ui
