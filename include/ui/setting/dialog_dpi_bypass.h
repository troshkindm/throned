#pragma once

#include <QDialog>

#include "ui_dialog_dpi_bypass.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogDpiBypass;
}
QT_END_NAMESPACE

class DialogDpiBypass : public QDialog {
    Q_OBJECT

public:
    explicit DialogDpiBypass(QWidget *parent = nullptr);

    ~DialogDpiBypass() override;

private:
    void loadDpiSettings() const;

    void saveDpiSettings() const;

    void refreshDpiEnabledState() const;

    void applyDpiPreset();

    Ui::DialogDpiBypass *ui;
};
