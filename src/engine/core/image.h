#pragma once
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "engine/core/pixel.h"

namespace pittore {

// Host RGBAf image, row-major. CPU side of the tile cache;
// GPU backends mirror it in device memory.
class Image {
  public:
    Image(std::uint32_t width, std::uint32_t height)
        : w_(width), h_(height), data_(static_cast<std::size_t>(width) * height) {
        if (width == 0 || height == 0)
            throw std::invalid_argument("Image dimensions cannot be zero");
    }

    std::uint32_t width() const { return w_; }
    std::uint32_t height() const { return h_; }
    std::size_t pixel_count() const { return data_.size(); }

    RGBAf& at(std::uint32_t x, std::uint32_t y) {
        return data_[static_cast<std::size_t>(y) * w_ + x];
    }
    const RGBAf& at(std::uint32_t x, std::uint32_t y) const {
        return data_[static_cast<std::size_t>(y) * w_ + x];
    }

    RGBAf* data() { return data_.data(); }
    const RGBAf* data() const { return data_.data(); }

    void fill(RGBAf v) { std::fill(data_.begin(), data_.end(), v); }

    // Deep copy. Undo shares Images via shared_ptr, so clone before
    // mutating (copy-on-write) or old snapshots see the edit.
    Image clone() const {
        Image c(w_, h_);
        c.data_ = data_;
        return c;
    }

  private:
    std::uint32_t w_;
    std::uint32_t h_;
    std::vector<RGBAf> data_;
};

}  // namespace pittore