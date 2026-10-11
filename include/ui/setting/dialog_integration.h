#pragma once

#include <QDialog>
#include "include/global/Configs.hpp"
#include "ui_dialog_integration.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogIntegration;
}
QT_END_NAMESPACE

class DialogIntegration : public QDialog {
    Q_OBJECT

public:
    explicit DialogIntegration(QWidget *parent = nullptr, const QList<QAction *> &actions = {});

    ~DialogIntegration() override;

public slots:
    void accept() override;

    void reject() override;

private:
    void generateShortcutItems(const QList<QAction *> &actions);

    void setupApiTab();

    void updateApiControls();

    void regenerateApiKey();

    void copyForScripts();

    static bool isValidAllowEntry(const QString &entry);

    QMap<QtExtKeySequenceEdit *, QString> seqEdit2ID;
    QString savedApiKey;
    Ui::DialogIntegration *ui;
};
