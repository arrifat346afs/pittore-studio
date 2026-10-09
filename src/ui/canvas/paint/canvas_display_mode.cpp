// Canvas display-mode governor: automatic Full/Draft switching plus manual
// overrides. GUI thread only.
#include "ui/canvas/paint/canvas_display_mode.h"

#include "ui/app_state.h"
#include "engine/core/log.h"

namespace pittore::ui {

namespace {

// A single slow frame proves nothing (shader compile, window drag); three
// in a row means the document is genuinely heavy. Recovery needs a full
// second of fast frames so the mode does not flap on borderline docs.
constexpr double kSlowMs = 50.0;
constexpr double kFastMs = 25.0;
constexpr int kSlowFrames = 3;
constexpr int kFastFrames = 60;
// Small documents never degrade: their frames are cheap already and a
// stray hitch should not visibly change rendering.
constexpr int kMinAutoLayers = 2000;
// Closed-document states are dropped by forget(); this caps the map anyway.
constexpr std::size_t kMaxStates = 64;

}  // namespace

DisplayModeGovernor& DisplayModeGovernor::instance() {
    static DisplayModeGovernor gov;
    return gov;
}

CanvasDisplayMode DisplayModeGovernor::modeFor(const DocumentItem* doc) {
    if (!doc) return CanvasDisplayMode::Full;
    const auto it = states_.find(doc);
    if (it != states_.end() && it->second.manual)
        return *it->second.manual;
    const bool heavy = (int)doc->layers.size() >= kMinAutoLayers;
    const bool draft =
        heavy && it != states_.end() && it->second.draft;
    return draft ? CanvasDisplayMode::Draft : CanvasDisplayMode::Full;
}

void DisplayModeGovernor::setOverride(
    const DocumentItem* doc, std::optional<CanvasDisplayMode> mode) {
    if (!doc) return;
    states_[doc].manual = mode;
}

std::optional<CanvasDisplayMode> DisplayModeGovernor::overrideFor(
    const DocumentItem* doc) const {
    if (!doc) return std::nullopt;
    const auto it = states_.find(doc);
    if (it == states_.end()) return std::nullopt;
    return it->second.manual;
}

void DisplayModeGovernor::noteFrame(const DocumentItem* doc, double ms) {
    if (!doc) return;
    if ((int)doc->layers.size() < kMinAutoLayers) return;
    if (states_.size() >= kMaxStates && !states_.count(doc)) {
        // Simplest bounded eviction: drop a non-manual entry.
        for (auto it = states_.begin(); it != states_.end(); ++it) {
            if (!it->second.manual) {
                states_.erase(it);
                break;
            }
        }
        if (states_.size() >= kMaxStates) return;
    }
    State& st = states_[doc];
    if (st.manual) return;
    if (ms >= kSlowMs) {
        st.fastFrames = 0;
        if (!st.draft && ++st.slowFrames >= kSlowFrames) {
            st.draft = true;
            st.slowFrames = 0;
            ::pittore::core::log::log_info(
                "[render][display-mode] doc=%p layers=%d -> Draft "
                "(frame %.1f ms over %.0f ms x%d)",
                (const void*)doc, (int)doc->layers.size(), ms, kSlowMs,
                kSlowFrames);
        }
        return;
    }
    st.slowFrames = 0;
    if (st.draft && ms <= kFastMs && ++st.fastFrames >= kFastFrames) {
        st.draft = false;
        st.fastFrames = 0;
        ::pittore::core::log::log_info(
            "[render][display-mode] doc=%p layers=%d -> Full (recovered)",
            (const void*)doc, (int)doc->layers.size());
    }
}

void DisplayModeGovernor::forget(const DocumentItem* doc) {
    if (doc) states_.erase(doc);
}

}  // namespace pittore::ui
