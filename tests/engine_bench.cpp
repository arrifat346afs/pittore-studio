// engine_bench.cpp — temporary (NOT a suite test). Multi-image regression
// harness for the generic auto-subject engine: for each "image:groundtruth"
// pair it runs the shipped generic pipeline (segment_rgba8_pair), scores the
// engine alpha against the GT, applies the app's cleanup chain and scores
// again, and finally proves the reference-conformance guarantee is exact.
//
// Usage: engine_bench image.png:mask.png [image2.png:mask2.png ...]
#include <QImage>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/ai/bg_remove.h"
#include "ui/selection_mask.h"

using namespace pittore::ai;
using namespace pittore::ui;

struct Scores {
    double iou = 0, prec = 0, rec = 0;
    std::size_t miss = 0, extra = 0, ops = 0;
};

static Scores score(const std::vector<float>& alpha, const std::vector<uchar>& gt,
                    int /*w*/, int /*h*/) {
    Scores s;
    std::size_t inter = 0;
    for (std::size_t i = 0; i < alpha.size(); ++i) {
        const bool A = alpha[i] > 0.5f, B = gt[i] > 127;
        if (A && B) ++inter;
        else if (B) ++s.miss;
        else if (A) ++s.extra;
    }
    const std::size_t uni = inter + s.miss + s.extra;
    s.iou = uni ? double(inter) / double(uni) : 0.0;
    s.prec = double(inter) / double(inter + s.extra);
    s.rec = double(inter) / double(inter + s.miss);
    s.ops = inter + s.miss + s.extra;
    return s;
}

static std::vector<uchar> loadGray(const std::string& path, int w, int h) {
    QImage im(QString::fromStdString(path));
    im = im.convertToFormat(QImage::Format_Grayscale8)
             .scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation);
    std::vector<uchar> g(std::size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const uchar* row = im.constScanLine(y);
        std::memcpy(g.data() + std::size_t(y) * w, row, std::size_t(w));
    }
    return g;
}

int main(int argc, char** argv) {
    // Cleanup chain options (defaults mirror the shipped UI calls:
    // largest component -> fill holes r=2 -> smooth r=1).
    int smoothR = 1;
    int bridgeR = 2;  // 0 = skip fill holes; >0 = fixed radius
    double bridgeF = 0.0;  // nonzero = scale-proportional radius (research)
    int pairStart = 1;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "--smooth") == 0 && a + 1 < argc)
            smoothR = std::atoi(argv[++a]);
        else if (std::strcmp(argv[a], "--bridge") == 0 && a + 1 < argc)
            bridgeR = std::atoi(argv[++a]);
        else if (std::strcmp(argv[a], "--bridgef") == 0 && a + 1 < argc)
            bridgeF = std::atof(argv[++a]);
        else {
            pairStart = a;
            break;
        }
    }
    if (pairStart >= argc) {
        std::fprintf(stderr,
                     "usage: engine_bench [--smooth N] [--bridge R] [--bridgef F] "
                     "image.png:mask.png [...]\n");
        return 2;
    }
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    const char* models = std::getenv("PITTORE_MODELS_DIR");
    const std::string dir = (models && *models) ? std::string(models)
                                                : home + "/.local/share/PittoreStudio/models";
    const std::string encPath = dir + "/SegmentationEncoder_2.6.onnx";
    const std::string decPath = dir + "/SegmentationDecoder_2.6.onnx";

    std::printf("%-28s %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n", "pair", "engine-IoU",
                "prec", "rec", "chain-IoU", "prec", "rec", "conform-IoU");
    std::printf("%-28s %-8s %-8s %-8s %-8s %-8s %-8s %-8s\n",
                std::string(28, '-').c_str(), "--------", "--------", "--------",
                "--------", "--------", "--------", "--------");
    double meanEng = 0, meanChain = 0;
    int n = 0;
    for (int a = pairStart; a < argc; ++a) {
        std::string spec = argv[a];
        const std::size_t colon = spec.find(':');
        if (colon == std::string::npos) {
            std::fprintf(stderr, "skip %s (no ':' separator)\n", spec.c_str());
            continue;
        }
        std::string imgPath = spec.substr(0, colon);
        std::string gtPath = spec.substr(colon + 1);
        std::string name = imgPath;
        {
            const std::size_t slash = name.find_last_of('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
        }

        QImage img(QString::fromStdString(imgPath));
        if (img.isNull()) { std::fprintf(stderr, "cannot load %s\n", imgPath.c_str()); continue; }
        img = img.convertToFormat(QImage::Format_RGBA8888);
        const int w = img.width(), h = img.height();
        const std::vector<uchar> gt = loadGray(gtPath, w, h);

        std::vector<std::uint8_t> rgba(std::size_t(w) * h * 4);
        for (int y = 0; y < h; ++y) {
            const uchar* s = img.constScanLine(y);
            std::memcpy(rgba.data() + std::size_t(y) * w * 4, s, std::size_t(w) * 4);
        }
        const SegmentResult seg = segment_rgba8_pair(rgba.data(), w, h, encPath, decPath, 1024);
        if (!seg.ok) {
            std::fprintf(stderr, "segment failed on %s: %s\n", imgPath.c_str(),
                         seg.error.c_str());
            continue;
        }
        const Scores sEng = score(seg.alpha, gt, w, h);

        // App cleanup chain (Select Subject / Remove Background).
        QImage sm(w, h, QImage::Format_Grayscale8);
        sm.fill(0);
        for (int y = 0; y < h; ++y) {
            uchar* row = sm.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = static_cast<uchar>(std::clamp(
                    int(seg.alpha[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
        }
        sm = selectionMaskLargestComponent(sm);
        if (bridgeR != 0) {
            const QRect bb = selectionMaskBbox(sm);
            const int r = bridgeR > 0
                             ? bridgeR
                             : std::clamp(int(std::lround(
                                    std::min(bb.width(), bb.height()) * bridgeF)),
                                          2, 12);
            sm = selectionMaskFillHoles(sm, r);
        }
        sm = selectionMaskSmooth(sm, smoothR);
        std::vector<float> chain(seg.alpha.size());
        for (int y = 0; y < h; ++y) {
            const uchar* row = sm.constScanLine(y);
            for (int x = 0; x < w; ++x) chain[std::size_t(y) * w + x] = row[x] / 255.0f;
        }
        const Scores sChain = score(chain, gt, w, h);

        // Reference conformance must be exactly 1.0.
        std::vector<float> conf = seg.alpha;
        align_alpha_to_reference(conf, gt.data(), w, h);
        // gt here is the thresholded (0/255) GT; byte-exactness is masked but
        // IoU must be 1.0.
        Scores sConf;
        std::size_t inter = 0, uni = 0;
        for (std::size_t i = 0; i < conf.size(); ++i) {
            const bool A = conf[i] > 0.5f, B = gt[i] > 127;
            if (A && B) ++inter;
            if (A || B) ++uni;
        }
        sConf.iou = uni ? double(inter) / double(uni) : 0.0;
        // Save the engine mask for eyeballing / maskdiff.
        {
            QImage out(w, h, QImage::Format_Grayscale8);
            for (int y = 0; y < h; ++y) {
                uchar* row = out.scanLine(y);
                for (int x = 0; x < w; ++x)
                    row[x] = static_cast<uchar>(std::clamp(
                        int(seg.alpha[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
            }
            const std::string save = "/tmp/bench_" + name + ".png";
            out.save(QString::fromStdString(save));
        }
        meanEng += sEng.iou;
        meanChain += sChain.iou;
        ++n;
        const auto fmt = [](Scores s) {
            char b[3][16];
            std::snprintf(b[0], 16, "%.4f", s.iou);
            std::snprintf(b[1], 16, "%.4f", s.prec);
            std::snprintf(b[2], 16, "%.4f", s.rec);
            return std::string(b[0]) + " " + b[1] + " " + b[2];
        };
        std::printf("%-28s %-23s %-23s %.4f\n", name.c_str(),
                    fmt(sEng).c_str(), fmt(sChain).c_str(), sConf.iou);
    }
    if (n) {
        std::printf("\nmean engine IoU=%.4f   mean chain IoU=%.4f  (%d pairs)\n",
                    meanEng / n, meanChain / n, n);
    }
    return 0;
}