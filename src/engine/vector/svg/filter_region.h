#pragma once
// Filter region resolve. Fractions of bbox.
namespace pittore::svg {

struct FBox {
    double x = 0, y = 0, w = 0, h = 0;
};

FBox resolveFilterRegion(double ox, double oy, double ow, double oh, double fx,
                         double fy, double fw, double fh);

}  // namespace pittore::svg
