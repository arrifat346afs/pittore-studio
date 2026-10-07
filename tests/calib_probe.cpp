// calib_probe.cpp — temporary (NOT a suite test). Dumps the shipped
// auto_subject intermediates for the current image/GT pair so an external
// script can analyze the residual vs the ground truth:
//   /tmp/calib_raw.png       (auto_subject alpha, pre-refine)
//   /tmp/calib_refined.png   (after guided refine + level)
//   /tmp/calib_gt.png        (binary ground truth, ours-aligned)
//
// Usage: calib_probe <image.png> <gt.png>
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

static void saveAlpha(const char* path, const std::vector<float>& a, int w, int h) {
    QImage m(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* row = m.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = static_cast<uchar>(std::clamp(
                int(a[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
    }
    m.save(path);
}

int main(int argc, char** argv) {
    const char* imgPath = argc > 1 ? argv[1] : "testimages/Test.png";
    const char* gtPath = argc > 2 ? argv[2] : "testimages/Proper-Mask.png";
    QImage img(imgPath);
    if (img.isNull()) { std::fprintf(stderr, "cannot load %s\n", imgPath); return 2; }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width(), h = img.height();
    std::printf("== image %dx%d\n", w, h);

    QImage gtImg(gtPath);
    if (gtImg.isNull()) { std::fprintf(stderr, "cannot load %s\n", gtPath); return 2; }
    gtImg = gtImg.convertToFormat(QImage::Format_Grayscale8)
                .scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    std::vector<uchar> gt(std::size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const uchar* row = gtImg.constScanLine(y);
        for (int x = 0; x < w; ++x) gt[std::size_t(y) * w + x] = row[x] > 127 ? 255 : 0;
    }

    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    const char* models = std::getenv("PITTORE_MODELS_DIR");
    const std::string dir = (models && *models) ? std::string(models)
                                                : home + "/.local/share/PittoreStudio/models";
    const std::string encPath = dir + "/SegmentationEncoder_2.6.onnx";
    const std::string decPath = dir + "/SegmentationDecoder_2.6.onnx";

    const SamEncodings enc = encode_rgba8(img.constBits(), w, h, encPath, 1024);
    if (!enc.ok) { std::fprintf(stderr, "encode: %s\n", enc.error.c_str()); return 3; }

    std::vector<std::uint8_t> rgba(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uchar* s = img.constScanLine(y);
        std::memcpy(rgba.data() + std::size_t(y) * w * 4, s, std::size_t(w) * 4);
    }

    SamDecodeResult a = auto_subject(enc, decPath);
    if (!a.ok) { std::fprintf(stderr, "auto_subject: %s\n", a.error.c_str()); return 4; }

    saveAlpha("/tmp/calib_raw.png", a.alpha, w, h);

    std::vector<float> ref = a.alpha;
    guided_refine_alpha(rgba.data(), w, h, ref, 10, 1e-3f);
    saveAlpha("/tmp/calib_refined.png", ref, w, h);

    // Mask-prompt iteration: feed the refined mask back to the decoder
    // (has_mask=1) and re-segment — SAM can re-position contours this way.
    // Sweep anchor candidates to find a GENERIC rule (not image-fitted).
    {
        // Mask bbox of the >0.5 region (largest quadrants only, cheap):
        struct BB { int x0, y0, x1, y1; };
        auto bboxOf = [&](const std::vector<float>& alpha) {
            BB b = {w, h, -1, -1};
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x)
                    if (alpha[std::size_t(y) * w + x] > 0.5f) {
                        b.x0 = std::min(b.x0, x); b.y0 = std::min(b.y0, y);
                        b.x1 = std::max(b.x1, x); b.y1 = std::max(b.y1, y);
                    }
            return b;
        };
        BB bb = bboxOf(a.alpha);
        const int cx = (bb.x0 + bb.x1) / 2, bh = bb.y1 - bb.y0;
        struct Anchor { int x, y; const char* name; };
        const Anchor cands[] = {
            {std::clamp(int(0.48 * w), 0, w - 1), std::clamp(int(0.55 * h), 0, h - 1), "fixed(0.48,0.55)"},
            {cx, bb.y0 + bh / 4, "bbox top-quarter"},
            {cx, bb.y0 + bh / 3, "bbox top-third"},
            {cx, bb.y0 + bh / 2, "bbox half"},
            {(bb.x0 + bb.x1) / 2, std::clamp(bb.y0 + bh / 8, 0, h - 1), "bbox near-top"},
            {std::clamp(int(0.5 * w), 0, w - 1), std::clamp(int(0.38 * h), 0, h - 1), "fixed(0.5,0.38)"},
        };
        for (const Anchor& an : cands) {
            std::vector<float> refm = ref;
            double best = 0.0;
            for (int iter = 1; iter <= 2; ++iter) {
                SamDecodeResult r = decode_mask(enc, an.x, an.y, refm.data(), decPath);
                if (!r.ok) break;
                refm = r.alpha;
                guided_refine_alpha(rgba.data(), w, h, refm, 10, 1e-3f);
                std::size_t inter = 0, uni = 0;
                for (std::size_t i = 0; i < refm.size(); ++i) {
                    const bool A = refm[i] > 0.5f, B = gt[i] > 127;
                    if (A && B) ++inter;
                    if (A || B) ++uni;
                }
                best = uni ? double(inter) / double(uni) : 0.0;
            }
            std::printf("anchor %-20s (%d,%d): IoU = %.4f\n", an.name, an.x, an.y,
                        best);
        }
    }

    // Combined re-segmentation: 25-point multi prompt + the refined mask
    // (has_mask=1) in one decode.
    {
        std::vector<std::pair<int, int>> pts;
        const double gx[] = {0.20, 0.35, 0.50, 0.65, 0.80};
        const double gy[] = {0.25, 0.40, 0.55, 0.70, 0.85};
        for (double fx : gx)
            for (double fy : gy)
                pts.push_back({std::clamp(int(fx * w), 0, w - 1),
                               std::clamp(int(fy * h), 0, h - 1)});
        SamDecodeResult r = decode_mask_points(enc, pts, ref.data(), decPath);
        if (!r.ok) {
            std::fprintf(stderr, "decode_mask_points: %s\n", r.error.c_str());
        } else {
            guided_refine_alpha(rgba.data(), w, h, r.alpha, 10, 1e-3f);
            std::size_t inter = 0, uni = 0;
            for (std::size_t i = 0; i < r.alpha.size(); ++i) {
                const bool A = r.alpha[i] > 0.5f, B = gt[i] > 127;
                if (A && B) ++inter;
                if (A || B) ++uni;
            }
            std::printf("combo mask+25pt: IoU vs GT = %.4f\n",
                        uni ? double(inter) / double(uni) : 0.0);
            saveAlpha("/tmp/calib_combo.png", r.alpha, w, h);
        }
    }

    saveAlpha("/tmp/calib_gt.png",
              std::vector<float>(gt.begin(), gt.end()), w, h);

    std::printf("== dumped calib_raw/refined/gt\n");
    return 0;
}