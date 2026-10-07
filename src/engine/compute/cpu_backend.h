#pragma once
#include <cstddef>
#include <vector>

#include "engine/compute/backend.h"

namespace pittore::compute {

// Canonical CPU device descriptor (one fake "device" per physical core group).
Device cpu_device();

// CPU reference backend. Always built — the GPU backends must match it bit-for-bit
// at float tolerance, which is what the kernel tests enforce.
class CpuBackend final : public ComputeBackend {
  public:
    CpuBackend();

    BackendType type() const override { return BackendType::CPU; }
    const std::string& name() const override { return name_; }
    const Device& device() const override { return device_; }

    std::unique_ptr<Buffer> make_buffer(std::size_t bytes) override;

    void grayscale(Buffer& src, Buffer& dst, std::uint32_t w,
                   std::uint32_t h) override;
    void composite(Buffer& bottom, const Buffer& top, std::uint32_t w,
                   std::uint32_t h, BlendMode mode) override;
    void composite_region(Buffer& bottom, const Buffer& top, std::uint32_t w,
                          std::uint32_t h, std::uint32_t x0, std::uint32_t y0,
                          std::uint32_t x1, std::uint32_t y1,
                          BlendMode mode) override;
    void composite_placed(Buffer& bottom, const Buffer& src,
                          std::uint32_t sw, std::uint32_t sh,
                          double ox, double oy, double sx, double sy,
                          std::uint32_t w, std::uint32_t x0,
                          std::uint32_t y0, std::uint32_t x1,
                          std::uint32_t y1, float alpha_fold,
                          BlendMode mode) override;
    void gaussian_blur(const Buffer& src, Buffer& dst, std::uint32_t w,
                       std::uint32_t h, float sigma) override;
    void sharpen(Buffer& src, Buffer& dst, std::uint32_t w, std::uint32_t h,
                 float amount, float radius, float threshold) override;
    void brightness_contrast(Buffer& src, Buffer& dst, std::uint32_t w,
                             std::uint32_t h, float brightness,
                             float contrast) override;
    void hue_saturation(Buffer& src, Buffer& dst, std::uint32_t w,
                        std::uint32_t h, float hue_shift, float saturation,
                        float lightness) override;
    void median_filter(Buffer& src, Buffer& dst, std::uint32_t w,
                       std::uint32_t h) override;
    void paint_dab(Buffer& dst, std::uint32_t w, std::uint32_t h, float cx,
                   float cy, float radius, float hardness, float opacity,
                   const RGBAf& color) override;
    void bg_erase(Buffer& dst, std::uint32_t w, std::uint32_t h, float cx,
                  float cy, float radius, float hardness, float opacity,
                  const RGBAf& sample, float tolerance, bool protectFg,
                  const RGBAf& fg, int* bboxOut) override;
    void pattern_stamp(Buffer& dst, std::uint32_t w, std::uint32_t h,
                       float cx, float cy, float radius, float hardness,
                       float opacity, const Buffer& tile, float ox, float oy,
                       int* bboxOut) override;
    void history_dab(Buffer& dst, std::uint32_t w, std::uint32_t h, float cx,
                     float cy, float radius, float hardness, float opacity,
                     const Buffer& src, int* bboxOut) override;
    void warp(Buffer& dst, const Buffer& src, std::uint32_t w, std::uint32_t h,
              const WarpSubgrid& grid, std::uint32_t x0, std::uint32_t y0,
              std::uint32_t x1, std::uint32_t y1) override;

  private:
    std::string name_;
    Device device_;
};

}  // namespace pittore::compute