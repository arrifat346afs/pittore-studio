// AI Select Subject / Remove Background through the real MainWindow.
// Skips cleanly with no model; model/project configurable via env.
#include <cstdio>

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QPointF>
#include <QPushButton>
#include <QWidget>

#include "test_util.h"
#include "ui/ai_models.h"
#include "ui/app_state.h"
#include "ui/canvas_view.h"
#include "ui/main_window.h"

using namespace pittore::ui;

namespace {

QString modelDir() {
    const QByteArray env = qgetenv("PITTORE_MODELS_DIR");
    if (!env.isEmpty()) return QString::fromLocal8Bit(env);
    return QDir(QDir::homePath())
        .filePath(QStringLiteral(".local/share/PittoreStudio/models"));
}

void prime(AppState& state, const QColor& color) {
    DocumentItem* d = state.activeDocument();
    if (!d) return;
    state.paintDab(QPointF(d->size.width() / 2.0, d->size.height() / 2.0), 2000.0,
                   1.0, 1.0, color);
    state.flushPaint();
}

QAction* findAction(QWidget* root, const QString& text) {
    for (QAction* a : root->findChildren<QAction*>()) {
        QString t = a->text();
        t.remove(QLatin1Char('&'));
        if (t == text) return a;
    }
    return nullptr;
}

QPushButton* findButton(QWidget* root, const QString& text) {
    for (QPushButton* b : root->findChildren<QPushButton*>()) {
        QString t = b->text();
        t.remove(QLatin1Char('&'));
        if (t == text) return b;
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    // Scratch config so a direct run never clobbers the user Settings.
    if (qgetenv("XDG_CONFIG_HOME").isEmpty()) {
        qputenv("XDG_CONFIG_HOME",
                (QDir::tempPath() + QStringLiteral("/pittore-test-ai-config"))
                    .toLocal8Bit());
    }
    QApplication app(argc, argv);

    const QString dir = modelDir();
    const QString modelId =
        QString::fromLocal8Bit(qgetenv("PITTORE_TEST_AI_MODEL"));
    const QString want = modelId.isEmpty() ? QStringLiteral("u2netp") : modelId;
    qputenv("PITTORE_MODELS_DIR", dir.toLocal8Bit());
    const QString modelFile = aiModelPath(want);
    const AiModel* m = aiModel(want);
    const bool pair = m && !m->decoderFile.isEmpty();
    const QString decFile = aiModelDecoderPath(want);
    if (want.isEmpty() || !m || !QFileInfo::exists(modelFile) ||
        (pair && !QFileInfo::exists(decFile))) {
        std::printf("SKIP: model not present at %s%s\n", qPrintable(modelFile),
                    pair ? qPrintable(QStringLiteral(" + ") + decFile) : "");
        return 0;
    }
    // Point at real cache before resolving model paths.
    std::printf("model=%s dir=%s file=%s\n", qPrintable(want), qPrintable(dir),
                qPrintable(QFileInfo(modelFile).fileName()));

    AppState state;
    AppSettings s = state.settings();
    s.bgModel = want;
    state.applySettings(s);

    const QString project =
        QString::fromLocal8Bit(qgetenv("PITTORE_TEST_AI_PROJECT"));
    if (!project.isEmpty() && QFileInfo::exists(project)) {
        QString err;
        if (!state.openProject(project, &err)) {
            std::printf("FAIL: cannot open project %s: %s\n", qPrintable(project),
                        qPrintable(err));
            return 1;
        }
        std::printf("opened %s (%dx%d, %d layers)\n", qPrintable(project),
                    state.activeDocument()->size.width(),
                    state.activeDocument()->size.height(),
                    int(state.activeDocument()->layers.size()));
    } else {
        DocumentItem* d = state.addDocument(QStringLiteral("ai"), QSize(256, 256), 300);
        CHECK(d != nullptr);
        if (!d) return 1;
        prime(state, QColor(40, 90, 160));
    }

    MainWindow win(&state);
    win.resize(1100, 800);
    win.show();
    app.processEvents();

    // Select Subject via the real menu action.
    {
        QAction* subject = findAction(&win, QStringLiteral("Subject"));
        CHECK(subject != nullptr);
        if (!subject) return 1;
        std::printf("triggering Select > Subject…\n");
        std::fflush(stdout);
        subject->trigger();
        app.processEvents();
        DocumentItem* d = state.activeDocument();
        CHECK(d != nullptr);
        if (d)
            std::printf("after select-subject: selection empty=%d rect=(%.0f,%.0f %.0fx%.0f)\n",
                        int(d->selection.isEmpty()), d->selection.x(), d->selection.y(),
                        d->selection.width(), d->selection.height());
    }

    // Remove Background via the task-bar button.
    {
        // Let the Selection rebuild land, then click.
        app.processEvents();
        QPushButton* remove = findButton(&win, QStringLiteral("Remove Background"));
        CHECK(remove != nullptr);
        if (remove) {
            std::printf("clicking Remove Background…\n");
            std::fflush(stdout);
            remove->click();
            app.processEvents();
        }
    }

    std::printf("done, no crash\n");
    return 0;
}
