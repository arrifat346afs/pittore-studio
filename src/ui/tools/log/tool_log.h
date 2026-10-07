#pragma once
// Per-tool logs: every tool's setup writes into its OWN file, so a crash names
// itself. Split from ui/tool_registry.h.
//
// Why this exists: with ~130 tools sharing one log line stream, a crash on a
// tool switch gave no way to tell which tool (or which setup stage) died.
// Now:
//     <ConfigLocation>/PittoreStudio/log/tools/006-lasso.log   one tool each
//     <ConfigLocation>/PittoreStudio/log/Pittore-Tools.log    rollup, tagged
//     <ConfigLocation>/PittoreStudio/log/Pittore-Crash.log    crash breadcrumb
//
// Every line is flushed on write (a crash must not eat the tail), and the
// crash handler records the tool + stage that was running when it died — the
// last line of that tool's own log is then the stage to debug.
//
// Setup stages covered: AppState::setActiveTool, the tools strip, the options
// bar (one line per OptionSpec), the contextual task bar, the canvas tool
// cursor/commit hooks, the main-window hint, plus input/mouse-press and
// input/mouse-release on the canvas.

#include "ui/tools/ids/tool_ids.h"

namespace pittore::ui {

// Wipe log/tools/, Pittore-Tools.log and Pittore-Crash.log so a run starts
// fresh. Called once at startup from install_logging(); safe to call again.
void tool_log_reset();

// Crash breadcrumb only: point Pittore-Crash.log at this tool/stage without
// writing a log line (the log calls do this too). Exposed for the crash test.
void tool_log_breadcrumb(ToolId id, const char* stage);

// Append one line to tool id's own log file, the tools rollup and stderr.
// `stage` is a short path like "options-bar/rebuild"; `detail` may be null.
void tool_log(ToolId id, const char* stage, const char* detail = nullptr);

// Same, printf-style detail (checked by the compiler where available).
void tool_logf(ToolId id, const char* stage, const char* fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

// RAII trace around one tool's activation: writes setup/begin + setup/end into
// that tool's own log and keeps the crash breadcrumb on it while alive.
class ToolSetupTrace {
  public:
    explicit ToolSetupTrace(ToolId id, const char* reason = "switch");
    ~ToolSetupTrace();

    ToolSetupTrace(const ToolSetupTrace&) = delete;
    ToolSetupTrace& operator=(const ToolSetupTrace&) = delete;

  private:
    ToolId id_;
};

// Install SIGSEGV/SIGBUS/SIGABRT/SIGFPE/SIGILL + std::terminate handlers that
// dump the tool, stage and per-tool log path into Pittore-Crash.log (plus a
// backtrace) before the process dies. Idempotent; called from install_logging().
void install_tool_crash_logging();

}  // namespace pittore::ui
