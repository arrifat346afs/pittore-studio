#pragma once
#include <QDialog>

#include <functional>

#include "engine/render/layer_style.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QSlider;
class QStackedWidget;
class QTimer;
class QToolButton;

namespace pittore::ui {

class AppState;
struct LayerItem;
class DocumentItem;

// The Layer Style order the effect list and the Layer > Layer Style menu share.
enum class StyleEffect {
    Bevel,
    Stroke,
    InnerShadow,
    InnerGlow,
    Satin,
    ColorOverlay,
    GradientOverlay,
    OuterGlow,
    DropShadow,
    Blur,
    Count,
};

// conventional Layer Effects dialog: a checkable effect list down the
// left, the selected effect's settings on the right, and a live preview on
// the canvas (the layer's style is re-rendered at composite time, never
// baked into its pixels). OK is one history step; Cancel (and Esc) restores
// the style the layer had when the dialog opened.
class LayerStyleDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit LayerStyleDialog(AppState* state, QWidget* parent = nullptr);
    ~LayerStyleDialog() override;

    // Bring up the dialog with `effect` selected and enabled.
    void selectEffect(StyleEffect effect);

  protected:
    void accept() override;
    void reject() override;

  private:
    void buildEffectList(QWidget* parent);
    void buildPages(QWidget* parent);
    QWidget* buildBevelPage();
    QWidget* buildStrokePage();
    QWidget* buildShadowPage(bool inner);
    QWidget* buildGlowPage(bool inner);
    QWidget* buildSatinPage();
    QWidget* buildColorOverlayPage();
    QWidget* buildGradientOverlayPage();
    QWidget* buildBlurPage();

    void setEffectEnabled(StyleEffect effect, bool on);
    void updateFooterStatus();
    void schedulePreview();    void previewNow();
    void settleFullPreview();
    // True when the targets' combined bake footprint exceeds the frame
    // budget (proxy previews pay off); false renders full-res throughout.
    bool wantProxyPreview() const;
    // The layers the dialog edits (multi-select and group folders expand to
    // their styleable rows via AppState::fxTargetLayers). Resolved by index
    // on every use: the layer stack is an implicitly-shared container, so a
    // cached LayerItem* would go stale the first time a snapshot shares (or
    // a write detaches) the stack.
    int targetCount() const;
    LayerItem* targetAt(int i) const;

    AppState* state_ = nullptr;
    DocumentItem* doc_ = nullptr;
    QVector<int> targetIndices_;
    QVector<pittore::render::LayerStyle> originals_;  // restored on cancel
    pittore::render::LayerStyle work_;       // edited live

    QListWidget* list_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QLabel* footerStatus_ = nullptr;
    bool changed_ = false;
    bool previewPending_ = false;
    // Set on the leading edge when wantProxyPreview() says the burst is
    // heavy: ticks bake at the proxy cap until idle settles full res.
    bool proxyActive_ = false;
    bool loading_ = false;   // suppress preview while seeding widgets
    // Interval preview throttle: leading immediate render plus a repeating
    // timer while ticks arrive, settling to full res on idle. Accept/reject
    // stop it and restore the full bake cap. See schedulePreview().
    QTimer* previewTimer_ = nullptr;
};

}  // namespace pittore::ui
