#pragma once
// Rotate list for text glyphs.
#include <string>
#include <vector>

namespace pittore::svg {

std::vector<double> parseRotateList(const std::string& s);

}  // namespace pittore::svg
