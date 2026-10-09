// Evenodd words.
#include "engine/vector/svg/clip_rule.h"

namespace pittore::svg {

bool isClipEvenOdd(const std::string& rule) {
    return rule == "evenodd" || rule == "evenOdd";
}

}  // namespace pittore::svg
