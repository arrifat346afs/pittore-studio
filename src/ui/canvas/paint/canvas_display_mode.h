#pragma once
// Canvas display modes: full-fidelity paint plus cheaper fallbacks for
// heavy documents.
//
// Full renders everything (composite blit plus crisp vector overlay).
// Draft skips the crisp vector overlay and paints the composite blit only -
// pixel-identical wherever the overlay would have covered its own raster,
// softer where a vector would have drawn sharper than the document bake.
// Outline skips the composite entirely and draws hairline vector contours,
// so even hundred-thousand-layer documents stay interactive while editing.
//
// The governor flips between Full and Draft automatically from measured
// frame times (manual override always wins). The thresholds only engage on
// large documents, so small files never flicker between modes.
#include <optional>
#include <unordered_map>

namespace pittore::ui {

struct DocumentItem;

enum class CanvasDisplayMode { Full, Draft, Outline };

class DisplayModeGovernor {
public:
    static DisplayModeGovernor& instance();

    // Effective mode for a document: manual override, else the automatic
    // Full/Draft state. Never null: null document means Full.
    CanvasDisplayMode modeFor(const DocumentItem* doc);
    // Pin a mode (null clears back to automatic). Outline is only ever
    // engaged this way, never automatically.
    void setOverride(const DocumentItem* doc,
                     std::optional<CanvasDisplayMode> mode);
    std::optional<CanvasDisplayMode> overrideFor(const DocumentItem* doc) const;
    // Feed one measured paint time in milliseconds.
    void noteFrame(const DocumentItem* doc, double ms);
    // Drop bookkeeping for a closed document (also ages out naturally via
    // the state cap, so this is best-effort only).
    void forget(const DocumentItem* doc);

private:
    DisplayModeGovernor() = default;
    struct State {
        int slowFrames = 0;
        int fastFrames = 0;
        bool draft = false;
        std::optional<CanvasDisplayMode> manual;
    };
    mutable std::unordered_map<const DocumentItem*, State> states_;
};

}  // namespace pittore::ui
