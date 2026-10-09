#pragma once
// Winning decl for one prop across rules.
#include <string>
#include <vector>

#include "engine/vector/svg/css_parse.h"

namespace pittore::svg {

std::string lookupSheet(const std::vector<CssRule>& sheet,
                        const std::string& tag, const std::string& id,
                        const std::string& classes, const std::string& prop);

}  // namespace pittore::svg
