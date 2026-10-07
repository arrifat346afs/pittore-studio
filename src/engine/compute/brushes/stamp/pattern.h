#pragma once
// PatternStamp tiles: procedural 64x64 pattern cells (clean-room,
// deterministic closed-form math — no bundled assets). One source of truth
// for the host core and the device kernels (tiles cross as buffers).
#include <cmath>
#include <cstdint>
#include <vector>

#include "engine/core/pixel.h"

namespace pittore::compute {

struct PatternTile {
    static constexpr int kSize = 64;
    int id = 0;  // 0 Bubbles, 1 Wrinkles, 2 Woven, 3 Herringbone
    std::vector<RGBAf> px;  // 64x64 opaque RGB, row-first

    bool valid() const {
        // Size-only: device/host staging round-trips carry pixels without
        // the pattern id (see ComputeBackend::pattern_stamp).
        return px.size() == std::size_t(kSize) * kSize;
    }
};

// Deterministic tile generator (same bytes every call, every backend).
inline PatternTile make_pattern_tile(int id) {
    PatternTile t;
    t.id = (id < 0 || id > 3) ? 0 : id;
    t.px.resize(std::size_t(PatternTile::kSize) * PatternTile::kSize);
    constexpr int S = PatternTile::kSize;
    for (int y = 0; y < S; ++y) {
        for (int x = 0; x < S; ++x) {
            float v = 0.5f, r = v, g = v, b = v;
            switch (t.id) {
                case 0: {  // Bubbles: 4x4 shaded cells on mid grey.
                    const float fx = float(x % 16) - 7.5f;
                    const float fy = float(y % 16) - 7.5f;
                    const float d =
                        std::sqrt(fx * fx + fy * fy) / 10.6f;
                    const float ball =
                        d >= 1.0f ? 0.0f : 0.5f + 0.5f * std::cos(d * 3.14159f);
                    v = 0.38f + 0.42f * ball;
                    r = v;
                    g = v;
                    b = v + 0.06f * ball;
                    break;
                }
                case 1: {  // Wrinkles: warped sine stripes.
                    const float w =
                        float(x) + 6.0f * std::sin(float(y) * 0.3f);
                    v = 0.5f + 0.3f * std::sin(w * 0.5f);
                    r = v;
                    g = v;
                    b = v;
                    break;
                }
                case 2: {  // Woven: alternating over/under 8px bands.
                    const int bx = x / 8, by = y / 8;
                    if ((bx + by) % 2 == 0) {
                        const float s = float(y % 8) / 7.0f;
                        v = 0.42f + 0.3f * (0.5f + 0.5f * std::cos(s * 6.28318f));
                    } else {
                        const float s = float(x % 8) / 7.0f;
                        v = 0.4f + 0.28f * (0.5f + 0.5f * std::cos(s * 6.28318f));
                    }
                    r = v + 0.03f;
                    g = v;
                    b = v - 0.03f;
                    break;
                }
                default: {  // Herringbone: alternating diagonal stripes.
                    const int row = y / 16;
                    const float s = (row % 2 == 0) ? float(x + y) : float(x - y);
                    v = 0.5f + 0.3f * std::sin(s * 0.4f);
                    r = v;
                    g = v + 0.02f;
                    b = v;
                    break;
                }
            }
            auto cl = [](float c) {
                return c < 0.0f ? 0.0f : (c > 1.0f ? 1.0f : c);
            };
            t.px[std::size_t(y) * S + x] = RGBAf{cl(r), cl(g), cl(b), 1.0f};
        }
    }
    return t;
}

}  // namespace pittore::compute
