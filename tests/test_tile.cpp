// TileGrid: the 256x256 compositing grid, edge-clamped rectangles, area math.
#include "engine/core/tile.h"
#include "test_util.h"

using pittore::TileGrid;
using pittore::kTileSize;

void test_tile() {
    // Exact multiple of tile size.
    TileGrid exact(512, 512);
    CHECK_EQ(exact.tiles_x(), 2u);
    CHECK_EQ(exact.tiles_y(), 2u);
    CHECK_EQ(exact.tile_count(), 4u);
    CHECK_EQ(exact.tile(0, 0).width(), kTileSize);
    CHECK_EQ(exact.tile(1, 1).height(), kTileSize);

    // Odd sizes: last tile on each axis clamps.
    TileGrid odd(1000, 700);
    CHECK_EQ(odd.tiles_x(), 4u);   // ceil(1000/256) = 4
    CHECK_EQ(odd.tiles_y(), 3u);   // ceil(700/256) = 3
    CHECK_EQ(odd.tile_count(), 12u);

    const auto last_x = odd.tile(3, 0);
    CHECK_EQ(last_x.x1, 768u);
    CHECK_EQ(last_x.x2, 1000u);    // 1000 - 3*256 = 232
    CHECK_EQ(last_x.width(), 232u);

    const auto last_y = odd.tile(0, 2);
    CHECK_EQ(last_y.y2, 700u);     // 700 - 2*256 = 188
    CHECK_EQ(last_y.height(), 188u);

    const auto interior = odd.tile(1, 1);
    CHECK_EQ(interior.width(), kTileSize);
    CHECK_EQ(interior.height(), kTileSize);

    // Every tile must be inside the image, tile_count partitions the area
    // exactly.
    std::uint32_t covered = 0;
    for (std::uint32_t ty = 0; ty < odd.tiles_y(); ++ty) {
        for (std::uint32_t tx = 0; tx < odd.tiles_x(); ++tx) {
            const auto r = odd.tile(tx, ty);
            CHECK(r.x1 < r.x2);
            CHECK(r.y1 < r.y2);
            CHECK(r.x2 <= 1000u);
            CHECK(r.y2 <= 700u);
            covered += r.area();
        }
    }
    CHECK_EQ(covered, 1000u * 700u);

    // Tiny image: one single tile, clamped to the image itself.
    TileGrid tiny(3, 5);
    CHECK_EQ(tiny.tile_count(), 1u);
    const auto t = tiny.tile(0, 0);
    CHECK_EQ(t.width(), 3u);
    CHECK_EQ(t.height(), 5u);
}

#ifndef PITTORE_TEST_NO_MAIN
TEST_MAIN_CALL(test_tile)
#endif