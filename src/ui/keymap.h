#pragma once
#include <QMap>
#include <QString>

class QKeyEvent;

namespace pittore::ui {

QString keymapFilePath();
QMap<QString, QString> defaultKeymapEntries();
QMap<QString, QString> loadKeymapEntries();
bool saveKeymapEntries(const QMap<QString, QString> &entries);

// True when a key press invokes the stored shortcut `seq` (PortableText).
// Single-key bindings only; Shift is the brush hardness flag, not part of
// the binding, so it is ignored on both sides. Empty/unparseable/multi-key
// sequences never match.
bool keyEventMatchesShortcut(const QKeyEvent *key, const QString &seq);

}
