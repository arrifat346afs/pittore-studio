// test_session.cpp — clean-quit session: saveSession records file-backed
// documents (native project path preferred over the import source) and skips
// unsaved ones; restoreSession reopens survivors, restores the active tab,
// and fails soft on missing files and garbage. Headless (QCoreApplication),
// scratch XDG_CONFIG_HOME, temp-dir projects.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "test_util.h"
#include "ui/app_state.h"

using namespace pittore::ui;

namespace {

QString tmpPath(const char* name) {
    return QDir::tempPath() + QStringLiteral("/pittore-session-") +
           QString::fromLatin1(name);
}

void rm(const QString& path) { QFile::remove(path); }

}  // namespace

int main(int argc, char** argv) {
    // Never touch the user's real config on manual runs (see test_color_policy).
    if (qEnvironmentVariableIsEmpty("XDG_CONFIG_HOME")) {
        static QTemporaryDir* scratch = new QTemporaryDir;
        if (scratch->isValid())
            qputenv("XDG_CONFIG_HOME", scratch->path().toLocal8Bit());
    }
    QCoreApplication app(argc, argv);
    const QString ifp = tmpPath("a.psc");
    const QString png = tmpPath("b.png");
    const QString session = tmpPath("session.json");
    rm(ifp);
    rm(png);
    rm(session);

    // 1. Native project round-trips through save/restore with its title.
    {
        AppState state;
        DocumentItem* d = state.addDocument(QStringLiteral("s1"), QSize(32, 24), 72);
        CHECK(d != nullptr);
        if (!d) return 1;
        QString error;
        CHECK(state.saveProject(ifp, &error));
        CHECK(state.saveSession(session));
        CHECK(QFileInfo::exists(session));
        state.closeDocument(0);
        CHECK(state.documents().isEmpty());
        CHECK(state.restoreSession(session) == 1);
        CHECK(state.activeDocument() != nullptr);
        if (state.activeDocument())
            CHECK(state.activeDocument()->title == QStringLiteral("s1"));
    }

    // 2. Imported images restore through their source path (PNG writer is
    // built into QtGui; skip cleanly if it ever is not).
    {
        QImage probe(4, 4, QImage::Format_ARGB32);
        probe.fill(QColor(10, 20, 30));
        if (probe.save(png, "PNG")) {
            AppState state;
            QString error;
            CHECK(state.openImageFile(png, &error));
            CHECK(state.saveSession(session));
            state.closeDocument(0);
            CHECK(state.restoreSession(session) == 1);
            CHECK(state.activeDocument() != nullptr);
            if (state.activeDocument())
                CHECK(state.activeDocument()->title ==
                      QStringLiteral("pittore-session-b"));
            rm(png);
        } else {
            std::printf("  (skip: PNG writer unavailable)\n");
        }
    }

    // 3. Unsaved documents are skipped; nothing restorable means false and
    // no stale file is expected from the caller contract.
    {
        AppState state;
        CHECK(state.addDocument(QStringLiteral("unsaved"), QSize(8, 8), 72) !=
              nullptr);
        CHECK(!state.saveSession(tmpPath("empty.json")));
        rm(tmpPath("empty.json"));
        state.closeDocument(0);
    }

    // 4. Missing files, wrong versions and garbage fail soft with zero.
    {
        AppState state;
        const QString missing = tmpPath("missing.json");
        {
            QJsonObject root;
            root.insert(QStringLiteral("version"), 1);
            root.insert(QStringLiteral("documents"),
                        QJsonArray::fromStringList(
                            QStringList{tmpPath("nope.ifp")}));
            root.insert(QStringLiteral("active"), 0);
            QFile f(missing);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        }
        CHECK(state.restoreSession(missing) == 0);
        CHECK(state.documents().isEmpty());
        rm(missing);

        const QString badver = tmpPath("badver.json");
        {
            QFile f(badver);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("{\"version\":99,\"documents\":[],\"active\":0}");
        }
        CHECK(state.restoreSession(badver) == 0);
        rm(badver);

        const QString garbage = tmpPath("garbage.json");
        {
            QFile f(garbage);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("not json at all{{{");
        }
        CHECK(state.restoreSession(garbage) == 0);
        rm(garbage);
    }

    // 5. A wild active index clamps onto the restored document.
    {
        AppState state;
        DocumentItem* d = state.addDocument(QStringLiteral("s2"), QSize(8, 8), 72);
        CHECK(d != nullptr);
        QString error;
        CHECK(state.saveProject(ifp, &error));
        CHECK(state.saveSession(session));
        state.closeDocument(0);
        // Rewrite the session with an out-of-range active index.
        QFile f(session);
        CHECK(f.open(QIODevice::ReadOnly));
        QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        f.close();
        root[QStringLiteral("active")] = 99;
        CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        f.close();
        CHECK(state.restoreSession(session) == 1);
        CHECK(state.activeDocumentIndex() == 0);
        state.closeDocument(0);
    }

    rm(ifp);
    rm(session);
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
