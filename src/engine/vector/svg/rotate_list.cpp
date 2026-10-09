// Reuse double list.
#include "engine/vector/svg/rotate_list.h"

#include "engine/vector/svg/number_list.h"

namespace pittore::svg {

std::vector<double> parseRotateList(const std::string& s) {
    return parseDoubles(s);
}

}  // namespace pittore::svg
