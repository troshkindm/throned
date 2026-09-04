#include "include/ui/stats/dialog_site_reachability.h"

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
        const auto colors = themeManager()->Colors();
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
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setObjectName(QStringLiteral("siteReachabilityStatus"));
    root->addWidget(m_status);

    themeManager()->RegisterStyle(this, QStringLiteral(R"(
QDialog#siteReachabilityDialog { background: #1B1E23; }
QTableWidget#siteReachabilityTable {
    background: #171B21; border: 1px solid #2F3136; border-radius: 7px;
    gridline-color: #2F3136; color: #F1F3F5;
}
QTableWidget#siteReachabilityTable::item { padding: 4px 8px; }
QLabel#siteReachabilityStatus { color: #A4ABB4; font-size: 12px; }
)"));
}

void SiteReachabilityDialog::beginRun(const QList<QPair<int, QString>> &profiles, const QStringList &sites) {
    m_profileIDs.clear();
    for (const auto &profile : profiles) m_profileIDs << profile.first;

    m_table->clear();
    m_table->setColumnCount(sites.size() + 1);
    m_table->setRowCount(profiles.size());
    QStringList headers{tr("Profile")};
    headers += sites;
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
    for (int column = 1; column < m_table->columnCount(); column++)
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);

    for (int row = 0; row < profiles.size(); row++) {
        auto *name = new QTableWidgetItem(profiles.at(row).second);
        name->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_table->setItem(row, kNameColumn, name);
        for (int column = 1; column < m_table->columnCount(); column++) {
            auto *cell = new QTableWidgetItem(QStringLiteral("…"));
            cell->setTextAlignment(Qt::AlignCenter);
            cell->setForeground(themeManager()->Colors().textSubtle);
            m_table->setItem(row, column, cell);
        }
    }

    m_status->setText(tr("Checking %n profile(s)…", "", profiles.size()));
}

void SiteReachabilityDialog::applyReport(const TestRunner::SiteReport &report) {
    int reported = 0;
    int failed = 0;
    int skipped = 0;
    QString firstError;
    for (int row = 0; row < m_profileIDs.size(); row++) {
        const auto it = report.rows.constFind(m_profileIDs.at(row));
        if (it == report.rows.constEnd()) {
            const int id = m_profileIDs.at(row);
            const bool isSkipped = report.skipped.contains(id);
            const QString reason = isSkipped
                ? tr("Auto-selectors are skipped. Select their individual profiles to test.")
                : report.errors.value(id, report.error.isEmpty()
                    ? tr("No test result was returned for this profile.") : report.error);
            if (isSkipped) ++skipped;
            else {
                ++failed;
                if (firstError.isEmpty()) firstError = reason;
            }
            for (int column = 1; column < m_table->columnCount(); ++column) {
                auto *cell = m_table->item(row, column);
                cell->setText(isSkipped ? tr("Skipped") : tr("Error"));
                cell->setToolTip(reason);
                cell->setForeground(isSkipped ? themeManager()->Colors().textSubtle : themeManager()->Colors().danger);
            }
            continue;
        }
        fillRow(row, *it);
        reported++;
    }
    QString status = tr("Checked: %1. Failed to run: %2. Skipped: %3.").arg(reported).arg(failed).arg(skipped);
    if (!firstError.isEmpty()) status += QLatin1Char('\n') + tr("Test error: %1").arg(firstError);
    if (reported > 0) status += QLatin1Char('\n') + tr("Green answered, amber refused, dash never answered.");
    m_status->setText(status);

    // Translated failure labels can be wider than timings. Preserve room for
    // profile names instead of letting the result columns squeeze them away.
    int resultWidth = 0;
    for (int column = 1; column < m_table->columnCount(); ++column) {
        m_table->resizeColumnToContents(column);
        resultWidth += m_table->columnWidth(column);
    }
    resize(qMax(width(), resultWidth + 180 + 2 * m_table->frameWidth()
                             + layout()->contentsMargins().left() + layout()->contentsMargins().right()), height());
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
