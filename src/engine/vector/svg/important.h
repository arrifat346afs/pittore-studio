#pragma once
// Important flag strip.
#include <string>

namespace pittore::svg {

struct DeclVal {
    std::string value;
    bool important = false;
};

DeclVal splitImportant(std::string s);

}  // namespace pittore::svg
