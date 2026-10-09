#pragma once
// Decoration flags.
#include <string>

namespace pittore::svg {

struct DecoFlags {
    bool underline = false;
    bool overline = false;
    bool strike = false;
};

DecoFlags parseDeco(const std::string& s);

}  // namespace pittore::svg
