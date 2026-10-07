#include "engine/ai/bg_remove.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "engine/core/log.h"

#ifdef PITTORE_HAS_ONNX
#include <onnxruntime_cxx_api.h>
#endif

#include "engine/ai/shared/ai_session.h"
#include "engine/ai/shared/ai_preprocess.h"

namespace pittore::ai {

using namespace ai_detail;


// ---------------------------------------------------------------------------
// Auto-subject selection (§4.1). With the encoder correctly ImageNet-normalised
// (see encode_rgba8) a multi-point prompt tells SAM which pixels belong to the
// same object: probing a dense grid of positive points and decoding them
// TOGETHER fuses the whole subject in one pass — including attached parts
// (headphones, an arm) that a single point misses and that the encoder's graph
// otherwise treats as separate objects. A coverage sanity gate guards against
// pathological all-background grids; the per-point score-and-select path (plus
// overlap-gated merge) remains as fallback for models whose decoder rejects
// batched prompts or for degenerate frames.
// ---------------------------------------------------------------------------

// Score one decode as the auto-subject hypothesis and report its >0.5 coverage.
float scoreSubjectCandidate(const SamDecodeResult& dec, int w, int h,
                            double& coverageOut, double subjectMaxCov) {
    double coverage = 0.0;
    for (float a : dec.alpha)
        if (a > 0.5f) coverage += 1.0;
    coverage /= double(std::size_t(w) * h);
    coverageOut = coverage;
    // A confident whole-object mask is large; fragments are tiny. The
    // predicted IoU breaks ties between rival whole-object hypotheses.
    float score = float(coverage) + 0.4f * dec.iou;
    // Background blobs that swallow most of the frame are not a subject.
    if (coverage > subjectMaxCov) score -= float(coverage - subjectMaxCov) * 3.0f;
    // A few stray pixels are never the subject.
    if (coverage < 0.02) score -= 0.3f;
    return score;
}


// §4.1 pipeline in layer pixel space. Decoder runs are cheap (~40 ms).
SamDecodeResult autoSelectSubject(const SamEncodings& enc,
                                  const std::string& decoderPath,
                                  double multiMaxCov, double perPointMaxCov) {
    SamDecodeResult res;
    const int w = enc.width, h = enc.height;
    if (w <= 0 || h <= 0) {
        res.error = "No embeddings for this layer.";
        return res;
    }

    // Build the candidate grid: 5×5 probes cover the usual subject placements
    // densely enough to land on attached objects (headphones, arms). Every grid
    // point is a positive prompt, so a point that lands on background would
    // pull the mask out to that background — keep the multi-point prompt to the
    // core 5×5 region only. Wide frames get two extra mid-height edge points
    // used ONLY by the per-point fallback (they are coverage-gated there, but
    // would corrupt the multi-point prompt on a wide frame).
    struct Pt { int x, y; };
    std::vector<Pt> corePts, allPts;
    constexpr double gridX[] = {0.20, 0.35, 0.50, 0.65, 0.80};
    constexpr double gridY[] = {0.25, 0.40, 0.55, 0.70, 0.85};
    for (double fx : gridX)
        for (double fy : gridY)
            corePts.push_back(
                {std::clamp(int(fx * w), 0, w - 1), std::clamp(int(fy * h), 0, h - 1)});
    allPts = corePts;
    if (double(w) / h > 1.4) {
        allPts.push_back({std::clamp(int(0.15 * w), 0, w - 1), std::clamp(int(0.50 * h), 0, h - 1)});
        allPts.push_back({std::clamp(int(0.85 * w), 0, w - 1), std::clamp(int(0.50 * h), 0, h - 1)});
    }

    // Fast path (§4.1 step 4): one multi-point decode with the core grid.
    // The decoder's transformer fuses all prompts and returns the object
    // they collectively describe — so even distant attached parts (ear cups
    // left behind a dark seam, an arm the single best guess missed) are
    // merged in one pass.
    {
        std::vector<std::pair<int,int>> pts;
        pts.reserve(corePts.size());
        for (const auto& p : corePts) pts.push_back({p.x, p.y});
        SamDecodeResult multi = decode_points(enc, pts, decoderPath);
        if (multi.ok && multi.alpha.size() == std::size_t(w) * h) {
            double coverage = 0.0;
            for (float a : multi.alpha)
                if (a > 0.5f) coverage += 1.0;
            coverage /= double(std::size_t(w) * h);
            // Sanity gate: a well-behaved subject covers 1% up to
            // `multiMaxCov` of the frame. If all grid points landed on a
            // background patch, SAM returns the whole wall; a decode far
            // above the configured cap is almost certainly that. A <1% decode
            // means the points straddle two unrelated objects. Fall back to
            // the slow per-point path when the gate fails.
            if (coverage > 0.01 && coverage < multiMaxCov) {
                PITTORE_LOG("[sam] auto-select multi-point: %zu prompts "
                             "coverage=%.3f iou=%.3f",
                             pts.size(), coverage, multi.iou);
                res.ok = true;
                res.iou = multi.iou;
                res.px = corePts.front().x;
                res.py = corePts.front().y;
                res.alpha = std::move(multi.alpha);
                return res;
            }
            PITTORE_LOG("[sam] multi-point gate failed (cov=%.3f); falling "
                         "back to per-point path", coverage);
        }
    }

    // Slow fallback: per-point decode + overlap-gated merge (§4.1 steps 2-4).
    const std::size_t n = std::size_t(w) * h;
    std::vector<SamDecodeResult> decs(allPts.size());
    std::vector<double> covs(allPts.size(), 0.0);
    int bestIdx = -1;
    float bestScore = -1e9f;
    for (std::size_t i = 0; i < allPts.size(); ++i) {
        SamDecodeResult r = decode_point(enc, allPts[i].x, allPts[i].y, decoderPath);
        if (!r.ok) continue;
        if (r.alpha.size() != n) continue;
        double coverage = 0.0;
        const float score = scoreSubjectCandidate(r, w, h, coverage, perPointMaxCov);
        covs[i] = coverage;
        PITTORE_LOG("[sam] candidate (%d,%d) iou=%.3f coverage=%.3f score=%.3f",
                     allPts[i].x, allPts[i].y, r.iou, coverage, score);
        decs[i] = std::move(r);
        if (score > bestScore) {
            bestScore = score;
            bestIdx = int(i);
        }
    }
    if (bestIdx < 0) {
        res.error = "No subject found: every candidate decode failed.";
        return res;
    }
    SamDecodeResult& best = decs[std::size_t(bestIdx)];
    std::vector<float> acc = best.alpha;
    std::size_t merged = 0;
    for (std::size_t i = 0; i < allPts.size(); ++i) {
        if (int(i) == bestIdx || !decs[i].ok) continue;
        if (covs[i] < 0.005 || covs[i] > perPointMaxCov) continue;
        const SamDecodeResult& r = decs[i];
        std::size_t inter = 0, cand = 0;
        for (std::size_t k = 0; k < n; ++k) {
            const bool A = acc[k] > 0.5f;
            const bool B = r.alpha[k] > 0.5f;
            if (B) ++cand;
            if (A && B) ++inter;
        }
        if (cand && double(inter) / double(cand) > 0.10) {
            for (std::size_t k = 0; k < n; ++k) acc[k] = std::max(acc[k], r.alpha[k]);
            ++merged;
            PITTORE_LOG("[sam] union candidate (%d,%d) (overlap %.2f)", r.px, r.py,
                         double(inter) / double(cand));
        }
    }
    PITTORE_LOG("[sam] auto-select fallback winner: (%d,%d) iou=%.3f coverage=%.3f "
                 "score=%.3f  merged=%zu/%zu",
                 best.px, best.py, best.iou, covs[std::size_t(bestIdx)], bestScore,
                 merged, allPts.size());
    res.ok = true;
    res.iou = best.iou;
    res.px = best.px;
    res.py = best.py;
    res.alpha = std::move(acc);
    return res;
}


SamDecodeResult auto_subject(const SamEncodings& enc,
                             const std::string& decoderPath,
                             double multiMaxCov, double perPointMaxCov) {
    return autoSelectSubject(enc, decoderPath, multiMaxCov, perPointMaxCov);
}

}  // namespace pittore::ai
