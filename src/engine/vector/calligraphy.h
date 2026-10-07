#pragma once
// Calligraphic strokes: nib-driven filled paths.
//
// A calligraphic stroke is a filled outline swept by a rotated elliptical nib
// along the input spine; nib angle, width, thinning, mass and flatness shape
// the sweep, and pressure/tilt modulate width. Output is fill Segments (Close
// chains) the rasterizer fills nonzero.
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {

struct CalligraphyNib {
    double width = 12.0;      // nib width at pressure 1
    double angleDeg = 30.0;   // nib angle
    double flatness = 0.15;   // minor/major ratio (1 = round brush)
    double thinning = 0.0;    // -1..1 pressure bias
    double mass = 0.0;        // 0..1 smoothing inertia
};

struct CalligraphySample {
    double x = 0.0, y = 0.0;
    double pressure = 1.0;  // 0..1
    double tiltX = 0.0, tiltY = 0.0;
};

// Sweep the nib along `spine`, producing one filled outline per subpath.
std::vector<Segment> calligraphyStroke(const std::vector<CalligraphySample>& spine,
                                       const CalligraphyNib& nib);

}  // namespace pittore::vector
