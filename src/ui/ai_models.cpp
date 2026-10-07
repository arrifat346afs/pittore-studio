#include "ui/ai_models.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace pittore::ui {
namespace {

// Every model below is a ready-made ONNX file published by rembg
// (github.com/danielgatis/rembg), which repackages the upstream research
// checkpoints. Nothing is bundled with Pittore Studio: the user (or the
// `just models` dev recipe) downloads the ones they want into the cache.
constexpr const char* kBase =
    "https://github.com/danielgatis/rembg/releases/download/v0.0.0/";

AiModel entry(const char* id, const char* name, const char* category,
              const char* license, const char* file, qint64 bytes, int inputSize,
              const char* description, const char* sha256 = "") {
    AiModel m;
    m.id = QString::fromLatin1(id);
    m.name = QString::fromLatin1(name);
    m.category = QString::fromLatin1(category);
    m.license = QString::fromLatin1(license);
    m.description = QString::fromLatin1(description);
    m.url = QString::fromLatin1(kBase) + QString::fromLatin1(file);
    m.sha256 = QString::fromLatin1(sha256);
    m.bytes = bytes;
    m.inputSize = inputSize;
    return m;
}

// Local-only encoder+decoder pair (no download). The two files must be placed
// in the model cache by the user; the panel shows no download button.
AiModel entryPair(const char* id, const char* name, const char* category,
                  const char* license, const char* encoderFile, qint64 encBytes,
                  const char* decoderFile, qint64 decBytes, int inputSize,
                  const char* description) {
    AiModel m;
    m.id = QString::fromLatin1(id);
    m.name = QString::fromLatin1(name);
    m.category = QString::fromLatin1(category);
    m.license = QString::fromLatin1(license);
    m.description = QString::fromLatin1(description);
    m.encoderFile = QString::fromLatin1(encoderFile);
    m.bytes = encBytes;
    m.decoderFile = QString::fromLatin1(decoderFile);
    m.decoderBytes = decBytes;
    m.inputSize = inputSize;
    return m;
}

// Downloadable model at a full (non-rembg) URL with an explicit cache
// filename — rembg asset names collide otherwise (several hosts serve a bare
// "model.onnx").
AiModel entryUrl(const char* id, const char* name, const char* category,
                 const char* license, const char* url, const char* file,
                 qint64 bytes, int inputSize, const char* description,
                 const char* sha256 = "") {
    AiModel m;
    m.id = QString::fromLatin1(id);
    m.name = QString::fromLatin1(name);
    m.category = QString::fromLatin1(category);
    m.license = QString::fromLatin1(license);
    m.description = QString::fromLatin1(description);
    m.url = QString::fromLatin1(url);
    m.file = QString::fromLatin1(file);
    m.sha256 = QString::fromLatin1(sha256);
    m.bytes = bytes;
    m.inputSize = inputSize;
    return m;
}

bool fileBytesMatch(const QString& path, qint64 expected) {
    const QFileInfo info(path);
    return info.isFile() && info.size() == expected;
}

}  // namespace

const QVector<AiModel>& allAiModels() {
    static const QVector<AiModel> models = {
        // -- Person -----------------------------------------------------------
        entry("birefnet-portrait", "BiRefNet Portrait", "Person", "MIT",
              "BiRefNet-portrait-epoch_150.onnx", 972666916, 1024,
              "Best quality on people: hair, hands and soft edges."),
        entry("u2net_human_seg", "U2-Net Human Seg", "Person", "Apache-2.0",
              "u2net_human_seg.onnx", 175997641, 320,
              "Classic human segmentation model. Small and fast."),
        entry("u2net-portrait-matting", "U2-Net Portrait Matting", "Person",
              "Apache-2.0", "u2net-portrait-matting.onnx", 175994013, 320,
              "Portrait matting with fine alpha detail."),
        entry("isnet-general-use", "IS-Net (people-friendly)", "Person",
              "Apache-2.0", "isnet-general-use.onnx", 178648008, 1024,
              "Sharp general model that handles people well."),
        entryUrl("rmbg-1.4", "RMBG-1.4", "Person", "CC BY-NC (non-commercial)",
                 "https://huggingface.co/briaai/RMBG-1.4/resolve/main/onnx/model.onnx",
                 "briaai-RMBG-1.4.onnx", 176153355, 1024,
                 "IS-Net saliency with crisp hair edges; licence is non-commercial only."),

        // -- General ----------------------------------------------------------
        entry("birefnet-general", "BiRefNet General", "General", "MIT",
              "BiRefNet-general-epoch_244.onnx", 972666916, 1024,
              "Best all-round background removal for any subject."),
        entry("birefnet-general-lite", "BiRefNet General Lite", "General", "MIT",
              "BiRefNet-general-bb_swin_v1_tiny-epoch_232.onnx", 224005088, 1024,
              "Smaller/faster BiRefNet with most of the quality."),
        entry("birefnet-hrsod", "BiRefNet HRSOD", "General", "MIT",
              "BiRefNet-HRSOD_DHU-epoch_115.onnx", 972666916, 1024,
              "Tuned for high-resolution salient objects."),
        entry("birefnet-dis", "BiRefNet DIS", "General", "MIT",
              "BiRefNet-DIS-epoch_590.onnx", 972666916, 1024,
              "Dichotomous segmentation; crisp edges."),
        entry("birefnet-cod", "BiRefNet COD", "General", "MIT",
              "BiRefNet-COD-epoch_125.onnx", 972666916, 1024,
              "Camouflaged-object detection."),
        entry("birefnet-massive", "BiRefNet Massive", "General", "MIT",
              "BiRefNet-massive-TR_DIS5K_TR_TEs-epoch_420.onnx", 972666916, 1024,
              "Trained on the large combined corpus."),
        entry("u2net", "U2-Net", "General", "Apache-2.0", "u2net.onnx",
              175997641, 320, "The original U2-Net salient-object model."),
        entry("u2netp", "U2-Net (tiny)", "General", "Apache-2.0", "u2netp.onnx",
              4574861, 320, "Tiny and quick; good for previews.",
              "309c8469258dda742793dce0ebea8e6dd393174f89934733ecc8b14c76f4ddd8"),
        entry("silueta", "Silueta", "General", "MIT", "silueta.onnx", 44173029,
              320, "Lightweight U2-Net variant."),

        // -- High resolution --------------------------------------------------
        entry("birefnet-hr-general", "BiRefNet HR General", "High resolution",
              "MIT", "BiRefNet_HR-general-epoch_130.onnx", 1098928953, 2048,
              "High-resolution BiRefNet for large images."),
        entry("birefnet-hr-matting", "BiRefNet HR Matting", "High resolution",
              "MIT", "BiRefNet_HR-matting-epoch_135.onnx", 1098928867, 2048,
              "High-resolution matting with fine alpha."),

        // -- Anime ------------------------------------------------------------
        entry("isnet-anime", "IS-Net Anime", "Anime", "Apache-2.0",
              "isnet-anime.onnx", 176069933, 1024,
              "Tuned for illustrated/anime subjects."),

        // -- Object Select (local-only SAM pair) ------------------------------
        // A heavy encoder plus a tiny prompt-driven decoder of third-party
        // origin: neither file is redistributed, neither is auto-downloaded,
        // and the entry only activates once both sit in the model cache. The
        // engraving path (encode once per layer, then prompt per click) is
        // wired into the Object Selection tool and the one-click commands.
        entryPair("segment-sam", "Object Select (local SAM)", "General",
                  "Third-party — local only",
                  "SegmentationEncoder_2.6.onnx", 332688821,
                  "SegmentationDecoder_2.6.onnx", 8757251, 1024,
                  "Interactive object masking. Put both .onnx files in the "
                  "model cache to use it."),
    };
    return models;
}

const AiModel* aiModel(const QString& id) {
    for (const AiModel& m : allAiModels())
        if (m.id == id) return &m;
    return nullptr;
}

QString aiModelsDir() {
    QByteArray override = qgetenv("PITTORE_MODELS_DIR");
    if (override.isEmpty()) override = qgetenv("INFINITY_MODELS_DIR");
    if (!override.isEmpty()) return QString::fromLocal8Bit(override);
    QDir base(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    const QString fresh = base.filePath(QStringLiteral("PittoreStudio/models"));
    if (QDir(fresh).exists()) return fresh;
    const QString legacy = base.filePath(QStringLiteral("InfinityPhoto/models"));
    if (QDir(legacy).exists()) return legacy;
    return fresh;
}

QString aiModelPath(const QString& id) {
    const AiModel* m = aiModel(id);
    QString name;
    if (m && !m->encoderFile.isEmpty())
        name = m->encoderFile;  // local-only pair: explicit first graph file
    else if (m && !m->file.isEmpty())
        name = m->file;  // full-URL entry: explicit cache filename
    else if (m)
        name = QFileInfo(QUrl(m->url).path()).fileName();
    else
        name = id + QStringLiteral(".onnx");
    return QDir(aiModelsDir()).filePath(name);
}

QString bestEnhanceModelId(const QString& preferred) {
    if (!preferred.isEmpty()) {
        if (const AiModel* m = aiModel(preferred);
            m && m->decoderFile.isEmpty() &&
            aiModelStore().state(preferred) == AiModelState::Present)
            return preferred;
    }
    static const char* kPrefs[] = {
        "birefnet-hr-matting", "birefnet-portrait", "rmbg-1.4",
        "u2net-portrait-matting", "isnet-general-use",
        "birefnet-hr-general", "birefnet-general",
    };
    for (const char* id : kPrefs) {
        const QString qid = QString::fromLatin1(id);
        if (!aiModel(qid)) continue;
        if (aiModelIsPair(qid)) continue;
        if (aiModelStore().state(qid) == AiModelState::Present) return qid;
    }
    return QString();
}

QString aiModelDecoderPath(const QString& id) {
    const AiModel* m = aiModel(id);
    if (!m || m->decoderFile.isEmpty()) return QString();
    return QDir(aiModelsDir()).filePath(m->decoderFile);
}

bool aiModelIsPair(const QString& id) {
    const AiModel* m = aiModel(id);
    return m && !m->decoderFile.isEmpty();
}

QString formatModelSize(qint64 bytes) {
    constexpr double kMB = 1024.0 * 1024.0;
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(bytes / (kMB * 1024.0), 0, 'f', 2);
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / kMB, 0, 'f', 0);
    return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
}

// ---------------------------------------------------------------------------
// AiModelStore
// ---------------------------------------------------------------------------
AiModelStore::AiModelStore(QObject* parent) : QObject(parent) {
    net_ = new QNetworkAccessManager(this);
}

AiModelStore::~AiModelStore() = default;

AiModelState AiModelStore::state(const QString& id) const {
    if (current_ == id) return AiModelState::Downloading;
    if (errors_.contains(id)) return AiModelState::Error;
    const AiModel* m = aiModel(id);
    if (!m) return AiModelState::Absent;
    if (!fileBytesMatch(aiModelPath(id), m->bytes)) return AiModelState::Absent;
    // Pair models need both files present.
    if (aiModelIsPair(id) && aiModelDecoderPath(id).isEmpty())
        return AiModelState::Absent;
    const QString dec = aiModelDecoderPath(id);
    if (!dec.isEmpty() && !fileBytesMatch(dec, m->decoderBytes))
        return AiModelState::Absent;
    return AiModelState::Present;
}

QString AiModelStore::errorText(const QString& id) const {
    return errors_.value(id);
}

void AiModelStore::download(const QString& id) {
    if (!aiModel(id)) return;
    if (state(id) == AiModelState::Present) return;
    if (current_ == id || queue_.contains(id)) return;
    // Local-only models (local SAM pair) have no download URL; the user
    // drops the files in the cache themselves.
    if (aiModel(id)->url.isEmpty()) return;
    errors_.remove(id);
    queue_.append(id);
    startNext();
}

void AiModelStore::downloadAll() {
    queue_.clear();
    for (const AiModel& m : allAiModels()) {
        errors_.remove(m.id);
        if (m.url.isEmpty()) continue;  // local-only, nothing to fetch
        if (state(m.id) != AiModelState::Present && current_ != m.id)
            queue_.append(m.id);
    }
    startNext();
}

void AiModelStore::remove(const QString& id) {
    if (current_ == id) return;  // let an active download finish first
    QFile::remove(aiModelPath(id));
    const QString dec = aiModelDecoderPath(id);
    if (!dec.isEmpty()) QFile::remove(dec);
    errors_.remove(id);
    emit changed();
}

QString AiModelStore::importPairFiles(const QString& id,
                                      const QString& encoderFile) {
    const AiModel* m = aiModel(id);
    if (!m || !m->url.isEmpty())
        return tr("This model is not a local-only pair.");
    if (!QFileInfo::exists(encoderFile))
        return tr("Encoder file not found: %1").arg(encoderFile);
    const QDir srcDir = QFileInfo(encoderFile).absoluteDir();
    const QString decoderSrc = srcDir.filePath(m->decoderFile);
    if (!QFileInfo::exists(decoderSrc))
        return tr("Expected the companion decoder beside it:\n%1").arg(decoderSrc);
    QDir().mkpath(aiModelsDir());
    const QString encDst = aiModelPath(id);
    const QString decDst = aiModelDecoderPath(id);
    QFile::remove(encDst);
    QFile::remove(decDst);
    if (!QFile::copy(encoderFile, encDst) || !QFile::copy(decoderSrc, decDst))
        return tr("Could not copy the files into the model cache (%1).")
            .arg(aiModelsDir());
    errors_.remove(id);
    emit changed();
    return QString();
}

void AiModelStore::startNext() {
    if (!current_.isEmpty()) return;
    while (!queue_.isEmpty()) {
        const QString id = queue_.takeFirst();
        if (state(id) == AiModelState::Present) continue;
        start(id);
        return;
    }
    emit allFinished();
}

void AiModelStore::start(const QString& id) {
    const AiModel* m = aiModel(id);
    if (!m) return;
    QDir().mkpath(aiModelsDir());
    partPath_ = aiModelPath(id) + QStringLiteral(".part");

    file_ = new QFile(partPath_, this);
    if (!file_->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        finishErr(id, tr("Cannot write %1").arg(partPath_));
        return;
    }

    QNetworkRequest req{QUrl(m->url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("PittoreStudio/0.1"));
    reply_ = net_->get(req);
    current_ = id;
    emit changed();

    connect(reply_, &QIODevice::readyRead, this,
            [this] { if (file_) file_->write(reply_->readAll()); });
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this, id](qint64 received, qint64 total) {
                emit progress(id, received, total);
            });
    connect(reply_, &QNetworkReply::finished, this, [this, id] {
        QNetworkReply* reply = reply_;
        reply_ = nullptr;
        reply->deleteLater();
        if (file_) {
            file_->close();
            file_->deleteLater();
            file_ = nullptr;
        }

        if (reply->error() != QNetworkReply::NoError) {
            QFile::remove(partPath_);
            finishErr(id, reply->errorString());
            return;
        }
        const AiModel* model = aiModel(id);
        const qint64 got = QFileInfo(partPath_).size();
        if (model && got != model->bytes) {
            QFile::remove(partPath_);
            finishErr(id, tr("Size mismatch: got %1 of %2 bytes")
                              .arg(got)
                              .arg(model->bytes));
            return;
        }
        if (model && !model->sha256.isEmpty()) {
            QFile f(partPath_);
            if (!f.open(QIODevice::ReadOnly)) {
                QFile::remove(partPath_);
                finishErr(id, tr("Cannot re-open download for verification"));
                return;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&f)) {
                QFile::remove(partPath_);
                finishErr(id, tr("Checksum read failed"));
                return;
            }
            const QString actual = QString::fromLatin1(hash.result().toHex());
            if (actual.compare(model->sha256, Qt::CaseInsensitive) != 0) {
                QFile::remove(partPath_);
                finishErr(id, tr("SHA-256 mismatch"));
                return;
            }
        }
        const QString finalPath = aiModelPath(id);
        QFile::remove(finalPath);
        if (!QFile::rename(partPath_, finalPath)) {
            QFile::remove(partPath_);
            finishErr(id, tr("Cannot move download into place"));
            return;
        }
        finishOk(id);
    });
}

void AiModelStore::finishOk(const QString& id) {
    current_.clear();
    emit changed();
    emit finished(id, true, QString());
    startNext();
}

void AiModelStore::finishErr(const QString& id, const QString& message) {
    current_.clear();
    errors_.insert(id, message);
    emit changed();
    emit finished(id, false, message);
    startNext();
}

AiModelStore& aiModelStore() {
    // Intentionally leaked: an app-lifetime service that must outlive the
    // widgets that connect to it (and QApplication's own teardown).
    static AiModelStore* store = new AiModelStore;
    return *store;
}

// ---------------------------------------------------------------------------
// Console entry point
// ---------------------------------------------------------------------------
int ai_model_cli(int argc, char** argv, bool list) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("painter"));
    QCoreApplication::setOrganizationName(QStringLiteral("PittoreStudio"));

    if (list) {
        for (const AiModel& m : allAiModels())
            std::printf("%-24s %-17s %-11s %9s  %s\n", qPrintable(m.id),
                        qPrintable(m.category), qPrintable(m.license),
                        qPrintable(formatModelSize(m.bytes)), qPrintable(m.url));
        return 0;
    }

    AiModelStore store;
    int failed = 0;
    QObject::connect(
        &store, &AiModelStore::finished, &store,
        [&failed](const QString& id, bool ok, const QString& error) {
            if (ok) {
                std::printf("  ok    %s\n", qPrintable(id));
            } else {
                std::printf("  FAIL  %s (%s)\n", qPrintable(id), qPrintable(error));
                ++failed;
            }
            std::fflush(stdout);
        });

    // `--fetch-model <id>` downloads a single model; plain `--fetch-models`
    // fetches the whole catalogue.
    QString only;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--fetch-model") == 0)
            only = QString::fromLocal8Bit(argv[i + 1]);

    std::printf("Model cache: %s\n", qPrintable(aiModelsDir()));
    if (!only.isEmpty()) {
        if (!aiModel(only)) {
            std::printf("Unknown model: %s\n", qPrintable(only));
            return 2;
        }
        store.download(only);
    } else {
        store.downloadAll();
    }
    if (!store.isDownloading()) {
        std::printf("Nothing to do; already present.\n");
        return failed == 0 ? 0 : 1;
    }

    QEventLoop loop;
    QObject::connect(&store, &AiModelStore::allFinished, &loop,
                     [&loop] { loop.quit(); });
    loop.exec();
    if (failed == 0) {
        std::printf("Done.\n");
        return 0;
    }
    std::printf("Finished with %d failure(s).\n", failed);
    return 1;
}

}  // namespace pittore::ui
