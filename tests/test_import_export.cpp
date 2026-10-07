// test_import_export.cpp — all-format import/export support:
//
//   qimageFromFile()   — decodes PSD/PSB/XCF via the built-in codecs, KRA via
//                        the built-in ZIP reader (mergedimage.png), and every
//                        raster format through Qt's image plugins (PNG/JPEG/…).
//   saveImageFile()    — writes flattened PSD/XCF/raster exports.
//   AppState::openImageFile() — any supported file becomes a one-pixel-layer
//                        document (native .ifp still routes to openProject).
//
// Lossless formats round-trip EXACTLY (including the straight-alpha critical
// pixel); JPEG is verified structurally (dimensions) since it is lossy.
//
// Runs headless under QCoreApplication with the CPU backend. XDG_CONFIG_HOME
// and PITTORE_PROJECTS_DIR are pointed at scratch dirs by meson so the real
// Settings.toml and project library are never touched.
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>

#include <algorithm>
#include <cstring>

#include "engine/core/log.h"
#include "engine/io/psd.h"
#include "engine/io/xcf.h"
#include "engine/io/zip.h"
#include "test_util.h"
#include "ui/app_state.h"
#include "ui/project_manager.h"

using namespace pittore::ui;

namespace {

// A straight-alpha RGBA64 canvas whose channels are 8-bit-exact (multiples of
// 257), so they survive both the 8-bit PNG/XCF path and the 16-bit PSD path.
// The critical pixel is half-/32%-transparent with a large red channel — any
// premultiply/divide round trip would halve it.
QImage expectedImage(int w = 48, int h = 32) {
    QImage img(w, h, QImage::Format_RGBA64);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<quint16*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int k = (x + y) % 8;
            row[x * 4 + 0] = static_cast<quint16>(((20 + x * 11) % 256) * 257);
            row[x * 4 + 1] = static_cast<quint16>(((y * 13) % 256) * 257);
            row[x * 4 + 2] = static_cast<quint16>((k * 37) % 256 * 257);
            row[x * 4 + 3] = (x == 7 && y == 3) ? 0x8080u : 0xFFFFu;   // a≈0.5 vs 1.0
        }
    }
    // The straight critical pixel: strong red at half alpha (both 8-bit-exact).
    auto* crit = reinterpret_cast<quint16*>(img.scanLine(3));
    crit[7 * 4 + 0] = 257 * 178;   // r ≈ 0.698 — would read ≈0.35 if premultiplied
    return img;
}

// Row-wise pixel comparison at full 16-bit precision.
bool samePixels(const QImage& a, const QImage& b) {
    const QImage ar = a.convertToFormat(QImage::Format_RGBA64);
    const QImage br = b.convertToFormat(QImage::Format_RGBA64);
    if (ar.size() != br.size() || ar.isNull() || br.isNull()) return false;
    for (int y = 0; y < ar.height(); ++y)
        if (std::memcmp(ar.constScanLine(y), br.constScanLine(y),
                        static_cast<std::size_t>(ar.bytesPerLine())) != 0)
            return false;
    return true;
}

// Engine-hosted pixels go through float32, which can drop the last 16-bit LSB;
// comparing at 8-bit granularity keeps the test exact for 8-bit-exact sources
// while still catching premultiplication (a ×2 error) and codec corruption.
bool samePixels8(const QImage& a, const QImage& b) {
    const QImage ar = a.convertToFormat(QImage::Format_RGBA64);
    const QImage br = b.convertToFormat(QImage::Format_RGBA64);
    if (ar.size() != br.size() || ar.isNull() || br.isNull()) return false;
    auto v8 = [](quint16 v) -> int { return (v + 128) / 257; };
    for (int y = 0; y < ar.height(); ++y) {
        const auto* ra = reinterpret_cast<const quint16*>(ar.constScanLine(y));
        const auto* rb = reinterpret_cast<const quint16*>(br.constScanLine(y));
        for (int i = 0; i < ar.width() * 4; ++i)
            if (v8(ra[i]) != v8(rb[i])) return false;
    }
    return true;
}

bool writeRaw(const QString& path, const QByteArray& bytes) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(bytes) == bytes.size();
}

QByteArray pngBytes(const QImage& img) {
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    QImageWriter writer(&buf, "png");
    writer.write(img);
    buf.close();
    return out;
}

// ---------------------------------------------------------------------------
// Format writers
// ---------------------------------------------------------------------------

bool writePng(const QImage& img, const QString& path) {
    QImageWriter writer(path, "png");
    return writer.write(img);
}

bool writeJpeg(QImage img, const QString& path) {
    QImageWriter writer(path, "jpeg");
    writer.setQuality(88);
    return writer.write(std::move(img));
}

bool writePsd(const QImage& img, const QString& path) {
    const std::vector<std::uint16_t> rgba = rgba16FromQImage(img);
    auto bytes = pittore::io::psdEncodeRgba(
        static_cast<std::uint32_t>(img.width()),
        static_cast<std::uint32_t>(img.height()), 16, rgba.data());
    if (!bytes) return false;
    return writeRaw(path, QByteArray(reinterpret_cast<const char*>(bytes->data()),
                                     static_cast<int>(bytes->size())));
}

bool writeXcf(const QImage& img, const QString& path) {
    const std::vector<std::uint16_t> rgba = rgba16FromQImage(img);
    auto bytes = pittore::io::xcfEncodeRgba(
        static_cast<std::uint32_t>(img.width()),
        static_cast<std::uint32_t>(img.height()), rgba.data());
    if (!bytes) return false;
    return writeRaw(path, QByteArray(reinterpret_cast<const char*>(bytes->data()),
                                     static_cast<int>(bytes->size())));
}

// KRA is a ZIP with the flattened image inside.
bool writeKra(const QImage& img, const QString& path) {
    std::vector<pittore::io::ZipEntry> entries;
    const QByteArray png = pngBytes(img);
    entries.push_back({"mergedimage.png",
                       std::vector<std::uint8_t>(png.constBegin(), png.constEnd())});
    auto bytes = pittore::io::zipWrite(entries);
    if (!bytes) return false;
    return writeRaw(path, QByteArray(reinterpret_cast<const char*>(bytes->data()),
                                     static_cast<int>(bytes->size())));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Every lossless format decodes back to the exact source pixels.
void test_qimage_from_file(const QString& png, const QString& psd,
                           const QString& xcf, const QString& kra,
                           const QString& jpg, const QString& previewOnlyKra) {
    const QImage expected = expectedImage();
    const QSize expectedSize = expected.size();

    QString error;
    const QImage pngGot = qimageFromFile(png, &error);
    CHECK(!pngGot.isNull());
    CHECK(pngGot.size() == expectedSize);
    CHECK(samePixels(pngGot, expected));

    const QImage psdGot = qimageFromFile(psd, &error);
    CHECK(!psdGot.isNull());
    CHECK(psdGot.size() == expectedSize);
    CHECK(samePixels(psdGot, expected));

    const QImage xcfGot = qimageFromFile(xcf, &error);
    CHECK(!xcfGot.isNull());
    CHECK(xcfGot.size() == expectedSize);
    CHECK(samePixels(xcfGot, expected));

    const QImage kraGot = qimageFromFile(kra, &error);
    CHECK(!kraGot.isNull());
    CHECK(kraGot.size() == expectedSize);
    CHECK(samePixels(kraGot, expected));

    // KRA with only preview.png still decodes.
    const QImage kraFallback = qimageFromFile(previewOnlyKra, &error);
    CHECK(!kraFallback.isNull());
    CHECK(samePixels(kraFallback, expected));

    // JPEG is lossy: structural check only.
    const QImage jpgGot = qimageFromFile(jpg, &error);
    CHECK(!jpgGot.isNull());
    CHECK(jpgGot.size() == expectedSize);
}

// saveImageFile → qimageFromFile round-trips PSD/XCF/PNG exactly.
void test_save_image_file(const QTemporaryDir& dir) {
    const QImage expected = expectedImage();

    const QString psdPath = dir.filePath(QStringLiteral("out.psd"));
    QString error;
    CHECK(saveImageFile(expected, psdPath, &error));
    CHECK(QFile::exists(psdPath));
    CHECK(samePixels(qimageFromFile(psdPath, &error), expected));

    const QString xcfPath = dir.filePath(QStringLiteral("out.xcf"));
    CHECK(saveImageFile(expected, xcfPath, &error));
    CHECK(QFile::exists(xcfPath));
    CHECK(samePixels(qimageFromFile(xcfPath, &error), expected));

    const QString pngPath = dir.filePath(QStringLiteral("out.png"));
    CHECK(saveImageFile(expected, pngPath, &error));
    CHECK(QFile::exists(pngPath));
    CHECK(samePixels(qimageFromFile(pngPath, &error), expected));

    const QString jpgPath = dir.filePath(QStringLiteral("out.jpg"));
    CHECK(saveImageFile(expected, jpgPath, &error));
    CHECK(QFile::exists(jpgPath));
    const QImage jpgGot = qimageFromFile(jpgPath, &error);
    CHECK(!jpgGot.isNull());
    CHECK(jpgGot.size() == expected.size());

    // Unwritable directory / unknown suffix / empty image → clean failures.
    CHECK(!saveImageFile(expected,
                         dir.path() + QStringLiteral("/missing/out.png"), &error));
    CHECK(!saveImageFile(expected, dir.filePath(QStringLiteral("out.xyz")), &error));
    CHECK(!saveImageFile(QImage(), dir.filePath(QStringLiteral("x.png")), &error));
}

// AppState::openImageFile turns every supported file into a one-pixel-layer
// document; .ifp still loads natively through openProject.
void test_open_via_app_state(QTemporaryDir& dir, const QString& png,
                             const QString& psd, const QString& xcf,
                             const QString& kra) {
    AppState state;
    CHECK(state.documents().isEmpty());
    const QImage expected = expectedImage();
    const QSize expectedSize = expected.size();

    struct Case {
        QString path;
        QString title;
        int dpi;   // expected document dpi (PNG carries pHYs → 300)
    };
    const Case cases[] = {
        {png, QStringLiteral("photo"), 300},
        {psd, QStringLiteral("photo"), 72},
        {xcf, QStringLiteral("photo"), 72},
        {kra, QStringLiteral("photo"), 72},
    };

    for (const Case& c : cases) {
        QString error;
        const bool ok = state.openImageFile(c.path, &error);
        if (!ok) std::fprintf(stderr, "    openImageFile(%s): %s\n",
                              c.path.toLocal8Bit().constData(),
                              error.toLocal8Bit().constData());
        CHECK(ok);
        DocumentItem* doc = state.activeDocument();
        CHECK(doc != nullptr);
        if (!doc) continue;
        CHECK(doc->title == c.title);
        CHECK(doc->size == expectedSize);
        CHECK_EQ(doc->dpi, c.dpi);
        CHECK_EQ(doc->layers.size(), 1);
        if (doc->layers.size() != 1) continue;
        const LayerItem& layer = doc->layers[0];
        CHECK(layer.pixels != nullptr);
        if (!layer.pixels) continue;
        // The imported native pixels equal the source exactly (8-bit granular
        // comparison: the engine float path may drop a 16-bit LSB).
        const QImage native = imageToStraightRgba64(*layer.pixels);
        CHECK(samePixels8(native, expected));
        CHECK(!doc->dirty);
        CHECK(doc->filePath.isEmpty());   // imported: Save still prompts for .psc
    }
    CHECK_EQ(state.documents().size(), 4);

    // Garbage and unknown suffixes are rejected with a real error.
    const QString garbagePng = dir.filePath(QStringLiteral("bad.png"));
    writeRaw(garbagePng, QByteArray("this is definitely not a png image"));
    const QString garbagePsd = dir.filePath(QStringLiteral("bad.psd"));
    writeRaw(garbagePsd, QByteArray("not a psd either"));
    const QString unknown = dir.filePath(QStringLiteral("mystery.xyz"));
    writeRaw(unknown, QByteArray("no plugin knows this"));
    const QString orphanKra = dir.filePath(QStringLiteral("empty.kra"));
    writeRaw(orphanKra, QByteArray("not even a zip"));

    QString error;
    CHECK(!state.openImageFile(garbagePng, &error));
    CHECK(!state.openImageFile(garbagePsd, &error));
    CHECK(!state.openImageFile(unknown, &error));
    CHECK(!state.openImageFile(orphanKra, &error));
    CHECK_EQ(state.documents().size(), 4);

    // The scratch library's project file still opens through openImageFile
    // (.psc today; legacy .ifp equally).
    ProjectFileData data;
    data.name = QStringLiteral("Native");
    data.size = expectedSize;
    data.dpi = 150;
    data.colorMode = QStringLiteral("RGB/8");
    data.background = QStringLiteral("white");
    CHECK(state.createNewProject(data, &error) != nullptr);
    const QString ifpPath = state.activeProjectPath();
    const int before = state.documents().size();
    CHECK(state.openImageFile(ifpPath, &error));
    CHECK_EQ(state.documents().size(), before + 1);
    CHECK(state.activeDocument()->title == QStringLiteral("Native"));
    CHECK_EQ(state.activeDocument()->layers.size(), 1);
}

// proxyFactorForPixels: exact fit -> 1, one pixel over -> 2, etc.
void test_proxy_factor() {
    CHECK_EQ(proxyFactorForPixels(100, 100), 1);
    CHECK_EQ(proxyFactorForPixels(0, 100), 1);
    CHECK_EQ(proxyFactorForPixels(1000, 0), 1);
    CHECK_EQ(proxyFactorForPixels(101, 100), 2);
    CHECK_EQ(proxyFactorForPixels(400, 100), 2);
    CHECK_EQ(proxyFactorForPixels(401, 100), 3);
    CHECK_EQ(proxyFactorForPixels(416592960ull, 16000000ull), 6);
}

#ifdef PITTORE_TIFF
// Capped TIFF import box-averages to a proxy and reports factor + full size.
void test_tiff_capped_proxy(const QTemporaryDir& dir) {
    QImage src(64, 48, QImage::Format_RGBA64);
    for (int y = 0; y < 48; ++y) {
        auto* row = reinterpret_cast<quint16*>(src.scanLine(y));
        for (int x = 0; x < 64; ++x) {
            row[x * 4 + 0] = static_cast<quint16>(((x * 37 + y * 11) % 256) * 257);
            row[x * 4 + 1] = static_cast<quint16>(((x * 53 + y * 29) % 256) * 257);
            row[x * 4 + 2] = static_cast<quint16>(((x * 17 + y * 71) % 256) * 257);
            row[x * 4 + 3] = 0xFFFFu;
        }
    }
    const QString tif = dir.filePath(QStringLiteral("big.tif"));
    QString error;
    CHECK(saveImageFile(src, tif, &error));

    QSize probed;
    int probeDpi = 0;
    CHECK(probeImageSize(tif, probed, probeDpi));
    CHECK(probed == QSize(64, 48));

    int dpiOut = 0, factor = 0;
    QSize full;
    const QImage dense =
        qimageFromFileCapped(tif, 0, &error, &dpiOut, &factor, &full);
    CHECK(!dense.isNull());
    CHECK(dense.size() == QSize(64, 48));
    CHECK_EQ(factor, 1);
    CHECK(full == QSize(64, 48));

    // 3072 px capped to 100 px -> factor 6 -> 11x8 proxy.
    const QImage proxy =
        qimageFromFileCapped(tif, 100, &error, &dpiOut, &factor, &full);
    CHECK(!proxy.isNull());
    CHECK_EQ(factor, 6);
    CHECK(full == QSize(64, 48));
    CHECK(proxy.size() == QSize(11, 8));

    // Proxy pixels equal a box average of the dense 8-bit values.
    const QImage d8 = dense.convertToFormat(QImage::Format_RGBA64);
    const QImage p8 = proxy.convertToFormat(QImage::Format_RGBA64);
    auto at8 = [](const QImage& im, int x, int y, int c) -> int {
        const auto* r =
            reinterpret_cast<const quint16*>(im.constScanLine(y));
        return r[x * 4 + c] / 257;
    };
    for (int oy = 0; oy < 8; ++oy)
        for (int ox = 0; ox < 11; ++ox) {
            const int x0 = ox * 6, y0 = oy * 6;
            const int x1 = std::min(x0 + 6, 64), y1 = std::min(y0 + 6, 48);
            for (int c = 0; c < 4; ++c) {
                int sum = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) sum += at8(d8, x, y, c);
                const int n = (x1 - x0) * (y1 - y0);
                CHECK_EQ(at8(p8, ox, oy, c), (sum + n / 2) / n);
            }
        }
}

// A small RAM budget turns openImageFile into a proxy open instead of the
// old "Could not create the document" refusal.
void test_open_proxy_via_app_state(const QTemporaryDir& dir) {
    QImage src(256, 256, QImage::Format_RGBA64);
    src.fill(Qt::white);
    const QString png = dir.filePath(QStringLiteral("large.png"));
    CHECK(writePng(src, png));

    AppState state;
    const AppSettings saved = state.settings();
    AppSettings limited = saved;
    limited.ramLimitMb = 1;   // ~20K px budget: 65K px source must proxy
    state.applySettings(limited);

    QString error;
    CHECK(state.openImageFile(png, &error));
    DocumentItem* doc = state.activeDocument();
    CHECK(doc != nullptr);
    if (doc) {
        CHECK(doc->isProxy);
        CHECK_EQ(doc->proxyFactor, 2);
        CHECK(doc->fullSize == QSize(256, 256));
        CHECK(doc->size == QSize(128, 128));
        CHECK(doc->sourcePath == QFileInfo(png).absoluteFilePath());
        CHECK(doc->statusText().contains(QStringLiteral("proxy")));
    }
    state.applySettings(saved);   // restore: never leak the 1MB budget
}

// Opt-in large-file smoke test: set PITTORE_BIG_TIFF to a huge TIFF path
// (e.g. the 42208x9870 STScI mosaic). Verifies a >100MP import opens as a
// tagged proxy under default settings instead of refusing or exploding.
// Skipped when the variable is unset (CI-safe).
void test_big_tiff_proxy_via_app_state() {
    const char* env = std::getenv("PITTORE_BIG_TIFF");
    if (!env || !*env) {
        std::puts("  [big-tiff] skipped (PITTORE_BIG_TIFF unset)");
        return;
    }
    const QString path = QString::fromLocal8Bit(env);
    QSize probed;
    int probeDpi = 0;
    CHECK(probeImageSize(path, probed, probeDpi));
    CHECK(!probed.isEmpty());
    const std::uint64_t npix =
        static_cast<std::uint64_t>(probed.width()) * probed.height();
    CHECK(npix > 100'000'000ull);

    AppState state;   // default settings: 90%-RAM budget, 100MP dense cap
    QString error;
    CHECK(state.openImageFile(path, &error));
    DocumentItem* doc = state.activeDocument();
    CHECK(doc != nullptr);
    if (!doc) return;
    const int wantFactor = proxyFactorForPixels(npix, 16'000'000ull);
    CHECK(doc->isProxy);
    CHECK_EQ(doc->proxyFactor, wantFactor);
    CHECK(doc->fullSize == probed);
    CHECK(doc->size == QSize((probed.width() + wantFactor - 1) / wantFactor,
                            (probed.height() + wantFactor - 1) / wantFactor));
    CHECK(doc->sourcePath == QFileInfo(path).absoluteFilePath());
}
#endif

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString logDir = QDir::tempPath() + QStringLiteral("/pittore-io-log");
    QDir().mkpath(logDir);
    ::pittore::core::log::set_log_dir(logDir.toStdString().c_str());

    QTemporaryDir dir;
    CHECK(dir.isValid());

    QImage expected = expectedImage();
    // PNG carries a 300-dpi pHYs chunk so the passthrough is exercised.
    expected.setDotsPerMeterX(11811);
    expected.setDotsPerMeterY(11811);

    const QString png = dir.filePath(QStringLiteral("photo.png"));
    const QString jpg = dir.filePath(QStringLiteral("photo.jpg"));
    const QString psd = dir.filePath(QStringLiteral("photo.psd"));
    const QString xcf = dir.filePath(QStringLiteral("photo.xcf"));
    const QString kra = dir.filePath(QStringLiteral("photo.kra"));
    const QString kra2 = dir.filePath(QStringLiteral("preview-only.kra"));
    if (!writePng(expected, png)) std::fprintf(stderr, "    writePng failed\n");
    if (!writeJpeg(expected, jpg)) std::fprintf(stderr, "    writeJpeg failed\n");
    if (!writePsd(expected, psd)) std::fprintf(stderr, "    writePsd failed\n");
    if (!writeXcf(expected, xcf)) std::fprintf(stderr, "    writeXcf failed\n");
    if (!writeKra(expected, kra)) std::fprintf(stderr, "    writeKra failed\n");
    {
        std::vector<pittore::io::ZipEntry> entries;
        const QByteArray pngBytes2 = pngBytes(expected);
        entries.push_back({"preview.png",
                           std::vector<std::uint8_t>(pngBytes2.constBegin(),
                                                     pngBytes2.constEnd())});
        auto bytes = pittore::io::zipWrite(entries);
        const bool ok = bytes &&
                        writeRaw(kra2, QByteArray(
                                           reinterpret_cast<const char*>(bytes->data()),
                                           static_cast<int>(bytes->size())));
        if (!ok) std::fprintf(stderr, "    writeKra(preview-only) failed\n");
    }
    CHECK(QFile::exists(png) && QFile::exists(jpg) && QFile::exists(psd) &&
          QFile::exists(xcf) && QFile::exists(kra));

    test_qimage_from_file(png, psd, xcf, kra, jpg, kra2);
    test_save_image_file(dir);
    test_open_via_app_state(dir, png, psd, xcf, kra);
    test_proxy_factor();
#ifdef PITTORE_TIFF
    test_tiff_capped_proxy(dir);
    test_open_proxy_via_app_state(dir);
    test_big_tiff_proxy_via_app_state();
#endif

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}