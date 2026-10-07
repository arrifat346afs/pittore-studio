// Marker geometry.
#include "engine/vector/marker.h"

#include <cmath>

#include "engine/vector/svg_dom.h"

namespace pittore::vector {

std::vector<MarkerInstance> markersForPolyline(
    const std::vector<std::pair<double, double>>& points, bool closed,
    const std::string& markerStart, const std::string& markerMid,
    const std::string& markerEnd, double strokeWidth) {
    std::vector<MarkerInstance> out;
    if (points.size() < 2) return out;
    double sw = strokeWidth > 0 ? strokeWidth : 1.0;
    auto angleAt = [&](size_t i) {
        size_t n = points.size();
        double x0, y0, x1, y1;
        if (i == 0) {
            x0 = points[0].first;
            y0 = points[0].second;
            x1 = points[1].first;
            y1 = points[1].second;
        } else if (i + 1 >= n) {
            x0 = points[n - 2].first;
            y0 = points[n - 2].second;
            x1 = points[n - 1].first;
            y1 = points[n - 1].second;
        } else {
            // Bisector of incoming/outgoing for smooth mid markers.
            double ax = points[i].first - points[i - 1].first;
            double ay = points[i].second - points[i - 1].second;
            double bx = points[i + 1].first - points[i].first;
            double by = points[i + 1].second - points[i].second;
            double aa = std::atan2(ay, ax), ab = std::atan2(by, bx);
            double m = (aa + ab) / 2.0;
            // Pick the average pointing forward.
            double fx = std::cos(m), fy = std::sin(m);
            double dot = fx * std::cos(ab) + fy * std::sin(ab);
            if (dot < 0) m += 3.14159265358979;
            return m * 180.0 / 3.14159265358979;
        }
        return std::atan2(y1 - y0, x1 - x0) * 180.0 / 3.14159265358979;
    };
    if (!markerStart.empty() && !closed)
        out.push_back(MarkerInstance{markerStart, points.front().first,
                                     points.front().second, angleAt(0), sw, 1.0});
    if (!markerMid.empty()) {
        size_t last = closed ? points.size() : points.size() - 1;
        for (size_t i = 1; i < last; i++)
            out.push_back(MarkerInstance{markerMid, points[i].first, points[i].second,
                                         angleAt(i), sw, 1.0});
    }
    if (!markerEnd.empty() && !closed)
        out.push_back(MarkerInstance{markerEnd, points.back().first,
                                     points.back().second, angleAt(points.size() - 1),
                                     sw, 1.0});
    return out;
}

MarkerRefs markerRefsFor(const SvgElement& el, const SvgDocument& doc) {
    MarkerRefs r;
    auto get = [&](const char* p) -> std::string {
        if (auto v = doc.resolved(el, p)) return *v;
        return "";
    };
    std::string all = get("marker");
    if (!all.empty() && all != "none") {
        std::string t = urlRefTarget(all);
        if (!t.empty()) r.start = r.mid = r.end = t;
    }
    std::string s = get("marker-start"), m = get("marker-mid"), e = get("marker-end");
    if (!s.empty() && s != "none") {
        std::string t = urlRefTarget(s);
        r.start = t.empty() ? s : t;
    }
    if (!m.empty() && m != "none") {
        std::string t = urlRefTarget(m);
        r.mid = t.empty() ? m : t;
    }
    if (!e.empty() && e != "none") {
        std::string t = urlRefTarget(e);
        r.end = t.empty() ? e : t;
    }
    return r;
}

std::optional<MarkerDef> markerDefFor(const std::string& id, const SvgDocument& doc) {
    SvgElement* el = doc.findId(id);
    if (!el || el->tag != "marker") return std::nullopt;
    MarkerDef d;
    d.id = id;
    try {
        if (auto v = el->get("refX")) d.refX = std::stod(*v);
        if (auto v = el->get("refY")) d.refY = std::stod(*v);
        if (auto v = el->get("markerWidth")) d.width = std::stod(*v);
        if (auto v = el->get("markerHeight")) d.height = std::stod(*v);
        if (auto v = el->get("orient"))
            d.orientAuto = (*v == "auto" || *v == "auto-start-reverse");
        else
            d.orientAuto = true;
        if (auto v = el->get("orient");
            v && *v != "auto" && *v != "auto-start-reverse")
            d.orientDeg = std::stod(*v);
        if (auto v = el->get("markerUnits")) d.strokeWidthUnits = (*v == "strokeWidth");
    } catch (...) {
    }
    return d;
}

std::vector<std::string> builtinMarkerIds() {
    return {"Arrow1",     "Arrow2",  "Triangle", "Dot",   "Square", "Diamond",
            "Cross",      "Circle",  "Tail",    "Head",  "Slash",  "Bar",
            "ArrowOpen",  "ArrowClosed", "DiamondThin", "CircleOpen", "SquareOpen",
            "Tick",       "DiamondDot", "DoubleArrow", "Fork",    "HalfArrow",
            "Leaf",       "Notch",   "Pentagon", "Star",   "Teardrop", "Wedge",
            "XMark",      "Plus"};
}

std::string builtinMarkersSvg() {
    // Compact arrow/dot library; each marker is ~3 lines and orient="auto".
    return std::string(
        "<defs>\n"
        "<marker id=\"mk-Arrow1\" viewBox=\"0 0 10 10\" refX=\"8\" refY=\"5\" "
        "markerWidth=\"4\" markerHeight=\"4\" orient=\"auto-start-reverse\">"
        "<path d=\"M0,0L10,5L0,10z\" fill=\"context-stroke\"/></marker>\n"
        "<marker id=\"mk-Arrow2\" viewBox=\"0 0 10 10\" refX=\"8\" refY=\"5\" "
        "markerWidth=\"4\" markerHeight=\"4\" orient=\"auto\">"
        "<path d=\"M0,1L9,5L0,9\" fill=\"none\" stroke=\"context-stroke\" "
        "stroke-width=\"2\"/></marker>\n"
        "<marker id=\"mk-Triangle\" viewBox=\"0 0 10 10\" refX=\"9\" refY=\"5\" "
        "markerWidth=\"4\" markerHeight=\"4\" orient=\"auto\">"
        "<path d=\"M0,0L10,5L0,10z\"/></marker>\n"
        "<marker id=\"mk-Dot\" viewBox=\"0 0 10 10\" refX=\"5\" refY=\"5\" "
        "markerWidth=\"3\" markerHeight=\"3\"><circle cx=\"5\" cy=\"5\" r=\"4\"/>"
        "</marker>\n"
        "<marker id=\"mk-Square\" viewBox=\"0 0 10 10\" refX=\"5\" refY=\"5\" "
        "markerWidth=\"3\" markerHeight=\"3\"><rect x=\"1\" y=\"1\" width=\"8\" "
        "height=\"8\"/></marker>\n"
        "<marker id=\"mk-Diamond\" viewBox=\"0 0 10 10\" refX=\"5\" refY=\"5\" "
        "markerWidth=\"3\" markerHeight=\"3\">"
        "<path d=\"M5,0L10,5L5,10L0,5z\"/></marker>\n"
        "<marker id=\"mk-Cross\" viewBox=\"0 0 10 10\" refX=\"5\" refY=\"5\" "
        "markerWidth=\"4\" markerHeight=\"4\">"
        "<path d=\"M1,1L9,9M9,1L1,9\" stroke=\"context-stroke\" "
        "stroke-width=\"2\"/></marker>\n"
        "</defs>\n");
}

}  // namespace pittore::vector
