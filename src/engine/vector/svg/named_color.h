#pragma once
// Extra named colors. False when unknown.
#include <string>

namespace pittore::svg {

bool namedRgba(const std::string& name, float out[4]);

}  // namespace pittore::svg
