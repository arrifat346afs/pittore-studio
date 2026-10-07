#pragma once
// Boolean operations on polygons (union, intersection, difference, xor).
//
// Split-classify-trace on flattened rings: every edge is split at every
// crossing/touch/overlap, each fragment is classified against the other
// operand, fragments are selected per operation (coincident edges resolved
// by local sidedness, so grid-aligned shapes with shared edges work), and
// the survivors are traced into loops by smallest-left-turn. Inputs are
// closed loops (closure implicit); degenerate rings are dropped. Toolkit-
// free; coordinates are doubles, snap tolerance 1e-9.
#include <cstddef>
#include <utility>
#include <vector>

namespace pittore::vector {

enum class BoolOp { Union, Intersection, Difference, Xor };

// Fill rule used for inside/outside classification of each operand.
enum class BoolFill { EvenOdd, NonZero };

using BoolRing = std::vector<std::pair<double, double>>;

// Combine subject with clip. Difference is subject-minus-clip. Empty when
// the result has no area.
std::vector<BoolRing> booleanOp(const std::vector<BoolRing>& subject,
                                const std::vector<BoolRing>& clip, BoolOp op,
                                BoolFill fill = BoolFill::NonZero);

// Signed area (positive for counter-clockwise loops).
double boolRingArea(const BoolRing& ring);

}  // namespace pittore::vector
