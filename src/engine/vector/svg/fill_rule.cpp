// Evenodd words.
#include "engine/vector/svg/fill_rule.h"

namespace pittore::svg {

bool isEvenOdd(const std::string& rule) {
    return rule == "evenodd" || rule == "evenOdd";
}

}  // namespace pittore::svg
