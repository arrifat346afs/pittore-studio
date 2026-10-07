#pragma once
// Owns the active persona plus per-persona tool/panel memory.
//
// Applies switches through injected callbacks and the public ToolsPanel API
// (hiddenTools/setHiddenTools), so this folder never includes MainWindow and
// no core file is modified. See vector.md §6.1–6.3.
#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

#include "ui/persona/persona.h"

namespace pittore::ui {

class AppState;
class ToolsPanel;

class PersonaManager final : public QObject {
    Q_OBJECT

  public:
    using ShowPanel = std::function<void(const QString& id, bool show)>;
    using PanelVisible = std::function<bool(const QString& id)>;

    explicit PersonaManager(AppState* state, ToolsPanel* tools,
                            QObject* parent = nullptr);

    void setShowPanel(ShowPanel fn) { showPanel_ = std::move(fn); }
    void setPanelVisible(PanelVisible fn) { panelVisible_ = std::move(fn); }

    Persona persona() const { return persona_; }
    void setPersona(Persona p);
    void applyCurrent();  // re-apply the loaded persona (startup sync)

    void noteHiddenTools(Persona p, const QStringList& hidden) {
        hiddenCache_.insert(static_cast<int>(p), hidden);
    }
    QStringList rememberedHiddenTools(Persona p) const {
        return hiddenCache_.value(static_cast<int>(p));
    }

  signals:
    void personaChanged(Persona p);

  private:
    void apply(Persona p);
    void load();
    void save() const;

    AppState* state_;
    ToolsPanel* tools_;
    ShowPanel showPanel_;
    PanelVisible panelVisible_;
    Persona persona_ = Persona::Pixel;
    Persona previous_ = Persona::Pixel;  // outgoing tab (panel restore key)
    QHash<int, QStringList> hiddenCache_;
    // Pixel panel layout captured on first departure (any non-Pixel tab),
    // restored on return. Keyed per departure target so Vector and Draw
    // round-trips don't clobber each other.
    QHash<int, QStringList> capturedPanels_;
    QHash<int, bool> panelsCaptured_;
};

}  // namespace pittore::ui
