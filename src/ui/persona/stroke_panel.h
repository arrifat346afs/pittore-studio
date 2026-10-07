#pragma once
// Stroke panel (ui/persona): fill/stroke/width/cap/join/dash/opacity for the
// selected vector layer's retained ArtNode. Each discrete change is one undo
// step via AppState::applyVectorPaint.
#include <QWidget>

#include "engine/vector/vector_art.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;

namespace pittore::ui {

class AppState;

class StrokePanel final : public QWidget {
    Q_OBJECT

  public:
    explicit StrokePanel(AppState* state, QWidget* parent = nullptr);

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
    QComboBox* capCombo_ = nullptr;
    QComboBox* joinCombo_ = nullptr;
    QLabel* profileLabel_ = nullptr;
    QPushButton* profileReset_ = nullptr;
    QComboBox* dashCombo_ = nullptr;
    QDoubleSpinBox* dashOnSpin_ = nullptr;
    QDoubleSpinBox* dashOffSpin_ = nullptr;
    QDoubleSpinBox* dashOffsetSpin_ = nullptr;
    QSlider* opacitySlider_ = nullptr;
    QLabel* opacityLabel_ = nullptr;
    int layerIndex_ = -1;
};

}  // namespace pittore::ui
