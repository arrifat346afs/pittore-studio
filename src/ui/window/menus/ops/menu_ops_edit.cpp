// Edit menu behaviour. buildEditMenu keeps the labels, shortcuts and the
// order of the menu; every handler it attaches lands here so one file owns
// the family and the builder stays structure-only.
#include "ui/main_window.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>

#include <algorithm>
#include <cmath>
#include <deque>
#include <vector>

#include "engine/compute/brushes/stamp/stamp.h"
#include "engine/core/image.h"
#include "ui/brushes/brush_library.h"
#include "ui/canvas/shared/canvas_helpers.h"
#include "ui/selection_mask.h"

namespace pittore::ui {
namespace {

// ---------------------------------------------------------------------------
// Shared geometry
// ---------------------------------------------------------------------------

QRect fullDocRect(const DocumentItem& doc) {
    return QRect(QPoint(0, 0), doc.size);
}

// A selection is worth acting on only when it covers something: a marquee
// rasterises into the very channel an arbitrary mask already fills.
bool hasSelection(const DocumentItem& doc) {
    return !selectionMaskBbox(selectionAsMask(doc)).isEmpty();
}

// Doc texel -> layer texel, the inverse of the placement the compositor
// samples with. False when the texel falls outside the layer's own pixels.
bool layerTexel(const QPointF& offset, double sx, double sy, int dx, int dy,
                int pw, int ph, int* lx, int* ly) {
    const int x = int(std::lround((dx + 0.5 - offset.x()) / sx - 0.5));
    const int y = int(std::lround((dy + 0.5 - offset.y()) / sy - 0.5));
    if (x < 0 || y < 0 || x >= pw || y >= ph) return false;
    *lx = x;
    *ly = y;
    return true;
}

// Straight-alpha source-over of an opaque colour at coverage `t` (0..1).
void paintOver(pittore::RGBAf& dst, const QColor& color, float t) {
    if (!(t > 0.0f)) return;
    if (t > 1.0f) t = 1.0f;
    const float na = t + dst.a * (1.0f - t);
    const float cr = color.red() / 255.0f;
    const float cg = color.green() / 255.0f;
    const float cb = color.blue() / 255.0f;
    dst.r = (cr * t + dst.r * dst.a * (1.0f - t)) / na;
    dst.g = (cg * t + dst.g * dst.a * (1.0f - t)) / na;
    dst.b = (cb * t + dst.b * dst.a * (1.0f - t)) / na;
    dst.a = na;
}

// Multiply the layer's alpha by (1 - coverage) over `clip` (doc space),
// walking every doc texel back through the layer's placement — the same
// inverse mapping AppState uses for selection erases. A doc texel can land
// on a layer texel more than once when the layer is scaled down, so the
// factors accumulate.
void eraseCovered(pittore::Image& img, const QImage& cov,
                  const QPointF& offset, double sx, double sy,
                  const QRect& clip) {
    const int pw = int(img.width());
    const int ph = int(img.height());
    if (pw == 0 || ph == 0 || clip.isEmpty()) return;
    pittore::RGBAf* px = img.data();
    for (int dy = clip.top(); dy <= clip.bottom(); ++dy) {
        const uchar* row = cov.constScanLine(dy);
        for (int dx = clip.left(); dx <= clip.right(); ++dx) {
            const int c = row[dx];
            if (!c) continue;
            int lx = 0, ly = 0;
            if (!layerTexel(offset, sx, sy, dx, dy, pw, ph, &lx, &ly)) continue;
            px[std::size_t(ly) * pw + lx].a *= (255.0f - c) / 255.0f;
        }
    }
}

// Alpha-weighted centre of a layer's own pixels in document space. Sampled
// on a stride so a 400 MP layer still answers in microseconds. Null when the
// layer carries no ink to match on.
QPointF alphaCentre(const LayerItem& l) {
    if (!l.pixels) return QPointF();
    const int w = int(l.pixels->width());
    const int h = int(l.pixels->height());
    if (w <= 0 || h <= 0) return QPointF();
    const int step = std::max(1, std::max(w, h) / 512);
    const pittore::RGBAf* px = l.pixels->data();
    double sum = 0.0, sx = 0.0, sy = 0.0;
    for (int y = 0; y < h; y += step) {
        for (int x = 0; x < w; x += step) {
            const float a = px[std::size_t(y) * w + x].a;
            if (!(a > 0.0f)) continue;
            sum += a;
            sx += double(x) * a;
            sy += double(y) * a;
        }
    }
    if (sum <= 0.0) return QPointF();
    const QPointF local(sx / sum + 0.5, sy / sum + 0.5);
    return QPointF(l.offset.x() + local.x() * l.scaleX,
                   l.offset.y() + local.y() * l.scaleY);
}

QRectF layerDocBounds(const LayerItem& l) {
    if (!l.pixels) return QRectF();
    return QRectF(l.offset,
                  QSizeF(l.pixels->width() * l.scaleX,
                         l.pixels->height() * l.scaleY));
}

// Selecting a layer for an edit the API only runs on the active layer.
// Returns false when the row cannot become active.
bool activateLayer(AppState* state, int index) {
    state->setActiveLayerIndex(index);
    return state->activeLayer() != nullptr &&
           state->selectedLayerIndices().contains(index);
}

// ---------------------------------------------------------------------------
// Stamp tips / grain patterns (the same conversions the Brushes panel makes
// of imported files: the panel's own copies are private to that file)
// ---------------------------------------------------------------------------

QString safeBaseName(const QString& name, const char* fallback) {
    QString safe = name;
    safe.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]+")),
                 QStringLiteral("_"));
    while (safe.startsWith(QLatin1Char('_'))) safe.remove(0, 1);
    return safe.isEmpty() ? QString::fromLatin1(fallback) : safe;
}

QString uniquePath(const QString& dir, const QString& base,
                   const QString& ext) {
    for (int n = 0; n < 1000; ++n) {
        const QString file =
            n == 0 ? base + ext : base + QStringLiteral("-%1").arg(n) + ext;
        const QString path = QDir(dir).filePath(file);
        if (!QFile::exists(path)) return path;
    }
    return QString();
}

// QImage -> StampTip. Alpha (when present) is coverage; opaque images use
// 1 - luma under the paint-mask convention. Colour rides along only when the
// image is genuinely colourful, so it can be tinted or painted as-is.
bool stampTipFromImage(const QImage& src, pittore::compute::StampTip* tip) {
    QImage img = src;
    if (img.width() > 1024 || img.height() > 1024)
        img = img.scaled(1024, 1024, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
    bool anyAlpha = false, colorful = false;
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = qAlpha(row[x]);
            if (a < 250) anyAlpha = true;
            if (a > 8 && (std::abs(qRed(row[x]) - qGreen(row[x])) > 8 ||
                          std::abs(qRed(row[x]) - qBlue(row[x])) > 8 ||
                          std::abs(qGreen(row[x]) - qBlue(row[x])) > 8))
                colorful = true;
        }
    }
    tip->w = std::uint32_t(w);
    tip->h = std::uint32_t(h);
    tip->color = anyAlpha && colorful;
    tip->alpha.assign(std::size_t(w) * h, 0.0f);
    if (tip->color) {
        tip->red.assign(std::size_t(w) * h, 0.0f);
        tip->green.assign(std::size_t(w) * h, 0.0f);
        tip->blue.assign(std::size_t(w) * h, 0.0f);
    }
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const std::size_t i = std::size_t(y) * std::size_t(w) + x;
            const int r = qRed(row[x]), g = qGreen(row[x]),
                      b = qBlue(row[x]), a = qAlpha(row[x]);
            tip->alpha[i] = anyAlpha ? float(a) / 255.0f
                                     : 1.0f - (0.299f * r + 0.587f * g +
                                               0.114f * b) / 255.0f;
            if (tip->color) {
                tip->red[i] = float(r) / 255.0f;
                tip->green[i] = float(g) / 255.0f;
                tip->blue[i] = float(b) / 255.0f;
            }
        }
    }
    tip->sanitize();
    return tip->valid();
}

// Write a tip as PNG under the brushes dir; returns its file name (the id
// presets and the stamp library both key on), empty on failure.
QString storeStampTip(const QString& baseName,
                      const pittore::compute::StampTip& tip) {
    QDir().mkpath(brushlibrary::brushTipsDir());
    const QString path = uniquePath(brushlibrary::brushTipsDir(),
                                    safeBaseName(baseName, "tip"),
                                    QStringLiteral(".png"));
    if (path.isEmpty()) return QString();
    QImage out(int(tip.w), int(tip.h),
               tip.color ? QImage::Format_ARGB32 : QImage::Format_Grayscale8);
    for (std::uint32_t y = 0; y < tip.h; ++y) {
        for (std::uint32_t x = 0; x < tip.w; ++x) {
            const std::size_t i = std::size_t(y) * tip.w + x;
            if (tip.color) {
                out.setPixel(int(x), int(y),
                             qRgba(int(tip.red[i] * 255.0f),
                                   int(tip.green[i] * 255.0f),
                                   int(tip.blue[i] * 255.0f),
                                   int(tip.alpha[i] * 255.0f)));
            } else {
                // Grayscale8 setPixel goes through an empty colour table, so
                // the value has to land in the scanline directly.
                out.scanLine(int(y))[int(x)] =
                    std::uint8_t(tip.alpha[i] * 255.0f);
            }
        }
    }
    if (!out.save(path, "PNG")) return QString();
    return QFileInfo(path).fileName();
}

// QImage -> the brush grain tile (grayscale luma thinned by alpha), capped
// the way repeating tiles are: grain repeats, so it needs no more.
bool patternFromImage(const QImage& src, AppState::PatternGray* pat) {
    QImage img = src;
    if (img.width() > 512 || img.height() > 512)
        img = img.scaled(512, 512, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    img = img.convertToFormat(QImage::Format_ARGB32);
    const int w = img.width(), h = img.height();
    if (w <= 0 || h <= 0 || w > 1024 || h > 1024) return false;
    pat->w = std::uint32_t(w);
    pat->h = std::uint32_t(h);
    pat->gray.assign(std::size_t(w) * h, 1.0f);
    for (int y = 0; y < h; ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float luma =
                (0.299f * qRed(row[x]) + 0.587f * qGreen(row[x]) +
                 0.114f * qBlue(row[x])) / 255.0f;
            pat->gray[std::size_t(y) * w + x] =
                luma * (qAlpha(row[x]) / 255.0f);
        }
    }
    return pat->valid();
}

// ---------------------------------------------------------------------------
// Content-Aware Fill: grow the surroundings over the selection
// ---------------------------------------------------------------------------

void diffuseFill(pittore::Image& img, const QImage& cov,
                 const QPointF& offset, double sx, double sy,
                 const QRect& clip) {
    const int w = clip.width(), h = clip.height();
    const int pw = int(img.width()), ph = int(img.height());
    if (w <= 0 || h <= 0 || pw == 0 || ph == 0) return;

    // Work on a coarsened copy: growth and relaxation run in the small, the
    // result is sampled back at full resolution. A document-sized hole then
    // costs tens of milliseconds instead of seconds.
    const int step = std::max(1, std::max(w, h) / 320 + 1);
    const int sw = (w + step - 1) / step;
    const int sh = (h + step - 1) / step;
    const std::size_t n = std::size_t(sw) * sh;

    auto premulAt = [&](int dx, int dy) {
        pittore::RGBAf out{0.0f, 0.0f, 0.0f, 0.0f};
        int lx = 0, ly = 0;
        if (layerTexel(offset, sx, sy, dx, dy, pw, ph, &lx, &ly)) {
            const pittore::RGBAf& s = img.at(std::uint32_t(lx),
                                             std::uint32_t(ly));
            out.r = s.r * s.a;
            out.g = s.g * s.a;
            out.b = s.b * s.a;
            out.a = s.a;
        }
        return out;
    };

    std::vector<pittore::RGBAf> col(n);
    std::vector<unsigned char> hole(n, 0);
    std::vector<unsigned char> grown(n, 0);
    for (int j = 0; j < sh; ++j) {
        const int dy = clip.top() + std::min(h - 1, j * step + step / 2);
        const uchar* mrow = cov.constScanLine(dy);
        for (int i = 0; i < sw; ++i) {
            const int dx = clip.left() + std::min(w - 1, i * step + step / 2);
            const std::size_t k = std::size_t(j) * sw + i;
            col[k] = premulAt(dx, dy);
            // The hole is what the selection covers; everything around it is
            // the donor and never moves.
            if (mrow[dx] >= 128) {
                hole[k] = 1;
            } else {
                grown[k] = 1;
            }
        }
    }

    // Seed: every hole cell touching a donor takes the mean of those donor
    // cells, then colour walks outward one ring at a time.
    std::deque<std::size_t> queue;
    auto at = [&](int i, int j) -> std::size_t {
        return std::size_t(j) * sw + i;
    };
    for (int j = 0; j < sh; ++j) {
        for (int i = 0; i < sw; ++i) {
            const std::size_t k = at(i, j);
            if (!hole[k] || grown[k]) continue;
            pittore::RGBAf sum{0.0f, 0.0f, 0.0f, 0.0f};
            int count = 0;
            for (int dj = -1; dj <= 1; ++dj) {
                const int jj = j + dj;
                if (jj < 0 || jj >= sh) continue;
                for (int di = -1; di <= 1; ++di) {
                    const int ii = i + di;
                    if (ii < 0 || ii >= sw || (!di && !dj)) continue;
                    const std::size_t k2 = at(ii, jj);
                    if (hole[k2]) continue;
                    sum.r += col[k2].r;
                    sum.g += col[k2].g;
                    sum.b += col[k2].b;
                    sum.a += col[k2].a;
                    ++count;
                }
            }
            if (count == 0) continue;
            col[k] = pittore::RGBAf{sum.r / count, sum.g / count,
                                    sum.b / count, sum.a / count};
            grown[k] = 1;
            queue.push_back(k);
        }
    }
    while (!queue.empty()) {
        const std::size_t k = queue.front();
        queue.pop_front();
        const int i = int(k % sw);
        const int j = int(k / sw);
        for (int dj = -1; dj <= 1; ++dj) {
            const int jj = j + dj;
            if (jj < 0 || jj >= sh) continue;
            for (int di = -1; di <= 1; ++di) {
                const int ii = i + di;
                if (ii < 0 || ii >= sw || (!di && !dj)) continue;
                const std::size_t k2 = at(ii, jj);
                if (!hole[k2] || grown[k2]) continue;
                col[k2] = col[k];
                grown[k2] = 1;
                queue.push_back(k2);
            }
        }
    }

    // Relax: growth arrives as rings, and a handful of Jacobi passes over the
    // hole blends them back into one continuous field.
    std::vector<pittore::RGBAf> scratch = col;
    for (int pass = 0; pass < 32; ++pass) {
        for (int j = 0; j < sh; ++j) {
            for (int i = 0; i < sw; ++i) {
                const std::size_t k = at(i, j);
                if (!hole[k]) continue;
                pittore::RGBAf sum{0.0f, 0.0f, 0.0f, 0.0f};
                int count = 0;
                for (int dj = -1; dj <= 1; ++dj) {
                    const int jj = j + dj;
                    if (jj < 0 || jj >= sh) continue;
                    for (int di = -1; di <= 1; ++di) {
                        const int ii = i + di;
                        if (ii < 0 || ii >= sw) continue;
                        const std::size_t k2 = at(ii, jj);
                        sum.r += col[k2].r;
                        sum.g += col[k2].g;
                        sum.b += col[k2].b;
                        sum.a += col[k2].a;
                        ++count;
                    }
                }
                if (count == 0) continue;
                scratch[k] = pittore::RGBAf{sum.r / count, sum.g / count,
                                            sum.b / count, sum.a / count};
            }
        }
        col.swap(scratch);
    }

    // Land it: bilinear sample of the grown field, blended into the layer by
    // the selection's own soft coverage.
    pittore::RGBAf* px = img.data();
    for (int y = 0; y < h; ++y) {
        const uchar* mrow = cov.constScanLine(clip.top() + y);
        for (int x = 0; x < w; ++x) {
            const int c = mrow[clip.left() + x];
            if (!c) continue;
            const float t = c / 255.0f;
            const float fx = std::min(float(sw) - 1.0f,
                                      std::max(0.0f, (x + 0.5f) / step - 0.5f));
            const float fy = std::min(float(sh) - 1.0f,
                                      std::max(0.0f, (y + 0.5f) / step - 0.5f));
            const int i0 = int(fx), j0 = int(fy);
            const int i1 = std::min(sw - 1, i0 + 1);
            const int j1 = std::min(sh - 1, j0 + 1);
            const float tx = fx - i0, ty = fy - j0;
            auto mix = [&](int i, int j) { return at(i, j); };
            const pittore::RGBAf& a = col[mix(i0, j0)];
            const pittore::RGBAf& b = col[mix(i1, j0)];
            const pittore::RGBAf& c2 = col[mix(i0, j1)];
            const pittore::RGBAf& d = col[mix(i1, j1)];
            const auto lerp2 = [](const pittore::RGBAf& p,
                                  const pittore::RGBAf& q, float u) {
                return pittore::RGBAf{p.r + (q.r - p.r) * u,
                                      p.g + (q.g - p.g) * u,
                                      p.b + (q.b - p.b) * u,
                                      p.a + (q.a - p.a) * u};
            };
            const pittore::RGBAf top = lerp2(a, b, tx);
            const pittore::RGBAf bot = lerp2(c2, d, tx);
            const pittore::RGBAf pm = lerp2(top, bot, ty);
            pittore::RGBAf fill = pm;
            if (fill.a > 0.0f) {
                fill.r /= fill.a;
                fill.g /= fill.a;
                fill.b /= fill.a;
            }

            const int dx = clip.left() + x, dy = clip.top() + y;
            int lx = 0, ly = 0;
            pittore::RGBAf* dst =
                layerTexel(offset, sx, sy, dx, dy, pw, ph, &lx, &ly)
                    ? &px[std::size_t(ly) * pw + lx]
                    : nullptr;
            if (!dst) continue;
            if (t >= 1.0f) {
                *dst = fill;
            } else {
                dst->r = dst->r + (fill.r - dst->r) * t;
                dst->g = dst->g + (fill.g - dst->g) * t;
                dst->b = dst->b + (fill.b - dst->b) * t;
                dst->a = dst->a + (fill.a - dst->a) * t;
            }
        }
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// History
// ---------------------------------------------------------------------------

// Toggle Last State: show what the document looked like before the newest
// step, and put it back on the next press (redo tail does the returning).
void MainWindow::toggleLastState() {
    if (state_->canRedo()) {
        const QString name = state_->redoStepName();
        state_->redo();
        state_->setStatusHint(tr("Restored \"%1\".").arg(name));
    } else if (state_->canUndo()) {
        const QString name = state_->undoStepName();
        state_->undo();
        state_->setStatusHint(
            tr("Hid \"%1\" — Toggle Last State again brings it back.").arg(name));
    } else {
        state_->setStatusHint(
            tr("Nothing to toggle: this document has no history yet."));
    }
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

// Cut = Copy, then erase the selected pixels — one step, the erase. The
// erase runs here through the one-shot edit path so a marquee selection
// cuts as cleanly as a lasso one.
void MainWindow::cutSelection() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Cut needs an open document."));
        return;
    }
    const QImage cov = selectionAsMask(*doc);
    const QRect clip =
        selectionMaskBbox(cov).intersected(fullDocRect(*doc));
    if (clip.isEmpty()) {
        state_->setStatusHint(tr("Cut needs a selection."));
        return;
    }
    QRectF rect;
    const QImage copy = maskedSelectionCopy(&rect);
    if (copy.isNull()) {
        state_->setStatusHint(tr("Cut needs an unlocked pixel layer."));
        return;
    }
    const LayerItem* layer = state_->activeLayer();
    if (!layer) {
        state_->setStatusHint(tr("Cut needs an unlocked pixel layer."));
        return;
    }
    const QPointF offset = layer->offset;
    const double sx = std::max(layer->scaleX, 1e-6);
    const double sy = std::max(layer->scaleY, 1e-6);
    if (!state_->applyLayerEditOneShot(
            [&](pittore::Image& img) {
                eraseCovered(img, cov, offset, sx, sy, clip);
            },
            tr("Cut"), QStringLiteral("eraser"))) {
        state_->setStatusHint(tr("Cut needs an unlocked pixel layer."));
        return;
    }
    clipboardImage_ = copy;
    clipboardRect_ = rect;
    clipboardLabel_ = tr("Clipboard") +
                      QStringLiteral(" (%1×%2)")
                          .arg(clipboardImage_.width())
                          .arg(clipboardImage_.height());
    state_->setStatusHint(tr("Cut %1×%2 px to the clipboard.")
                              .arg(clipboardImage_.width())
                              .arg(clipboardImage_.height()));
}

// Copy Merged: the flattened composite, clipped to the selection — what the
// eye sees, not just the active layer.
void MainWindow::copyMerged() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc || doc->composite.isNull() || doc->size.isEmpty()) {
        state_->setStatusHint(tr("Copy Merged needs an open document."));
        return;
    }
    const QRect full = fullDocRect(*doc);
    const QImage cov = selectionAsMask(*doc);
    const bool clipped = !selectionMaskBbox(cov).isEmpty();
    QRect clip = clipped ? selectionMaskBbox(cov) : full;
    clip = clip.intersected(full);
    if (clip.isEmpty()) {
        state_->setStatusHint(tr("Copy Merged needs an open document."));
        return;
    }
    QImage out = doc->composite.convertToFormat(QImage::Format_ARGB32).copy(clip);
    if (out.isNull()) {
        state_->setStatusHint(tr("Copy Merged needs an open document."));
        return;
    }
    if (clipped) {
        for (int y = 0; y < clip.height(); ++y) {
            const uchar* mrow = cov.constScanLine(clip.top() + y);
            QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
            for (int x = 0; x < clip.width(); ++x) {
                const QRgb p = row[x];
                row[x] = qRgba(qRed(p), qGreen(p), qBlue(p),
                               qAlpha(p) * mrow[clip.left() + x] / 255);
            }
        }
    }
    clipboardImage_ = out;
    clipboardRect_ = QRectF(clip);
    clipboardLabel_ = tr("Clipboard") +
                      QStringLiteral(" (%1×%2)")
                          .arg(clipboardImage_.width())
                          .arg(clipboardImage_.height());
    state_->setStatusHint(tr("Copied %1×%2 px of the merged image to the clipboard.")
                              .arg(clipboardImage_.width())
                              .arg(clipboardImage_.height()));
}

// Paste Into / Paste Outside: the clipboard lands at its own spot as a new
// pixel layer whose alpha is the selection (or its complement), so only the
// chosen side of the marquee shows the pasted content. One step either way.
void MainWindow::pasteMaskedToSelection(bool inside) {
    const QString mode = inside ? tr("Paste Into") : tr("Paste Outside");
    DocumentItem* doc = state_->activeDocument();
    if (clipboardImage_.isNull()) {
        state_->setStatusHint(tr("Nothing on the clipboard — Copy first."));
        return;
    }
    if (!doc || doc->size.isEmpty()) {
        state_->setStatusHint(tr("%1 needs an open document.").arg(mode));
        return;
    }
    const QImage cov = selectionAsMask(*doc);
    if (selectionMaskBbox(cov).isEmpty()) {
        state_->setStatusHint(tr("%1 needs a selection.").arg(mode));
        return;
    }
    QImage img = clipboardImage_.convertToFormat(QImage::Format_ARGB32);
    QPointF origin = clipboardRect_.topLeft();
    if (clipboardRect_.isEmpty())
        origin = QPointF(doc->size.width() * 0.5 - img.width() * 0.5,
                         doc->size.height() * 0.5 - img.height() * 0.5);
    const QPoint tl(int(std::floor(origin.x())), int(std::floor(origin.y())));
    const QRect full = fullDocRect(*doc);
    for (int y = 0; y < img.height(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
        const int dy = tl.y() + y;
        for (int x = 0; x < img.width(); ++x) {
            const int dx = tl.x() + x;
            // Outside the document there is no coverage to read: "into"
            // drops the pixels, "outside" keeps them.
            const int c = full.contains(QPoint(dx, dy))
                              ? cov.constScanLine(dy)[dx]
                              : 0;
            const int keep = inside ? c : 255 - c;
            if (keep == 255) continue;
            const QRgb p = row[x];
            row[x] = qRgba(qRed(p), qGreen(p), qBlue(p),
                           qAlpha(p) * keep / 255);
        }
    }
    state_->addPixelLayerFromImage(img, tr("Pasted Layer"), QPointF(tl), 1.0,
                                   1.0, mode, QStringLiteral("place"));
    state_->setStatusHint(inside ? tr("Pasted the clipboard inside the selection.")
                                 : tr("Pasted the clipboard outside the selection."));
}

// ---------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------

// Find and Replace Text: one pass over the live text layers (their spec is
// the source of truth, refreshTextLayer re-renders), one undo step for the
// whole run.
void MainWindow::findReplaceTextDialog() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Find and Replace Text needs an open document."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Find and Replace Text"));
    auto* form = new QFormLayout(&dialog);
    auto* find = new QLineEdit(&dialog);
    find->setPlaceholderText(tr("Text to find"));
    form->addRow(tr("Find:"), find);
    auto* replace = new QLineEdit(&dialog);
    replace->setPlaceholderText(tr("Leave empty to remove it"));
    form->addRow(tr("Replace with:"), replace);
    auto* note = new QLabel(
        tr("Every editable text layer in the document is searched."), &dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Replace All"));
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    const QString needle = find->text();
    if (needle.isEmpty()) {
        state_->setStatusHint(tr("Type the text to find first."));
        return;
    }
    int editable = 0, hits = 0;
    state_->beginUndoStep();
    for (int i = 0; i < doc->layers.size(); ++i) {
        LayerItem& layer = doc->layers[i];
        if (!layer.liveText) continue;
        ++editable;
        if (!layer.textSpec.text.contains(needle)) continue;
        layer.textSpec.text.replace(needle, replace->text());
        if (state_->refreshTextLayer(i)) ++hits;
    }
    if (hits > 0)
        state_->commitUndoStep(tr("Find and Replace"),
                               QStringLiteral("type"));
    else
        state_->discardUndoStep();
    if (hits > 0) {
        state_->setStatusHint(
            tr("Replaced the text in %1 layer%2.")
                .arg(hits)
                .arg(hits == 1 ? QString() : QStringLiteral("s")));
    } else if (editable == 0) {
        state_->setStatusHint(
            tr("No editable text layers — add one with the Type tool."));
    } else {
        state_->setStatusHint(tr("No text layer contains \"%1\".").arg(needle));
    }
}

// ---------------------------------------------------------------------------
// Fill / Stroke / Content-Aware Fill
// ---------------------------------------------------------------------------

void MainWindow::fillDialog() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Fill needs an open document."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Fill"));
    auto* form = new QFormLayout(&dialog);
    auto* note = new QLabel(
        tr("Paints the selection on the active pixel layer; with no "
           "selection the whole layer is filled."),
        &dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* source = new QComboBox(&dialog);
    source->addItem(tr("Foreground Color"), 0);
    source->addItem(tr("Background Color"), 1);
    source->addItem(tr("Black"), 2);
    source->addItem(tr("White"), 3);
    source->addItem(tr("50% Gray"), 4);
    form->addRow(tr("Use:"), source);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;

    QColor color;
    switch (source->currentData().toInt()) {
        case 0: color = state_->foreground(); break;
        case 1: color = state_->background(); break;
        case 2: color = QColor(0, 0, 0); break;
        case 3: color = QColor(255, 255, 255); break;
        default: color = QColor(128, 128, 128); break;
    }
    if (!state_->fillActiveSelectionWithColor(color, tr("Fill")))
        state_->setStatusHint(tr("Fill needs an unlocked pixel layer."));
}

void MainWindow::strokeSelectionDialog() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Stroke needs an open document."));
        return;
    }
    if (!hasSelection(*doc)) {
        state_->setStatusHint(tr("Stroke needs a selection."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Stroke"));
    auto* form = new QFormLayout(&dialog);
    auto* note = new QLabel(
        tr("Draws the selection outline on the active pixel layer, centred "
           "on the marquee."),
        &dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* width = new QSpinBox(&dialog);
    width->setRange(1, 200);
    width->setValue(3);
    width->setSuffix(QStringLiteral(" px"));
    form->addRow(tr("Width:"), width);
    QColor chosen = state_->foreground();
    auto* color = new QPushButton(&dialog);
    auto paintButton = [color](const QColor& c) {
        color->setStyleSheet(
            QStringLiteral("background:%1;color:%2;")
                .arg(c.name(QColor::HexRgb),
                     c.lightness() > 128 ? QStringLiteral("#000000")
                                         : QStringLiteral("#ffffff")));
        color->setText(c.name(QColor::HexRgb).toUpper());
    };
    paintButton(chosen);
    connect(color, &QPushButton::clicked, &dialog, [&, paintButton] {
        const QColor picked =
            QColorDialog::getColor(chosen, &dialog, tr("Stroke Color"));
        if (picked.isValid()) {
            chosen = picked;
            paintButton(chosen);
        }
    });
    form->addRow(tr("Color:"), color);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    strokeSelectionOutline(chosen, width->value());
}

// The stroke itself: a band around the marquee's 50% contour, painted in
// one shot. The band comes from a chamfer distance-to-the-other-side pass,
// so it follows a feathered outline as readily as a rectangular one.
void MainWindow::strokeSelectionOutline(const QColor& color, int widthPx) {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) return;
    const QImage cov = selectionAsMask(*doc);
    const QRect bounds = selectionMaskBbox(cov);
    if (bounds.isEmpty()) {
        state_->setStatusHint(tr("Stroke needs a selection."));
        return;
    }
    const LayerItem* layer = state_->activeLayer();
    if (!layer || layer->locked || layer->kind != LayerItem::Kind::Pixel ||
        !layer->pixels) {
        state_->setStatusHint(tr("Stroke needs an unlocked pixel layer."));
        return;
    }
    const QRect layerBoundsDoc = layerDocBounds(*layer).toAlignedRect();
    if (!layerBoundsDoc.intersects(fullDocRect(*doc))) {
        state_->setStatusHint(
            tr("Stroke: the active layer does not reach the selection."));
        return;
    }
    const QPointF offset = layer->offset;
    const double sx = std::max(layer->scaleX, 1e-6);
    const double sy = std::max(layer->scaleY, 1e-6);

    const QRect region = bounds.adjusted(-widthPx - 2, -widthPx - 2,
                                         widthPx + 2, widthPx + 2)
                             .intersected(fullDocRect(*doc));
    const int w = region.width(), h = region.height();
    if (w <= 0 || h <= 0) return;
    auto insideAt = [&](int i, int j) {
        return cov.constScanLine(region.top() + j)[region.left() + i] >= 128;
    };
    // Chamfer distance (1 / 1.414 weights) from each texel to the nearest
    // texel on the other side of the contour.
    const float diag = 1.41421356f;
    auto distanceToOtherSide = [&](bool fromOutside) {
        std::vector<float> d(std::size_t(w) * h, 1e9f);
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                const bool in = insideAt(i, j);
                if (in != fromOutside) d[std::size_t(j) * w + i] = 0.0f;
            }
        for (int j = 0; j < h; ++j)
            for (int i = 0; i < w; ++i) {
                float& v = d[std::size_t(j) * w + i];
                if (j > 0) v = std::min(v, d[std::size_t(j - 1) * w + i] + 1.0f);
                if (i > 0) v = std::min(v, d[std::size_t(j) * w + i - 1] + 1.0f);
                if (j > 0 && i > 0)
                    v = std::min(v, d[std::size_t(j - 1) * w + i - 1] + diag);
                if (j > 0 && i + 1 < w)
                    v = std::min(v, d[std::size_t(j - 1) * w + i + 1] + diag);
            }
        for (int j = h - 1; j >= 0; --j)
            for (int i = w - 1; i >= 0; --i) {
                float& v = d[std::size_t(j) * w + i];
                if (j + 1 < h)
                    v = std::min(v, d[std::size_t(j + 1) * w + i] + 1.0f);
                if (i + 1 < w)
                    v = std::min(v, d[std::size_t(j) * w + i + 1] + 1.0f);
                if (j + 1 < h && i + 1 < w)
                    v = std::min(v, d[std::size_t(j + 1) * w + i + 1] + diag);
                if (j + 1 < h && i > 0)
                    v = std::min(v, d[std::size_t(j + 1) * w + i - 1] + diag);
            }
        return d;
    };
    const std::vector<float> dOut = distanceToOtherSide(false);
    const std::vector<float> dIn = distanceToOtherSide(true);
    const float half = widthPx * 0.5f + 0.5f;

    const bool ok = state_->applyLayerEditOneShot(
        [&](pittore::Image& img) {
            const int pw = int(img.width());
            const int ph = int(img.height());
            pittore::RGBAf* px = img.data();
            for (int j = 0; j < h; ++j) {
                for (int i = 0; i < w; ++i) {
                    const std::size_t k = std::size_t(j) * w + i;
                    const float dist = insideAt(i, j) ? dOut[k] : dIn[k];
                    const float t = half - dist;
                    if (!(t > 0.0f)) continue;
                    int lx = 0, ly = 0;
                    if (!layerTexel(offset, sx, sy, region.left() + i,
                                    region.top() + j, pw, ph, &lx, &ly))
                        continue;
                    paintOver(px[std::size_t(ly) * pw + lx], color,
                              std::min(t, 1.0f));
                }
            }
        },
        tr("Stroke"), QStringLiteral("stroke-width"));
    if (!ok) {
        state_->setStatusHint(tr("Stroke needs an unlocked pixel layer."));
        return;
    }
    state_->setStatusHint(tr("Stroked the selection outline at %1 px.")
                              .arg(widthPx));
}

void MainWindow::contentAwareFill() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Content-Aware Fill needs an open document."));
        return;
    }
    const QImage cov = selectionAsMask(*doc);
    const QRect hole = selectionMaskBbox(cov).intersected(fullDocRect(*doc));
    if (hole.isEmpty()) {
        state_->setStatusHint(tr("Content-Aware Fill needs a selection."));
        return;
    }
    const LayerItem* layer = state_->activeLayer();
    if (!layer || layer->locked || layer->kind != LayerItem::Kind::Pixel ||
        !layer->pixels) {
        state_->setStatusHint(
            tr("Content-Aware Fill needs an unlocked pixel layer."));
        return;
    }
    const QPointF offset = layer->offset;
    const double sx = std::max(layer->scaleX, 1e-6);
    const double sy = std::max(layer->scaleY, 1e-6);
    if (!layerDocBounds(*layer).intersects(QRectF(hole))) {
        state_->setStatusHint(
            tr("Content-Aware Fill: the active layer does not reach the "
               "selection."));
        return;
    }
    if (!state_->applyLayerEditOneShot(
            [&](pittore::Image& img) {
                diffuseFill(img, cov, offset, sx, sy, hole);
            },
            tr("Content-Aware Fill"), QStringLiteral("heal"))) {
        state_->setStatusHint(
            tr("Content-Aware Fill needs an unlocked pixel layer."));
        return;
    }
    state_->setStatusHint(
        tr("Filled the selection by growing the surrounding pixels into it."));
}

// ---------------------------------------------------------------------------
// Transform submenu
// ---------------------------------------------------------------------------

// The submenu is label-keyed: buildEditMenu owns the strings, so matching
// against the same tr() calls keeps the menu file down to one dispatch.
void MainWindow::transformMenuCommand(const QString& label) {
    if (label == tr("Again")) {
        transformAgain();
    } else if (label == tr("Scale")) {
        scaleLayerDialog();
    } else if (label == tr("Rotate")) {
        rotateLayerDialog();
    } else if (label == tr("Warp")) {
        // The build's mesh warp: a live session over the active layer.
        liquifyDialog();
    } else if (label == tr("Skew") || label == tr("Distort") ||
               label == tr("Perspective")) {
        // These need corner-by-corner handles driven from the canvas; this
        // build's document model stores only offset + scale per layer, so
        // there is nothing here to drag or bake.
        state_->setStatusHint(
            tr("%1 needs interactive transform handles, which this build "
               "does not have.")
                .arg(label));
    } else if (label == tr("Rotate 180°")) {
        applyRotateTransform(180.0);
    } else if (label == tr("Rotate 90° Clockwise")) {
        applyRotateTransform(90.0);
    } else if (label == tr("Rotate 90° Counter Clockwise")) {
        applyRotateTransform(-90.0);
    } else if (label == tr("Flip Horizontal")) {
        applyFlipTransform(true);
    } else if (label == tr("Flip Vertical")) {
        applyFlipTransform(false);
    } else {
        state_->setStatusHint(tr("%1 is not available.").arg(label));
    }
}

void MainWindow::transformAgain() {
    if (!lastTransform_) {
        state_->setStatusHint(
            tr("Nothing to repeat — Again repeats the last rotation, flip "
               "or scale from this menu."));
        return;
    }
    lastTransform_();
}

void MainWindow::applyRotateTransform(double degrees) {
    if (std::fabs(degrees) < 0.01) {
        state_->setStatusHint(tr("Enter a non-zero angle to rotate."));
        return;
    }
    if (!state_->rotateActiveLayer(degrees)) {
        state_->setStatusHint(tr("Rotate needs an unlocked pixel layer."));
        return;
    }
    lastTransform_ = [this, degrees] { applyRotateTransform(degrees); };
    state_->setStatusHint(
        tr("Rotated the active layer %1°.").arg(degrees));
}

void MainWindow::applyFlipTransform(bool horizontal) {
    const LayerItem* layer = state_->activeLayer();
    if (!layer || layer->locked || layer->kind != LayerItem::Kind::Pixel ||
        !layer->pixels) {
        state_->setStatusHint(
            horizontal ? tr("Flip Horizontal needs an unlocked pixel layer.")
                       : tr("Flip Vertical needs an unlocked pixel layer."));
        return;
    }
    const bool ok = state_->applyLayerEditOneShot(
        [this, horizontal](pittore::Image& img) {
            const int w = int(img.width()), h = int(img.height());
            pittore::RGBAf* px = img.data();
            if (horizontal) {
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w / 2; ++x)
                        std::swap(px[std::size_t(y) * w + x],
                                  px[std::size_t(y) * w + (w - 1 - x)]);
            } else {
                for (int y = 0; y < h / 2; ++y)
                    for (int x = 0; x < w; ++x)
                        std::swap(px[std::size_t(y) * w + x],
                                  px[std::size_t(h - 1 - y) * w + x]);
            }
            // A linked mask lives in the same native grid; it has to follow
            // the pixels. The copy-on-write detaches the layer row, so the
            // mask is cloned (undo snapshots share it) before it is flipped.
            LayerItem* live = state_->activeLayer();
            if (live && live->mask &&
                int(live->mask->width()) == w &&
                int(live->mask->height()) == h) {
                pittore::Image mask = live->mask->clone();
                pittore::RGBAf* mp = mask.data();
                if (horizontal) {
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w / 2; ++x)
                            std::swap(mp[std::size_t(y) * w + x],
                                      mp[std::size_t(y) * w + (w - 1 - x)]);
                } else {
                    for (int y = 0; y < h / 2; ++y)
                        for (int x = 0; x < w; ++x)
                            std::swap(mp[std::size_t(y) * w + x],
                                      mp[std::size_t(h - 1 - y) * w + x]);
                }
                live->mask = std::make_shared<pittore::Image>(std::move(mask));
                ++live->maskStamp;
            }
        },
        horizontal ? tr("Flip Horizontal") : tr("Flip Vertical"),
        QStringLiteral("point-xform"));
    if (!ok) {
        state_->setStatusHint(
            horizontal ? tr("Flip Horizontal needs an unlocked pixel layer.")
                       : tr("Flip Vertical needs an unlocked pixel layer."));
        return;
    }
    lastTransform_ = [this, horizontal] { applyFlipTransform(horizontal); };
    state_->setStatusHint(
        horizontal ? tr("Flipped the active layer horizontally.")
                   : tr("Flipped the active layer vertically."));
}

// Scale about the layer's own centre: the placement is the only transform
// this document model keeps per layer, so no resampling is involved.
void MainWindow::applyScaleTransform(double fx, double fy) {
    const LayerItem* layer = state_->activeLayer();
    if (!layer || layer->locked || layer->kind != LayerItem::Kind::Pixel ||
        !layer->pixels) {
        state_->setStatusHint(tr("Scale needs an unlocked pixel layer."));
        return;
    }
    const double sx = layer->scaleX, sy = layer->scaleY;
    const double nsx = sx * fx, nsy = sy * fy;
    const QPointF centre(layer->offset.x() + layer->pixels->width() * sx * 0.5,
                          layer->offset.y() + layer->pixels->height() * sy * 0.5);
    const QPointF next(centre.x() - layer->pixels->width() * nsx * 0.5,
                        centre.y() - layer->pixels->height() * nsy * 0.5);
    if (next == layer->offset && qFuzzyCompare(nsx, sx) &&
        qFuzzyCompare(nsy, sy)) {
        state_->setStatusHint(tr("Nothing to scale: the layer is already at that size."));
        return;
    }
    state_->beginUndoStep();
    if (!state_->setActiveLayerPlacement(next, nsx, nsy)) {
        state_->discardUndoStep();
        state_->setStatusHint(tr("Scale needs an unlocked pixel layer."));
        return;
    }
    state_->commitUndoStep(tr("Scale"), QStringLiteral("move"));
    lastTransform_ = [this, fx, fy] { applyScaleTransform(fx, fy); };
    state_->setStatusHint(tr("Scaled the active layer to %1% × %2%.")
                              .arg(fx * 100.0, 0, 'g', 4)
                              .arg(fy * 100.0, 0, 'g', 4));
}

void MainWindow::scaleLayerDialog() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Scale needs an open document."));
        return;
    }
    const LayerItem* layer = state_->activeLayer();
    if (!layer || layer->locked || layer->kind != LayerItem::Kind::Pixel ||
        !layer->pixels) {
        state_->setStatusHint(tr("Scale needs an unlocked pixel layer."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Scale"));
    auto* form = new QFormLayout(&dialog);
    auto* note = new QLabel(
        tr("Scales the active layer about its own centre; the layer's "
           "pixels are resampled when it is composited."),
        &dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* width = new QDoubleSpinBox(&dialog);
    auto* height = new QDoubleSpinBox(&dialog);
    for (QDoubleSpinBox* box : {width, height}) {
        box->setRange(1.0, 800.0);
        box->setDecimals(1);
        box->setSingleStep(10.0);
        box->setSuffix(QStringLiteral(" %"));
        box->setValue(100.0);
    }
    form->addRow(tr("Width:"), width);
    form->addRow(tr("Height:"), height);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    applyScaleTransform(width->value() / 100.0, height->value() / 100.0);
}

void MainWindow::rotateLayerDialog() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Rotate needs an open document."));
        return;
    }
    const LayerItem* layer = state_->activeLayer();
    if (!layer || layer->locked || layer->kind != LayerItem::Kind::Pixel ||
        !layer->pixels) {
        state_->setStatusHint(tr("Rotate needs an unlocked pixel layer."));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(tr("Rotate"));
    auto* form = new QFormLayout(&dialog);
    auto* note = new QLabel(
        tr("Bakes the angle into the active layer's pixels, keeping its "
           "centre where it is."),
        &dialog);
    note->setWordWrap(true);
    form->addRow(note);
    auto* angle = new QDoubleSpinBox(&dialog);
    angle->setRange(-360.0, 360.0);
    angle->setDecimals(1);
    angle->setSingleStep(5.0);
    angle->setSuffix(QStringLiteral("°"));
    angle->setValue(15.0);
    form->addRow(tr("Angle:"), angle);
    auto* buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                             &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() != QDialog::Accepted) return;
    applyRotateTransform(angle->value());
}

// ---------------------------------------------------------------------------
// Auto-Align / Auto-Blend
// ---------------------------------------------------------------------------

// Bounds-based registration: every selected layer with ink of its own slides
// so its alpha centroid meets the reference layer's. Translation only — the
// same honest limit as the rest of the transform tooling — one undo step.
void MainWindow::autoAlignLayers() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Auto-Align Layers needs an open document."));
        return;
    }
    QVector<int> selected = state_->selectedLayerIndices();
    if (selected.size() < 2) {
        state_->setStatusHint(
            tr("Auto-Align Layers needs at least two selected layers."));
        return;
    }
    struct Match {
        int index;
        QPointF centre;
    };
    QVector<Match> matches;
    for (int index : selected) {
        if (index < 0 || index >= doc->layers.size()) continue;
        const LayerItem& layer = doc->layers.at(index);
        if (layer.kind != LayerItem::Kind::Pixel || !layer.pixels ||
            !layer.visible)
            continue;
        const QPointF centre = alphaCentre(layer);
        if (centre.isNull()) continue;
        matches.push_back({index, centre});
    }
    if (matches.size() < 2) {
        state_->setStatusHint(
            tr("Auto-Align Layers needs two layers with visible pixels."));
        return;
    }
    const int activeIndex = doc->activeLayer;
    int reference = matches.constLast().index;
    for (const Match& m : matches)
        if (m.index == activeIndex) reference = m.index;
    const QPointF target = [&] {
        for (const Match& m : matches)
            if (m.index == reference) return m.centre;
        return matches.constLast().centre;
    }();

    state_->beginUndoStep();
    int moved = 0;
    const int restore = activeIndex;
    for (const Match& m : matches) {
        if (m.index == reference) continue;
        const LayerItem& layer = doc->layers.at(m.index);
        const QPointF delta = target - m.centre;
        if (delta.isNull()) continue;
        if (!activateLayer(state_, m.index)) continue;
        if (state_->setActiveLayerPlacement(layer.offset + delta, layer.scaleX,
                                            layer.scaleY))
            ++moved;
    }
    activateLayer(state_, restore);
    if (moved == 0) {
        state_->discardUndoStep();
        state_->setStatusHint(
            tr("Auto-Align Layers found nothing to move."));
        return;
    }
    state_->commitUndoStep(tr("Auto-Align Layers"), QStringLiteral("move"));
    state_->setStatusHint(tr("Aligned %1 layer%2 to the reference layer's "
                             "centre of content.")
                              .arg(moved)
                              .arg(moved == 1 ? QString()
                                              : QStringLiteral("s")));
}

// The seam blend: the top of the two topmost selected layers cross-fades
// over the region it shares with the layer underneath, so their edges stop
// reading as a cut. One undo step.
void MainWindow::autoBlendLayers() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Auto-Blend Layers needs an open document."));
        return;
    }
    QVector<int> selected = state_->selectedLayerIndices();
    std::sort(selected.begin(), selected.end());
    int top = -1, below = -1;
    for (int i = selected.size() - 1; i >= 0; --i) {
        const int index = selected.at(i);
        if (index < 0 || index >= doc->layers.size()) continue;
        const LayerItem& layer = doc->layers.at(index);
        if (layer.kind != LayerItem::Kind::Pixel || !layer.pixels ||
            !layer.visible)
            continue;
        if (top < 0) {
            top = index;
        } else {
            below = index;
            break;
        }
    }
    if (top < 0 || below < 0) {
        state_->setStatusHint(
            tr("Auto-Blend Layers needs at least two visible pixel layers."));
        return;
    }
    const QRectF topBounds = layerDocBounds(doc->layers.at(top));
    const QRectF belowBounds = layerDocBounds(doc->layers.at(below));
    const QRectF overlap = topBounds.intersected(belowBounds);
    if (overlap.width() < 2.0 || overlap.height() < 2.0) {
        state_->setStatusHint(
            tr("Auto-Blend Layers needs two overlapping layers."));
        return;
    }
    // Only an edge of the top layer that cuts through the shared region can
    // show a seam; when the top layer is the one boxed in by the overlap its
    // own edge is exactly where the cross-fade has to start.
    const bool seamLeft = topBounds.left() >= belowBounds.left();
    const bool seamRight = topBounds.right() <= belowBounds.right();
    const bool seamTop = topBounds.top() >= belowBounds.top();
    const bool seamBottom = topBounds.bottom() <= belowBounds.bottom();
    if (!(seamLeft || seamRight || seamTop || seamBottom)) {
        state_->setStatusHint(tr("Auto-Blend Layers: the top layer already "
                                 "covers the overlap edge to edge."));
        return;
    }
    const double feather =
        qBound(1.0, std::min(overlap.width(), overlap.height()) * 0.5, 256.0);

    const LayerItem* layer = &doc->layers[top];
    const QPointF offset = layer->offset;
    const double sx = std::max(layer->scaleX, 1e-6);
    const double sy = std::max(layer->scaleY, 1e-6);
    const int restore = doc->activeLayer;
    if (!activateLayer(state_, top)) {
        state_->setStatusHint(tr("Auto-Blend Layers needs an unlocked layer."));
        return;
    }
    const QRect clip = overlap.toAlignedRect().intersected(fullDocRect(*doc));
    const bool ok = state_->applyLayerEditOneShot(
        [&](pittore::Image& img) {
            const int pw = int(img.width()), ph = int(img.height());
            pittore::RGBAf* px = img.data();
            for (int dy = clip.top(); dy <= clip.bottom(); ++dy) {
                for (int dx = clip.left(); dx <= clip.right(); ++dx) {
                    double d = 1e9;
                    if (seamLeft) d = std::min(d, dx - topBounds.left());
                    if (seamRight) d = std::min(d, topBounds.right() - dx);
                    if (seamTop) d = std::min(d, dy - topBounds.top());
                    if (seamBottom) d = std::min(d, topBounds.bottom() - dy);
                    if (!(d < feather)) continue;
                    int lx = 0, ly = 0;
                    if (!layerTexel(offset, sx, sy, dx, dy, pw, ph, &lx, &ly))
                        continue;
                    pittore::RGBAf& p = px[std::size_t(ly) * pw + lx];
                    p.a *= float(qBound(0.0, d / feather, 1.0));
                }
            }
        },
        tr("Auto-Blend Layers"), QStringLiteral("mixer"));
    activateLayer(state_, restore);
    if (!ok) {
        state_->setStatusHint(tr("Auto-Blend Layers needs an unlocked layer."));
        return;
    }
    state_->setStatusHint(
        tr("Cross-faded the top layer over %1 px of the overlap.")
            .arg(qRound(feather)));
}

// ---------------------------------------------------------------------------
// Define Brush Preset / Define Pattern
// ---------------------------------------------------------------------------

// Capture what the selection shows as a stamp tip and file it as a custom
// preset: the tip PNG lands in the brushes folder, the preset in the custom
// library, so the Brushes panel picks both up on its next load.
void MainWindow::defineBrushPreset() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(
            tr("Define Brush Preset needs an open document."));
        return;
    }
    QRectF rect;
    const QImage source = maskedSelectionCopy(&rect);
    if (source.isNull()) {
        state_->setStatusHint(
            tr("Define Brush Preset needs an unlocked pixel layer."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Define Brush Preset"), tr("Name:"), QLineEdit::Normal,
        tr("Custom Brush"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    pittore::compute::StampTip tip;
    if (!stampTipFromImage(source, &tip)) {
        state_->setStatusHint(
            tr("That selection could not be read as a brush tip."));
        return;
    }
    const QString file = storeStampTip(name.trimmed(), tip);
    if (file.isEmpty()) {
        state_->setStatusHint(
            tr("Could not write the brush tip into the brushes folder."));
        return;
    }
    state_->setBrushStamp(file, tip);

    brushlibrary::BrushPreset preset;
    preset.name = name.trimmed();
    preset.factory = false;
    preset.size = std::min(256.0, double(std::max(tip.w, tip.h)));
    preset.hardness = 100.0;
    preset.spacing = 15.0;
    preset.tipKind = QStringLiteral("stamp");
    preset.stampId = file;
    preset.stampMode = 0;  // alpha mask, tinted by the foreground
    auto customs = brushlibrary::loadCustomPresets();
    customs.erase(std::remove_if(customs.begin(), customs.end(),
                                 [&](const brushlibrary::BrushPreset& q) {
                                     return !q.factory && q.name == preset.name;
                                 }),
                  customs.end());
    customs.push_back(preset);
    brushlibrary::saveCustomPresets(customs);
    state_->setStatusHint(
        tr("Brush preset \"%1\" saved with %2×%3 px of tip.")
            .arg(preset.name)
            .arg(tip.w)
            .arg(tip.h));
}

// Capture the selection as a grain tile. Patterns in this build are brush
// grain, so a freshly defined one is also set as the active paint tool's
// texture — otherwise it would sit in the folder with nothing to pick it up.
void MainWindow::definePattern() {
    DocumentItem* doc = state_->activeDocument();
    if (!doc) {
        state_->setStatusHint(tr("Define Pattern needs an open document."));
        return;
    }
    QRectF rect;
    const QImage source = maskedSelectionCopy(&rect);
    if (source.isNull()) {
        state_->setStatusHint(
            tr("Define Pattern needs an unlocked pixel layer."));
        return;
    }
    AppState::PatternGray pattern;
    if (!patternFromImage(source, &pattern)) {
        state_->setStatusHint(
            tr("That selection could not be read as a pattern."));
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Define Pattern"), tr("Name:"), QLineEdit::Normal,
        tr("Custom Pattern"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QDir().mkpath(brushlibrary::brushPatternsDir());
    const QString path = uniquePath(brushlibrary::brushPatternsDir(),
                                    safeBaseName(name.trimmed(), "pattern"),
                                    QStringLiteral(".png"));
    if (path.isEmpty() ||
        !source.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
             .save(path, "PNG")) {
        state_->setStatusHint(
            tr("Could not write the pattern into the patterns folder."));
        return;
    }
    const QString file = QFileInfo(path).fileName();
    state_->setBrushPattern(file, std::move(pattern));

    // The grain id the paint engine reads for the active tool is the live
    // "brush_texture" option: hand it over so the new tile is in use, not
    // just on disk.
    const ToolId tool = state_->activeTool();
    const bool paintable = isPaintTool(tool);
    if (paintable) state_->setOption(tool, QStringLiteral("brush_texture"), file);
    state_->setStatusHint(
        paintable
            ? tr("Pattern \"%1\" saved and set as the active tool's grain.")
                  .arg(name.trimmed())
            : tr("Pattern \"%1\" saved to the patterns folder — pick it as a "
                 "brush texture once a paint tool is active.")
                  .arg(name.trimmed()));
}

// ---------------------------------------------------------------------------
// Purge and the honest refusals
// ---------------------------------------------------------------------------

void MainWindow::purgeClipboard() {
    const bool had = !clipboardImage_.isNull();
    clipboardImage_ = QImage();
    clipboardRect_ = QRectF();
    clipboardLabel_.clear();
    state_->setStatusHint(had ? tr("Clipboard purged.")
                              : tr("The clipboard was already empty."));
}

void MainWindow::spellCheckUnavailable() {
    state_->setStatusHint(
        tr("Spell checking needs a system dictionary, which this build does "
           "not provide."));
}

void MainWindow::generativeExpandUnavailable() {
    state_->setStatusHint(
        tr("Generative Expand needs a generative model, which this build "
           "does not bundle."));
}

void MainWindow::skyReplacementUnavailable() {
    state_->setStatusHint(
        tr("Sky Replacement needs a library of skies, which this build does "
           "not bundle."));
}

void MainWindow::defineCustomShapeUnavailable() {
    state_->setStatusHint(
        tr("Define Custom Shape needs a user shape library; this build's "
           "custom-shape gallery is a fixed set."));
}

}  // namespace pittore::ui
