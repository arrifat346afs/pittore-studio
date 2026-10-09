#pragma once
// Path data string to segments. Arcs kept raw here.
#include <string>
#include <string_view>
#include <vector>

namespace pittore::svg {

enum class SegType {
    Move,
    Line,
    H,
    V,
    Cubic,
    SmoothCubic,
    Quad,
    SmoothQuad,
    Arc,
    Close
};

// One command with up to 7 numbers (arc).
struct PathSeg {
    SegType type = SegType::Move;
    bool rel = false;
    double v[7] = {0, 0, 0, 0, 0, 0, 0};
    int n = 0;
};

std::vector<PathSeg> parsePathData(std::string_view s);

}  // namespace pittore::svg
