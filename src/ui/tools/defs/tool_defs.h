#pragma once
// Toolbar sections, ToolDef table and flyout groups.
// Split from ui/tool_registry.h.
#include <QString>

#include <vector>

#include "ui/tools/ids/tool_ids.h"

namespace pittore::ui {

enum class ToolSection {
    Selection,
    CropMeasure,
    RetouchPaint,
    DrawType,
    Navigation,
    Generative,
};

struct ToolDef {
    ToolId id = ToolId::Move;
    const char* name = "";
    const char* iconKey = "";
    char key = 0;
    ToolSection section = ToolSection::Selection;
    bool groupLeader = false;
    const char* hint = "";
};

struct ToolGroup {
    ToolId leader = ToolId::Move;
    char key = 0;
    ToolSection section = ToolSection::Selection;
    std::vector<ToolId> members;
};

const std::vector<ToolDef>& allTools();
const std::vector<ToolGroup>& allGroups();

const ToolDef& toolDef(ToolId id);
QString toolName(ToolId id);
QString toolShortcutText(ToolId id);

const ToolGroup* groupForTool(ToolId id);
const ToolGroup* groupForKey(char key);
ToolId nextInGroup(ToolId id);

}  // namespace pittore::ui
