#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QLineEdit>
#include <QList>

#include <functional>

// Ties a control to the setting it edits.
//
// A preference used to be described in three places that drifted apart: the widget in
// the .ui, a setChecked() in one block near the top of the dialog and an isChecked()
// in accept() six hundred lines below. Forgetting one of the three is silent -- the
// setting simply stops persisting, or the control is left behind when its page is
// rebuilt and the next read touches freed memory.
//
// Binding once removes the choice. Whatever is bound is loaded and saved; whatever is
// not bound was never meant to be.
class SettingsBindings {
public:
    void bind(QCheckBox *box, bool &target) {
        load_ << [box, &target] { box->setChecked(target); };
        save_ << [box, &target] { target = box->isChecked(); };
    }

    void bind(QLineEdit *edit, QString &target) {
        load_ << [edit, &target] { edit->setText(target); };
        save_ << [edit, &target] { target = edit->text(); };
    }

    // Integer fields keep their existing habit of leaving the setting alone when the
    // box is empty or unparseable, rather than silently writing a zero.
    void bind(QLineEdit *edit, int &target) {
        load_ << [edit, &target] { edit->setText(QString::number(target)); };
        save_ << [edit, &target] {
            bool ok = false;
            if (const int value = edit->text().toInt(&ok); ok) target = value;
        };
    }

    void bind(QComboBox *combo, QString &target) {
        load_ << [combo, &target] { combo->setCurrentText(target); };
        save_ << [combo, &target] { target = combo->currentText(); };
    }

    void load() const {
        for (const auto &fn: load_) fn();
    }

    void save() const {
        for (const auto &fn: save_) fn();
    }

private:
    QList<std::function<void()>> load_;
    QList<std::function<void()>> save_;
};
