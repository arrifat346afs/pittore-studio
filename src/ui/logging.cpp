#include "ui/logging.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QThread>

#include "engine/core/log.h"
#include "ui/tools/log/tool_log.h"

#include <cstdio>
#include <mutex>

namespace pittore::ui {

QString log_dir() {
    QDir base(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation));
    base.mkpath(QStringLiteral("PittoreStudio/log"));
    return base.filePath(QStringLiteral("PittoreStudio/log"));
}

void reset_log_file() {
    ::pittore::core::log::set_log_dir(log_dir().toStdString().c_str());
    ::pittore::core::log::reset_file();
    ::pittore::core::log::reset_route_files();
    std::remove(::pittore::core::log::default_log_path().c_str());
    for (::pittore::core::log::Route r : {::pittore::core::log::Route::Paint,
                                           ::pittore::core::log::Route::Render,
                                           ::pittore::core::log::Route::Gpu,
                                           ::pittore::core::log::Route::Ux})
        std::remove(::pittore::core::log::route_log_path(r).c_str());
    // Per-tool logs live in their own subfolder (plus the tools rollup and the
    // crash breadcrumb) and start fresh with everything else.
    tool_log_reset();
}

void message_handler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    const bool verbose = (type == QtDebugMsg || type == QtInfoMsg);
    if (verbose && !logging_debug()) return;

    const char* sev = type == QtDebugMsg      ? "D"
                      : type == QtInfoMsg     ? "I"
                      : type == QtWarningMsg  ? "W"
                      : type == QtCriticalMsg ? "C"
                                              : "F";
    const quint64 tid = reinterpret_cast<quintptr>(QThread::currentThreadId());
    const QByteArray utf8 = msg.toUtf8();
    const QByteArray line = QString("[gui %1 %2 th=%3] %4")
                                .arg(QLatin1String(sev))
                                .arg(QDateTime::currentDateTime().toString(
                                    QStringLiteral("HH:mm:ss.zzz")))
                                .arg(tid)
                                .arg(QString::fromUtf8(utf8))
                                .toUtf8();

    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stderr);
    std::fputc('\n', stderr);
    std::lock_guard<std::mutex> lk_(::pittore::core::log::mutex());
    if (FILE* f = ::pittore::core::log::file_for(utf8.constData()); f) {
        std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), f);
        std::fputc('\n', f);
        std::fflush(f);
    }
}

void install_logging() {
    ::pittore::core::log::set_log_dir(log_dir().toStdString().c_str());
    reset_log_file();
    qInstallMessageHandler(message_handler);
    // Crash breadcrumbs name the tool + setup stage that was running; without
    // this a crash on a tool switch loses the answer we instrumented for.
    install_tool_crash_logging();
}

}  // namespace pittore::ui