// Auto or angle.
#include "engine/vector/svg/marker_orient.h"

#include "engine/vector/svg/angle.h"

namespace pittore::svg {

MarkerOrient parseMarkerOrient(const std::string& s) {
    MarkerOrient o;
    if (s == "auto") {
        o.autoMode = true;
        return o;
    }
    o.deg = parseAngleDeg(s);
    return o;
}

}  // namespace pittore::svg
