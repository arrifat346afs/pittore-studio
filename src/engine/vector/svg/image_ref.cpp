// Prefix checks only.
#include "engine/vector/svg/image_ref.h"

namespace pittore::svg {

bool isDataUri(const std::string& href) {
    return href.compare(0, 5, "data:") == 0;
}

bool isEmbeddedImage(const std::string& href) {
    return href.compare(0, 11, "data:image/") == 0;
}

}  // namespace pittore::svg
