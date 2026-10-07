#pragma once
#include <cstdint>
#include <stdexcept>

namespace pittore {

// Tile size is fixed: 256x256, one CUDA workgroup / AMD wavefront chunk.
inline constexpr std::uint32_t kTileSize = 256;

// Tile bounds in image pixels. x1/y1 inside, x2/y2 outside.
struct TileRect {
    std::uint32_t x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    std::uint32_t width() const { return x2 - x1; }
    std::uint32_t height() const { return y2 - y1; }
    std::uint32_t area() const { return width() * height(); }
};

// Splits an image into tiles, clamped at the edges.
// Independent of tile contents (host pixels, device textures, cached results).
class TileGrid {
  public:
    TileGrid(std::uint32_t image_width, std::uint32_t image_height)
        : w_(image_width), h_(image_height),
          tx_((image_width + kTileSize - 1) / kTileSize),
          ty_((image_height + kTileSize - 1) / kTileSize) {
        if (image_width == 0 || image_height == 0)
            throw std::invalid_argument("TileGrid dimensions cannot be zero");
    }

    std::uint32_t image_width() const { return w_; }
    std::uint32_t image_height() const { return h_; }
    std::uint32_t tiles_x() const { return tx_; }
    std::uint32_t tiles_y() const { return ty_; }
    std::uint32_t tile_count() const { return tx_ * ty_; }

    // Pixel rect of tile (x, y); edge tiles are smaller.
    TileRect tile(std::uint32_t x, std::uint32_t y) const {
        const std::uint32_t x1 = x * kTileSize;
        const std::uint32_t y1 = y * kTileSize;
        return TileRect{x1, y1, x1 + tile_width(x), y1 + tile_height(y)};
    }

  private:
    std::uint32_t tile_width(std::uint32_t x) const {
        const std::uint32_t leftover = w_ - x * kTileSize;
        return leftover < kTileSize ? leftover : kTileSize;
    }
    std::uint32_t tile_height(std::uint32_t y) const {
        const std::uint32_t leftover = h_ - y * kTileSize;
        return leftover < kTileSize ? leftover : kTileSize;
    }

    std::uint32_t w_;
    std::uint32_t h_;
    std::uint32_t tx_;
    std::uint32_t ty_;
};

}  // namespace pittore