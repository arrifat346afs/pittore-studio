#pragma once
// Internal tool-table storage. Only tool_lookup.cpp and options_registry.cpp
// (via the public toolDef()) need this; everyone else uses ui/tool_registry.h.
#include <unordered_map>
#include <vector>

#include "ui/tools/defs/tool_defs.h"

namespace pittore::ui::detail {

const std::vector<ToolDef>& toolTable();
std::vector<ToolGroup> buildToolGroups();
const std::unordered_map<int, const ToolDef*>& toolTableIndex();

}  // namespace pittore::ui::detail
