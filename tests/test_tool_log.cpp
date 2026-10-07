// Per-tool logs: every tool's setup writes into its OWN file, tagged lines are
// flushed as they go, and the crash breadcrumb names the tool + stage that was
// running when a signal arrived.
//
// Headless — QtCore only (QCoreApplication), no canvas or widgets, and it
// deliberately does NOT link pittore-ui, so it still builds and runs while an
// unrelated UI file is mid-edit.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QStringList>

#include <csignal>
#include <cstdlib>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#ifndef _WIN32
#include <execinfo.h>
#endif

#include "test_util.h"
#include "ui/logging.h"
#include "ui/tools/log/tool_log.h"

using namespace pittore::ui;

namespace {

std::string readAll(const QString& path) {
    QFile f(path);
    if (path.isEmpty() || !f.open(QIODevice::ReadOnly)) return {};
    const QByteArray bytes = f.readAll();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

// The log tool id wrote to: tools/<zero-padded id>-<slug>.log. Found by id
// prefix so the test asserts the layout without re-deriving the slug rules.
QString toolLogFile(ToolId id) {
    const QDir dir(log_dir() + QStringLiteral("/tools"));
    const QString prefix =
        QStringLiteral("%1-").arg(static_cast<int>(id), 3, 10, QLatin1Char('0'));
    const QStringList names = dir.entryList(QStringList{QStringLiteral("*.log")});
    for (const QString& n : names)
        if (n.startsWith(prefix)) return dir.filePath(n);
    return {};
}

}  // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("painter"));

    // Fresh slate: the test owns its XDG_CONFIG_HOME, so wiping is safe.
    tool_log_reset();

    // 1. One file per tool, created on first use; an untouched tool gets none.
    tool_log(ToolId::Lasso, "setup/begin", "reason=test");
    tool_logf(ToolId::Crop, "options-bar/spec", "id=%s kind=%d", "ratio", 3);

    const QString lassoPath = toolLogFile(ToolId::Lasso);
    const QString cropPath = toolLogFile(ToolId::Crop);
    CHECK(!lassoPath.isEmpty());
    CHECK(!cropPath.isEmpty());
    CHECK(toolLogFile(ToolId::Move).isEmpty());

    // 2. Each file holds only its own tool: the whole point of the feature.
    const std::string lasso = readAll(lassoPath);
    CHECK(lasso.find("[tool ") != std::string::npos);
    CHECK(lasso.find("setup/begin") != std::string::npos);
    CHECK(lasso.find("reason=test") != std::string::npos);
    CHECK(lasso.find("options-bar/spec") == std::string::npos);

    const std::string crop = readAll(cropPath);
    CHECK(crop.find("id=ratio kind=3") != std::string::npos);
    CHECK(crop.find("Lasso") == std::string::npos);

    // 3. Filenames are shell-friendly: <id>-<slug>.log.
    CHECK(lassoPath.contains(QStringLiteral("lasso")));
    CHECK(cropPath.contains(QStringLiteral("crop")));

    // 4. The rollup sees every tool, each line tagged with its name.
    const std::string rollup = readAll(log_dir() + QStringLiteral("/Pittore-Tools.log"));
    CHECK(rollup.find("[Lasso]") != std::string::npos);
    CHECK(rollup.find("[Crop]") != std::string::npos);

    // 5. A setup trace brackets the work: begin … end.
    {
        ToolSetupTrace trace(ToolId::Brush, "test-switch");
        tool_log(ToolId::Brush, "options-bar/rebuild", "row built");
    }
    const std::string brush = readAll(toolLogFile(ToolId::Brush));
    CHECK(brush.find("setup/begin") != std::string::npos);
    CHECK(brush.find("reason=test-switch") != std::string::npos);
    CHECK(brush.find("options-bar/rebuild") != std::string::npos);
    CHECK(brush.find("setup/end") != std::string::npos);

    // 6. The crash path, for real: a forked child dies on its own signal and
    //    the handler must have named the tool, the stage and the log to open.
#ifndef _WIN32
    install_tool_crash_logging();
    const QString crashPath = log_dir() + QStringLiteral("/Pittore-Crash.log");
    CHECK(QFile::exists(crashPath));  // opened (and created) by the installer

    // Parent warms the breadcrumb for this exact tool, so the child's own
    // breadcrumb call allocates nothing after fork(); and warms backtrace()
    // in-process so the handler cannot trip over first-use allocation.
    tool_log_breadcrumb(ToolId::MagicWand, "crash-test/raise");
    void* warmup[4];
    ::backtrace(warmup, 4);

    const pid_t child = ::fork();
    CHECK(child >= 0);
    if (child == 0) {
        tool_log_breadcrumb(ToolId::MagicWand, "crash-test/raise");
        std::raise(SIGSEGV);
        _exit(2);  // unreachable: the handler re-raises to the default action
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    CHECK(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGSEGV);

    const std::string crash = readAll(crashPath);
    CHECK(crash.find("SIGSEGV") != std::string::npos);
    CHECK(crash.find("Magic Wand") != std::string::npos);
    CHECK(crash.find("crash-test/raise") != std::string::npos);
    CHECK(crash.find("Pittore-Crash.log") == std::string::npos);  // named the tool log
    CHECK(crash.find("tools/") != std::string::npos);
#endif

    const int rc = pittore_test::failures() == 0 ? 0 : 1;
    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return rc;
}
