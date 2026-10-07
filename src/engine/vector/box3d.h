#pragma once
// 3D boxes: perspective boxes from vanishing points.
//
// A box is 3 extents from an origin plus one/two vanishing directions; faces
// project to quads the shape engine fills. Kept 2.5D (no full scene graph):
// the editor stores the box params and expands to Segments on commit.
#include <array>
#include <vector>

#include "engine/vector/path.h"

namespace pittore::vector {

struct Box3D {
    double ox = 0, oy = 0;          // origin (front-top-left in doc space)
    double ex = 100, ey = 0;        // X extent vector
    double fx = 0, fy = 100;        // Y extent vector
    double gx = -40, gy = -40;      // Z (depth) extent vector
    bool twoPoint = true;           // perspective hint for the tool UI
};

// Six faces as quads (front, back, left, right, top, bottom), each 4 points.
std::array<std::vector<std::pair<double, double>>, 6> boxFaces(const Box3D& box);
// All visible faces as Segment loops (front + top + side by depth sign).
std::vector<Segment> boxToSegments(const Box3D& box);

}  // namespace pittore::vector
