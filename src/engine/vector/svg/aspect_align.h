#pragma once
// Aspect align parse.
#include <string>

namespace pittore::svg {

struct Aspect {
    int align = 4;  // 0 none, else 1..9 grid
    bool slice = false;
};

Aspect parseAspect(const std::string& s);

}  // namespace pittore::svg
