#pragma once
// Per-pressure response curves ("value|x,y;x,y;..." strings). A preset can
// carry the author's own pressure response for size, opacity and flow
// instead of the built-in linear/concave fallbacks; empty means legacy.
// Points are pressure in [0,1] with linear interpolation between them.
#include <QString>

#include <algorithm>
#include <utility>
#include <vector>

namespace pittore::ui::brushcurve {

struct Curve {
    double value = 1.0;  // base multiplier the curve scales
    std::vector<std::pair<double, double>> points;
    bool has() const { return !points.empty(); }
};

// "v|x1,y1;x2,y2;" (the v| header may be omitted). Tolerates junk: bad
// points are dropped, x is clamped to [0,1], y to [0,2], capped at 64.
inline Curve parseEncoded(const QString& s) {
    Curve c;
    QString body = s;
    const int bar = s.indexOf(QLatin1Char('|'));
    if (bar >= 0) {
        bool ok = false;
        const double v = s.left(bar).toDouble(&ok);
        if (ok) c.value = v;
        body = s.mid(bar + 1);
    }
    for (const QString& item : body.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        const int comma = item.indexOf(QLatin1Char(','));
        if (comma < 0) continue;
        bool okX = false, okY = false;
        const double x = item.left(comma).toDouble(&okX);
        const double y = item.mid(comma + 1).toDouble(&okY);
        if (!okX || !okY) continue;
        c.points.emplace_back(std::clamp(x, 0.0, 1.0),
                              std::clamp(y, 0.0, 2.0));
        if (c.points.size() >= 64) break;
    }
    std::sort(c.points.begin(), c.points.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return c;
}

inline QString encode(double value, const QString& points) {
    return QString::number(value) + QLatin1Char('|') + points;
}

// Piecewise-linear evaluation with clamped ends.
inline double eval(const Curve& c, double p) {
    if (c.points.empty()) return c.value;
    if (p <= c.points.front().first) return c.value * c.points.front().second;
    for (std::size_t i = 1; i < c.points.size(); ++i) {
        if (p <= c.points[i].first) {
            const double x0 = c.points[i - 1].first;
            const double y0 = c.points[i - 1].second;
            const double x1 = c.points[i].first;
            const double y1 = c.points[i].second;
            const double t = (x1 > x0) ? (p - x0) / (x1 - x0) : 0.0;
            return c.value * (y0 + (y1 - y0) * std::clamp(t, 0.0, 1.0));
        }
    }
    return c.value * c.points.back().second;
}

}  // namespace pittore::ui::brushcurve
