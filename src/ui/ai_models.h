#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <QVector>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace pittore::ui {

// One downloadable on-device segmentation model, described purely as data. The
// weights are fetched from `url` into the per-user model cache (see
// aiModelsDir) and are never bundled with a build. `sha256` is empty when the
// upstream asset publishes no digest (older GitHub release assets); the exact
// `bytes` size is always verified. `inputSize` is the square network input edge
// the inference op will resize to.
struct AiModel {
    QString id;           // stable key persisted in Settings ("birefnet-portrait")
    QString name;         // human label for the panel
    QString category;     // "Person" | "General" | "High resolution" | "Anime"
    QString license;      // short license label shown in the panel
    QString description;  // one line, shown as a tooltip
    QString url;          // HTTPS download; EMPTY = local-only, never fetched
    QString file;         // explicit cache filename; empty = derive from url
    QString sha256;       // lowercase hex, or empty when unknown
    qint64 bytes = 0;     // exact expected size
    int inputSize = 1024; // square network input edge

    // Encoder + decoder pair ("Object Select" SAM models): when set, the model
    // is served by two local ONNX graphs and `url` must stay empty (these are
    // proprietary assets the user places in the cache themselves).
    // `encoderFile` names the first graph; `decoderFile` the second.
    QString encoderFile;
    QString decoderFile;
    qint64 decoderBytes = 0;
};

// The full curated catalogue, best-first within each category. Person-aware
// models come first because that is the common case.
const QVector<AiModel>& allAiModels();
const AiModel* aiModel(const QString& id);

// Per-user model cache: $PITTORE_MODELS_DIR when set (used by tests and the
// `just models` dev recipe), otherwise ~/.local/share/PittoreStudio/models.
QString aiModelsDir();
QString aiModelPath(const QString& id);
// Decoder half of a pair model, or the empty string for single-file models.
QString aiModelDecoderPath(const QString& id);
// Whether the model is an encoder+decoder pair (local-only).
bool aiModelIsPair(const QString& id);
// Best installed single-file model for Enhance Edges: `preferred` (the
// Preferences ▸ Machine Learning pick) when installed, else the first
// installed entry of the hair-matting preference order. Empty when nothing
// suitable is cached; callers fall back to the classical edge snap.
QString bestEnhanceModelId(const QString& preferred);
// "412 MB" / "1.02 GB" for the panel.
QString formatModelSize(qint64 bytes);

enum class AiModelState { Absent, Downloading, Present, Error };

// Console entry point for `--list-models` / `--fetch-models` (the `just models`
// recipe). Runs without a GUI and returns a process exit code. It lives here
// rather than in main.cpp because the app's entry translation unit is linked
// non-PIE by nvcc, which cannot relocate Qt's protected data symbols.
int ai_model_cli(int argc, char** argv, bool list);

// Downloads models into the cache with size (and SHA-256 when known) checks,
// one at a time, reporting progress. Use the process-wide aiModelStore() so the
// Preferences AI tab and the AI Models panel share one queue; the CLI
// `--fetch-models` path drives one directly.
class AiModelStore : public QObject {
    Q_OBJECT

  public:
    explicit AiModelStore(QObject* parent = nullptr);
    ~AiModelStore() override;

    AiModelState state(const QString& id) const;
    QString errorText(const QString& id) const;
    bool isDownloading() const { return !current_.isEmpty(); }

    void download(const QString& id);  // no-op when present or already queued
    void downloadAll();                // every model not already present
    void remove(const QString& id);    // delete the cached file(s)

    // Local-only pair models have no download URL; the user imports the two
    // .onnx files from a folder (e.g. an existing install) instead. Copies the
    // encoder and its companion decoder into the cache. Returns an error
    // string, or empty on success; emits changed() when installed.
    QString importPairFiles(const QString& id, const QString& encoderFile);

  signals:
    void changed();  // a row's state changed; the panel refreshes
    void progress(const QString& id, qint64 received, qint64 total);
    void finished(const QString& id, bool ok, const QString& error);
    void allFinished();  // queue drained (used by --fetch-models)

  private:
    void startNext();
    void start(const QString& id);
    void finishOk(const QString& id);
    void finishErr(const QString& id, const QString& message);

    QNetworkAccessManager* net_ = nullptr;
    QNetworkReply* reply_ = nullptr;
    QFile* file_ = nullptr;
    QString current_;
    QString partPath_;
    QVector<QString> queue_;
    QHash<QString, QString> errors_;
};

// Process-wide store shared by the Preferences AI tab and the AI Models panel
// so both reflect the same downloads. Created lazily after QCoreApplication
// exists; lives for the whole process.
AiModelStore& aiModelStore();

}  // namespace pittore::ui
