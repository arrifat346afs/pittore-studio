// object_select_probe.cpp — manual verification of the Object Select pipeline
// on a real photo (run with QT_QPA_PLATFORM=offscreen). Loads the image,
// encodes it once (the SAM encoder), prompts the decoder at a grid of query
// points, keeps the largest object, builds the document grayscale selection
// channel exactly as CanvasView::runAiObjectSelect does, and writes
// /tmp/object_outline.png + object_mask.png plus stats.
//
// NOT a suite test: it runs the real (proprietary, locally-imported) ONNX
// models, so it never runs under `meson test`.
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
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

using namespace pittore::ui;

int main(int argc, char** argv) {
    const char* imgPath = argc > 1 ? argv[1] : std::getenv("PITTORE_SELECT_IMAGE");
    if (!imgPath) {
        std::fprintf(stderr, "usage: object_select_probe <image>  (or set PITTORE_SELECT_IMAGE)\n");
        return 2;
    }
    QImage img(imgPath);
    if (img.isNull()) {
        std::fprintf(stderr, "cannot load %s\n", imgPath);
        return 2;
    }
    img = img.convertToFormat(QImage::Format_RGBA8888);
    const int w = img.width(), h = img.height();
    std::printf("== image %dx%d\n", w, h);

    std::string home = std::getenv("HOME") ? std::getenv("HOME") : ".";
    const char* models = std::getenv("PITTORE_MODELS_DIR");
    const std::string dir = (models && *models) ? std::string(models)
                                                : home + "/.local/share/PittoreStudio/models";
    const std::string encPath = dir + "/SegmentationEncoder_2.6.onnx";
    const std::string decPath = dir + "/SegmentationDecoder_2.6.onnx";

    const pittore::ai::SamEncodings enc =
        pittore::ai::encode_rgba8(img.constBits(), w, h, encPath, 1024);
    if (!enc.ok) {
        std::fprintf(stderr, "encode failed: %s\n", enc.error.c_str());
        return 3;
    }
    std::printf("== encoded %dx%d (input %d) in %.0fms\n", enc.width, enc.height,
                enc.inputSize, 0.0);

    // The real shipped pipeline exactly as runAiBackgroundRemoval calls it:
    // encode -> auto_subject (25-point multi prompt) -> guided refine ->
    // mask-prompt re-segmentation (2 passes). This is segment_rgba8_pair.
    int bestPx = -1, bestPy = -1;
    std::vector<float> bestAlpha;
    float bestIou = 0.0f;
    const pittore::ai::SegmentResult seg =
        pittore::ai::segment_rgba8_pair(img.constBits(), w, h, encPath, decPath,
                                         1024);
    if (!seg.ok) {
        std::fprintf(stderr, "segment_rgba8_pair failed: %s\n", seg.error.c_str());
        return 4;
    }
    bestAlpha = seg.alpha;
    {
        // Diagnostic: save the shipped raw alpha (pre-cleanup) for diffing.
        QImage ra(w, h, QImage::Format_Grayscale8);
        for (int y = 0; y < h; ++y) {
            uchar* row = ra.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = static_cast<uchar>(std::clamp(
                    int(bestAlpha[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
        }
        ra.save("/tmp/object_mask_raw.png");
    }
    {
        std::int64_t ax = 0, ay = 0, an = 0;
        for (std::size_t i = 0; i < bestAlpha.size(); ++i)
            if (bestAlpha[i] > 0.5f) {
                ax += i % std::size_t(w);
                ay += i / std::size_t(w);
                ++an;
            }
        bestPx = an ? int(ax / an) : w / 2;
        bestPy = an ? int(ay / an) : h / 2;
    }
    std::size_t selN = 0;
    for (float a : bestAlpha)
        if (a > 0.5f) ++selN;
    std::printf("== auto-subject (25-pt multi + mask-refine) selected=%.1f%%\n",
                100.0 * double(selN) / double(bestAlpha.size()));

    // Stage report helper: coverage % and largest-component bbox of bestAlpha.
    const auto report = [&](const char* label) {
        std::size_t n = 0;
        for (float a : bestAlpha)
            if (a > 0.5f) ++n;
        QImage m(w, h, QImage::Format_Grayscale8);
        m.fill(0);
        for (int y = 0; y < h; ++y) {
            uchar* row = m.scanLine(y);
            for (int x = 0; x < w; ++x)
                row[x] = static_cast<uchar>(std::clamp(
                    int(bestAlpha[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
        }
        const QRect kb = selectionMaskBbox(selectionMaskLargestComponent(m));
        std::printf("== %-22s coverage=%5.1f%%  largest=(%d,%d)-(%d,%d)\n", label,
                    100.0 * double(n) / double(bestAlpha.size()), kb.left(), kb.top(),
                    kb.right(), kb.bottom());
    };
    report("auto-subject");

    // Document selection channel exactly as the canvas builds it (identity
    // layer transform: offset 0, scale 1 — the photo is one full-canvas layer).
    // Rows go through scanLine(): QImage pads scanlines to 4-byte boundaries,
    // and writing/reading at the raw stride `w` diagonally smears the mask.
    QImage mask(w, h, QImage::Format_Grayscale8);
    mask.fill(0);
    for (int y = 0; y < h; ++y) {
        uchar* row = mask.scanLine(y);
        for (int x = 0; x < w; ++x) {
            row[x] = static_cast<uchar>(std::clamp(
                int(bestAlpha[std::size_t(y) * w + x] * 255.0f + 0.5f), 0, 255));
        }
    }
    const QRect bbox = selectionMaskBbox(mask);
    std::size_t selCount = 0;
    for (float a : bestAlpha)
        if (a > 0.5f) ++selCount;
    std::printf("== auto-subject mask (%d,%d) iou=%.3f selected=%.1f%%\n",
                bestPx, bestPy, bestIou,
                100.0 * double(selCount) / double(bestAlpha.size()));
    std::printf("== mask bbox (%d,%d)-(%d,%d)  (%d%% of frame)\n", bbox.left(),
                bbox.top(), bbox.right(), bbox.bottom(),
                int(100.0 * double(bbox.width() * bbox.height()) / double(w * h)));

    // The tool commits the largest connected component only (strays stripped).
    const QImage kept = selectionMaskLargestComponent(mask);
    const QRect kbbox = selectionMaskBbox(kept);
    std::printf("== largest-component bbox (%d,%d)-(%d,%d)  (%d%% of the raw "
                "selection's area)\n",
                kbbox.left(), kbbox.top(), kbbox.right(), kbbox.bottom(),
                int(kbbox.isEmpty()
                        ? 0
                        : 100.0 * double(kbbox.width() * kbbox.height()) /
                              double(bbox.width() * bbox.height())));

    // Alpha-restriction mirror: what CanvasView now commits on a cutout layer
    // (identity transform here). The layer's own alpha plane is ground truth
    // for "pixels with information" — intersect, and snap to the click's
    // cutout component when the click is opaque.
    // Mirror the UI cleanup chain exactly (CanvasView::objectSelectMaskAt /
    // MainWindow select-subject): after stripping strays, fill enclosed holes
    // with the channel-sealing closing probe and smooth the ants contour.
    // Keeping the probe on the shipping chain is what stops "probe looks clean,
    // app looks fat" divergences from hiding here.
    QImage finalMask = kept;
    {
        const QRect bb = selectionMaskBbox(finalMask);
        const int bridgeRadius = std::clamp(
            int(std::lround(std::min(bb.width(), bb.height()) * 0.02)), 2, 12);
        const QImage before = finalMask;
        finalMask = selectionMaskFillHoles(finalMask, bridgeRadius);
        finalMask = selectionMaskSmooth(finalMask, 1);
        std::size_t filled = 0;
        for (int y = 0; y < h; ++y) {
            const uchar* b = before.constScanLine(y);
            const uchar* a = finalMask.constScanLine(y);
            for (int x = 0; x < w; ++x)
                if (b[x] <= 127 && a[x] > 127) ++filled;
        }
        std::printf("== cleanup: fillHoles(r=%d)+smooth -> +%zu px\n", bridgeRadius,
                    filled);
    }
    {
        QImage opaque(w, h, QImage::Format_Grayscale8);
        opaque.fill(0);
        std::size_t opaqueCount = 0;
        const int stride = img.bytesPerLine();
        const uchar* bits = img.constBits();
        for (int y = 0; y < h; ++y) {
            const uchar* src = bits + std::size_t(y) * stride;
            uchar* dst = opaque.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const uchar a = src[std::size_t(x) * 4 + 3];
                if (a > 16) {
                    dst[x] = 255;
                    ++opaqueCount;
                }
            }
        }
        const double opaqueFraction =
            double(opaqueCount) / double(std::size_t(w) * h);
        std::printf("== opaque fraction of image: %.2f%%\n",
                    opaqueFraction * 100.0);
        if (opaqueFraction < 0.9995) {
            // Bogus pixels the unrestricted pipeline selected: selected in
            // `kept` while fully transparent in the image.
            std::size_t bogus = 0;
            for (int y = 0; y < h; ++y) {
                const uchar* krow = kept.constScanLine(y);
                const uchar* src = bits + std::size_t(y) * stride;
                for (int x = 0; x < w; ++x)
                    if (krow[x] > 127 && src[std::size_t(x) * 4 + 3] <= 16)
                        ++bogus;
            }
            std::printf("== transparent pixels the old pipeline selected: %zu "
                        "(%.2f%% of image)\n",
                        bogus, 100.0 * double(bogus) / double(std::size_t(w) * h));
            // Snap seed: the click (best point), spiralling out to an opaque
            // pixel when the click itself is transparent.
            int sx = bestPx, sy = bestPy;
            auto isOpaque = [&](int x, int y) {
                return x >= 0 && y >= 0 && x < w && y < h &&
                       opaque.constScanLine(y)[x] > 127;
            };
            if (!isOpaque(sx, sy)) {
                bool found = false;
                for (int r = 1; r < 64 && !found; ++r)
                    for (int dy = -r; dy <= r && !found; ++dy)
                        for (int dx = -r; dx <= r; ++dx) {
                            if (std::max(std::abs(dx), std::abs(dy)) != r)
                                continue;
                            if (isOpaque(sx + dx, sy + dy)) {
                                sx += dx;
                                sy += dy;
                                found = true;
                                break;
                            }
                        }
                if (!found) { sx = -1; sy = -1; }
            }
            if (sx >= 0) {
                // BFS the 8-connected opaque component at the click.
                std::vector<std::uint8_t> seen(std::size_t(w) * h, 0);
                std::vector<int> stack;
                stack.reserve(1 << 16);
                stack.push_back(sy * w + sx);
                seen[std::size_t(sy) * w + sx] = 1;
                while (!stack.empty()) {
                    const int idx = stack.back();
                    stack.pop_back();
                    const int x = idx % w, y = idx / w;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (!dx && !dy) continue;
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= w || ny >= h)
                                continue;
                            if (seen[std::size_t(ny) * w + nx]) continue;
                            if (opaque.constScanLine(ny)[nx] <= 127) continue;
                            seen[std::size_t(ny) * w + nx] = 1;
                            stack.push_back(ny * w + nx);
                        }
                }
                finalMask.fill(0);
                std::size_t snapped = 0;
                for (int y = 0; y < h; ++y) {
                    uchar* dst = finalMask.scanLine(y);
                    const std::uint8_t* srow = seen.data() + std::size_t(y) * w;
                    for (int x = 0; x < w; ++x)
                        if (srow[x]) {
                            dst[x] = 255;
                            ++snapped;
                        }
                }
                const QRect sb = selectionMaskBbox(finalMask);
                std::printf("== RESTRICTED (cutout snap): coverage=%.1f%%  "
                            "bbox=(%d,%d)-(%d,%d)  selected opaque share=%.1f%%\n",
                            100.0 * double(snapped) / double(std::size_t(w) * h),
                            sb.left(), sb.top(), sb.right(), sb.bottom(),
                            100.0 * double(snapped) / double(opaqueCount));
            } else {
                for (int y = 0; y < h; ++y) {
                    uchar* dst = finalMask.scanLine(y);
                    const uchar* orow = opaque.constScanLine(y);
                    for (int x = 0; x < w; ++x)
                        if (orow[x] <= 127) dst[x] = 0;
                }
                std::printf("== RESTRICTED (intersect; click not on cutout)\n");
            }
        } else {
            std::printf("== no transparency: restriction is a no-op\n");
        }
    }

    const QRect fbbox = selectionMaskBbox(finalMask);
    const QPainterPath outline =
        selectionOutlineFromMask(finalMask, fbbox.adjusted(-1, -1, 1, 1));
    std::printf("== outline elements=%d bounds=(%.1f,%.1f)-(%.1f,%.1f)\n",
                outline.elementCount(), outline.boundingRect().left(),
                outline.boundingRect().top(), outline.boundingRect().right(),
                outline.boundingRect().bottom());
    const QPointF click(bestPx + 0.5, bestPy + 0.5);
    std::printf("== outline contains click point: %d\n", int(outline.contains(click)));

    // Visible outputs: the frame with the marching-ants contour, and the mask.
    QImage overlay = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&overlay);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255), 7));
        p.drawPath(outline);
        QPen dash(QColor(0, 0, 0), 7, Qt::CustomDashLine);
        dash.setDashPattern({30, 30});
        p.setPen(dash);
        p.drawPath(outline);
    }
    if (!overlay.save("/tmp/object_outline.png"))
        std::fprintf(stderr, "failed to write outline png\n");
    // The smoothed display outline the canvas actually draws (spike-stripped,
    // collinear-collapsed, Catmull-Rom rounded) — the "clean ants" version.
    const QPainterPath smooth = selectionOutlineSmoothed(outline);
    std::printf("== smoothed outline elements=%d (raw %d)\n", smooth.elementCount(),
                outline.elementCount());
    QImage overlaySm = img.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&overlaySm);
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(QColor(255, 255, 255), 7));
        p.drawPath(smooth);
        QPen dash(QColor(0, 0, 0), 7, Qt::CustomDashLine);
        dash.setDashPattern({30, 30});
        p.setPen(dash);
        p.drawPath(smooth);
    }
    if (!overlaySm.save("/tmp/object_outline_smoothed.png"))
        std::fprintf(stderr, "failed to write smoothed outline png\n");
    if (!finalMask.save("/tmp/object_mask.png"))
        std::fprintf(stderr, "failed to write mask png\n");
    if (!mask.save("/tmp/object_mask_unrestricted.png"))
        std::fprintf(stderr, "failed to write unrestricted mask png\n");
    if (!kept.save("/tmp/object_mask_largest.png"))
        std::fprintf(stderr, "failed to write largest-component mask png\n");
    std::printf("== wrote /tmp/object_outline{,_smoothed}.png and "
                "object_mask.png\n");
    return 0;
}