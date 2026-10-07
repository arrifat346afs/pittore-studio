#pragma once

// GUI-side debug logging.
//
// Always on in alpha: the Qt message handler writes to stderr and to log
// files next to Settings.toml with no env var needed. Set PITTORE_DEBUG=0
// to silence verbose (Debug/Info) lines; warnings always land.
//     ./painter
//
// Files, all wiped per run:
//     pittore_debug.log   everything not routed below
//     Pittore-Paint/Render/GPU/UX.log   per-area routes (engine/core/log.h)
//     Pittore-Tools.log   tool setup + input, tagged with the tool name
//     tools/<id>-<slug>.log  ONE tool each: its setup stages, option specs,
//                            press/release — the file to open after a crash
//     Pittore-Crash.log   fatal signal/terminate dump: tool, stage, backtrace

#include <QString>
#include <QtGlobal>

#include <cstdlib>
#include <string>

namespace pittore::ui {

// True unless PITTORE_DEBUG=0 explicitly silences verbose logging.
// Logging is always on (alpha needs its logs); the env var is an opt-out,
// not an opt-in.
inline bool logging_debug() {
    static const bool on = [] {
        const char* e = std::getenv("PITTORE_DEBUG");
        return !(e && *e && std::string(e) == "0");
    }();
    return on;
}

// Log folder next to Settings.toml: ConfigLocation/PittoreStudio/log.
QString log_dir();

// Wipe the log files so each run starts fresh. Called once at startup.
void reset_log_file();

// Qt message handler: tags severity/time/thread, writes to stderr + log file.
void message_handler(QtMsgType type, const QMessageLogContext& context, const QString& msg);

// Install the handler and point the engine logger at the same folder.
// Lives in logging.cpp (kept out of main, which links non-PIC via nvcc).
void install_logging();

}  // namespace pittore::ui