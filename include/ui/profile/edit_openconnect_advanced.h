#pragma once

#include <QDialog>
#include <QStringList>
#include "ui_edit_openconnect_advanced.h"
#include "include/database/entities/Profile.h"

namespace Ui {
class EditOpenConnectAdvanced;
}

class EditOpenConnectAdvanced : public QDialog {
    Q_OBJECT

public:
    EditOpenConnectAdvanced(QWidget *parent, const std::shared_ptr<Configs::Profile> &_ent);

    ~EditOpenConnectAdvanced() override;

public slots:
    void accept() override;

private:
    void editPem(QPushButton *button, const QString &title, QStringList &target);

    void addTnccCertificateRow(const QStringList &certificate, const QString &path);

    void editTnccCertificate();

    void addFormEntryRow(const QString &formId, const QString &submissionKey, const QString &name,
                         const QString &value, bool promote);

    Ui::EditOpenConnectAdvanced *ui;
    std::shared_ptr<Configs::Profile> ent;

    struct {
        QStringList peerFingerprint;
        QStringList mcaCertificate;
        QStringList mcaKey;
    } CACHE;
};
