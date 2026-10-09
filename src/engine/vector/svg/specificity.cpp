// Count selectors. Later wins on ties elsewhere.
#include "engine/vector/svg/specificity.h"

namespace pittore::svg {

int specificity(const std::string& selector) {
    int score = 0;
    for (char c : selector) {
        if (c == '#') {
            score += 100;
        } else if (c == '.') {
            score += 10;
        }
    }
    if (!selector.empty() && selector[0] != '#' && selector[0] != '.' &&
        selector != "*") {
        score += 1;
    }
    return score;
}

}  // namespace pittore::svg
