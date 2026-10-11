#pragma once

#include <QDialog>
#include <QHash>
#include <QList>
#include <memory>

#include "include/database/entities/IpList.h"
#include "ui_dialog_ip_lists.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogIpLists;
}
QT_END_NAMESPACE

class QTimer;

class DialogIpLists : public QDialog {
    Q_OBJECT

public:
    explicit DialogIpLists(QWidget *parent = nullptr);

    ~DialogIpLists() override;

    void selectList(int id);

private:
    Ui::DialogIpLists *ui;

    QList<std::shared_ptr<Configs::IpList>> lists;

    QTimer *reloadTimer = nullptr;

    bool importing = false;

    bool rebuildPending = false;

    int pendingSelectId = -1;

    void reloadLists(bool deferRebuild = false);

    void rebuildRows(const QList<std::shared_ptr<Configs::IpList>> &fresh, const QHash<int, QString> &scanNames);

    bool selectRow(int id);

    void refreshBusyStates() const;

    [[nodiscard]] std::shared_ptr<Configs::IpList> listById(int id) const;

    [[nodiscard]] std::shared_ptr<Configs::IpList> selectedList() const;

    void updateButtons() const;

    void createList(Configs::IpList::SourceKind kind, const QString &prefillText = {});

    void editList(int id);

    void refreshList(int id);

    void deleteList(const std::shared_ptr<Configs::IpList> &list);

    void moveList(int from, int to);

    void importFromFiles();

private slots:
    void on_new_list_clicked();

    void on_duplicate_clicked();

    void on_export_list_clicked();
};
