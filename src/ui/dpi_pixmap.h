#pragma once
// DPI-aware QImage→QPixmap bridge (ui/dpi_pixmap.h).
// Brush thumbs, stroke previews and navigator tiles are rendered once at a
// fixed pixel size; without a device-pixel ratio Qt upscales them on
// 150/200% displays and they go soft. Stamping the showing widget's ratio
// keeps logical size identical (geometry tests unaffected) while giving
// the compositor full-resolution pixels.
#include <QImage>
#include <QPixmap>
#include <QWidget>

namespace pittore::ui {

inline QPixmap pixmapForWidget(const QImage& img, const QWidget* widget) {
    QPixmap pm = QPixmap::fromImage(img);
    if (widget) {
        const qreal dpr = widget->devicePixelRatioF();
        if (dpr > 0.0) pm.setDevicePixelRatio(dpr);
    }
    return pm;
}

inline QPixmap pixmapForDpr(const QImage& img, qreal dpr) {
    QPixmap pm = QPixmap::fromImage(img);
    if (dpr > 0.0) pm.setDevicePixelRatio(dpr);
    return pm;
}

}  // namespace pittore::ui
