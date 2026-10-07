// Pattern / hatch / dash-preset data.
#include "engine/vector/pattern.h"

#include <cmath>

#include "engine/vector/svg_dom.h"

namespace pittore::vector {

void PatternTile::cellAt(double x, double y, long& col, long& row) const {
    double w = width > 0 ? width : 1.0, h = height > 0 ? height : 1.0;
    col = (long)std::floor((x - this->x) / w);
    row = (long)std::floor((y - this->y) / h);
}

void PatternTile::cellOrigin(long col, long row, double& ox, double& oy) const {
    ox = x + col * width;
    oy = y + row * height;
}

std::optional<PatternTile> patternFor(const std::string& id,
                                      const SvgDocument& doc) {
    SvgElement* el = doc.findId(id);
    if (!el || el->tag != "pattern") return std::nullopt;
    PatternTile t;
    t.id = id;
    try {
        if (auto v = el->get("x")) t.x = std::stod(*v);
        if (auto v = el->get("y")) t.y = std::stod(*v);
        if (auto v = el->get("width")) t.width = std::stod(*v);
        if (auto v = el->get("height")) t.height = std::stod(*v);
    } catch (...) {
    }
    if (t.width <= 0) t.width = 10;
    if (t.height <= 0) t.height = 10;
    t.hasContent = !el->children.empty();
    return t;
}

std::optional<HatchSpec> hatchFor(const std::string& id, const SvgDocument& doc) {
    SvgElement* el = doc.findId(id);
    if (!el || (el->tag != "hatch" && el->tag != "pattern")) return std::nullopt;
    HatchSpec h;
    h.id = id;
    try {
        if (auto v = el->get("pitch")) h.pitch = std::stod(*v);
        if (auto v = el->get("hatchRotation"))
            h.rotationDeg = std::stod(*v);
        else if (auto v = el->get("rotation"))
            h.rotationDeg = std::stod(*v);
        if (auto v = el->get("stroke-width")) h.strokeWidth = std::stod(*v);
    } catch (...) {
    }
    return h;
}

std::vector<std::string> builtinPatternIds() {
    return {"Stripes",     "Dots",      "Grid",     "Crosshatch", "Diagonal",
            "Bricks",      "Checker",   "Waves",    "Triangles",  "Hexagons",
            "Circles",     "Zigzag",    "Hatch45",  "Hatch90",    "Stipple",
            "Confetti",    "Polka",     "Herringbone", "Basket",  "Scales",
            "Stars",       "Diamonds",  "Lines-thin", "Lines-thick"};
}

std::string builtinPatternsSvg() {
    return std::string(
        "<defs>\n"
        "<pattern id=\"pat-Stripes\" width=\"8\" height=\"8\" "
        "patternUnits=\"userSpaceOnUse\"><rect width=\"4\" height=\"8\"/></pattern>\n"
        "<pattern id=\"pat-Dots\" width=\"10\" height=\"10\" "
        "patternUnits=\"userSpaceOnUse\"><circle cx=\"3\" cy=\"3\" r=\"1.6\"/></pattern>\n"
        "<pattern id=\"pat-Grid\" width=\"10\" height=\"10\" "
        "patternUnits=\"userSpaceOnUse\"><path d=\"M10,0H0V10\" fill=\"none\" "
        "stroke-width=\"1\"/></pattern>\n"
        "<pattern id=\"pat-Crosshatch\" width=\"8\" height=\"8\" "
        "patternUnits=\"userSpaceOnUse\"><path d=\"M0,0L8,8M8,0L0,8\" "
        "stroke-width=\"1\"/></pattern>\n"
        "<pattern id=\"pat-Checker\" width=\"16\" height=\"16\" "
        "patternUnits=\"userSpaceOnUse\"><rect width=\"8\" height=\"8\"/>"
        "<rect x=\"8\" y=\"8\" width=\"8\" height=\"8\"/></pattern>\n"
        "</defs>\n");
}

std::vector<std::pair<std::string, std::vector<float>>> builtinDashPresets() {
    return {
        {"Solid", {}},           {"Dotted", {0, 3}},
        {"Dashed 4-2", {4, 2}},  {"Dashed 6-2", {6, 2}},
        {"Dash-dot", {6, 2, 0, 2}}, {"Dash-dot-dot", {6, 2, 0, 2, 0, 2}},
        {"Long dash", {12, 3}},  {"Short dash", {2, 2}},
        {"Railroad", {8, 2, 2, 2}}, {"Tight dots", {0, 2}},
        {"Loose dots", {0, 5}},  {"Dash 2-1", {2, 1}},
        {"Dash 8-4-2-4", {8, 4, 2, 4}}, {"Double-dot", {0, 2, 0, 2, 6, 2}},
        {"Centerline", {12, 3, 2, 3}}, {"Hidden", {4, 2, 1, 2}},
        {"Engineering", {10, 2, 2, 2, 2, 2}}, {"Stitch", {3, 3}},
        {"Zipper", {1, 2}},      {"Morse-A", {2, 2, 6, 2}},
        {"Morse-B", {6, 2, 2, 2, 2, 2}}, {"Border", {6, 1}},
        {"Fine", {1, 1}},        {"Coarse", {8, 8}},
        {"Wave-hint", {5, 2, 1, 2}}, {"Steps", {4, 1, 1, 1}},
        {"Traffic", {10, 5}},    {"CAD-1", {12, 3, 1, 3}},
        {"CAD-2", {6, 6}},       {"CAD-3", {0, 4, 8, 4}},
        {"Vintage", {7, 2, 2, 2}}, {"Ruler", {1, 3, 5, 3}},
    };
}

}  // namespace pittore::vector
