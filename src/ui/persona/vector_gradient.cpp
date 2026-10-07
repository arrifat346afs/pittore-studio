#include "ui/persona/vector_gradient.h"

#include <algorithm>
#include <cmath>

namespace pittore::ui {
namespace {

void pushStop(std::vector<pittore::vector::ArtStop>& out, float pos,
              const QColor& c, int alpha = -1) {
    pittore::vector::ArtStop s;
    s.pos = pos;
    s.rgba[0] = static_cast<std::uint8_t>(c.red());
    s.rgba[1] = static_cast<std::uint8_t>(c.green());
    s.rgba[2] = static_cast<std::uint8_t>(c.blue());
    s.rgba[3] = static_cast<std::uint8_t>(alpha < 0 ? c.alpha() : alpha);
    out.push_back(s);
}

}  // namespace

std::vector<pittore::vector::ArtStop> gradientPresetStops(
    int preset, const QColor& foreground, const QColor& background,
    bool reverse) {
    std::vector<pittore::vector::ArtStop> stops;
    const QColor fg = foreground.isValid() ? foreground : QColor(0, 0, 0);
    const QColor bg = background.isValid() ? background : QColor(255, 255, 255);
    switch (preset) {
        case 1:
            pushStop(stops, 0.0f, fg);
            pushStop(stops, 1.0f, fg, 0);
            break;
        case 2:
            pushStop(stops, 0.0f, QColor(0, 0, 0));
            pushStop(stops, 1.0f, QColor(255, 255, 255));
            break;
        case 3:  // Chrome
            pushStop(stops, 0.0f, QColor(238, 238, 238));
            pushStop(stops, 0.5f, QColor(136, 136, 136));
            pushStop(stops, 1.0f, QColor(34, 34, 34));
            break;
        case 4:  // Spectrum
            pushStop(stops, 0.0f, QColor(255, 0, 0));
            pushStop(stops, 0.2f, QColor(255, 255, 0));
            pushStop(stops, 0.4f, QColor(0, 255, 0));
            pushStop(stops, 0.6f, QColor(0, 255, 255));
            pushStop(stops, 0.8f, QColor(0, 0, 255));
            pushStop(stops, 1.0f, QColor(255, 0, 255));
            break;
        case 5:  // Copper
            pushStop(stops, 0.0f, QColor(184, 115, 51));
            pushStop(stops, 1.0f, QColor(90, 45, 12));
            break;
        default:
            pushStop(stops, 0.0f, fg);
            pushStop(stops, 1.0f, bg);
            break;
    }
    if (reverse) {
        for (auto& s : stops) s.pos = 1.0f - s.pos;
        std::reverse(stops.begin(), stops.end());
    }
    return stops;
}

pittore::vector::ArtPaint paintWithGradient(
    const pittore::vector::ArtPaint& paint, const QPointF& nodeStart,
    const QPointF& nodeEnd, bool radial,
    const std::vector<pittore::vector::ArtStop>& stops) {
    pittore::vector::ArtPaint out = paint;
    out.hasFill = true;
    out.hasGradient = true;
    pittore::vector::ArtGradient& g = out.gradient;
    g.radial = radial;
    if (radial) {
        g.cx = nodeStart.x();
        g.cy = nodeStart.y();
        g.r = std::max(1.0, std::hypot(nodeEnd.x() - nodeStart.x(),
                                       nodeEnd.y() - nodeStart.y()));
    } else {
        g.x1 = nodeStart.x();
        g.y1 = nodeStart.y();
        g.x2 = nodeEnd.x();
        g.y2 = nodeEnd.y();
    }
    g.stops = stops;
    return out;
}

pittore::vector::ArtPaint paintWithTransparency(
    const pittore::vector::ArtPaint& paint, const QPointF& nodeStart,
    const QPointF& nodeEnd, int type, bool reverse, const QColor& fallback) {
    pittore::vector::ArtPaint out = paint;
    if (type <= 0) {
        // None: back to a flat fill.
        out.hasGradient = false;
        out.gradient.stops.clear();
        return out;
    }
    QColor base(fallback);
    if (out.hasFill)
        base = QColor(out.fill[0], out.fill[1], out.fill[2], out.fill[3]);
    else if (out.hasStroke)
        base = QColor(out.stroke[0], out.stroke[1], out.stroke[2],
                      out.stroke[3]);
    if (!base.isValid()) base = fallback;
    const bool radial = (type == 2 || type == 3);
    std::vector<pittore::vector::ArtStop> stops;
    pushStop(stops, 0.0f, base, reverse ? 0 : 255);
    pushStop(stops, 1.0f, base, reverse ? 255 : 0);
    out.hasFill = true;
    out.hasGradient = true;
    pittore::vector::ArtGradient& g = out.gradient;
    g.radial = radial;
    if (radial) {
        g.cx = nodeStart.x();
        g.cy = nodeStart.y();
        g.r = std::max(1.0, std::hypot(nodeEnd.x() - nodeStart.x(),
                                       nodeEnd.y() - nodeStart.y()));
    } else {
        g.x1 = nodeStart.x();
        g.y1 = nodeStart.y();
        g.x2 = nodeEnd.x();
        g.y2 = nodeEnd.y();
    }
    g.stops = std::move(stops);
    return out;
}

}  // namespace pittore::ui
