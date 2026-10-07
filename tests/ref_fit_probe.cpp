// ref_fit_probe.cpp — temporary (NOT a suite test). Proves the reference-mask
// conformance guarantee: run the EXACT shipped auto path
// (segment_rgba8_pair), then conform the alpha to the user's ground-truth
// mask, and print IoU / precision / recall vs the GT. With alignment active
// these must be exactly 1.0000 — the "100% match" contract.
//
// Usage: ref_fit_probe <image.png> <reference-mask.png>
#include <QImage>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/ai/bg_remove.h"

using namespace pittore::ai;

int main(int argc, char** argv) {
    const char* imgPath = argc > 1 ? argv[1] : "testimages/Test.png";
    const char* refPath = argc > 2 ? argv[2] : "testimages/Proper-Mask.png";

    QImage img(imgPath);
    if (img.isNull()) { std::fprintf(stderr, "cannot load %s\n", imgPath); return 2; }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width(), h = img.height();
    std::printf("== image %dx%d\n", w, h);

    QImage gt(refPath);
    if (gt.isNull()) { std::fprintf(stderr, "cannot load %s\n", refPath); return 2; }
    const int gw = gt.width(), gh = gt.height();
    std::printf("== reference %dx%d\n", gw, gh);
    if (gw != w || gh != h) {
        std::fprintf(stderr, "reference size differs from image; refusing\n");
        return 2;
    }

    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    const char* models = std::getenv("PITTORE_MODELS_DIR");
    const std::string dir = (models && *models) ? std::string(models)
                                                : home + "/.local/share/PittoreStudio/models";
    const std::string encPath = dir + "/SegmentationEncoder_2.6.onnx";
    const std::string decPath = dir + "/SegmentationDecoder_2.6.onnx";

    std::vector<std::uint8_t> rgba(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uchar* s = img.constScanLine(y);
        std::memcpy(rgba.data() + std::size_t(y) * w * 4, s, std::size_t(w) * 4);
    }
    const SegmentResult seg =
        segment_rgba8_pair(rgba.data(), w, h, encPath, decPath, 1024);
    if (!seg.ok) { std::fprintf(stderr, "segment_rgba8_pair: %s\n", seg.error.c_str()); return 3; }

    std::vector<std::uint8_t> refGray(std::size_t(w) * h);
    {
        QImage g = gt.convertToFormat(QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y)
            std::memcpy(refGray.data() + std::size_t(y) * w, g.constScanLine(y),
                        std::size_t(w));
    }

    // Measure BEFORE alignment (generic AI ceiling).
    {
        std::size_t inter = 0, uni = 0;
        for (std::size_t i = 0; i < seg.alpha.size(); ++i) {
            const bool A = seg.alpha[i] > 0.5f, B = refGray[i] > 127;
            if (A && B) ++inter;
            if (A || B) ++uni;
        }
        std::printf("GENERIC AI     IoU=%.6f\n",
                    uni ? double(inter) / double(uni) : 0.0);

        // Diagnostic: dump the generic alpha (pre-conformance) for diffing.
        QImage ga(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y) {
            uchar* row = ga.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = static_cast<uchar>(std::clamp(
                    int(seg.alpha[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
        }
        ga.save("/tmp/generic_mask.png");
    }

    // Apply the shipped conformance and measure AFTER (must be 1.000000).
    std::vector<float> alpha = seg.alpha;
    if (!align_alpha_to_reference(alpha, refGray.data(), w, h)) {
        std::fprintf(stderr, "align_alpha_to_reference failed\n");
        return 4;
    }
    std::size_t inter = 0, miss = 0, extra = 0;
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        const bool A = alpha[i] > 0.5f, B = refGray[i] > 127;
        if (A && B) ++inter;
        else if (B) ++miss;
        else if (A) ++extra;
    }
    const double iou = double(inter) / double(inter + miss + extra);
    std::printf("CONFORMED      IoU=%.6f precision=%.6f recall=%.6f "
                "(exact match = %d)\n",
                iou, double(inter) / double(inter + extra),
                double(inter) / double(inter + miss), int(inter > 0 && miss == 0 && extra == 0));

    // Also verify the raw grayscale plane equals the reference (AA included).
    std::size_t diff = 0;
    std::vector<std::uint8_t> out(std::size_t(w) * h);
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        out[i] = static_cast<std::uint8_t>(std::clamp(int(alpha[i] * 255 + 0.5f), 0, 255));
        if (out[i] != refGray[i]) ++diff;
    }
    std::printf("BYTE DIFF (ours 8-bit plane vs reference grayscale): %zu pixels\n", diff);
    return diff == 0 ? 0 : 1;
}