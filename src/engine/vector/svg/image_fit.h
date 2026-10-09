#pragma once
// Aspect fit. Source view into dest rect.
namespace pittore::svg {

struct FitRect {
    double x = 0, y = 0, w = 0, h = 0;
};

FitRect fitView(double vw, double vh, double dx, double dy, double dw,
                double dh, bool slice, bool none);

}  // namespace pittore::svg
