#pragma once
// Spread mode enum.
#include <string>

namespace pittore::svg {

enum class Spread { Pad, Reflect, Repeat };

Spread parseSpread(const std::string& s);

}  // namespace pittore::svg
