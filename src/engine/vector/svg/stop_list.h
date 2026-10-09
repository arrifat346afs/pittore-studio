#pragma once
// Stop rows to sorted stops.
#include <string>
#include <vector>

#include "engine/vector/svg/gradient.h"

namespace pittore::svg {

struct StopRow {
    std::string offset;
    std::string color;
    std::string opacity;
};

std::vector<GradStop> buildStops(const std::vector<StopRow>& rows);

}  // namespace pittore::svg
