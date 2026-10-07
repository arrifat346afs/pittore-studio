// test_keymap.cpp — keyEventMatchesShortcut(): single-key matching with
// Shift reserved as the brush hardness flag. No config file, no widgets.
#include <cstdio>

#include <QKeyEvent>

#include "test_util.h"
#include "ui/keymap.h"

int main() {
    using pittore::ui::keyEventMatchesShortcut;
    const QKeyEvent bareLeft(QEvent::KeyPress, Qt::Key_BracketLeft,
                             Qt::NoModifier, QStringLiteral("["));
    const QKeyEvent shiftLeft(QEvent::KeyPress, Qt::Key_BracketLeft,
                              Qt::ShiftModifier, QStringLiteral("{"));
    const QKeyEvent ctrlLeft(QEvent::KeyPress, Qt::Key_BracketLeft,
                             Qt::ControlModifier, QStringLiteral("["));
    const QKeyEvent bareRight(QEvent::KeyPress, Qt::Key_BracketRight,
                              Qt::NoModifier, QStringLiteral("]"));
    const QKeyEvent ctrlB(QEvent::KeyPress, Qt::Key_B, Qt::ControlModifier,
                          QStringLiteral("b"));
    const QKeyEvent bareB(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier,
                          QStringLiteral("b"));
    const QKeyEvent bareX(QEvent::KeyPress, Qt::Key_X, Qt::NoModifier,
                          QStringLiteral("x"));
    // Defaults: [ / ] shrink/grow, Shift ignored (hardness flag).
    CHECK(keyEventMatchesShortcut(&bareLeft, QStringLiteral("[")));
    CHECK(keyEventMatchesShortcut(&shiftLeft, QStringLiteral("[")));
    CHECK(!keyEventMatchesShortcut(&ctrlLeft, QStringLiteral("[")));
    CHECK(!keyEventMatchesShortcut(&bareRight, QStringLiteral("[")));
    CHECK(keyEventMatchesShortcut(&bareRight, QStringLiteral("]")));
    // Modifier bindings compare exactly (minus Shift).
    CHECK(keyEventMatchesShortcut(&ctrlB, QStringLiteral("Ctrl+B")));
    CHECK(!keyEventMatchesShortcut(&bareB, QStringLiteral("Ctrl+B")));
    CHECK(keyEventMatchesShortcut(&bareX, QStringLiteral("X")));
    // Empty, garbage and multi-key sequences never match.
    CHECK(!keyEventMatchesShortcut(&bareLeft, QString()));
    CHECK(!keyEventMatchesShortcut(&bareLeft, QStringLiteral("Ctrl+K,Ctrl+U")));
    CHECK(!keyEventMatchesShortcut(nullptr, QStringLiteral("[")));

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
