#pragma once

#include <QWidget>
#include "profile_editor.h"
#include "ui_edit_openconnect.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class EditOpenConnect;
}
QT_END_NAMESPACE

class EditOpenConnect : public QWidget, public ProfileEditor {
    Q_OBJECT

public:
    explicit EditOpenConnect(QWidget *parent = nullptr);

    ~EditOpenConnect() override;

    void onStart(std::shared_ptr<Configs::Profile> _ent) override;

    bool onEnd() override;

    QList<QPair<QPushButton *, QString>> get_editor_cached() override;

private:
    void editPem(QPushButton *button, const QString &title, QStringList &target);

    Ui::EditOpenConnect *ui;
    std::shared_ptr<Configs::Profile> ent;

    struct {
        QStringList certificateAuthority;
        QStringList clientCertificate;
        QStringList clientKey;
    } CACHE;
};
