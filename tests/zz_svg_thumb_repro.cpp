// THROWAWAY: render user-SVG group thumbs to PNG for visual inspection.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <cstdio>

#include "engine/core/log.h"
#include "test_util.h"
#include "ui/app_state.h"
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
    SvgImportResult svg;
    int dpi = 96;
    QString error;
    CHECK(svgPartsImport(f.readAll(), &svg, &dpi, &error));
    QString openError;
    CHECK(state.openSvgParts(QStringLiteral("u.svg"), svg, dpi, &openError));
    DocumentItem* d = state.activeDocument();
    CHECK(d != nullptr);

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
