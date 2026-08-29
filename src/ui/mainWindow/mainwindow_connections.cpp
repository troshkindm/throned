#include "include/ui/mainwindow.h"
#include "include/api/RPC.h"
#include "include/ui/utils/ConnectionsFilterHeader.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QFileInfo>
#include <QHeaderView>
#include <QHostAddress>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

namespace
{
    constexpr int CLOSE_COLUMN_WIDTH = 26;
    constexpr int CLOSE_ICON_SIZE = 12;
    // Carries the row's current connection id to the click handler; refreshed on every poll.
    constexpr char CONN_ID_PROPERTY[] = "throne_conn_id";

    QString ProtocolText(const Stats::ConnectionMetadata& conn)
    {
        return conn.protocol.isEmpty() ? conn.network : conn.network + " (" + conn.protocol + ")";
    }

    // The material set is pure black, so it has to be tinted for dark themes.
    QIcon RecolorIcon(const QString& path, const QColor& color)
    {
        QPixmap pixmap(path);
        if (pixmap.isNull()) return QIcon(path);
        QPainter painter(&pixmap);
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(pixmap.rect(), color);
        painter.end();
        return QIcon(pixmap);
    }
}

#include "include/database/RoutesRepo.h"
#include "include/database/DatabaseManager.h"

void MainWindow::setupConnectionList()
{
    connectionFilterHeader = new ConnectionsFilterHeader(ui->connections);
    ui->connections->setHorizontalHeader(connectionFilterHeader);

    ui->connections->horizontalHeader()->setHighlightSections(false);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->connections->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    ui->connections->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    // ResizeToContents would collapse the title-less close column: cell widgets do not count as contents.
    ui->connections->horizontalHeader()->setSectionResizeMode(ConnectionsFilterHeader::ColClose, QHeaderView::Fixed);
    ui->connections->setColumnWidth(ConnectionsFilterHeader::ColClose, CLOSE_COLUMN_WIDTH);
    ui->connections->verticalHeader()->hide();
    ui->connections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->connections, &QWidget::customContextMenuRequested, this, &MainWindow::showConnectionMenu);
    refreshConnectionCloseIcons();
    restoreConnectionSort();
    setupConnectionSortMenu();
    setupConnectionFilter();
    connect(ui->connections, &QTableWidget::cellClicked, this, [=,this](int row, int column)
    {
        auto selected = ui->connections->item(row, column);
        if (selected == nullptr) return;
        QApplication::clipboard()->setText(selected->text());
        QPoint pos = ui->connections->mapToGlobal(ui->connections->visualItemRect(selected).center());
        QToolTip::showText(pos, tr("Copied!"), this);
        auto r = ++toolTipID;
        QTimer::singleShot(1500, [=,this] {
            if (r != toolTipID)
            {
                return;
            }
            QToolTip::hideText();
        });
    });
}

namespace {
    struct RuleCandidate {
        QString label;
        QString entry;
    };

    QList<RuleCandidate> candidatesFor(const QString &dest, const QString &domain,
                                       const QString &process, const QString &processPath) {
        QList<RuleCandidate> candidates;
        const QString host = domain.isEmpty() ? QString() : domain;
        if (!host.isEmpty()) {
            candidates.append({MainWindow::tr("This domain — %1").arg(host),
                               QStringLiteral("domain:") + host});
            candidates.append({MainWindow::tr("Domain and subdomains — *.%1").arg(host),
                               QStringLiteral("suffix:") + host});
        }
        if (!process.isEmpty())
            candidates.append({MainWindow::tr("This process — %1").arg(process),
                               QStringLiteral("processName:") + process});
        if (!processPath.isEmpty())
            candidates.append({MainWindow::tr("This executable — %1").arg(QFileInfo(processPath).fileName()),
                               QStringLiteral("processPath:") + processPath});
        // dest is host:port, and a port makes a poor routing rule on its own.
        const QString address = dest.contains(QLatin1Char(']'))
            ? dest.section(QLatin1Char(']'), 0, 0).mid(1)
            : dest.section(QLatin1Char(':'), 0, 0);
        if (!address.isEmpty() && !QHostAddress(address).isNull())
            candidates.append({MainWindow::tr("This address — %1").arg(address),
                               QStringLiteral("ip:") + address});
        return candidates;
    }

    struct RuleTarget {
        Configs::simpleAction action;
        QString label;
    };

    QList<RuleTarget> ruleTargets() {
        return {
            {Configs::proxy, MainWindow::tr("Through proxy")},
            {Configs::bypass, MainWindow::tr("Directly")},
            {Configs::block, MainWindow::tr("Block")},
        };
    }
} // namespace

QString MainWindow::existingRuleAction(const QString &entry) const
{
    const auto profile = Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
    if (!profile || profile->isRaw) return {};
    for (const auto &target : ruleTargets())
        if (profile->GetSimpleRules(target.action).split('\n', Qt::SkipEmptyParts).contains(entry))
            return target.label;
    return {};
}

void MainWindow::addRuleFromConnection(const QString &entry, int action)
{
    auto profile = Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
    if (!profile) {
        MessageBoxWarning(tr("No routing profile"), tr("There is no active routing profile to add the rule to."));
        return;
    }
    if (profile->isRaw) {
        MessageBoxWarning(tr("Raw routing profile"),
                          tr("“%1” is a raw profile and is edited as JSON, so a rule cannot be appended to it here.")
                              .arg(profile->name));
        return;
    }
    const auto simple = static_cast<Configs::simpleAction>(action);
    QStringList current = profile->GetSimpleRules(simple).split('\n', Qt::SkipEmptyParts);
    if (current.contains(entry)) return;
    current << entry;
    if (const QString error = profile->UpdateSimpleRules(current.join('\n'), simple); !error.isEmpty()) {
        MessageBoxWarning(tr("Rule not added"), error);
        return;
    }
    Configs::dataManager->routesRepo->Save(profile);
    refreshRoutingStatus();
    if (Configs::dataManager->settingsRepo->started_id >= 0)
        profile_start(Configs::dataManager->settingsRepo->started_id);
}

void MainWindow::showConnectionMenu(const QPoint &pos)
{
    const int row = ui->connections->rowAt(pos.y());
    if (row < 0) return;
    const auto *anchor = ui->connections->item(row, 0);
    if (anchor == nullptr) return;
    const QString dest = anchor->data(Stats::DESTKEY).toString();
    const QString domain = anchor->data(Stats::DOMAINKEY).toString();
    const QString process = anchor->data(Stats::PROCESSKEY).toString();
    const QString processPath = anchor->data(Stats::PROCESSPATHKEY).toString();
    const QString outbound = anchor->data(Stats::OUTBOUNDKEY).toString();

    QMenu menu(this);
    auto *verdict = menu.addAction(tr("%1 → %2")
        .arg(domain.isEmpty() ? dest : domain, outbound.isEmpty() ? tr("unknown") : outbound));
    verdict->setEnabled(false);
    menu.addSeparator();

    const auto candidates = candidatesFor(dest, domain, process, processPath);
    if (candidates.isEmpty()) {
        auto *none = menu.addAction(tr("Nothing to build a rule from"));
        none->setEnabled(false);
    }
    for (const auto &candidate : candidates) {
        const QString already = existingRuleAction(candidate.entry);
        auto *submenu = menu.addMenu(already.isEmpty()
            ? candidate.label
            : tr("%1  ·  already %2").arg(candidate.label, already.toLower()));
        submenu->setToolTip(candidate.entry);
        for (const auto &target : ruleTargets()) {
            auto *action = submenu->addAction(target.label);
            const QString entry = candidate.entry;
            const int simple = target.action;
            connect(action, &QAction::triggered, this, [this, entry, simple] {
                addRuleFromConnection(entry, simple);
            });
        }
    }

    menu.addSeparator();
    if (!processPath.isEmpty()) {
        auto *copyPath = menu.addAction(tr("Copy executable path"));
        connect(copyPath, &QAction::triggered, this, [processPath] {
            QApplication::clipboard()->setText(processPath);
        });
    }
    auto *copyDest = menu.addAction(tr("Copy destination"));
    connect(copyDest, &QAction::triggered, this, [dest, domain] {
        QApplication::clipboard()->setText(domain.isEmpty() ? dest : domain);
    });
    menu.exec(ui->connections->viewport()->mapToGlobal(pos));
}

void MainWindow::restoreConnectionSort()
{
    const auto* settings = Configs::dataManager->settingsRepo.get();
    const int stored = settings->connection_sort;
    if (stored < Stats::Default || stored > Stats::BySpeed) return;
    // Runs before setup_rpc() spawns the lister thread, so writing the pair unguarded is safe.
    Stats::connection_lister->restoreSort(static_cast<Stats::ConnectionSort>(stored), settings->connection_sort_asc);
}

void MainWindow::applyConnectionSort(Stats::ConnectionSort sort)
{
    Stats::connection_lister->setSort(sort);
    auto* settings = Configs::dataManager->settingsRepo.get();
    settings->connection_sort = Stats::connection_lister->getSort();
    settings->connection_sort_asc = Stats::connection_lister->isSortAscending();
    settings->Save();
    Stats::connection_lister->ForceUpdate();
}

void MainWindow::setupConnectionFilter()
{
    auto* btnFilter = new QToolButton(this);
    btnFilter->setIcon(QIcon(":/icon/filter.png"));
    btnFilter->setToolTip(tr("Enable Filter"));
    btnFilter->setCheckable(true);
    connect(btnFilter, &QToolButton::toggled, connectionFilterHeader, &ConnectionsFilterHeader::setFiltersVisible);
    connect(connectionFilterHeader, &ConnectionsFilterHeader::closeRequested, btnFilter, [btnFilter] { btnFilter->setChecked(false); });

    connectionCloseAllButton = new QToolButton(this);
    connectionCloseAllButton->setIcon(connectionCloseIcon);
    connectionCloseAllButton->setToolTip(tr("Close every connection listed below"));
    connect(connectionCloseAllButton, &QToolButton::clicked, this, [this] { closeConnections(listedConnectionIds()); });

    auto* corner = new QWidget(this);
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(2);
    cornerLayout->addWidget(btnFilter);
    cornerLayout->addWidget(connectionCloseAllButton);
    // The log tools already hold this corner, so join them there; taking it would evict them.
    if (auto* host = ui->stats_widget->cornerWidget(Qt::TopRightCorner);
        host != nullptr && host->layout() != nullptr) {
        host->layout()->addWidget(corner);
        statsPanelTools.append(corner);
    } else {
        ui->stats_widget->setCornerWidget(corner, Qt::TopRightCorner);
    }

    // The corner widget spans the whole tab bar, so it stays put and only greys out away from the connections tab.
    auto syncEnabled = [=,this] { corner->setEnabled(ui->stats_widget->currentWidget() == ui->connections_tab); };
    connect(ui->stats_widget, &QTabWidget::currentChanged, this, [syncEnabled](int) { syncEnabled(); });
    syncEnabled();

    connectionFilterDebounce = new QTimer(this);
    connectionFilterDebounce->setSingleShot(true);
    connectionFilterDebounce->setInterval(50);
    connect(connectionFilterDebounce, &QTimer::timeout, this, [this] { applyConnectionFilters(); });
    connect(connectionFilterHeader, &ConnectionsFilterHeader::filtersChanged, this, [this] { connectionFilterDebounce->start(); });
}

void MainWindow::applyConnectionFilters()
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    const bool active = connectionFilterHeader->hasActiveFilter();
    for (int row = 0; row < ui->connections->rowCount(); row++)
    {
        bool hide = false;
        if (active)
        {
            auto text = [this, row](int column)
            {
                const auto* item = ui->connections->item(row, column);
                return item == nullptr ? QString() : item->text();
            };
            hide = !connectionFilterHeader->accepts(text(0), text(1), text(2), text(3));
        }
        ui->connections->setRowHidden(row, hide);
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}

// Right-click the Traffic / Speed headers to pick the sub-field they sort by;
// left-clicking still sorts by total.
void MainWindow::setupConnectionSortMenu()
{
    auto* header = ui->connections->horizontalHeader();
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this, [=,this](const QPoint& pos)
    {
        const int columnIndex = header->logicalIndexAt(pos);
        const bool isTraffic = columnIndex == 4;
        const bool isSpeed = columnIndex == 5;
        if (!isTraffic && !isSpeed) return;

        struct SortOption { Stats::ConnectionSort value; QString label; };
        const QList<SortOption> options = isTraffic
            ? QList<SortOption>{
                { Stats::ByTraffic, tr("Total") },
                { Stats::ByDownload, tr("Downloaded") },
                { Stats::ByUpload, tr("Uploaded") } }
            : QList<SortOption>{
                { Stats::BySpeed, tr("Total") },
                { Stats::ByDownloadSpeed, tr("Download Speed") },
                { Stats::ByUploadSpeed, tr("Upload Speed") } };

        QMenu menu(this);
        auto* sortByLabel = menu.addAction(tr("Sort By:"));
        sortByLabel->setEnabled(false);

        const auto current = Stats::connection_lister->getSort();
        for (const auto& opt : options)
        {
            auto* act = menu.addAction(opt.label);
            act->setData(static_cast<int>(opt.value));
            act->setCheckable(true);
            act->setChecked(current == opt.value);
        }

        auto* chosen = menu.exec(header->mapToGlobal(pos));
        if (chosen == nullptr || !chosen->data().isValid()) return;

        applyConnectionSort(static_cast<Stats::ConnectionSort>(chosen->data().toInt()));
    });
}

void MainWindow::refreshConnectionCloseIcons()
{
    // ApplyTheme() fires PaletteChange from the constructor, one line before setupUi() builds the table.
    if (connectionFilterHeader == nullptr) return;

    connectionCloseIcon = RecolorIcon(":/icon/material/cancel.png", palette().color(QPalette::ButtonText));
    if (connectionCloseAllButton != nullptr) connectionCloseAllButton->setIcon(connectionCloseIcon);
    for (int row = 0; row < ui->connections->rowCount(); row++)
    {
        if (auto* btn = qobject_cast<QToolButton*>(ui->connections->cellWidget(row, ConnectionsFilterHeader::ColClose)))
        {
            btn->setIcon(connectionCloseIcon);
        }
    }
}

void MainWindow::buildConnectionRow(const int row)
{
    if (ui->connections->item(row, 0) == nullptr)
    {
        for (int column = 0; column < ConnectionsFilterHeader::ColClose; column++)
        {
            ui->connections->setItem(row, column, new QTableWidgetItem());
        }
    }
    if (ui->connections->cellWidget(row, ConnectionsFilterHeader::ColClose) != nullptr) return;

    auto* btn = new QToolButton(ui->connections);
    btn->setAutoRaise(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setFocusPolicy(Qt::NoFocus);
    btn->setIcon(connectionCloseIcon);
    btn->setIconSize(QSize(CLOSE_ICON_SIZE, CLOSE_ICON_SIZE));
    btn->setToolTip(tr("Close this connection"));
    connect(btn, &QToolButton::clicked, this, [this, btn] {
        const auto id = btn->property(CONN_ID_PROPERTY).toString();
        if (!id.isEmpty()) closeConnections({id});
    });
    ui->connections->setCellWidget(row, ConnectionsFilterHeader::ColClose, btn);
}

void MainWindow::fillConnectionRow(const int row, const Stats::ConnectionMetadata& conn)
{
    const auto dest = DisplayDest(conn.dest, conn.domain);
    const auto prot = ProtocolText(conn);

    const QString columns[ConnectionsFilterHeader::ColClose] = {
        dest,
        conn.process,
        prot,
        conn.outbound,
        ReadableSize(conn.upload) + "↑" + " " + ReadableSize(conn.download) + "↓",
        ReadableSize(conn.uploadSpeed) + "/s↑" + " " + ReadableSize(conn.downloadSpeed) + "/s↓",
    };
    for (int column = 0; column < ConnectionsFilterHeader::ColClose; column++)
    {
        auto* item = ui->connections->item(row, column);
        item->setText(columns[column]);
        item->setData(Stats::IDKEY, conn.id);
    }

    // The context menu reads its rule candidates off the first column only.
    auto* anchor = ui->connections->item(row, 0);
    anchor->setData(Stats::DESTKEY, conn.dest);
    anchor->setData(Stats::DOMAINKEY, conn.domain);
    anchor->setData(Stats::PROCESSKEY, conn.process);
    anchor->setData(Stats::PROCESSPATHKEY, conn.processPath);
    anchor->setData(Stats::OUTBOUNDKEY, conn.outbound);

    // Sorting reshuffles which connection a row shows, so the button is re-stamped every poll.
    if (auto* btn = ui->connections->cellWidget(row, ConnectionsFilterHeader::ColClose))
    {
        btn->setProperty(CONN_ID_PROPERTY, conn.id);
    }

    ui->connections->setRowHidden(row, !connectionFilterHeader->accepts(dest, conn.process, prot, conn.outbound));
}

// Rows are reused rather than recreated: a rebuilt row would delete the close button out from under a click.
void MainWindow::resizeConnectionRows(const int count)
{
    while (ui->connections->rowCount() > count) ui->connections->removeRow(ui->connections->rowCount() - 1);
    while (ui->connections->rowCount() < count)
    {
        const int row = ui->connections->rowCount();
        ui->connections->insertRow(row);
        buildConnectionRow(row);
    }
}

QStringList MainWindow::listedConnectionIds() const
{
    QStringList ids;
    for (int row = 0; row < ui->connections->rowCount(); row++)
    {
        if (ui->connections->isRowHidden(row)) continue;
        const auto* item = ui->connections->item(row, 0);
        if (item == nullptr) continue;
        const auto id = item->data(Stats::IDKEY).toString();
        if (!id.isEmpty()) ids << id;
    }
    return ids;
}

void MainWindow::closeConnections(const QStringList& ids)
{
    if (ids.isEmpty()) return;
    // Deferred: ForceUpdate() rewrites the table synchronously, and we are inside a row button's own click handler.
    QTimer::singleShot(0, this, [this, ids] {
        bool rpcOK = false;
        const auto err = API::defaultClient->CloseConnections(&rpcOK, ids);
        if (!rpcOK || !err.isEmpty())
        {
            MW_show_log(tr("Failed to close connections: %1").arg(err.isEmpty() ? tr("IPC error") : err));
            return;
        }
        Stats::connection_lister->ForceUpdate();
    });
}

void MainWindow::UpdateConnectionList(const QMap<QString, Stats::ConnectionMetadata>& toUpdate, const QMap<QString, Stats::ConnectionMetadata>& toAdd)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    for (int row=0;row<ui->connections->rowCount();row++)
    {
        const auto key = ui->connections->item(row, 0)->data(Stats::IDKEY).toString();
        if (!toUpdate.contains(key))
        {
            ui->connections->removeRow(row);
            row--;
            continue;
        }
        fillConnectionRow(row, toUpdate[key]);
    }
    for (const auto& conn : toAdd)
    {
        const int row = ui->connections->rowCount();
        ui->connections->insertRow(row);
        buildConnectionRow(row);
        fillConnectionRow(row, conn);
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}

void MainWindow::UpdateConnectionListWithRecreate(const QList<Stats::ConnectionMetadata>& connections)
{
    connectionListMu.lock();
    ui->connections->setUpdatesEnabled(false);
    resizeConnectionRows(static_cast<int>(connections.size()));
    for (int row = 0; row < connections.size(); row++)
    {
        fillConnectionRow(row, connections[row]);
    }
    ui->connections->setUpdatesEnabled(true);
    connectionListMu.unlock();
}
