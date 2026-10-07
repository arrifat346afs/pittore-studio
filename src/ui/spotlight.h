#pragma once
#include <QDialog>
#include <QString>
#include <QVector>
#include <functional>

class QListWidget;
class QLineEdit;

namespace pittore::ui {

struct SpotlightEntry {
    QString title;
    QString hint;
    QString group;
    std::function<void()> run;
};

class SpotlightDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SpotlightDialog(QVector<SpotlightEntry> entries, QWidget *parent = nullptr);
    void setEntries(QVector<SpotlightEntry> entries);

private:
    void rebuild(const QString &query);
    QLineEdit *search_ = nullptr;
    QListWidget *list_ = nullptr;
    QVector<SpotlightEntry> all_;
};

}
