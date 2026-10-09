#pragma once
// Split leading number and unit suffix.
#include <string>

namespace pittore::svg {

struct NumUnit {
    double value = 0;
    std::string unit;
    bool valid = false;
};

NumUnit splitNumUnit(const std::string& s);

}  // namespace pittore::svg
