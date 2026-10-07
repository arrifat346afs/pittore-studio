#pragma once
// Per-section option tables. Each answers for its own tools and returns {}
// otherwise; the dispatcher in options_registry.cpp owns the fallback.
#include <vector>

#include "ui/tools/options/tool_options.h"

namespace pittore::ui::detail {

std::vector<OptionSpec> selectionToolOptions(ToolId id);
std::vector<OptionSpec> cropToolOptions(ToolId id);
std::vector<OptionSpec> retouchToolOptions(ToolId id);
std::vector<OptionSpec> drawToolOptions(ToolId id);
std::vector<OptionSpec> navGenToolOptions(ToolId id);

}  // namespace pittore::ui::detail
