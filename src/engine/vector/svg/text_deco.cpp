// Space split words.
#include "engine/vector/svg/text_deco.h"

namespace pittore::svg {

DecoFlags parseDeco(const std::string& s) {
    DecoFlags f;
    if (s.find("underline") != std::string::npos) {
        f.underline = true;
    }
    if (s.find("overline") != std::string::npos) {
        f.overline = true;
    }
    if (s.find("line-through") != std::string::npos) {
        f.strike = true;
    }
    return f;
}

}  // namespace pittore::svg
