// ablate_probe.cpp — temporary (NOT a suite test). Pipeline-stage ablation for
// the generic auto-subject engine: runs variants of segment_rgba8_pair built
// from the exported pieces (encode, auto_subject, guided refine, mask-prompt
// re-segmentation) and scores each against a ground truth. Purpose: find which
// stage cuts low-contrast boundary strips (e.g. an arm/hand along the subject's
// left edge) without regressing the other pair.
//
// Usage: ablate_probe image.png mask.png [bandLo bandHi radius passes anchor]
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

struct Scores {
    double iou = 0, prec = 0, rec = 0;
    std::size_t miss = 0, extra = 0;
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

static void saveMask(const std::string& path, const std::vector<float>& alpha,
                     int w, int h) {
    QImage out(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y) {
        uchar* row = out.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = static_cast<uchar>(std::clamp(
                int(alpha[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
    }
    out.save(QString::fromStdString(path));
}

// Replicates the shipped mask-prompt re-segmentation loop from
// segment_rgba8_pair, parameterized for ablation: `maxCov` is the degenerate
// re-decode coverage cap (shipped 0.70). When mergeThresh > 0 the
// re-seg result replaces the input only where the input was *not confident*
// (alpha < mergeThresh); confident input pixels are kept, so a thin hand/hem
// strip the decoder re-opinions on survives.
static void reseg(SamDecodeResult& dec, const SamEncodings& enc, const std::uint8_t* rgba,
                  int w, int h, const std::string& decPath, int radius, float lo,
                  float hi, int passes, bool topThird, float mergeThresh,
                  double maxCov, int secondAnchor = 0) {
    const std::size_t n = std::size_t(w) * std::size_t(h);
    if (dec.alpha.size() != n || n == 0) return;
    auto bbox = [&]() {
        std::int64_t bx0 = w, by0 = h, bx1 = -1, by1 = -1;
        for (std::size_t i = 0; i < n; ++i)
            if (dec.alpha[i] > 0.5f) {
                const std::int64_t x = i % std::size_t(w);
                const std::int64_t y = i / std::size_t(w);
                bx0 = std::min(bx0, x); by0 = std::min(by0, y);
                bx1 = std::max(bx1, x); by1 = std::max(by1, y);
            }
        return std::array<std::int64_t, 4>{bx0, by0, bx1, by1};
    };
    auto runOne = [&](int cxp, int cyp, const float* seed) {
        SamDecodeResult r = decode_mask(enc, cxp, cyp, seed, decPath);
        if (!r.ok || r.alpha.size() != n) return false;
        guided_refine_alpha(rgba, w, h, r.alpha, radius, 1e-3f, lo, hi);
        std::size_t cov = 0;
        for (std::size_t i = 0; i < n; ++i)
            if (r.alpha[i] > 0.5f) ++cov;
        const double c = double(cov) / double(n);
        if (c < 0.005 || c > maxCov) return false;
        if (mergeThresh <= 0.0f) {
            dec.alpha = std::move(r.alpha);
        } else {
            for (std::size_t i = 0; i < n; ++i)
                if (dec.alpha[i] < mergeThresh) dec.alpha[i] = r.alpha[i];
        }
        return true;
    };
    {
        const auto b = bbox();
        const int cxp = b[2] > b[0] ? int((b[0] + b[2]) / 2) : w / 2;
        const int cyp = b[3] > b[1] ? int((b[1] + (topThird ? (b[3] - b[1]) / 3 : (b[3] - b[1]) / 2)))
                                    : h / 2;
        const float* seed = dec.alpha.data();
        for (int it = 0; it < passes; ++it) {
            if (!runOne(cxp, cyp, seed)) break;
            seed = dec.alpha.data();
        }
    }
    // Optional extra pass from a secondary anchor, recomputed from the current
    // mask: 1 = right-edge mid-height (pulls a cut right flank), 2 = top edge
    // (pulls an under-scooped crown), 3 = right then top. Same merge rule and
    // cap as the main loop.
    const int extraAnchors = (secondAnchor == 3) ? 2 : ((secondAnchor == 1 || secondAnchor == 2) ? 1 : 0);
    for (int e = 0; e < extraAnchors; ++e) {
        const int which = (secondAnchor == 3) ? (e == 0 ? 1 : 2) : secondAnchor;
        const auto b = bbox();
        int cxp = w / 2, cyp = h / 2;
        if (b[2] > b[0] && b[3] > b[1]) {
            if (which == 1) {
                cxp = int(b[2] - (b[2] - b[0]) / 10);
                cyp = int((b[1] + b[3]) / 2);
            } else {
                cxp = int((b[0] + b[2]) / 2);
                cyp = int(b[1] + (b[3] - b[1]) / 10);
            }
        }
        runOne(cxp, cyp, dec.alpha.data());
    }
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: ablate_probe image.png mask.png\n");
        return 2;
    }
    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    const char* models = std::getenv("PITTORE_MODELS_DIR");
    const std::string dir = (models && *models) ? std::string(models)
                                                : home + "/.local/share/PittoreStudio/models";
    const std::string encPath = dir + "/SegmentationEncoder_2.6.onnx";
    const std::string decPath = dir + "/SegmentationDecoder_2.6.onnx";

    QImage img(QString::fromStdString(argv[1]));
    if (img.isNull()) { std::fprintf(stderr, "cannot load %s\n", argv[1]); return 2; }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width(), h = img.height();
    const std::vector<uchar> gt = loadGray(argv[2], w, h);
    std::vector<std::uint8_t> rgba(std::size_t(w) * h * 4);
    for (int y = 0; y < h; ++y) {
        const uchar* s = img.constScanLine(y);
        std::memcpy(rgba.data() + std::size_t(y) * w * 4, s, std::size_t(w) * 4);
    }

    SamEncodings enc = encode_rgba8(rgba.data(), w, h, encPath, 1024);
    if (!enc.ok) { std::fprintf(stderr, "encode: %s\n", enc.error.c_str()); return 2; }

    struct Variant {
        const char* name;
        int radius = 10;
        float lo = 0.35f, hi = 0.65f;
        int passes = 2;         // re-seg passes
        bool topThird = true;   // anchor
        bool doRefine = true;
        float mergeThresh = -1.0f;  // <=0 = replace (shipped); >0 = keep confident
        double multiMax = 0.65;     // auto-subject multi-point accept cap
        double perPointMax = 0.60;  // auto-subject per-point candidate cap
        double resegMax = 0.70;     // mask-prompt re-seg coverage cap
        int secondAnchor = 0;     // 0 none; 1 right-edge extra pass; 2 top-edge extra pass
    };
    const Variant variants[] = {
        {"V0 shipped  r10 band.35-.65 2pass top3rd", 10, 0.35f, 0.65f, 2, true, true, -1},
        {"V1 no reseg r10 band.35-.65",              10, 0.35f, 0.65f, 0, true, true, -1},
        {"V2 1pass    r10 band.35-.65 top3rd",       10, 0.35f, 0.65f, 1, true, true, -1},
        {"V4 2pass    r4  band.35-.65 top3rd",        4, 0.35f, 0.65f, 2, true, true, -1},
        {"V5 2pass    r10 band.20-.80 top3rd",       10, 0.20f, 0.80f, 2, true, true, -1},
        {"V7 2pass    r6  band.25-.75 top3rd",        6, 0.25f, 0.75f, 2, true, true, -1},
        {"V12 merge>=.7  r10 band.35-.65 top3rd",    10, 0.35f, 0.65f, 2, true, true, 0.70f},
        {"V13 merge>=.5  r10 band.35-.65 top3rd",    10, 0.35f, 0.65f, 2, true, true, 0.50f},
        {"V14 merge>=.8  r10 band.35-.65 top3rd",    10, 0.35f, 0.65f, 2, true, true, 0.80f},
        {"V15 merge>=.6  r10 band.35-.65 top3rd",    10, 0.35f, 0.65f, 2, true, true, 0.60f},
        // Gate sweep for large-subject frames (test-2 fills ~75% of the canvas;
        // the shipped 0.65/0.60/0.70 caps reject the whole decode).
        {"G20 gates .85/.75/.85",                    10, 0.35f, 0.65f, 2, true, true, -1,
                                                     0.85, 0.75, 0.85},
        {"G21 gates .90/.80/.90",                    10, 0.35f, 0.65f, 2, true, true, -1,
                                                     0.90, 0.80, 0.90},
        {"G22 gates .80/.70/.80",                    10, 0.35f, 0.65f, 2, true, true, -1,
                                                     0.80, 0.70, 0.80},
        {"G23 gates .95/.85/.95",                    10, 0.35f, 0.65f, 2, true, true, -1,
                                                     0.95, 0.85, 0.95},
        // Isolate the corner: did the re-seg remove it, or was it never in the
        // multi-point decode? And can more passes / a different anchor grab it?
        {"G24 gates .85/.75/.85 no-reseg",           10, 0.35f, 0.65f, 0, true, true, -1,
                                                     0.85, 0.75, 0.85},
        {"G25 gates .85/.75/.85 3pass",              10, 0.35f, 0.65f, 3, true, true, -1,
                                                     0.85, 0.75, 0.85},
        {"G26 gates .85/.75/.85 centroid",           10, 0.35f, 0.65f, 2, false, true, -1,
                                                     0.85, 0.75, 0.85},
        {"G27 gates .85/.75/.85 merge>=.5",          10, 0.35f, 0.65f, 2, true, true, 0.50f,
                                                     0.85, 0.75, 0.85},
        // Conditional-merge candidate: large subjects keep confident pixels.
        {"G28 gates .85/.75/.85 merge>=.7",          10, 0.35f, 0.65f, 2, true, true, 0.70f,
                                                     0.85, 0.75, 0.85},
        {"G29 gates .85/.75/.85 merge>=.8",          10, 0.35f, 0.65f, 2, true, true, 0.80f,
                                                     0.85, 0.75, 0.85},
        {"G30 gates .85/.75/.85 merge>=.6",          10, 0.35f, 0.65f, 2, true, true, 0.60f,
                                                     0.85, 0.75, 0.85},
        // Hair-targeted: extra re-seg pass from a secondary anchor to pull the
        // cut right flank (H31) / under-scooped crown (H32); H33/H34 test a
        // plain 3rd same-anchor pass under each merge rule.
        {"H31 merge>=.5 +right anchor",               10, 0.35f, 0.65f, 2, true, true, 0.50f,
                                                     0.85, 0.75, 0.85, 1},
        {"H32 merge>=.5 +top anchor",                 10, 0.35f, 0.65f, 2, true, true, 0.50f,
                                                     0.85, 0.75, 0.85, 2},
        {"H33 merge>=.7 3pass",                       10, 0.35f, 0.65f, 3, true, true, 0.70f,
                                                     0.85, 0.75, 0.85},
        {"H34 merge>=.5 3pass",                       10, 0.35f, 0.65f, 3, true, true, 0.50f,
                                                     0.85, 0.75, 0.85},
        {"H35 merge>=.5 +right+top",                  10, 0.35f, 0.65f, 2, true, true, 0.50f,
                                                     0.85, 0.75, 0.85, 3},
        // White-on-white isolation: raw multi (no refine, no reseg) vs small-
        // radius refine, to find where the background bleed enters.
        {"H36 raw multi only",                         10, 0.35f, 0.65f, 0, true, false, -1,
                                                     0.85, 0.75, 0.85},
        {"H37 r4 refine no-reseg",                      4, 0.35f, 0.65f, 0, true, true, -1,
                                                     0.85, 0.75, 0.85},
    };

    std::printf("%-40s %-8s %-8s %-8s %8s %8s\n", "variant", "IoU", "prec", "rec",
                "miss", "extra");
    for (const Variant& v : variants) {
        SamDecodeResult dec = auto_subject(enc, decPath, v.multiMax, v.perPointMax);
        if (!dec.ok) { std::fprintf(stderr, "auto_subject: %s\n", dec.error.c_str()); continue; }
        if (v.doRefine) guided_refine_alpha(rgba.data(), w, h, dec.alpha, v.radius, 1e-3f, v.lo, v.hi);
        if (v.passes > 0)
            reseg(dec, enc, rgba.data(), w, h, decPath, v.radius, v.lo, v.hi, v.passes,
                  v.topThird, v.mergeThresh, v.resegMax, v.secondAnchor);
        const Scores s = score(dec.alpha, gt, w, h);
        std::printf("%-40s %.4f  %.4f  %.4f  %8zu %8zu\n", v.name, s.iou, s.prec, s.rec,
                    s.miss, s.extra);
        if (std::strncmp(v.name, "V0 ", 3) == 0 || std::strncmp(v.name, "V1 ", 3) == 0 ||
            std::strncmp(v.name, "V12", 3) == 0 || std::strncmp(v.name, "G20", 3) == 0 ||
            std::strncmp(v.name, "G21", 3) == 0 || std::strncmp(v.name, "G24", 3) == 0 ||
            std::strncmp(v.name, "G27", 3) == 0 || std::strncmp(v.name, "H36", 3) == 0)
            saveMask("/tmp/abl_" + std::string(v.name, 0, 3) + ".png", dec.alpha, w, h);
    }
    return 0;
}