#pragma once
// Map to style string. Stable order.
#include <map>
#include <string>

namespace pittore::svg {

std::string joinStyle(const std::map<std::string, std::string>& decls);

}  // namespace pittore::svg
