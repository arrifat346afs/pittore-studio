#pragma once
// Conservative path bounds from segments. Tight enough to cull with.
#include <vector>

#include "engine/vector/svg/path_data.h"
#include "engine/vector/svg/transform.h"

namespace pittore::svg {

struct SegBox {
    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty = true;
};

// Local-space box over the segments. Curves add control points, arcs pad
// the end point by radii. Returns false when there is nothing to paint.
bool pathSegBox(const std::vector<PathSeg>& segs, SegBox& out);

// Local box pushed through the matrix with a stroke pad.
SegBox pathWorldBox(const std::vector<PathSeg>& segs, const Affine& m,
                    double pad);

}  // namespace pittore::svg
