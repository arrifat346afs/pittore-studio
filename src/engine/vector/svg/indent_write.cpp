// Two spaces per level.
#include "engine/vector/svg/indent_write.h"

namespace pittore::svg {

std::string indentPad(int depth) {
    return std::string((size_t)(depth > 0 ? depth * 2 : 0), ' ');
}

}  // namespace pittore::svg
