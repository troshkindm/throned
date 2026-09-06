#pragma once

#include <QWidget>
#include "profile_editor.h"
#include "ui_edit_autoselector.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class EditAutoSelector;
}
QT_END_NAMESPACE

class EditAutoSelector : public QWidget, public ProfileEditor {
    Q_OBJECT

public:
    explicit EditAutoSelector(QWidget *parent = nullptr);

    ~EditAutoSelector() override;

    void onStart(std::shared_ptr<Configs::Profile> _ent) override;

    bool onEnd() override;

private:
    Ui::EditAutoSelector *ui;
    std::shared_ptr<Configs::Profile> ent;
    bool m_loading = false;

    void refreshPlanSummary();

    void updateBalanceEnabled() const;

    void refreshPinnedRow() const;

    int m_pinnedID = -1;

    void mirrorTooltipsToLabels() const;

    void resizeDialogToContent();
};
