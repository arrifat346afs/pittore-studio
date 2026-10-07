#pragma once
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/compute/backend.h"
#include "engine/core/image.h"

namespace pittore {

// One layer. Pixels are RGBAf, straight alpha, doc-sized.
// Per-layer look tweaks run during compose(); layer 0 is the bottom.
struct Layer {
    explicit Layer(std::uint32_t w, std::uint32_t h, std::string name = "Layer")
        : name(std::move(name)), pixels(w, h) {}

    std::string name;
    bool visible = true;
    float opacity = 1.0f;
    compute::BlendMode mode = compute::BlendMode::Normal;

    bool grayscale_enabled = false;
    float brightness = 0.0f;
    float contrast = 0.0f;
    float hue_shift = 0.0f;
    float saturation = 0.0f;
    float lightness = 0.0f;
    bool median_enabled = false;
    bool sharpen_enabled = false;
    float sharpen_amount = 1.0f;
    float sharpen_radius = 2.0f;
    float sharpen_threshold = 0.0f;

    Image pixels;
};

// Layer stack, composed through a backend. Tweak a layer, call
// compose(), use the result. Storage is host Images; the backend
// is only borrowed during compose().
class Document {
  public:
    Document(std::uint32_t width, std::uint32_t height)
        : w_(width), h_(height) {
        if (width == 0 || height == 0)
            throw std::invalid_argument("Document dimensions cannot be zero");
    }

    std::uint32_t width() const { return w_; }
    std::uint32_t height() const { return h_; }

    // New layer. Adding one may move the vector and invalidate old refs.
    Layer& add_layer(std::string name = "Layer") {
        layers_.emplace_back(w_, h_, std::move(name));
        return layers_.back();
    }

    const std::vector<Layer>& layers() const { return layers_; }
    std::vector<Layer>& layers() { return layers_; }

    // Blend all visible layers bottom-to-top into `out` (doc-sized).
    // Each layer runs gray, brightness/contrast, hue/sat, median,
    // sharpen first, then blends with its mode + opacity.
    void compose(compute::ComputeBackend& be, Image& out) const;

  private:
    std::uint32_t w_;
    std::uint32_t h_;
    std::vector<Layer> layers_;
};

inline void Document::compose(compute::ComputeBackend& be, Image& out) const {
    if (out.width() != w_ || out.height() != h_)
        throw std::invalid_argument("compose output size must match document");
    out.fill(RGBAf{0.0f, 0.0f, 0.0f, 0.0f});

    const std::size_t bytes = static_cast<std::size_t>(w_) * h_ * sizeof(RGBAf);
    auto acc = be.make_buffer(bytes);
    auto work = be.make_buffer(bytes);
    auto tmp = be.make_buffer(bytes);

    std::memcpy(acc->host(), out.data(), bytes);
    acc->upload();

    for (const Layer& layer : layers_) {
        if (!layer.visible) continue;

        // Load this layer's pixels into scratch.
        std::unique_ptr<compute::Buffer>& cur = work;
        std::memcpy(cur->host(), layer.pixels.data(), bytes);
        cur->upload();

        // Tweaks ping-pong between cur and tmp.
        if (layer.grayscale_enabled) {
            be.grayscale(*cur, *tmp, w_, h_);
            std::swap(cur, tmp);
        }
        if (layer.brightness != 0.0f || layer.contrast != 0.0f) {
            be.brightness_contrast(*cur, *tmp, w_, h_, layer.brightness, layer.contrast);
            std::swap(cur, tmp);
        }
        if (layer.hue_shift != 0.0f || layer.saturation != 0.0f ||
            layer.lightness != 0.0f) {
            be.hue_saturation(*cur, *tmp, w_, h_, layer.hue_shift, layer.saturation,
                              layer.lightness);
            std::swap(cur, tmp);
        }
        if (layer.median_enabled) {
            be.median_filter(*cur, *tmp, w_, h_);
            std::swap(cur, tmp);
        }
        if (layer.sharpen_enabled) {
            be.sharpen(*cur, *tmp, w_, h_, layer.sharpen_amount,
                       layer.sharpen_radius, layer.sharpen_threshold);
            std::swap(cur, tmp);
        }

        // Opacity folds into alpha (straight-alpha compositing).
        if (layer.opacity < 1.0f) {
            cur->download();
            auto* px = static_cast<RGBAf*>(cur->host());
            for (std::size_t i = 0; i < static_cast<std::size_t>(w_) * h_; ++i)
                px[i].a *= layer.opacity;
            cur->upload();
        }

        be.composite(*acc, *cur, w_, h_, layer.mode);
    }

    acc->download();
    std::memcpy(out.data(), acc->host(), bytes);
}

}  // namespace pittore