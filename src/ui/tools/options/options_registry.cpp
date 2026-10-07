#include "ui/tools/options/tool_options.h"

#include <algorithm>
#include <string>
#include <vector>

#include "engine/text/text_engine.h"
#include "ui/tools/defs/tool_defs.h"
#include "ui/tools/options/option_builders.h"
#include "ui/tools/options/section_options.h"

namespace pittore::ui {

const std::vector<std::string>& typeFontFamilies() {
    static const std::vector<std::string> list = [] {
        std::vector<std::string> names = pittore::text::familyNames();
        std::string def = pittore::text::defaultFamily();
        if (pittore::text::hasFamily("Inter")) def = "Inter";
        auto it = std::find(names.begin(), names.end(), def);
        if (it != names.end())
            std::rotate(names.begin(), it, it + 1);
        else
            names.insert(names.begin(), def);
        return names;
    }();
    return list;
}


// ---------------------------------------------------------------------------
// Per-tool options bars. Content follows the standard options-bar layout for
// each tool; anything the engine cannot honour yet is still present and wired
// into AppState so the UX is complete ahead of the backend.
// ---------------------------------------------------------------------------
std::vector<OptionSpec> optionsFor(ToolId id) {
    std::vector<OptionSpec> out;
    switch (toolDef(id).section) {
        case ToolSection::Selection:   out = detail::selectionToolOptions(id); break;
        case ToolSection::CropMeasure: out = detail::cropToolOptions(id); break;
        case ToolSection::RetouchPaint: out = detail::retouchToolOptions(id); break;
        case ToolSection::DrawType:    out = detail::drawToolOptions(id); break;
        case ToolSection::Navigation:
        case ToolSection::Generative:  out = detail::navGenToolOptions(id); break;
    }
    if (!out.empty()) return out;
    return {detail::label("No options for this tool.")};
}

}  // namespace pittore::ui
