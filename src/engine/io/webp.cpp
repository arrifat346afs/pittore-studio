#include "engine/io/webp.h"

#include <webp/decode.h>
#include <webp/encode.h>

#include <cmath>
#include <cstring>

namespace pittore::io {

bool webpDecodeRgba16(const std::vector<std::uint8_t>& data,
                      std::uint32_t& width, std::uint32_t& height,
                      std::vector<std::uint16_t>& rgba) {
    if (data.empty()) return false;
    WebPDecoderConfig config;
    if (!WebPInitDecoderConfig(&config)) return false;
    config.output.colorspace = MODE_RGBA;
    config.options.use_threads = 1;
    config.options.flip = 0;
    const VP8StatusCode rc = WebPDecode(data.data(), data.size(), &config);
    if (rc != VP8_STATUS_OK) {
        WebPFreeDecBuffer(&config.output);
        return false;
    }
    width = config.output.width;
    height = config.output.height;
    const std::uint64_t n = static_cast<std::uint64_t>(width) * height;
    if (width == 0 || height == 0 || n > (1ull << 30)) {
        WebPFreeDecBuffer(&config.output);
        return false;
    }
    const std::uint8_t* src = config.output.u.RGBA.rgba;  // w*h*4 bytes
    rgba.assign(static_cast<std::size_t>(n) * 4, 0);
    for (std::uint64_t i = 0; i < n; ++i) {
        for (int c = 0; c < 4; ++c)
            rgba[i * 4 + c] = static_cast<std::uint16_t>(src[i * 4 + c]) * 257u;
    }
    WebPFreeDecBuffer(&config.output);
    return true;
}

bool webpEncodeRgba16(std::uint32_t width, std::uint32_t height, bool lossless,
                      float quality, const std::uint16_t* rgba,
                      std::vector<std::uint8_t>& out) {
    const std::uint64_t n = static_cast<std::uint64_t>(width) * height;
    if (width == 0 || height == 0 || n > (1ull << 30) || !rgba) return false;
    std::vector<std::uint8_t> src8(static_cast<std::size_t>(n) * 4);
    for (std::uint64_t i = 0; i < n; ++i)
        for (int c = 0; c < 4; ++c)
            src8[i * 4 + c] = static_cast<std::uint8_t>(rgba[i * 4 + c] >> 8);

    std::uint8_t* encoded = nullptr;
    std::size_t size = 0;
    if (lossless) {
        size = WebPEncodeLosslessRGBA(src8.data(), static_cast<int>(width),
                                      static_cast<int>(height),
                                      static_cast<int>(width) * 4, &encoded);
    } else {
        const float q = std::isnan(quality) ? 90.0f
                        : (quality < 0.0f ? 0.0f
                           : (quality > 100.0f ? 100.0f : quality));
        size = WebPEncodeRGBA(src8.data(), static_cast<int>(width),
                              static_cast<int>(height),
                              static_cast<int>(width) * 4, q, &encoded);
    }
    if (!encoded) return false;
    out.assign(encoded, encoded + size);
    WebPFree(encoded);
    return true;
}

}  // namespace pittore::io