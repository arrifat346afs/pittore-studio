// Preserve words.
#include "engine/vector/svg/lang_space.h"

namespace pittore::svg {

bool preservesSpace(const std::string& space) {
    return space == "preserve";
}

}  // namespace pittore::svg
