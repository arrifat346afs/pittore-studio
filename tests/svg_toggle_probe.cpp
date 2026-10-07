// svg_toggle_probe.cpp — headless measure of the Layers-panel toggle path on
// the SVG benchmark under the GPU backend. Mirrors panels.cpp exactly:
//   visible = !visible; recompositeRegion(stageBounds · canvas)
// and also times a full rebuild for contrast. Not a registered test.
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRectF>

#include <chrono>
#include <cstdio>
#include <string>

#include "engine/compute/factory.h"
#include "engine/core/log.h"
#include "ui/app_state.h"

using namespace pittore::ui;

namespace {

using clock = std::chrono::steady_clock;
double ms(clock::time_point a, clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

void togglePath(DocumentItem* d, int idx) {
    LayerItem& l = d->layers[idx];
    l.visible = !l.visible;
    const QRectF sb =
        stageBounds(*d, l).intersected(QRectF(QPointF(0, 0), QSizeF(d->size)));
    if (!sb.isEmpty()) d->recompositeRegion(sb.toAlignedRect());
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-svg-toggle-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    AppState state;
    AppSettings settings = state.settings();
    settings.gpuEnabled = true;
    state.applySettings(settings);
    if (state.computeBackend().type() == pittore::compute::BackendType::CPU) {
        std::printf("no GPU backend — abort\n");
        return 0;
    }

    QString error;
    const QString path = argv[1] ? QString::fromLocal8Bit(argv[1])
                                 : QStringLiteral("tests/SVG/benchmark_complex.svg");
    if (!QFileInfo::exists(path)) {
        std::printf("skip: %s not found (pass the SVG as argv[1])\n",
                    qPrintable(path));
        return 0;
    }
    const bool ok = state.openImageFile(path, &error);
    std::printf("open ok=%d err=%s\n", ok ? 1 : 0,
                error.toUtf8().constData());
    DocumentItem* d = state.activeDocument();
    if (!d) return 1;
    std::printf("doc \"%s\" %dx%d layers=%d backend=%s\n",
                d->title.toUtf8().constData(), d->size.width(), d->size.height(),
                static_cast<int>(d->layers.size()),
                d->backend ? d->backend->name().c_str() : "cpu");

    d->rebuildComposite();  // warm (sources already uploaded at open)
    d->rebuildComposite();

    const int rep = 6;
    {
        auto a = clock::now();
        for (int i = 0; i < rep; ++i) d->rebuildComposite();
        auto b = clock::now();
        std::printf("FULL rebuild avg %.3f ms (%d iters)\n", ms(a, b) / rep, rep);
    }

    // Toggle a spread of small part layers on/off.
    for (int it = 0; it < 5; ++it) {
        const int idx = it;  // top few layers
        LayerItem& l = d->layers[idx];
        const bool from = l.visible;
        auto a = clock::now();
        togglePath(d, idx);
        auto c = clock::now();
        std::printf("TOGGLE idx=%d name=\"%s\" %s->%s rect=(%.0f,%.0f,%.0f,%.0f) "
                    "%.3f ms\n",
                    idx, l.name.toUtf8().constData(), from ? "on" : "off",
                    l.visible ? "on" : "off", stageBounds(*d, l).x(),
                    stageBounds(*d, l).y(), stageBounds(*d, l).width(),
                    stageBounds(*d, l).height(), ms(a, c));
    }

    // Toggle the full-canvas background: worst case = full rebuild cost.
    {
        const int bg = static_cast<int>(d->layers.size()) - 1;
        auto a = clock::now();
        togglePath(d, bg);
        togglePath(d, bg);  // back on
        auto c = clock::now();
        std::printf("TOGGLE background pair %.3f ms (full-canvas rect)\n",
                    ms(a, c));
    }
    return 0;
}