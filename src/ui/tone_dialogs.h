#pragma once
// Tonal / filter dialogs with a live preview on the active pixel layer
// (R49 Add Noise / Median / Unsharp Mask/Sharpen, R54 Levels / Curves).
//
// Each dialog edits the layer's native pixels in place through one undo step:
// the first parameter change opens the session (AppState::beginTonalEdit, which
// snapshots + copy-on-writes), every preview re-applies the parameters and
// recomposites, and Accept promotes the single history step while Cancel
// restores the pre-edit pixels. See app_state.h for the session contract.
#include <QDialog>
#include <QPointer>
#include <QString>
#include <QVector>

namespace pittore::ui {

class AppState;

class LevelsDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit LevelsDialog(AppState* state, QWidget* parent = nullptr);

  private:
    void preview();
    class Impl;
    Impl* impl_;
};

class CurvesDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit CurvesDialog(AppState* state, QWidget* parent = nullptr);
    ~CurvesDialog() override;

  private:
    void preview();
    class Impl;
    Impl* impl_;
};

class AddNoiseDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit AddNoiseDialog(AppState* state, QWidget* parent = nullptr);

  private:
    void preview();
    class Impl;
    Impl* impl_;
};

class MedianDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit MedianDialog(AppState* state, QWidget* parent = nullptr);

  private:
    void preview();
    class Impl;
    Impl* impl_;
};

class UnsharpMaskDialog final : public QDialog {
    Q_OBJECT
  public:
    explicit UnsharpMaskDialog(AppState* state, QWidget* parent = nullptr);

  private:
    void preview();
    class Impl;
    Impl* impl_;
};

}  // namespace pittore::ui