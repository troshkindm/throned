#pragma once

#include <QWidget>
#include "profile_editor.h"
#include "ui_edit_openvpn.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class EditOpenVPN;
}
QT_END_NAMESPACE

class EditOpenVPN : public QWidget, public ProfileEditor {
    Q_OBJECT

public:
    explicit EditOpenVPN(QWidget *parent = nullptr);

    ~EditOpenVPN() override;

    void onStart(std::shared_ptr<Configs::Profile> _ent) override;

    bool onEnd() override;

    QList<QPair<QPushButton *, QString>> get_editor_cached() override;

private:
    void editPem(QPushButton *button, const QString &title, QStringList &target);

    Ui::EditOpenVPN *ui;
    std::shared_ptr<Configs::Profile> ent;

    struct {
        QStringList certificate;
        QStringList clientCertificate;
        QStringList clientKey;
        QStringList controlWrapKey;
    } CACHE;
};
