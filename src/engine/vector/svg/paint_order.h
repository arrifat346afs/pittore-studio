#pragma once
// Paint order slots. Defaults to fill then stroke.
#include <string>
#include <vector>

namespace pittore::svg {

std::vector<int> paintOrderSlots(const std::string& s);

}  // namespace pittore::svg
