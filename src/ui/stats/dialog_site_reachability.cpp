#include "include/ui/stats/dialog_site_reachability.h"

#include "include/database/ProfilesRepo.h"
#include "include/database/entities/Profile.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/ThemeManager.hpp"

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
    constexpr int kNameColumn = 0;

    QString cellText(const TestRunner::SiteVerdict &verdict) {
        if (verdict.served()) return SiteReachabilityDialog::tr("%1 ms").arg(verdict.latencyMs);
        if (verdict.reached()) return QString::number(verdict.status);
        return QStringLiteral("—");
    }

    QColor cellColor(const TestRunner::SiteVerdict &verdict) {
        const auto colors = themeManager->Colors();
        if (verdict.served()) return colors.success;
        // Answered but refused: reached the service, and the service said no.
        if (verdict.reached()) return colors.warning;
        return colors.textSubtle;
    }

    QString cellTip(const TestRunner::SiteVerdict &verdict) {
        if (verdict.served()) return SiteReachabilityDialog::tr("Answered %1 in %2 ms")
                                       .arg(verdict.status).arg(verdict.latencyMs);
        if (verdict.reached()) return SiteReachabilityDialog::tr(
            "Reached the site, which answered %1").arg(verdict.status);
        return verdict.error.isEmpty() ? SiteReachabilityDialog::tr("Nothing answered")
                                       : verdict.error;
    }
}

SiteReachabilityDialog::SiteReachabilityDialog(QWidget *parent) : QDialog(parent) {
    setObjectName(QStringLiteral("siteReachabilityDialog"));
    setWindowTitle(tr("Site reachability"));
    resize(680, 420);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(10);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("siteReachabilityTable"));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setWordWrap(false);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setHighlightSections(false);
    root->addWidget(m_table, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("siteReachabilityStatus"));
    root->addWidget(m_status);

    themeManager->RegisterStyle(this, QStringLiteral(R"(
QDialog#siteReachabilityDialog { background: #1B1E23; }
QTableWidget#siteReachabilityTable {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
    gridline-color: #2F3136; color: #F1F3F5;
}
QTableWidget#siteReachabilityTable::item { padding: 4px 8px; }
QLabel#siteReachabilityStatus { color: #A4ABB4; font-size: 12px; }
)"));
}

void SiteReachabilityDialog::beginRun(const QList<int> &profileIDs, const QStringList &sites) {
    m_profileIDs = profileIDs;

    m_table->clear();
    m_table->setColumnCount(sites.size() + 1);
    m_table->setRowCount(profileIDs.size());
    QStringList headers{tr("Profile")};
    headers += sites;
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    for (int column = 1; column < m_table->columnCount(); column++)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);

    for (int row = 0; row < profileIDs.size(); row++) {
        const auto profile = Configs::dataManager->profilesRepo->GetProfile(profileIDs.at(row));
        auto *name = new QTableWidgetItem(profile == nullptr ? tr("Unknown profile") : profile->name);
        name->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_table->setItem(row, kNameColumn, name);
        for (int column = 1; column < m_table->columnCount(); column++) {
            auto *cell = new QTableWidgetItem(QStringLiteral("…"));
            cell->setTextAlignment(Qt::AlignCenter);
            cell->setForeground(themeManager->Colors().textSubtle);
            m_table->setItem(row, column, cell);
        }
    }

    m_status->setText(tr("Checking %n profile(s)…", "", profileIDs.size()));
}

void SiteReachabilityDialog::applyReport(const TestRunner::SiteReport &report) {
    int reported = 0;
    for (int row = 0; row < m_profileIDs.size(); row++) {
        const auto it = report.rows.constFind(m_profileIDs.at(row));
        if (it == report.rows.constEnd()) {
            // The batch never got to it: leave the placeholder rather than claim a failure.
            continue;
        }
        fillRow(row, *it);
        reported++;
    }
    m_status->setText(reported == m_profileIDs.size()
        ? tr("Green answered, amber refused, dash never answered.")
        : tr("%1 of %2 profiles answered. Green answered, amber refused, dash never answered.")
              .arg(reported).arg(m_profileIDs.size()));
}

void SiteReachabilityDialog::fillRow(int row, const QList<TestRunner::SiteVerdict> &verdicts) {
    for (int i = 0; i < verdicts.size() && i + 1 < m_table->columnCount(); i++) {
        auto *cell = m_table->item(row, i + 1);
        if (cell == nullptr) continue;
        const auto &verdict = verdicts.at(i);
        cell->setText(cellText(verdict));
        cell->setForeground(cellColor(verdict));
        cell->setToolTip(cellTip(verdict));
    }
}
