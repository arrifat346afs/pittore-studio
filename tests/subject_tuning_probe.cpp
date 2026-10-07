// subject_tuning_probe.cpp — temporary tuning harness (NOT a suite test).
// Encodes testimages/Test.png once, then compares several auto-subject
// strategies against testimages/Proper-Mask.png (the user's ground truth) with
// IoU / precision / recall. Used to pick the shipping strategy; delete freely.
#include <QImage>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/ai/bg_remove.h"
#include "ui/selection_mask.h"

using namespace pittore::ai;
using namespace pittore::ui;

static QImage toImage(const std::vector<float>& a, int w, int h) {
    QImage m(w, h, QImage::Format_Grayscale8);
    m.fill(0);
    for (int y = 0; y < h; ++y) {
        uchar* row = m.scanLine(y);
        for (int x = 0; x < w; ++x)
            row[x] = static_cast<uchar>(std::clamp(
                int(a[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
    }
    return m;
}
static std::vector<float> fromImage(const QImage& m) {
    std::vector<float> a(std::size_t(m.width()) * m.height());
    for (int y = 0; y < m.height(); ++y) {
        const uchar* row = m.constScanLine(y);
        for (int x = 0; x < m.width(); ++x) a[std::size_t(y) * m.width() + x] = row[x] / 255.0f;
    }
    return a;
}

struct Metrics { double iou = 0, prec = 0, rec = 0, area = 0; };

static Metrics eval(const std::vector<float>& a, const std::vector<uchar>& gt, int w,
                    int h) {
    std::size_t inter = 0, uni = 0, gtonly = 0, oo = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const bool A = a[i] > 0.5f;
        const bool B = gt[i] > 127;
        if (A && B) ++inter;
        if (A || B) ++uni;
        if (B && !A) ++gtonly;
        if (A && !B) ++oo;
    }
    const double n = double(std::size_t(w) * h);
    Metrics m;
    m.area = double(inter + oo) / n;
    m.iou = uni ? double(inter) / double(uni) : 0;
    m.prec = (inter + oo) ? double(inter) / double(inter + oo) : 0;
    m.rec = (inter + gtonly) ? double(inter) / double(inter + gtonly) : 0;
    return m;
}

static void report(const char* label, const std::vector<float>& a,
                   const std::vector<uchar>& gt, int w, int h, bool refine,
                   const std::vector<std::uint8_t>& rgba) {
    std::vector<float> v = a;
    if (refine) guided_refine_alpha(rgba.data(), w, h, v, 10, 1e-3f);
    const Metrics m = eval(v, gt, w, h);
    std::printf("  %-46s area=%5.1f%%  IoU=%.4f  P=%.4f  R=%.4f\n", label,
                100.0 * m.area, m.iou, m.prec, m.rec);
}

static float scoreSubject(const std::vector<float>& a, float iou, int w, int h,
                          double& covOut) {
    double coverage = 0.0;
    for (float v : a)
        if (v > 0.5f) coverage += 1.0;
    coverage /= double(std::size_t(w) * h);
    covOut = coverage;
    float score = float(coverage) + 0.4f * iou;
    if (coverage > 0.60) score -= float(coverage - 0.60) * 3.0f;
    if (coverage < 0.02) score -= 0.3f;
    return score;
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

    // Grid candidates (mirror autoSelectSubject).
    struct Pt { int x, y; };
    std::vector<Pt> pts;
    const double gridX[] = {0.30, 0.50, 0.70};
    const double gridY[] = {0.35, 0.50, 0.65};
    for (double fx : gridX)
        for (double fy : gridY)
            pts.push_back({std::clamp(int(fx * w), 0, w - 1), std::clamp(int(fy * h), 0, h - 1)});
    if (double(w) / h > 1.4) {
        pts.push_back({std::clamp(int(0.15 * w), 0, w - 1), std::clamp(int(0.50 * h), 0, h - 1)});
        pts.push_back({std::clamp(int(0.85 * w), 0, w - 1), std::clamp(int(0.50 * h), 0, h - 1)});
    }
    std::vector<SamDecodeResult> decs;
    std::vector<double> covs;
    std::vector<float> scores;
    int bestIdx = -1;
    float bestScore = -1e9f;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        SamDecodeResult r = decode_point(enc, pts[i].x, pts[i].y, decPath);
        decs.push_back(std::move(r));
        double cov = 0; float sc = -1e9f;
        if (decs.back().ok)
            sc = scoreSubject(decs.back().alpha, decs.back().iou, w, h, cov);
        covs.push_back(cov); scores.push_back(sc);
        if (sc > bestScore) { bestScore = sc; bestIdx = int(i); }
        std::printf("== cand %zu (%d,%d) ok=%d iou=%.3f cov=%.3f score=%.3f\n", i,
                    pts[i].x, pts[i].y, int(decs.back().ok), decs.back().iou, cov, sc);
    }
    const SamDecodeResult& win = decs[std::size_t(bestIdx)];
    std::printf("== winner idx=%d (%d,%d) iou=%.3f cov=%.3f\n", bestIdx, win.px, win.py,
                win.iou, covs[std::size_t(bestIdx)]);

    // The SHIPPED auto path: auto_subject now implements the dense overlap-gated
    // union internally — verify it reproduces the probe's hand-rolled result.
    {
        SamDecodeResult a = auto_subject(enc, decPath);
        if (!a.ok) { std::printf("== auto_subject FAILED: %s\n", a.error.c_str()); }
        else {
            report("SHIPPED auto_subject, refined", a.alpha, gt, w, h, true, rgba);
            std::vector<float> v = a.alpha;
            guided_refine_alpha(rgba.data(), w, h, v, 10, 1e-3f);
            const Metrics m = eval(v, gt, w, h);
            std::printf("  ^ shipped: IoU=%.4f P=%.4f R=%.4f\n", m.iou, m.prec, m.rec);
        }
    }

    // MULTI-POINT prompt: torso + arm + a point in the dark left region, all in
    // one decode. SAM is trained to merge multiple positive clicks.
    {
        const int ax = 541, ay = 536;  // comp #0 centroid (missed dark region)
        std::vector<std::pair<int, int>> pts2 = {
            {win.px, win.py}, {ax, ay}, {923, 169}, {1190, 422}};
        SamDecodeResult r = decode_points(enc, pts2, decPath);
        if (!r.ok) {
            std::printf("== decode_points FAILED: %s\n", r.error.c_str());
        } else {
            report("multi-point (torso+arm+head+none) refined", r.alpha, gt, w, h,
                   true, rgba);
            toImage(r.alpha, w, h).save("/tmp/object_mask_multipoint.png");
        }
    }
    // Two-point only: torso + arm centroid.
    {
        SamDecodeResult r = decode_points(enc, {{win.px, win.py}, {541, 536}}, decPath);
        if (r.ok) report("multi-point (torso+arm) refined", r.alpha, gt, w, h, true, rgba);
    }

    // BRUTE FORCE: multi-point with ALL 25 grid points simultaneously.
    {
        std::vector<std::pair<int,int>> pts;
        for (double fy : {0.25,0.40,0.55,0.70,0.85})
            for (double fx : {0.20,0.35,0.50,0.65,0.80})
                pts.push_back({std::clamp(int(fx*w),0,w-1), std::clamp(int(fy*h),0,h-1)});
        SamDecodeResult r = decode_points(enc, pts, decPath);
        if (r.ok) {
            report("25-point brute multi-point", r.alpha, gt, w, h, true, rgba);
            std::vector<float> v = r.alpha;
            guided_refine_alpha(rgba.data(), w, h, v, 10, 1e-3f);
            toImage(v, w, h).save("/tmp/object_mask_multi25.png");
        } else {
            std::printf("== 25-point multi FAILED: %s\n", r.error.c_str());
        }
    }
    // 9-point (3x3) multi-point — the original grid, one decode.
    {
        std::vector<std::pair<int,int>> pts;
        for (double fy : {0.35,0.50,0.65})
            for (double fx : {0.30,0.50,0.70})
                pts.push_back({std::clamp(int(fx*w),0,w-1), std::clamp(int(fy*h),0,h-1)});
        SamDecodeResult r = decode_points(enc, pts, decPath);
        if (r.ok) {
            report("9-point (3x3) multi-point", r.alpha, gt, w, h, true, rgba);
            std::vector<float> v = r.alpha;
            guided_refine_alpha(rgba.data(), w, h, v, 10, 1e-3f);
            toImage(v, w, h).save("/tmp/object_mask_multi9.png");
        } else {
            std::printf("== 9-point multi FAILED: %s\n", r.error.c_str());
        }
    }

    // HYBRID: 27 individual decodes to find merged subjects, then ONE
    // multi-point decode with all merged candidate centroids — gets the
    // "SAM sees everything as one object" effect with only ONE extra decode.
    {
        const double gridX[] = {0.20, 0.35, 0.50, 0.65, 0.80};
        const double gridY[] = {0.25, 0.40, 0.55, 0.70, 0.85};
        std::vector<SamDecodeResult> d;
        std::vector<double> c;
        for (double fy : gridY)
            for (double fx : gridX)
                d.push_back(decode_point(enc, int(fx*w), int(fy*h), decPath));
        if (double(w)/h > 1.4) {
            d.push_back(decode_point(enc, int(0.15*w), int(0.50*h), decPath));
            d.push_back(decode_point(enc, int(0.85*w), int(0.50*h), decPath));
        }
        for (auto& r : d) {
            double cov = 0; r.ok ? void(scoreSubject(r.alpha, r.iou, w, h, cov)) : void(0);
            c.push_back(cov);
        }
        int bi = 0; float bs = -1e9f;
        for (std::size_t i = 0; i < d.size(); ++i) {
            if (!d[i].ok) continue;
            const float s = scoreSubject(d[i].alpha, d[i].iou, w, h, c[i]);
            if (s > bs) { bs = s; bi = int(i); }
        }
        // overlap-gate against the best (same as shipped path)
        std::vector<float> acc = d[bi].alpha;
        std::vector<std::pair<int,int>> mergedPts = {{d[bi].px, d[bi].py}};
        for (std::size_t i = 0; i < d.size(); ++i) {
            if (int(i)==bi || !d[i].ok || c[i]<0.005 || c[i]>0.60) continue;
            std::size_t inter=0, cand=0;
            for (std::size_t k=0;k<d[i].alpha.size();++k){
                if(d[i].alpha[k]>0.5f) ++cand;
                if(acc[k]>0.5f && d[i].alpha[k]>0.5f) ++inter;
            }
            if (cand && double(inter)/double(cand)>0.10){
                for(std::size_t k=0;k<d[i].alpha.size();++k) acc[k]=std::max(acc[k],d[i].alpha[k]);
                mergedPts.push_back({d[i].px, d[i].py});
            }
        }
        std::printf("== hybrid: %zu/%zu merged -> multi-point decode\n", mergedPts.size(), d.size());
        SamDecodeResult r = decode_points(enc, mergedPts, decPath);
        if (r.ok) {
            report("hybrid grid+multi-point refined", r.alpha, gt, w, h, true, rgba);
            toImage(r.alpha, w, h).save("/tmp/object_mask_hybrid.png");
        } else {
            std::printf("== hybrid multi-point FAILED: %s\n", r.error.c_str());
            report("hybrid union-only refined", acc, gt, w, h, true, rgba);
        }
    }

    auto unionMax = [&](std::vector<float> acc, const std::vector<float>& b) {
        for (std::size_t i = 0; i < acc.size(); ++i) acc[i] = std::max(acc[i], b[i]);
        return acc;
    };
    auto largest = [&](const std::vector<float>& a) {
        return fromImage(selectionMaskLargestComponent(toImage(a, w, h)));
    };

    std::printf("\n--- strategies (R=after guided refine+level; shipped path) ---\n");
    report("winner only", win.alpha, gt, w, h, false, rgba);
    report("winner only, refined", win.alpha, gt, w, h, true, rgba);

    // Strategy: union all candidates that overlap the winner and are not tiny,
    // then keep the largest component.
    {
        std::vector<float> u = win.alpha;
        for (std::size_t i = 0; i < decs.size(); ++i) {
            if (!decs[i].ok || int(i) == bestIdx) continue;
            if (covs[i] < 0.005) continue;  // skip dust
            // overlap with current union
            std::size_t inter = 0, cand = 0;
            for (std::size_t k = 0; k < u.size(); ++k) {
                const bool A = u[k] > 0.5f;
                const bool B = decs[i].alpha[k] > 0.5f;
                if (B) ++cand;
                if (A && B) ++inter;
            }
            if (cand && double(inter) / double(cand) > 0.30) u = unionMax(u, decs[i].alpha);
        }
        std::vector<float> c = largest(u);
        report("union overlap>30% all, largest-comp", c, gt, w, h, false, rgba);
        report("union overlap>30% all, largest-comp, refined", c, gt, w, h, true, rgba);
    }

    // Strategy: refine_mask seeded from the winner.
    {
        SamDecodeResult r = refine_mask(enc, win.px, win.py, win.alpha.data(), decPath);
        if (r.ok) {
            report("refine_mask(winner) 3x", r.alpha, gt, w, h, false, rgba);
            report("refine_mask(winner) 3x, refined", r.alpha, gt, w, h, true, rgba);
        } else {
            std::printf("  refine_mask failed: %s\n", r.error.c_str());
        }
    }

    // Strategy: prompt extra points at winner extremes (top/left/right), union
    // decodes that overlap the winner, then refine_mask the union.
    auto extremes = [&](const std::vector<float>& a, int& tx, int& ty, int& lx, int& ly,
                        int& rx, int& ry) {
        int minY = h, lxMin = w, rxMax = -1;
        tx = ty = lx = ly = rx = ry = -1;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if (a[std::size_t(y) * w + x] > 0.5f) {
                    if (y < minY) { minY = y; tx = x; ty = y; }
                    if (x < lxMin) { lxMin = x; lx = x; ly = y; }
                    if (x > rxMax) { rxMax = x; rx = x; ry = y; }
                }
    };
    {
        int tx, ty, lx, ly, rx, ry;
        extremes(win.alpha, tx, ty, lx, ly, rx, ry);
        std::printf("== winner extremes top=(%d,%d) left=(%d,%d) right=(%d,%d)\n", tx, ty,
                    lx, ly, rx, ry);
        std::vector<float> u = win.alpha;
        const int ex[3][2] = {{tx, ty}, {lx, ly}, {rx, ry}};
        for (auto& e : ex) {
            SamDecodeResult r = decode_point(enc, e[0], e[1], decPath);
            if (!r.ok) continue;
            std::size_t inter = 0, cand = 0;
            for (std::size_t k = 0; k < u.size(); ++k) {
                const bool A = u[k] > 0.5f;
                const bool B = r.alpha[k] > 0.5f;
                if (B) ++cand;
                if (A && B) ++inter;
            }
            std::printf("== extreme decode (%d,%d) iou=%.3f cov=%.3f overlap=%.2f\n", e[0],
                        e[1], r.iou,
                        double(std::count_if(r.alpha.begin(), r.alpha.end(),
                                             [](float v) { return v > 0.5f; })) /
                            double(u.size()),
                        cand ? double(inter) / double(cand) : 0.0);
            if (cand && double(inter) / double(cand) > 0.25) u = unionMax(u, r.alpha);
        }
        u = largest(u);
        report("winner+extremes union, largest, refined", u, gt, w, h, true, rgba);
        SamDecodeResult rr = refine_mask(enc, win.px, win.py, u.data(), decPath);
        if (rr.ok) report("winner+extremes -> refine_mask, refined", rr.alpha, gt, w, h, true, rgba);
    }

    // Probe the missed GT-only regions directly: label the connected
    // components of (gt && !winner), decode at each large one's centroid, and
    // report what SAM returns there (does the ground truth even come from SAM?).
    {
        std::vector<std::uint8_t> miss(std::size_t(w) * h, 0);
        for (std::size_t i = 0; i < miss.size(); ++i)
            if (gt[i] > 127 && !(win.alpha[i] > 0.5f)) miss[i] = 1;
        std::vector<int> label(miss.size(), -1);
        std::size_t nComps = 0;
        struct MissComp { int n = 0; long sx = 0, sy = 0; int id = -1; };
        std::vector<MissComp> comps;
        for (int sy = 0; sy < h; ++sy)
            for (int sx = 0; sx < w; ++sx) {
                const std::size_t s = std::size_t(sy) * w + sx;
                if (!miss[s] || label[s] >= 0) continue;
                const int id = int(comps.size());
                comps.push_back(MissComp{});
                comps.back().id = id;
                std::vector<int> stack{static_cast<int>(s)};
                label[s] = id;
                while (!stack.empty()) {
                    const int idx = stack.back();
                    stack.pop_back();
                    comps[std::size_t(id)].n++;
                    comps[std::size_t(id)].sx += idx % w;
                    comps[std::size_t(id)].sy += idx / w;
                    const int x = idx % w, y = idx / w;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (!dx && !dy) continue;
                            if (x + dx < 0 || y + dy < 0 || x + dx >= w || y + dy >= h)
                                continue;
                            const std::size_t ni = std::size_t(y + dy) * w + (x + dx);
                            if (miss[ni] && label[ni] < 0) {
                                label[ni] = id;
                                stack.push_back(int(ni));
                            }
                        }
                }
                ++nComps;
            }
        std::sort(comps.begin(), comps.end(),
                  [](const MissComp& a, const MissComp& b) { return a.n > b.n; });
        std::printf("\n== missed GT-only components: %zu total\n", nComps);
        // For the largest missed component, probe a grid inside its bbox to test
        // whether SAM can ever return a mask that overlaps/attaches to the body.
        if (!comps.empty() && comps[0].n > 5000) {
            int bx0 = w, by0 = h, bx1 = 0, by1 = 0;
            const int cid = comps[0].id;
            for (std::size_t i = 0; i < miss.size(); ++i)
                if (miss[i] && label[std::size_t(i)] == cid) {
                    bx0 = std::min(bx0, int(i % w));
                    by0 = std::min(by0, int(i / w));
                    bx1 = std::max(bx1, int(i % w));
                    by1 = std::max(by1, int(i / w));
                }
            std::printf("  comp #0 bbox (%d,%d)-(%d,%d)\n", bx0, by0, bx1, by1);
            std::vector<float> m0 = win.alpha;
            for (double fy : {0.3, 0.5, 0.7})
                for (double fx : {0.3, 0.5, 0.7}) {
                    const int px = std::clamp(int(bx0 + fx * (bx1 - bx0)), 0, w - 1);
                    const int py = std::clamp(int(by0 + fy * (by1 - by0)), 0, h - 1);
                    SamDecodeResult r = decode_point(enc, px, py, decPath);
                    if (!r.ok) continue;
                    std::size_t inter = 0, cand = 0;
                    for (std::size_t k = 0; k < m0.size(); ++k) {
                        const bool A = m0[k] > 0.5f;
                        const bool B = r.alpha[k] > 0.5f;
                        if (B) ++cand;
                        if (A && B) ++inter;
                    }
                    const double cov =
                        double(std::count_if(r.alpha.begin(), r.alpha.end(),
                                             [](float v) { return v > 0.5f; })) /
                        double(w * h);
                    std::printf("    probe (%d,%d) iou=%.3f cov=%.3f overlapWithBody=%.2f\n",
                                px, py, r.iou, cov,
                                cand ? double(inter) / double(cand) : 0.0);
                    if (cand && double(inter) / double(cand) > 0.10)
                        m0 = unionMax(m0, r.alpha);
                }
            m0 = largest(m0);
            report("comp#0 probes, largest, refined", m0, gt, w, h, true, rgba);
        }
        std::vector<float> u = win.alpha;
        for (std::size_t ci = 0; ci < comps.size() && ci < 8; ++ci) {
            const MissComp& c = comps[ci];
            const int cx = int(c.sx / c.n), cy = int(c.sy / c.n);
            SamDecodeResult r = decode_point(enc, cx, cy, decPath);
            std::size_t inter = 0, cand = 0;
            if (r.ok)
                for (std::size_t k = 0; k < u.size(); ++k) {
                    const bool A = u[k] > 0.5f;
                    const bool B = r.alpha[k] > 0.5f;
                    if (B) ++cand;
                    if (A && B) ++inter;
                }
            const double cov = r.ok ? double(std::count_if(
                                          r.alpha.begin(), r.alpha.end(),
                                          [](float v) { return v > 0.5f; })) /
                                          double(w * h)
                                    : 0.0;
            std::printf("  miss comp #%zu n=%d (%.2f%%) ctr=(%d,%d) decode ok=%d "
                        "iou=%.3f cov=%.3f overlapWithU=%.2f\n",
                        ci, c.n, 100.0 * double(c.n) / double(w * h), cx, cy,
                        int(r.ok), r.ok ? r.iou : 0.0f, cov,
                        cand ? double(inter) / double(cand) : 0.0);
            if (r.ok && cand && double(inter) / double(cand) > 0.05)
                u = unionMax(u, r.alpha);
        }
        if (nComps > 0) {
            u = largest(u);
            report("winner + missed-region decodes, largest, refined", u, gt, w, h, true,
                   rgba);
            // IoU of the union against GT (gives the ceiling if SAM finds them).
            std::vector<float> v = u;
            guided_refine_alpha(rgba.data(), w, h, v, 10, 1e-3f);
            const Metrics m = eval(v, gt, w, h);
            std::printf("  ^ union ceiling against GT: IoU=%.4f P=%.4f R=%.4f\n", m.iou,
                        m.prec, m.rec);
        }
    }

    // Dense multi-prompt union strategy: decode a 5x5 grid, pick the best-scored
    // subject, union every other candidate whose mask overlaps the subject
    // (strict and lax variants), keep the largest component, then optionally a
    // mask-guided refine pass.
    {
        const double gx[] = {0.20, 0.35, 0.50, 0.65, 0.80};
        const double gy[] = {0.25, 0.40, 0.55, 0.70, 0.85};
        std::vector<SamDecodeResult> dd;
        std::vector<double> cc;
        int bi = -1;
        float bs = -1e9f;
        for (double fy : gy)
            for (double fx : gx) {
                const int px = std::clamp(int(fx * w), 0, w - 1);
                const int py = std::clamp(int(fy * h), 0, h - 1);
                SamDecodeResult r = decode_point(enc, px, py, decPath);
                double cov = 0;
                float sc = -1e9f;
                if (r.ok) sc = scoreSubject(r.alpha, r.iou, w, h, cov);
                dd.push_back(std::move(r));
                cc.push_back(cov);
                if (sc > bs) { bs = sc; bi = int(dd.size()) - 1; }
            }
        std::printf("\n== dense grid: %zu decodes, best idx=%d (%d,%d) cov=%.3f\n",
                    dd.size(), bi, dd[std::size_t(bi)].px, dd[std::size_t(bi)].py,
                    cc[std::size_t(bi)]);
        auto overlapFrac = [&](const std::vector<float>& u, const std::vector<float>& a,
                               double& interOut, double& candOut) {
            interOut = candOut = 0;
            for (std::size_t i = 0; i < u.size(); ++i) {
                const bool A = u[i] > 0.5f;
                const bool B = a[i] > 0.5f;
                if (B) ++candOut;
                if (A && B) ++interOut;
            }
        };
        for (double thr : {0.10, 0.02}) {
            std::vector<float> u = dd[std::size_t(bi)].alpha;
            for (std::size_t i = 0; i < dd.size(); ++i) {
                if (!dd[i].ok || int(i) == bi) continue;
                if (cc[i] < 0.005 || cc[i] > 0.60) continue;
                double inter = 0, cand = 0;
                overlapFrac(u, dd[i].alpha, inter, cand);
                if (cand && inter / cand > thr) u = unionMax(u, dd[i].alpha);
            }
            u = largest(u);
            char lbl[96];
            std::snprintf(lbl, sizeof(lbl), "dense union thr=%.2f, largest, refined", thr);
            report(lbl, u, gt, w, h, true, rgba);
            SamDecodeResult rr = refine_mask(enc, dd[std::size_t(bi)].px,
                                             dd[std::size_t(bi)].py, u.data(), decPath);
            if (rr.ok) {
                char lbl2[120];
                std::snprintf(lbl2, sizeof(lbl2), "dense union thr=%.2f -> refine_mask, refined",
                              thr);
                report(lbl2, rr.alpha, gt, w, h, true, rgba);
                if (thr < 0.5) {
                    std::vector<float> v = rr.alpha;
                    guided_refine_alpha(rgba.data(), w, h, v, 10, 1e-3f);
                    toImage(v, w, h).save("/tmp/object_mask_dense.png");
                    std::printf("  * saved /tmp/object_mask_dense.png\n");
                }
            }
        }
    }

    // Same overlap-gated union, but on the CHEAP 11-point grid (the shipping
    // candidate count), to see if the latency stays affordable.
    {
        std::vector<float> u = win.alpha;
        for (std::size_t i = 0; i < decs.size(); ++i) {
            if (!decs[i].ok || int(i) == bestIdx) continue;
            if (covs[i] < 0.005 || covs[i] > 0.60) continue;
            std::size_t inter = 0, cand = 0;
            for (std::size_t k = 0; k < u.size(); ++k) {
                const bool A = u[k] > 0.5f;
                const bool B = decs[i].alpha[k] > 0.5f;
                if (B) ++cand;
                if (A && B) ++inter;
            }
            if (cand && double(inter) / double(cand) > 0.10) u = unionMax(u, decs[i].alpha);
        }
        u = largest(u);
        report("11pt union thr=0.10, largest, refined", u, gt, w, h, true, rgba);
        SamDecodeResult rr = refine_mask(enc, win.px, win.py, u.data(), decPath);
        if (rr.ok)
            report("11pt union thr=0.10 -> refine_mask, refined", rr.alpha, gt, w, h, true,
                   rgba);
    }

    // Best possible: GT itself (sanity).
    report("GROUND TRUTH", [&] {
        std::vector<float> g(std::size_t(w) * h);
        for (std::size_t i = 0; i < g.size(); ++i) g[i] = gt[i] ? 1.0f : 0.0f;
        return g;
    }(), gt, w, h, false, rgba);
    return 0;
}
