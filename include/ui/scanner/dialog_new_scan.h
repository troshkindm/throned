#pragma once

#include <QDialog>

class QLineEdit;
class QRadioButton;

class DialogNewScan : public QDialog {
    Q_OBJECT

public:
    explicit DialogNewScan(QWidget *parent = nullptr);

    [[nodiscard]] int CreatedScanId() const { return createdId; }

    void accept() override;

private:
    QLineEdit *nameEdit;
    QRadioButton *genericRadio;
    QRadioButton *warpRadio;
    int createdId = -1;
};
