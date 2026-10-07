#pragma once
// Stroke symmetry math: mirror painting across the canvas-center axes.
// Pure document-space geometry (no Qt), so unit tests can include this
// directly. Angles: stroke directions are canvas-clockwise degrees
// (atan2(dy, dx)); tip angles run counter-clockwise.
#include <cmath>

namespace pittore::ui::symmetry {

// Number of stroke copies for the toggle pair (base + mirrors).
inline int copyCount(bool symX, bool symY) {
    return (symX ? 2 : 1) * (symY ? 2 : 1);
}

// Copy flags: bit 0 mirrors x (vertical axis), bit 1 mirrors y.
inline bool copyFlipX(int copy) { return (copy & 1) != 0; }
inline bool copyFlipY(int copy) { return (copy & 2) != 0; }

inline double mirrorX(double x, double cx, bool flip) {
    return flip ? 2.0 * cx - x : x;
}

inline double mirrorComp(double c, bool flip) { return flip ? -c : c; }

inline double normDeg(double deg) {
    while (deg > 180.0) deg -= 360.0;
    while (deg <= -180.0) deg += 360.0;
    return deg;
}

// Mirror a canvas-clockwise stroke direction across the enabled axes.
inline double mirrorDirDeg(double dirDeg, bool flipX, bool flipY) {
    double d = dirDeg;
    if (flipX) d = 180.0 - d;
    if (flipY) d = -d;
    return normDeg(d);
}

// Mirror a counter-clockwise tip angle: each single-axis mirror flips the
// rotation sense; both axes together preserve it.
inline double mirrorTipAngle(double angleDeg, bool flipX, bool flipY) {
    if (flipX == flipY) return angleDeg;
    return -angleDeg;
}

}  // namespace pittore::ui::symmetry
