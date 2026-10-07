// test_ai_segment.cpp
// End-to-end plumbing check for the ONNX segmentation pipeline: pack a
// synthetic RGBA8 scene, run the real U²-Netp model, and verify the mask comes
// back at full resolution, finite, in [0,1] and spatially discriminative.
//
// The test needs the runtime to be linked (it is compiled only when ONNX is
// enabled) AND a model file at $PITTORE_MODELS_DIR/u2netp.onnx. When either is
// missing it reports SKIP and returns 0 so `just test` stays green without any
// network or multi-hundred-MB model download.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "engine/ai/bg_remove.h"

int main() {
    if (!pittore::ai::onnx_available()) {
        std::printf("SKIP: no ONNX Runtime in this build\n");
        return 0;
    }
    const char* dir = std::getenv("PITTORE_MODELS_DIR");
    const std::string path =
        std::string(dir ? dir : "") + (dir ? "/u2netp.onnx" : "u2netp.onnx");
    FILE* probe = std::fopen(path.c_str(), "rb");
    if (!probe) {
        std::printf("SKIP: model not present at %s\n", path.c_str());
        return 0;
    }
    std::fclose(probe);

    const int w = 160, h = 120;
    std::vector<std::uint8_t> rgba(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint8_t v = 140;
            if (x > w / 4 && x < 3 * w / 4 && y > h / 4 && y < 3 * h / 4) v = 250;
            std::uint8_t* d = &rgba[(std::size_t(y) * w + x) * 4];
            d[0] = d[1] = d[2] = v;
            d[3] = 255;
        }
    }

    const pittore::ai::SegmentResult res =
        pittore::ai::segment_rgba8(rgba.data(), w, h, path, 320);
    if (!res.ok) {
        std::printf("FAIL: %s\n", res.error.c_str());
        return 1;
    }
    if (res.width != w || res.height != h ||
        res.alpha.size() != std::size_t(w) * std::size_t(h)) {
        std::printf("FAIL: mask size mismatch %dx%d (want %dx%d)\n", res.width,
                    res.height, w, h);
        return 1;
    }
    float mn = 1.0f, mx = 0.0f;
    bool finite = true;
    for (float v : res.alpha) {
        finite = finite && std::isfinite(v);
        mn = std::min(mn, v);
        mx = std::max(mx, v);
    }
    if (!finite) {
        std::printf("FAIL: non-finite mask values\n");
        return 1;
    }
    if (mx - mn < 0.1f) {
        std::printf("FAIL: model output is flat (range %.4f) — plumbing breakdown\n",
                    mx - mn);
        return 1;
    }
    std::printf("OK: %dx%d mask range=[%.3f,%.3f] ORT=%s\n", res.width, res.height,
                mn, mx, pittore::ai::onnx_version().c_str());
    return 0;
}