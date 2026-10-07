#pragma once
#include <QColor>
#include <QPalette>
#include <QString>

namespace pittore::ui {

// UI brightness, matching the interface colour theme.
enum class UiTheme { Black, DarkGray, MediumGray, LightGray };

// All chrome colours come from the active theme. The pasteboard behind
// documents stays darker so the canvas feels "in front".
struct ThemeColors {
    QColor chrome;        // panel / dock / toolbar background
    QColor chromeAlt;     // list rows, headers, slightly lifted surfaces
    QColor chromeSunken;  // wells, entry fields, sunken groups
    QColor surround;      // the pasteboard behind the document
    QColor border;        // control outlines (inputs, cards)
    QColor divider;       // 1px separators between panels/sections (white on dark)
    QColor text;          // primary label text
    QColor textDim;       // secondary / disabled label text
    QColor accent;        // selection highlight (PS blue)
    QColor accentText;    // text on accent
    QColor hover;         // hover wash on buttons/rows
    QColor pressed;       // pressed / checked wash
};

ThemeColors colorsFor(UiTheme theme);

// Opacity (0-255) of every 1px panel/section divider. Tune here only.
inline constexpr int kDividerAlpha = 80;

// CSS colour with alpha ("rgba(r,g,b,a)"); QColor::name() drops alpha.
inline QString cssColor(const QColor& c) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}
QPalette paletteFor(UiTheme theme);

// App-wide stylesheet on top of Fusion (set in main.cpp). Covers dock tabs,
// scrollbars, combos, menus — the bits Fusion leaves unpolished.
QString styleSheetFor(UiTheme theme);

// Pasteboard colour cycling (Space+F): theme / black / gray.
QColor canvasSurround(int index, UiTheme theme);

}  // namespace pittore::ui
