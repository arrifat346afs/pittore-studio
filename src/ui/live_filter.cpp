// Live (Smart) filter layers (AppState ops defined here, not in
// app_state.cpp, per the new-source-files rule; only declared in app_state.h).
#include "ui/live_filter.h"

#include <algorithm>
#include <cmath>

#include "engine/filter/filters.h"
#include "engine/filter/registry/filter_registry.h"
#include "ui/app_state.h"

namespace pittore::ui {

const pittore::Image* liveFilterBase(const LayerItem& l) {
    if (l.hasLiveFilter && l.liveFilterEnabled && l.filtered &&
        l.filteredValid)
        return l.filtered.get();
    return l.pixels.get();
}

void ensureLayerFilter(const LayerItem& l) {
    auto drop = [&l] {
        if (l.filtered) {
            l.filtered.reset();
            ++l.styledRev;
            // The styled raster renders over this base; dropping the base
            // drops the style render too (it rebuilds lazily).
            l.styledValid = false;
        }
        // No filter state: leave the style cache strictly alone (a pure
        // translation must not re-render it).
    };
    if (!l.hasLiveFilter || !l.liveFilterEnabled || !l.pixels ||
        l.liveFilterId.isEmpty()) {
        drop();
        return;
    }
    const std::string id = l.liveFilterId.toStdString();
    if (!pittore::filter::findFilter(id)) {
        drop();  // unknown id (e.g. future filter): plain pixels, no crash
        return;
    }
    if (l.filteredValid && l.filtered && l.filteredStamp == l.sourceStamp &&
        l.filteredId == id && l.filteredParams == l.liveFilterParams &&
        l.filtered->width() == l.pixels->width() &&
        l.filtered->height() == l.pixels->height())
        return;
    auto out = std::make_shared<pittore::Image>(*l.pixels);
    pittore::filter::applyFilter(*out, id, l.liveFilterParams);
    if (out->width() != l.pixels->width() ||
        out->height() != l.pixels->height()) {
        drop();  // size-changing filters can't ride as an overlay
        return;
    }
    l.filtered = std::move(out);
    l.filteredStamp = l.sourceStamp;
    l.filteredId = id;
    l.filteredParams = l.liveFilterParams;
    l.filteredValid = true;
    l.styledValid = false;
    ++l.styledRev;
}

bool AppState::convertToLiveFilter(const QString& filterId) {
    return convertToLiveFilter(
        filterId, pittore::filter::defaultParams(filterId.toStdString()));
}

bool AppState::convertToLiveFilter(const QString& filterId,
                                   const std::vector<double>& params) {
    DocumentItem* d = activeDocument();
    LayerItem* layer = activeLayer();
    // Mirrors isPaintable in app_state.cpp (visible pixel layer, not
    // transparency-locked).
    if (!d || !layer || !layer->visible ||
        layer->kind != LayerItem::Kind::Pixel || layer->lockTransparency ||
        !layer->pixels) {
        setStatusHint(tr("Need an editable pixel layer."));
        return false;
    }
    const std::string id = filterId.toStdString();
    const pittore::filter::FilterDef* def =
        pittore::filter::findFilter(id);
    if (!def) {
        setStatusHint(tr("Unknown filter."));
        return false;
    }
    beginUndoStep();
    layer = activeLayer();
    layer->hasLiveFilter = true;
    layer->liveFilterEnabled = true;
    layer->liveFilterId = filterId;
    layer->liveFilterParams = params;
    layer->filtered.reset();
    layer->filteredValid = false;
    layer->styledValid = false;
    ++layer->styledRev;
    commitUndoStep(tr("Convert for Smart Filters"), QStringLiteral("filter"));
    d->rebuildComposite();
    emit layersChanged();
    emit documentModified(d);
    return true;
}

bool AppState::setLiveFilterParam(int index, double value) {
    DocumentItem* d = activeDocument();
    LayerItem* l = activeLayer();
    if (!d || !l || !l->hasLiveFilter || !l->liveFilterEnabled ||
        index < 0 ||
        index >= static_cast<int>(l->liveFilterParams.size()))
        return false;
    l->liveFilterParams[static_cast<std::size_t>(index)] = value;
    // Slider contract (like opacity/adjustment sliders): live, no undo step.
    // The cache rebuilds on demand; styledRev bumps inside ensureLayerFilter
    // only when the render actually changes... force it here so drags never
    // sample a stale device upload.
    l->filteredValid = false;
    ++l->styledRev;
    d->rebuildComposite();
    emit documentModified(d);
    return true;
}

bool AppState::setLiveFilterEnabled(bool enabled) {
    DocumentItem* d = activeDocument();
    LayerItem* layer = activeLayer();
    if (!d || !layer || !layer->hasLiveFilter ||
        layer->liveFilterEnabled == enabled)
        return false;
    beginUndoStep();
    activeLayer()->liveFilterEnabled = enabled;
    commitUndoStep(enabled ? tr("Enable Live Filter")
                           : tr("Disable Live Filter"),
                   QStringLiteral("filter"));
    d->rebuildComposite();
    emit layersChanged();
    emit documentModified(d);
    return true;
}

bool AppState::removeLiveFilter() {
    DocumentItem* d = activeDocument();
    LayerItem* layer = activeLayer();
    if (!d || !layer || !layer->hasLiveFilter) {
        if (d && layer) setStatusHint(tr("No live filter to remove."));
        return false;
    }
    // Native pixels were never mutated: removing the recipe simply reveals
    // them (plus any destructive edits made before conversion).
    beginUndoStep();
    layer = activeLayer();
    layer->hasLiveFilter = false;
    layer->liveFilterEnabled = true;
    layer->liveFilterId.clear();
    layer->liveFilterParams.clear();
    layer->filtered.reset();
    layer->filteredValid = false;
    layer->styledValid = false;
    ++layer->styledRev;
    commitUndoStep(tr("Remove Live Filter"), QStringLiteral("filter"));
    d->rebuildComposite();
    emit layersChanged();
    emit documentModified(d);
    return true;
}

}  // namespace pittore::ui
