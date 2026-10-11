#pragma once

#include <QDialog>
#include <QList>
#include <QSet>
#include <memory>

#include "include/database/entities/IpScan.h"
#include "ui_dialog_scanner.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogScanner;
}
QT_END_NAMESPACE

class ScanItem;

class DialogScanner : public QDialog {
    Q_OBJECT

public:
    explicit DialogScanner(QWidget *parent = nullptr);

    ~DialogScanner() override;

protected:
    void showEvent(QShowEvent *event) override;

    void hideEvent(QHideEvent *event) override;

private:
    Ui::DialogScanner *ui;

    QList<std::shared_ptr<Configs::IpScan>> scans;

    QSet<int> watchedIds;

    bool showing = false;

    void reloadScans();

    void syncWatched();

    [[nodiscard]] ScanItem *itemFor(int scanId) const;

    void refreshItem(int scanId, bool reload) const;

    void startOrPause(int scanId);

    void deleteScan(int scanId);

    void moveScan(int from, int to);

    void newScan();
};
