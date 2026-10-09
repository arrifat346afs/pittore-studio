#pragma once
// Path data scan over a view. Same segments as parsePathData, no copy.
#include <string_view>
#include <vector>

#include "engine/vector/svg/path_data.h"

namespace pittore::svg {

// Append segments for the view. Bad input is skipped, never hangs.
void scanPathData(std::string_view in, std::vector<PathSeg>& out);

}  // namespace pittore::svg
