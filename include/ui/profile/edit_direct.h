#pragma once

#include <QWidget>
#include "profile_editor.h"
#include "ui_edit_direct.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class EditDirect;
}
QT_END_NAMESPACE

class EditDirect : public QWidget, public ProfileEditor {
    Q_OBJECT

public:
    explicit EditDirect(QWidget *parent = nullptr);

    ~EditDirect() override;

    void onStart(std::shared_ptr<Configs::Profile> _ent) override;

    bool onEnd() override;

private:
    Ui::EditDirect *ui;
    std::shared_ptr<Configs::Profile> ent;
};