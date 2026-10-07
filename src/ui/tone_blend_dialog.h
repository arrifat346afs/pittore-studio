#pragma once
#include <QDialog>

#include <functional>

#include "engine/compute/tone_blend.h"

class QComboBox;
class QDoubleSpinBox;
class QSlider;
class QTimer;

namespace pittore::ui {

class AppState;
struct LayerItem;
class DocumentItem;

// Live Tone Blend Group settings: Blend Strength / Color / Contrast /
// Low Pass sliders plus the Content Type combo. Mirrors the Layer Style
// dialog contract — live preview on the canvas while open (the group's
// params re-render at composite time, never baked into pixels), OK lands
// one history step, Cancel (and Esc) restores the params the group had when
// the dialog opened.
class ToneBlendDialog final : public QDialog {
    Q_OBJECT
  public:
    // Edits the tone-blend header at `groupIndex` in the active document.
    explicit ToneBlendDialog(AppState* state, int groupIndex,
                             QWidget* parent = nullptr);
    ~ToneBlendDialog() override;

  protected:
    void accept() override;
    void reject() override;

  private:
    void schedulePreview();
    void previewNow();
    // The header the dialog edits. Resolved by index on every use: the layer
    // stack is an implicitly-shared container, so a cached LayerItem* would
    // go stale the first time a snapshot shares (or a write detaches) it.
    // Null unless the index still addresses a live tone-blend group.
    LayerItem* targetGroup() const;

    AppState* state_ = nullptr;
    DocumentItem* doc_ = nullptr;
    int groupIndex_ = -1;
    pittore::compute::ToneBlendParams original_;  // restored on cancel
    pittore::compute::ToneBlendParams work_;      // edited live
    bool changed_ = false;
    bool previewPending_ = false;
    bool loading_ = false;  // suppress preview while seeding widgets
    // Interval preview throttle (leading immediate + ~15fps while ticks
    // arrive): tone pyramids are full-frame, so an unpaced slider storm
    // would serialize a pyramid build per tick on the UI thread.
    QTimer* previewTimer_ = nullptr;
};

}  // namespace pittore::ui
