#pragma once

#include <QDialog>

#include "ui_dialog_traffic_stats.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class DialogTrafficStats;
}
QT_END_NAMESPACE

class DialogTrafficStats : public QDialog {
    Q_OBJECT

public:
    explicit DialogTrafficStats(QWidget* parent = nullptr);
    ~DialogTrafficStats() override;

private:
    void refresh();
    void populateProfileTable(long long fromSecs, long long toSecs);
    void populateAppTable(long long fromSecs, long long toSecs);

    long long selectedWindowSecs() const;
    long long selectedBucketSecs() const;

    Ui::DialogTrafficStats* ui;
};
