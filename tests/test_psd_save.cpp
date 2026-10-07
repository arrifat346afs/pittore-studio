// test_psd_save.cpp — Save/Save As to layered PSD through AppState:
// saveProject() writes .psd through the exporter, adopts the path (so a
// later Save writes back), and re-saving after edits picks them up.
//
// AppState harness like test_psd_export (headless, scratch XDG_CONFIG_HOME).
#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <cstdio>
#include <vector>

#include "engine/core/log.h"
#include "engine/io/psd.h"
#include "test_util.h"
#include "ui/app_state.h"

using namespace pittore::ui;

namespace {

std::shared_ptr<pittore::Image> solidImg(int w, int h, float r, float g,
                                          float b, float a = 1.0f) {
    auto img = std::make_shared<pittore::Image>(
        static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h));
    for (int i = 0; i < w * h; ++i) img->data()[i] = {r, g, b, a};
    return img;
}

}  // namespace

static void test_psd_save_adopts() {
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("s"), QSize(8, 4), 72);
    CHECK(doc != nullptr);
    if (!doc) return;
    LayerItem bg;
    bg.name = QStringLiteral("bg");
    bg.kind = LayerItem::Kind::Pixel;
    bg.pixels = solidImg(8, 4, 1, 0, 0);
    bg.sourceStamp = 1;
    doc->layers = {bg};
    doc->rebuildComposite();

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    if (!tmp.isValid()) return;
    const QString path = tmp.filePath(QStringLiteral("s.psd"));
    QString error;
    CHECK(state.saveProject(path, &error));
    CHECK(doc->filePath == QFileInfo(path).absoluteFilePath());
    CHECK(!doc->dirty);

    // Edit, then Save to the adopted path (the Ctrl+S route).
    doc->layers[0].pixels->data()[0] = {0.0f, 0.0f, 1.0f, 1.0f};
    ++doc->layers[0].sourceStamp;
    doc->dirty = true;
    CHECK(state.saveProject(doc->filePath, &error));
    CHECK(!doc->dirty);

    QFile f(path);
    CHECK(f.open(QIODevice::ReadOnly));
    if (!f.isOpen()) return;
    const QByteArray raw = f.readAll();
    std::string codecError;
    auto dec = pittore::io::psdDecodeLayers(
        std::vector<std::uint8_t>(raw.constBegin(), raw.constEnd()), &codecError);
    CHECK(dec.has_value());
    if (!dec || dec->layers.size() != 1) return;
    // Painted blue pixel survived the save.
    CHECK(dec->layers[0].rgba[0] == 0 && dec->layers[0].rgba[1] == 0 &&
          dec->layers[0].rgba[2] == 65535);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir =
        QDir::tempPath() + QStringLiteral("/pittore-psd-save-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());
    test_psd_save_adopts();
    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
