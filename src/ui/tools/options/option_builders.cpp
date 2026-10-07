#include "ui/tools/options/option_builders.h"

#include <algorithm>
#include <string>
#include <vector>

namespace pittore::ui::detail {

// ---------------------------------------------------------------------------
// Option-spec helpers
// ---------------------------------------------------------------------------
OptionSpec sep() { return {OptionKind::Separator, "", "", {}, 0, 0, 0, {}, "", 0}; }

OptionSpec label(const char* text) {
    OptionSpec s;
    s.kind = OptionKind::Label;
    s.label = text;
    return s;
}

OptionSpec combo(const char* id, const char* lbl, std::vector<const char*> items,
                 int def, int width) {
    OptionSpec s;
    s.kind = OptionKind::Combo;
    s.id = id;
    s.label = lbl;
    s.items = std::move(items);
    s.defaultValue = def;
    s.width = width;
    return s;
}

OptionSpec spin(const char* id, const char* lbl, double mn, double mx, double def,
                const char* suffix, double step) {
    OptionSpec s;
    s.kind = OptionKind::Spin;
    s.id = id;
    s.label = lbl;
    s.min = mn;
    s.max = mx;
    s.step = step;
    s.defaultValue = def;
    s.suffix = suffix;
    return s;
}

OptionSpec check(const char* id, const char* lbl, bool def) {
    OptionSpec s;
    s.kind = OptionKind::Check;
    s.id = id;
    s.label = lbl;
    s.defaultValue = def;
    return s;
}

OptionSpec toggles(const char* id, std::vector<const char*> items, int def) {
    OptionSpec s;
    s.kind = OptionKind::ToggleGroup;
    s.id = id;
    s.items = std::move(items);
    s.defaultValue = def;
    return s;
}

OptionSpec button(const char* id, const char* lbl) {
    OptionSpec s;
    s.kind = OptionKind::Button;
    s.id = id;
    s.label = lbl;
    return s;
}

OptionSpec brushPreset() {
    OptionSpec s;
    s.kind = OptionKind::BrushPreset;
    // The id matches what strokeRadius/CanvasView read (the popup writes
    // "brush_size"), so the default below is what `option(tool,
    // "brush_size")` resolves to before the options bar exists.
    s.id = "brush_size";
    s.label = "Brush";
    // Default brush size for every painting/dab tool: a prominent 64 px circle.
    s.defaultValue = QVariant(64.0);
    return s;
}

OptionSpec textField(const char* id, const char* lbl, const char* def, int width) {
    OptionSpec s;
    s.kind = OptionKind::Text;
    s.id = id;
    s.label = lbl;
    s.defaultValue = QString::fromUtf8(def);
    s.width = width;
    return s;
}

OptionSpec colorWell(const char* id, const char* lbl) {
    OptionSpec s;
    s.kind = OptionKind::ColorWell;
    s.id = id;
    s.label = lbl;
    return s;
}

// The four selection boolean-mode buttons shared by every selection tool.
OptionSpec selectionModes() {
    return toggles("selmode", {"New", "Add", "Subtract", "Intersect"}, 0);
}

// The blend-mode list used by every painting tool's options bar (R28).
std::vector<const char*> blendModeItems() {
    return {"Normal", "Dissolve", "Behind", "Clear",
            "Darken", "Multiply", "Color Burn", "Linear Burn", "Darker Color",
            "Lighten", "Screen", "Color Dodge", "Linear Dodge (Add)", "Lighter Color",
            "Overlay", "Soft Light", "Hard Light", "Vivid Light", "Linear Light",
            "Pin Light", "Hard Mix",
            "Difference", "Exclusion", "Subtract", "Divide",
            "Hue", "Saturation", "Color", "Luminosity"};
}

// Pointers into `typeFontFamilies`, whose storage is stable for the process.
std::vector<const char*> familyItems() {
    std::vector<const char*> out;
    for (const std::string& name : typeFontFamilies()) out.push_back(name.c_str());
    return out;
}

std::vector<OptionSpec> paintingOptions(bool flow, bool airbrush, bool smoothing) {
    std::vector<OptionSpec> o{
        brushPreset(), sep(),
        combo("mode", "Mode", blendModeItems(), 0, 130),
        spin("opacity", "Opacity", 1, 100, 100, "%"),
    };
    if (flow) o.push_back(spin("flow", "Flow", 1, 100, 100, "%"));
    if (airbrush) {
        o.push_back(check("airbrush", "Airbrush", false));
        o.push_back(spin("airbrush_rate", "Rate", 1, 100, 20, ""));
    }
    if (smoothing) o.push_back(spin("smoothing", "Smoothing", 0, 100, 0, "%"));
    o.push_back(check("pressure_size", "Pressure for size", true));
    o.push_back(check("pressure_opacity", "Pressure for opacity", true));
    o.push_back(check("symmetry", "Symmetry"));
    return o;
}

std::vector<OptionSpec> shapeOptions(bool radius, bool sides, bool star,
                                     bool corners) {
    std::vector<OptionSpec> o{
        combo("shapemode", "", {"Shape", "Path", "Pixels"}, 0, 90), sep(),
        colorWell("fill", "Fill"),
        colorWell("stroke", "Stroke"),
        spin("strokewidth", "", 0, 1000, 1, " px", 0.5),
        combo("strokestyle", "", {"Solid", "Dashed", "Dotted"}, 0, 80), sep(),
        combo("cap", "", {"Butt", "Round", "Square"}, 0, 80),
        combo("join", "", {"Miter", "Round", "Bevel"}, 0, 80), sep(),
        spin("w", "W", 0, 100000, 0, " px"),
        spin("h", "H", 0, 100000, 0, " px"), sep(),
        combo("pathop", "", {"New Layer", "Combine", "Subtract", "Intersect", "Exclude"}, 0, 110),
        combo("align", "", {"Align Edges", "Align to Canvas"}, 0, 110),
    };
    if (radius) o.push_back(spin("radius", "Radius", 0, 10000, 0, " px"));
    if (corners) {
        // Per-corner treatments: individual radii win over Radius.
        o.push_back(combo("corner_type", "Corners",
                          {"Rounded", "Straight", "Concave", "Cutout"}, 0, 110));
        o.push_back(spin("radius_tl", "TL", 0, 10000, 0, " px"));
        o.push_back(spin("radius_tr", "TR", 0, 10000, 0, " px"));
        o.push_back(spin("radius_br", "BR", 0, 10000, 0, " px"));
        o.push_back(spin("radius_bl", "BL", 0, 10000, 0, " px"));
    }
    if (sides) o.push_back(spin("sides", "Sides", 3, 100, 5));
    if (star) o.push_back(spin("indent", "Indent", 1, 100, 50, "%"));
    o.push_back(check("aligned_edges", "Align Edges", true));
    o.push_back(check("keep_selected", "Keep Selected", true));
    return o;
}

}  // namespace pittore::ui::detail
