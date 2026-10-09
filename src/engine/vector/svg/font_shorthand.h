#pragma once
// Font shorthand split.
#include <string>

namespace pittore::svg {

struct FontShorthand {
    std::string style;
    std::string weight;
    std::string size;
    std::string family;
};

FontShorthand parseFontShorthand(const std::string& s);

}  // namespace pittore::svg
