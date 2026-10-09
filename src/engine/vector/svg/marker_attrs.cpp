// Numbers with units flag.
#include "engine/vector/svg/marker_attrs.h"

#include <cstdlib>

namespace pittore::svg {

MarkerArgs parseMarkerArgs(const std::string& refX, const std::string& refY,
                           const std::string& mw, const std::string& mh,
                           const std::string& units) {
    MarkerArgs o;
    o.refX = refX.empty() ? 0 : std::strtod(refX.c_str(), nullptr);
    o.refY = refY.empty() ? 0 : std::strtod(refY.c_str(), nullptr);
    o.mw = mw.empty() ? 3 : std::strtod(mw.c_str(), nullptr);
    o.mh = mh.empty() ? 3 : std::strtod(mh.c_str(), nullptr);
    o.strokeWidthUnits = units.empty() || units == "strokeWidth";
    return o;
}

}  // namespace pittore::svg
