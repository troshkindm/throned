#pragma once

#include <QDialog>
#include "ui_edit_wireguard_amnezia.h"
#include "include/configs/outbounds/wireguard.h"

struct WireguardAmneziaOptions
{
    int jc = 0;
    int jmin = 0;
    int jmax = 0;
    int s1 = 0;
    int s2 = 0;
    int s3 = 0;
    int s4 = 0;
    QString h1;
    QString h2;
    QString h3;
    QString h4;
    QString i1;
    QString i2;
    QString i3;
    QString i4;
    QString i5;
    QString headerProtectionKey;
    QString contentPaddingAddition;
    QString rekeyAfterTime;
    QString rekeyTimeout;
    QString rejectAfterTime;
    QString keepaliveTimeout;
    QString maxHandshakeAttempts;
    bool randomTrailers = false;
    bool disableCookies = false;

    void load(const Configs::wireguard *outbound);
    void apply(Configs::wireguard *outbound) const;
};

namespace Ui {
class EditWireguardAmnezia;
}

class EditWireguardAmnezia : public QDialog
{
    Q_OBJECT

public:
    EditWireguardAmnezia(QWidget *parent, WireguardAmneziaOptions *options);

    ~EditWireguardAmnezia() override;

public slots:
    void accept() override;

private:
    Ui::EditWireguardAmnezia *ui;
    WireguardAmneziaOptions *options;
};
