#pragma once
// Marker orient. Auto flag plus angle.
#include <string>

namespace pittore::svg {

struct MarkerOrient {
    bool autoMode = false;
    double deg = 0;
};

MarkerOrient parseMarkerOrient(const std::string& s);

}  // namespace pittore::svg
