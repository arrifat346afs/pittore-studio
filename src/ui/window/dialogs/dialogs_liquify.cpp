#include "ui/main_window.h"

#include "ui/app_state.h"
#include "ui/liquify_dialog.h"

namespace pittore::ui {

void MainWindow::liquifyDialog() {
    DocumentItem *doc = state_->activeDocument();
    LayerItem *layer = state_->activeLayer();
    if (!doc || !layer || !layer->pixels || layer->pixels->width() == 0 ||
        layer->pixels->height() == 0) {
        state_->setStatusHint(tr("Liquify needs an editable pixel layer."));
        return;
    }
    LiquifyDialog dialog(state_, this);
    dialog.exec();
    updateStatus();
    syncUndoRedo();
}

}
