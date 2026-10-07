#pragma once
// Appearance panel v1 (ui/persona): the per-object fill/stroke list.
// Single fill + single stroke today; Add buttons are inert until the stacked
// model lands (Phase 2b). Same apply path as the Stroke panel.
#include <QWidget>

#include "engine/vector/vector_art.h"

class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace pittore::ui {

class AppState;

class AppearancePanel final : public QWidget {
    Q_OBJECT

  public:
    explicit AppearancePanel(AppState* state, QWidget* parent = nullptr);

  private:
    void rebuild();
    void commit(const pittore::vector::ArtPaint& paint, double opacity,
                const QString& undoName);
    void pickColor(bool fill);

    AppState* state_;
    QLabel* title_ = nullptr;
    QCheckBox* fillCheck_ = nullptr;
    QPushButton* fillButton_ = nullptr;
    QCheckBox* strokeCheck_ = nullptr;
    QPushButton* strokeButton_ = nullptr;
    QDoubleSpinBox* widthSpin_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QLabel* opacityLabel_ = nullptr;
    QPushButton* addStrokeButton_ = nullptr;
    QPushButton* addFillButton_ = nullptr;
    int layerIndex_ = -1;
};

}  // namespace pittore::ui
