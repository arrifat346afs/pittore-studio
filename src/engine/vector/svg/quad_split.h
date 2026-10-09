#pragma once
// Quad to cubic control points.
namespace pittore::svg {

void quadToCubic(double x0, double y0, double qx, double qy, double x1,
                 double y1, double& c1x, double& c1y, double& c2x,
                 double& c2y);

}  // namespace pittore::svg
