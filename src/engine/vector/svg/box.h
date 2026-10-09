#pragma once
// Box parse. Used for view and tile rects.
#include <string>

namespace pittore::svg {

struct Box {
    double x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};

Box parseBox(const std::string& s);

}  // namespace pittore::svg
