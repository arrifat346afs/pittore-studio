#pragma once
// Pattern tiles over a fill box.
#include <vector>

namespace pittore::svg {

struct Tile {
    double x = 0, y = 0;
};

std::vector<Tile> tilesFor(double bx, double by, double bw, double bh,
                            double tx, double ty, double tw, double th);

}  // namespace pittore::svg
