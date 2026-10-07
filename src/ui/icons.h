#pragma once
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QString>

#include "ui/tool_registry.h"

namespace pittore::ui {

// Icons are Lucide (ISC, vendored in ui/icons/lucide/) rendered from embedded
// SVG and recoloured by theme, with procedural QPainter fallbacks for niche
// photo tools with no Lucide equivalent. Crisp at any HiDPI. All glyphs on a
// 24x24 grid.
QPixmap glyphPixmap(const QString& key, int size, const QColor& color, qreal dpr = 1.0);

// Tool icon with active colours baked into QIcon modes.
QIcon toolIcon(ToolId id, const QColor& normal, const QColor& active);

// Panel + chrome icons (layers, color, history, quick-mask, …).
QIcon chromeIcon(const QString& key, const QColor& normal, const QColor& active);

}  // namespace pittore::ui
