// conventional "Embedded Profile Mismatch" dialog plus the canonical
// profile comparison behind it. Clean-room implementation (own layout and
// strings in house style).

#include "ui/color_mismatch.h"

#include "ui/settings.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QRadioButton>
#include <QVBoxLayout>

namespace pittore::ui {

ImportProfileChoice askImportedProfile(QWidget* parent,
                                       const QString& embedded,
                                       const QString& working) {
    QDialog dialog(parent);
    // QObject::tr needs a context object; the dialog itself serves (the
    // class is not Q_OBJECT, so no new tr context is introduced).
    dialog.setWindowTitle(QObject::tr("Embedded Profile Mismatch"));
    dialog.setMinimumWidth(440);
    // Conventional-behaviour "always ask" can land here with matching families
    // (same meaning, different bytes): say so instead of crying mismatch.
    const bool mismatch = canonicalProfileName(embedded) !=
                          canonicalProfileName(working);
    auto* column = new QVBoxLayout(&dialog);
    column->setContentsMargins(16, 16, 16, 16);
    column->setSpacing(10);
    auto* info = new QLabel(
        mismatch
            ? QObject::tr("The document has an embedded color profile that "
                          "does not match the working space.\nEmbedded: "
                          "%1\nWorking: %2")
                  .arg(embedded, working)
            : QObject::tr("The document has an embedded color profile with "
                          "the same meaning as the working space, but "
                          "different data.\nEmbedded: %1\nWorking: %2")
                  .arg(embedded, working),
        &dialog);
    info->setWordWrap(true);
    column->addWidget(info);

    auto* useEmbedded = new QRadioButton(
        QObject::tr("Use the embedded profile (instead of the working space)"),
        &dialog);
    auto* convert = new QRadioButton(
        QObject::tr("Convert document's colors to the working space"), &dialog);
    auto* discard = new QRadioButton(
        QObject::tr("Discard the embedded profile (don't color manage)"),
        &dialog);
    useEmbedded->setChecked(true);
    column->addWidget(useEmbedded);
    column->addWidget(convert);
    column->addWidget(discard);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog,
                     &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog,
                     &QDialog::reject);
    column->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted) return ImportProfileChoice::Cancel;
    if (convert->isChecked()) return ImportProfileChoice::ConvertToWorking;
    if (discard->isChecked()) return ImportProfileChoice::Discard;
    return ImportProfileChoice::UseEmbedded;
}

}  // namespace pittore::ui
