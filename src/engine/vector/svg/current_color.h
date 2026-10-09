#pragma once
// Current color resolve.
#include <string>

namespace pittore::svg {

bool resolveCurrentColor(const std::string& paint, const std::string& color,
                         std::string& out);

}  // namespace pittore::svg
