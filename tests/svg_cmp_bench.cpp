#include <QApplication>
#include <QFile>
#include <QElapsedTimer>
#include <cstdio>

#include "test_util.h"
#include "ui/app_state.h"
#include "ui/svg_parts.h"

using namespace pittore::ui;

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    const char* path = argc > 1 ? argv[1]
                                : "tests/SVG/Mapa_do_Brasil_por_código_DDD.svg";
    QFile f(QString::fromLocal8Bit(path));
    if (!f.open(QIODevice::ReadOnly)) {
        std::printf("open fail %s\n", path);
        return 1;
    }
    const QByteArray xml = f.readAll();
    std::printf("file %s bytes=%lld\n", path, (long long)xml.size());
    int layers = 0;
    QElapsedTimer t;
    t.start();
    SvgImportResult result;
    int dpi = 96;
    QString error;
    if (!svgPartsImport(xml, &result, &dpi, &error)) {
        std::printf("  import FAILED: %s\n", error.toUtf8().constData());
        return 2;
    }
    AppState st;
    QString openError;
    if (!st.openSvgParts(QString::fromLatin1("cmp"), result, dpi, &openError)) {
        std::printf("  open FAILED: %s\n", openError.toUtf8().constData());
        return 2;
    }
    DocumentItem* d = st.activeDocument();
    if (!d) return 2;
    layers = (int)d->layers.size();
    std::printf("  layers=%d ms=%lld doc=%dx%d\n", layers, (long long)t.elapsed(),
                d->size.width(), d->size.height());
    const QImage img = d->composite.copy();
    const QString out = QString::fromLocal8Bit(qgetenv("PITTORE_CMP_OUT"));
    if (!out.isEmpty()) {
        CHECK(img.save(out));
        std::printf("  saved %s\n", out.toUtf8().constData());
    }
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
