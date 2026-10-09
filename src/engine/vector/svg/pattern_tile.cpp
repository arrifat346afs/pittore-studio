// Tile grid over box. Clamped to fill area.
#include "engine/vector/svg/pattern_tile.h"

#include <cmath>

namespace pittore::svg {

std::vector<Tile> tilesFor(double bx, double by, double bw, double bh,
                            double tx, double ty, double tw, double th) {
    std::vector<Tile> out;
    if (bw <= 0 || bh <= 0 || tw <= 0 || th <= 0) {
        return out;
    }
    const int nx = (int)std::ceil(bw / tw) + 1;
    const int ny = (int)std::ceil(bh / th) + 1;
    if (nx > 512 || ny > 512) {
        return out;
    }
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const double x = tx + i * tw;
            const double y = ty + j * th;
            if (x + tw < bx || x > bx + bw || y + th < by || y > by + bh) {
                continue;
            }
            out.push_back(Tile{x, y});
        }
    }
    return out;
}

}  // namespace pittore::svg
