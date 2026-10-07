#include "ui/window/shared/window_helpers.h"

#include <QChar>

namespace pittore::ui {

QString shortcutTextFor(char key) {
    return key ? QString(QChar::fromLatin1(key)) : QString();
}

QImage renderLayerForExport(DocumentItem* doc, int index) {
    if (!doc || index < 0 || index >= doc->layers.size()) return doc->composite;

    struct SoloGuard {
        DocumentItem* doc;
        QVector<bool> saved;
        explicit SoloGuard(DocumentItem* d) : doc(d) {
            saved.reserve(d->layers.size());
            for (const LayerItem& l : d->layers) saved.push_back(l.visible);
        }
        ~SoloGuard() {
            for (int i = 0; i < doc->layers.size(); ++i)
                doc->layers[i].visible = saved[i];
            doc->rebuildComposite();
        }
    } guard(doc);

    for (int i = 0; i < doc->layers.size(); ++i)
        doc->layers[i].visible = (i == index);
    doc->rebuildComposite();
    return doc->composite.copy();
}

}  // namespace pittore::ui
