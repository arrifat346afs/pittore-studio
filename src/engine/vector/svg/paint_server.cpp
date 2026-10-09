// Paint resolve. Ref miss returns false.
#include "engine/vector/svg/paint_server.h"

namespace pittore::svg {

bool resolvePaint(const Paint& p, const GradientStore& gs, double t,
                  float out[4]) {
    if (p.none) {
        return false;
    }
    if (p.hasRef) {
        auto it = gs.byId.find(p.ref);
        if (it == gs.byId.end()) {
            return false;
        }
        sampleStops(it->second.stops, t, out);
        return true;
    }
    out[0] = p.r;
    out[1] = p.g;
    out[2] = p.b;
    out[3] = p.a;
    return true;
}

}  // namespace pittore::svg
