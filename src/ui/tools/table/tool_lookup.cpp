#include "ui/tools/table/tool_table.h"

#include <QCoreApplication>

#include <vector>

namespace pittore::ui {

const std::vector<ToolDef>& allTools() { return detail::toolTable(); }

const std::vector<ToolGroup>& allGroups() {
    static const std::vector<ToolGroup> groups = detail::buildToolGroups();
    return groups;
}

const ToolDef& toolDef(ToolId id) {
    auto it = detail::toolTableIndex().find(static_cast<int>(id));
    if (it != detail::toolTableIndex().end()) return *it->second;
    return detail::toolTable().front();
}

QString toolName(ToolId id) {
    return QCoreApplication::translate("Tools", toolDef(id).name);
}

QString toolShortcutText(ToolId id) {
    const char key = toolDef(id).key;
    return key ? QString(QChar(key)) : QString();
}

const ToolGroup* groupForTool(ToolId id) {
    for (const ToolGroup& g : allGroups())
        for (ToolId m : g.members)
            if (m == id) return &g;
    return nullptr;
}

const ToolGroup* groupForKey(char key) {
    if (!key) return nullptr;
    for (const ToolGroup& g : allGroups())
        if (g.key == key) return &g;
    return nullptr;
}

ToolId nextInGroup(ToolId id) {
    const ToolGroup* g = groupForTool(id);
    if (!g || g->members.size() < 2) return id;
    for (std::size_t i = 0; i < g->members.size(); ++i)
        if (g->members[i] == id) return g->members[(i + 1) % g->members.size()];
    return id;
}

}  // namespace pittore::ui
