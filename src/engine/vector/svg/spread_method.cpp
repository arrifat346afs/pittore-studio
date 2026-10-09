// Exact words.
#include "engine/vector/svg/spread_method.h"

namespace pittore::svg {

Spread parseSpread(const std::string& s) {
    if (s == "reflect") {
        return Spread::Reflect;
    }
    if (s == "repeat") {
        return Spread::Repeat;
    }
    return Spread::Pad;
}

}  // namespace pittore::svg
