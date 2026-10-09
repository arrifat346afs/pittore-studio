#pragma once
// One-call intake. Bytes to paint list plus issues.
#include <string>
#include <vector>

#include "engine/vector/svg/parse_stats.h"
#include "engine/vector/svg/render_item.h"
#include "engine/vector/svg/scene.h"
#include "engine/vector/svg/validate.h"

namespace pittore::svg {

struct ImportedSvg {
    Scene scene;
    std::vector<RenderItem> items;
    SvgStats stats;
    std::vector<Issue> issues;
    bool ok = false;
    std::string error;
};

ImportedSvg importSvg(const std::string& xml);

}  // namespace pittore::svg
