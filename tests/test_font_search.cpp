// test_font_search.cpp — the Character panel family combo is searchable:
// typing a substring + Enter resolves exact match first (case-insensitive),
// then first contains match; garbage reverts to the current item. Popup
// picks keep flowing through currentIndexChanged untouched.
// Runs headless (QT_QPA_PLATFORM=offscreen).
#include <cstdio>

#include <QApplication>
#include <QComboBox>
#include <QCompleter>
#include <QKeyEvent>
#include <QLineEdit>

#include "test_util.h"
#include "ui/app_state.h"
#include "ui/panels/registry/panel_creators.h"

using namespace pittore::ui;

namespace {

void sendReturn(QLineEdit* edit) {
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(edit, &press);
    QKeyEvent release(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(edit, &release);
}

}  // namespace

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    AppState state;
    DocumentItem* doc = state.addDocument(QStringLiteral("f"), QSize(400, 300), 300);
    CHECK(doc != nullptr);
    if (!doc) return 1;

    QWidget window;
    QWidget* panel = createCharacterPanel(&state, &window);
    window.show();
    app.processEvents();
    auto* family =
        panel->findChild<QComboBox*>(QStringLiteral("character.family"));
    CHECK(family != nullptr);
    if (!family) return 1;
    CHECK(family->count() > 1);
    CHECK(family->isEditable());

    // Completer configured for contains-match popup search.
    auto* completer = family->completer();
    CHECK(completer != nullptr);
    if (completer) {
        CHECK(completer->filterMode() == Qt::MatchContains);
        CHECK(completer->caseSensitivity() == Qt::CaseInsensitive);
    }

    const int initial = family->currentIndex();
    const QString firstItem = family->itemText(0);
    const QString lastItem = family->itemText(family->count() - 1);

    // Substring + Enter jumps to the first contains match.
    const QString needle = lastItem.mid(0, qMax(2, lastItem.size() / 2));
    family->lineEdit()->setText(needle);
    sendReturn(family->lineEdit());
    app.processEvents();
    const int landed = family->currentIndex();
    CHECK(family->itemText(landed).contains(needle, Qt::CaseInsensitive));

    // Exact name with different casing resolves too.
    family->lineEdit()->setText(firstItem.toUpper());
    sendReturn(family->lineEdit());
    app.processEvents();
    CHECK_EQ(family->currentIndex(), 0);

    // Garbage reverts to the current item instead of sticking.
    family->lineEdit()->setText(QStringLiteral("\x01\x02no-such-font\x03\x04"));
    sendReturn(family->lineEdit());
    app.processEvents();
    CHECK_EQ(family->currentIndex(), 0);
    CHECK(family->lineEdit()->text() == firstItem);
    Q_UNUSED(initial);

    std::printf("  checks=%d failures=%d\n", pittore_test::checks(),
                pittore_test::failures());
    return pittore_test::failures() == 0 ? 0 : 1;
}
