// THROWAWAY: render user-SVG group thumbs to PNG for visual inspection.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cstdio>
#include <functional>

#include "engine/core/log.h"
#include "engine/vector/svg_dom.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/persona/vector_raster.h"
#include "ui/svg_bridge.h"
#include "ui/svg_parts.h"

using namespace pittore::ui;

static void stats(const QImage& t, const char* tag) {
    int opaque = 0, r = 0, g = 0, b = 0;
    for (int y = 0; y < t.height(); ++y)
        for (int x = 0; x < t.width(); ++x) {
            const QRgb p = t.pixel(x, y);
            if (qAlpha(p) > 8) {
                ++opaque;
                r += qRed(p);
                g += qGreen(p);
                b += qBlue(p);
            }
        }
    std::printf("  %s %dx%d opaque=%d mean=(%d,%d,%d)\n", tag, t.width(),
                t.height(), opaque,
                opaque ? r / opaque : 0, opaque ? g / opaque : 0,
                opaque ? b / opaque : 0);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-zz");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    AppState state;
    QFile f(QStringLiteral("/tmp/user-svg.svg"));
    if (!f.exists()) {
        std::printf("  SKIP: no /tmp/user-svg.svg fixture\n");
        return 0;
    }
    CHECK(f.open(QIODevice::ReadOnly));
    // TEMP-DIAG (revert): round-trip the glyph defs through parse/serialize.
    {
        QFile g(QStringLiteral("/tmp/glyph.svg"));
        if (g.open(QIODevice::ReadOnly)) {
            const std::string in = g.readAll().toStdString();
            const auto parsed = ::pittore::vector::parseSvgDom(in);
            std::printf("  domRT: ok=%d root=%s\n", parsed.ok,
                        parsed.doc.root ? parsed.doc.root->tag.c_str() : "(null)");
            if (parsed.doc.root) {
                const std::string s = ::pittore::vector::serializeSvgDom(parsed.doc);
                std::printf("  domRT bytes=%d/%d\n", (int)s.size(), (int)in.size());
                const auto pos = s.find("<path");
                if (pos != std::string::npos)
                    std::printf("  domRT path: %.300s\n", s.c_str() + pos);
            }
        }
    }
    // TEMP-DIAG (revert): compare empty-d <path> before/after normalize.
    {
        QFile g(QStringLiteral("/tmp/user-svg.svg"));
        if (g.open(QIODevice::ReadOnly)) {
            const QByteArray in = g.readAll();
            auto countEmpty = [](const QByteArray& b) {
                int n = 0, pos = 0;
                while ((pos = b.indexOf("<path", pos)) >= 0) {
                    const int gt = b.indexOf('>', pos);
                    if (gt < 0) break;
                    const QByteArray tag = b.mid(pos, gt - pos);
                    const int dq = tag.indexOf("d=\"");
                    const int sq = tag.indexOf("d='");
                    int q = -1;
                    char qc = 0;
                    if (dq >= 0 && (sq < 0 || dq < sq)) {
                        q = dq + 3;
                        qc = '"';
                    } else if (sq >= 0) {
                        q = sq + 3;
                        qc = '\'';
                    }
                    if (q < 0) {
                        ++n;
                    } else {
                        const int e = tag.indexOf(qc, q);
                        if (e >= q && e - q == 0) ++n;
                    }
                    pos = gt + 1;
                }
                return n;
            };
            const QByteArray norm = svgNormalizeForImport(in);
            QFile dump(QStringLiteral("/tmp/norm.svg"));
            if (dump.open(QIODevice::WriteOnly)) {
                dump.write(norm);
            }
            int totalPaths = 0, tp = 0;
            while ((tp = norm.indexOf("<path", tp)) >= 0) {
                ++totalPaths;
                ++tp;
            }
            std::printf("  normEmpty: in=%d normPaths=%d normBytes=%lld/%lld\n",
                        countEmpty(in), totalPaths,
                        static_cast<long long>(norm.size()),
                        static_cast<long long>(in.size()));
            int pos = 0, shown = 0;
            while (shown < 3 && (pos = norm.indexOf("<path", pos)) >= 0) {
                const int gt = norm.indexOf('>', pos);
                if (gt < 0) break;
                const QByteArray tag = norm.mid(pos, gt - pos);
                if (tag.contains("d=\"\"") ||
                    (!tag.contains("d=\"") && !tag.contains("d='"))) {
                    std::printf("  NEMPTY-CTX: %.200s\n  NEMPTY-TAG: %.2000s\n",
                                norm.mid(pos > 200 ? pos - 200 : 0, pos > 200 ? 200 : pos).constData(),
                                tag.constData());
                    ++shown;
                }
                pos = gt + 1;
            }
        }
    }
    SvgImportResult svg;
    int dpi = 96;
    QString error;
    CHECK(svgPartsImport(f.readAll(), &svg, &dpi, &error));
    QString openError;
    CHECK(state.openSvgParts(QStringLiteral("u.svg"), svg, dpi, &openError));
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);
    // TEMP-DIAG (revert): dump the flattened composite for viewing.
    {
        d->rebuildComposite();
        d->composite.save(QStringLiteral("/tmp/comp.png"));
        std::printf("  comp %dx%d saved\n", d->composite.width(),
                    d->composite.height());
    }
    // TEMP-DIAG (revert): canvas block moved to svg_canvas_probe.cpp.
    {
        const LayerItem& l = d->layers[1240];
        int done = 0;
        for (const auto& a : l.flatArt) {
            if (done++ >= 3) break;
            std::printf("  SEG mat=(%.3f,%.3f,%.3f,%.3f,%.1f,%.1f) sw=%.2f stroke=(%d,%d,%d,%d)\n",
                        a->matrix[0], a->matrix[1], a->matrix[2], a->matrix[3],
                        a->matrix[4], a->matrix[5], (double)a->paint.strokeWidth,
                        (int)a->paint.stroke[0], (int)a->paint.stroke[1],
                        (int)a->paint.stroke[2], (int)a->paint.stroke[3]);
            int n = 0;
            for (const auto& s : a->segments) {
                if (n++ >= 8) break;
                std::printf("    k=%d xy=(%.2f,%.2f) c1=(%.2f,%.2f) c2=(%.2f,%.2f)\n",
                            (int)s.kind, (double)s.x, (double)s.y,
                            (double)s.c1x, (double)s.c1y, (double)s.c2x,
                            (double)s.c2y);
            }
        }
    }
    {
        int total = d->layers.size(), nullPx = 0, blankPx = 0;
        struct Row {
            int i, w, h;
            long opaque;
        };
        std::vector<Row> big;
        for (int i = 0; i < d->layers.size(); ++i) {
            const LayerItem& l = d->layers[i];
            if (l.kind != LayerItem::Kind::Pixel) continue;
            if (!l.pixels) {
                ++nullPx;
                continue;
            }
            const pittore::Image& im = *l.pixels;
            const int w = (int)im.width(), h = (int)im.height();
            if ((long)w * h >= 200000) big.push_back(Row{i, w, h, -1});
            long opaque = 0;
            for (int y = 0; y < h; y += 4)
                for (int x = 0; x < w; x += 4)
                    if (im.at((std::uint32_t)x, (std::uint32_t)y).a > 0.03f)
                        ++opaque;
            if (opaque == 0) ++blankPx;
        }
        for (auto& r : big) {
            const pittore::Image& im = *d->layers[r.i].pixels;
            long opaque = 0, n = 0;
            double rr = 0, gg = 0, bb = 0;
            for (int y = 0; y < r.h; y += 2)
                for (int x = 0; x < r.w; x += 2) {
                    const auto& px = im.at((std::uint32_t)x, (std::uint32_t)y);
                    ++n;
                    if (px.a > 0.03f) {
                        ++opaque;
                        rr += px.r;
                        gg += px.g;
                        bb += px.b;
                    }
                }
            std::printf("  BIG layer[%d] '%s' %dx%d frac=%.3f mean=(%.2f,%.2f,%.2f)\n",
                        r.i, d->layers[r.i].name.toUtf8().constData(), r.w,
                        r.h, (double)opaque / (double)n,
                        opaque ? rr / opaque : 0, opaque ? gg / opaque : 0,
                        opaque ? bb / opaque : 0);
        }
        std::printf("  census: total=%d nullPx=%d blankPx=%d\n", total, nullPx,
                    blankPx);
    }

    // Wrap everything in an outer group like the user's 'Fedora'.
    d->selectedLayers.clear();
    for (int i = 0; i < d->layers.size(); ++i)
        d->selectedLayers.push_back(i);
    const int fed = state.groupSelectedLayers();
    CHECK(fed == 0);

    d = state.activeDocument();
    const QImage fed30 = groupThumbnail(*d, 0, 30);
    CHECK(!fed30.isNull());
    fed30.save(QStringLiteral("/tmp/thumb-fedora-30.png"));
    stats(fed30, "fedora30");
    const QImage fed120 = groupThumbnail(*d, 0, 120);
    fed120.save(QStringLiteral("/tmp/thumb-fedora-120.png"));
    stats(fed120, "fedora120");

    // First single-path group + a tiny-sliver group.
    int done = 0;
    for (int i = 0; i < d->layers.size() && done < 3; ++i) {
        if (d->layers[i].kind != LayerItem::Kind::Group || i == 0) continue;
        const QImage t = groupThumbnail(*d, i, 120);
        if (t.isNull()) {
            std::printf("  group[%d] NULL\n", i);
            continue;
        }
        char tag[64];
        std::snprintf(tag, sizeof(tag), "single[%d]", i);
        const QString p =
            QStringLiteral("/tmp/thumb-single-%1.png").arg(i);
        t.save(p);
        stats(t, tag);
        ++done;
    }
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
