#pragma once
// Id remap for import. Rewrites url refs too.
#include <map>
#include <string>

namespace pittore::svg {

std::map<std::string, std::string> remapIds(
    const std::map<std::string, std::string>& attrs, const std::string& suffix);

}  // namespace pittore::svg
