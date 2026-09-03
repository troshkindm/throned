#pragma once

#include <QDialog>

#include "include/ui/mainWindow/TestRunner.h"

class QLabel;
class QTableWidget;

// Profiles down the side, sites across the top. The question it answers is the one
// people actually ask a node — "does this one get me to ChatGPT" — which no single
// latency number can.
class SiteReachabilityDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SiteReachabilityDialog(QWidget *parent = nullptr);

    // Rows appear as soon as the test starts so the dialog is never an empty box.
    void beginRun(const QList<int> &profileIDs, const QStringList &sites);

    void applyReport(const TestRunner::SiteReport &report);

private:
    void fillRow(int row, const QList<TestRunner::SiteVerdict> &verdicts);

    QTableWidget *m_table = nullptr;
    QLabel *m_status = nullptr;
    QList<int> m_profileIDs;
};
