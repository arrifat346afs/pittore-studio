#pragma once
// Cubic deviation estimate.
namespace pittore::svg {

double cubicFlatness(double x0, double y0, double x1, double y1, double x2,
                     double y2, double x3, double y3);

}  // namespace pittore::svg
