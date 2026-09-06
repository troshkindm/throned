#pragma once

#include <QDialog>
#include <QStringList>
#include "ui_edit_openvpn_advanced.h"
#include "include/database/entities/Profile.h"

namespace Ui {
class EditOpenVPNAdvanced;
}

class EditOpenVPNAdvanced : public QDialog {
    Q_OBJECT

public:
    EditOpenVPNAdvanced(QWidget *parent, const std::shared_ptr<Configs::Profile> &_ent);

    ~EditOpenVPNAdvanced() override;

public slots:
    void accept() override;

private:
    void editPem(QPushButton *button, const QString &title, QStringList &target);

    void addServerRow(const QString &server, const QString &port, const QString &network);

    void addPullFilterRow(const QString &action, const QString &text);

    Ui::EditOpenVPNAdvanced *ui;
    std::shared_ptr<Configs::Profile> ent;

    struct {
        QStringList staticKey;
        QStringList peerFingerprint;
    } CACHE;
};
