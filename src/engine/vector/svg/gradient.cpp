// Stop sample. Sorted input assumed.
#include "engine/vector/svg/gradient.h"

namespace pittore::svg {

void sampleStops(const std::vector<GradStop>& stops, double t, float out[4]) {
    if (stops.empty()) {
        out[0] = out[1] = out[2] = 0;
        out[3] = 1;
        return;
    }
    if (t <= stops.front().offset) {
        out[0] = stops.front().r;
        out[1] = stops.front().g;
        out[2] = stops.front().b;
        out[3] = stops.front().a;
        return;
    }
    if (t >= stops.back().offset) {
        out[0] = stops.back().r;
        out[1] = stops.back().g;
        out[2] = stops.back().b;
        out[3] = stops.back().a;
        return;
    }
    for (size_t k = 1; k < stops.size(); ++k) {
        if (t <= stops[k].offset) {
            const double span = stops[k].offset - stops[k - 1].offset;
            const double f = span <= 0 ? 0 : (t - stops[k - 1].offset) / span;
            out[0] = (float)(stops[k - 1].r + (stops[k].r - stops[k - 1].r) * f);
            out[1] = (float)(stops[k - 1].g + (stops[k].g - stops[k - 1].g) * f);
            out[2] = (float)(stops[k - 1].b + (stops[k].b - stops[k - 1].b) * f);
            out[3] = (float)(stops[k - 1].a + (stops[k].a - stops[k - 1].a) * f);
            return;
        }
    }
}

}  // namespace pittore::svg
