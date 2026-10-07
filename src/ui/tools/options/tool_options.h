#pragma once
// Options-bar schema: each tool declares its options declaratively.
// Split from ui/tool_registry.h.
#include <QVariant>

#include <string>
#include <vector>

#include "ui/tools/ids/tool_ids.h"

namespace pittore::ui {

enum class OptionKind {
    Separator,
    Label,
    Combo,
    Spin,
    Slider,
    Check,
    ToggleGroup,
    Button,
    ColorWell,
    BrushPreset,
    Text,
};

struct OptionSpec {
    OptionKind kind = OptionKind::Label;
    const char* id = "";
    const char* label = "";
    std::vector<const char*> items;
    double min = 0.0;
    double max = 100.0;
    double step = 1.0;
    QVariant defaultValue;
    const char* suffix = "";
    int width = 0;
};

std::vector<OptionSpec> optionsFor(ToolId id);

const std::vector<std::string>& typeFontFamilies();

}  // namespace pittore::ui
