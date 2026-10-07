// AI model catalogue + cache bookkeeping (ui/ai_models). No network is used:
// the download path is exercised only through its on-disk state predicates.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include "test_util.h"
#include "ui/ai_models.h"

using namespace pittore::ui;

namespace {

bool isHex64(const QString& s) {
    if (s.size() != 64) return false;
    for (const QChar c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

void test_registry() {
    const QVector<AiModel>& models = allAiModels();
    CHECK(!models.isEmpty());

    QSet<QString> ids;
    for (const AiModel& m : models) {
        CHECK(!m.id.isEmpty());
        CHECK(!ids.contains(m.id));  // ids are the persisted key: must be unique
        ids.insert(m.id);
        CHECK(!m.name.isEmpty());
        CHECK(!m.category.isEmpty());
        CHECK(!m.license.isEmpty());
        // Downloadable entries must have an https URL; local-only pair entries
        // have none (nothing to fetch) and must name both graph files.
        if (m.url.isEmpty()) {
            CHECK(!m.encoderFile.isEmpty());
            CHECK(!m.decoderFile.isEmpty());
            CHECK(m.decoderBytes > 0);
        } else {
            CHECK(m.url.startsWith(QStringLiteral("https://")));
        }
        CHECK(m.bytes > 0);
        CHECK(m.inputSize > 0);
        CHECK(m.sha256.isEmpty() || isHex64(m.sha256));
        // Lookup agrees with the catalogue entry.
        const AiModel* found = aiModel(m.id);
        CHECK(found != nullptr);
        if (found) CHECK(found->url == m.url);
    }

    CHECK(aiModel(QStringLiteral("does-not-exist")) == nullptr);
    // Person-aware models are the default, so they must exist.
    CHECK(aiModel(QStringLiteral("birefnet-portrait")) != nullptr);
}

void test_paths_and_sizes() {
    const QString scratch = QDir::tempPath() + QStringLiteral("/pittore-ai-models-test");
    QDir(scratch).removeRecursively();
    qputenv("PITTORE_MODELS_DIR", scratch.toLocal8Bit());

    CHECK(aiModelsDir() == scratch);
    CHECK(aiModelPath(QStringLiteral("u2netp"))
              .endsWith(QStringLiteral("/u2netp.onnx")));
    // Unknown ids fall back to <id>.onnx rather than crashing.
    CHECK(aiModelPath(QStringLiteral("nope")).endsWith(QStringLiteral("/nope.onnx")));

    CHECK(formatModelSize(4 * 1024 * 1024).contains(QStringLiteral("MB")));
    CHECK(formatModelSize(1500LL * 1024 * 1024).contains(QStringLiteral("GB")));
}

void test_store_state() {
    const QString scratch = QDir::tempPath() + QStringLiteral("/pittore-ai-models-test");
    QDir(scratch).removeRecursively();
    qputenv("PITTORE_MODELS_DIR", scratch.toLocal8Bit());

    const AiModel* model = aiModel(QStringLiteral("u2netp"));
    CHECK(model != nullptr);
    if (!model) return;

    AiModelStore store;
    CHECK(store.state(QStringLiteral("unknown")) == AiModelState::Absent);
    CHECK(store.state(model->id) == AiModelState::Absent);
    CHECK(store.errorText(model->id).isEmpty());

    const QString path = aiModelPath(model->id);
    QDir().mkpath(scratch);

    // Wrong size is not "Present": this is the integrity check the downloader
    // relies on when the upstream asset publishes no SHA-256.
    {
        QFile f(path);
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        CHECK(f.resize(16));
        f.close();
    }
    CHECK(store.state(model->id) == AiModelState::Absent);

    // Exact size is.
    {
        QFile f(path);
        CHECK(f.open(QIODevice::ReadWrite));
        CHECK(f.resize(model->bytes));
        f.close();
    }
    CHECK(store.state(model->id) == AiModelState::Present);

    store.remove(model->id);
    CHECK(store.state(model->id) == AiModelState::Absent);
    CHECK(!QFileInfo::exists(path));

    QDir(scratch).removeRecursively();
    qunsetenv("PITTORE_MODELS_DIR");
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);  // QNetworkAccessManager needs one
    test_registry();
    test_paths_and_sizes();
    test_store_state();
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
