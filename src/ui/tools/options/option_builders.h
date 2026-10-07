#pragma once
// Shared option-spec builders for the per-section option files.
// Split from src/ui/tool_registry.cpp; single definition lives in
// tools/options/option_builders.cpp.
#include <vector>

#include "ui/tools/options/tool_options.h"

namespace pittore::ui::detail {

OptionSpec sep();
OptionSpec label(const char* text);
OptionSpec combo(const char* id, const char* lbl, std::vector<const char*> items,
                 int def = 0, int width = 0);
OptionSpec spin(const char* id, const char* lbl, double mn, double mx, double def,
                const char* suffix = "", double step = 1.0);
OptionSpec check(const char* id, const char* lbl, bool def = false);
OptionSpec toggles(const char* id, std::vector<const char*> items, int def = 0);
OptionSpec button(const char* id, const char* lbl);
OptionSpec brushPreset();
OptionSpec textField(const char* id, const char* lbl, const char* def = "", int width = 140);
OptionSpec colorWell(const char* id, const char* lbl);
OptionSpec selectionModes();
std::vector<const char*> blendModeItems();
std::vector<const char*> familyItems();
std::vector<OptionSpec> paintingOptions(bool flow, bool airbrush, bool smoothing);
std::vector<OptionSpec> shapeOptions(bool radius, bool sides, bool star,
                                     bool corners = false);

}  // namespace pittore::ui::detail
