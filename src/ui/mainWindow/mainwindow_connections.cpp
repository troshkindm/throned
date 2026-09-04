#include "include/ui/mainwindow.h"
#include "include/api/RPC.h"
#include "include/database/DatabaseManager.h"
#include "include/database/RoutesRepo.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/global/LocalNetwork.hpp"
#include "include/ui/utils/ConnectionCloseDelegate.h"
#include "include/ui/utils/ConnectionsFilterHeader.h"
#include "include/ui/utils/ConnectionsFilterProxyModel.h"
#include "include/ui/utils/ConnectionsTableModel.h"
#include "include/ui/widget/MaterialIcon.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QBoxLayout>
#include <QClipboard>
#include <QFileInfo>
#include <QHeaderView>
#include <QHostAddress>
#include <QIcon>
#include <QMenu>
#include <QTabWidget>
#include <QTableView>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>

void MainWindow::setupConnectionList()
{
    connectionsModel = new ConnectionsTableModel(this);
    connectionsFilterModel = new ConnectionsFilterProxyModel(this);
    connectionsFilterModel->setSourceModel(connectionsModel);
    ui->connections->setModel(connectionsFilterModel);

    // Order matters: setModel() after this would re-init the sections and drop the resize modes below.
    connectionFilterHeader = new ConnectionsFilterHeader(ui->connections);
    ui->connections->setHorizontalHeader(connectionFilterHeader);

    connectionCloseDelegate = new ConnectionCloseDelegate(this);
    ui->connections->setItemDelegateForColumn(ConnectionsTableModel::ColClose, connectionCloseDelegate);
    connect(connectionCloseDelegate, &ConnectionCloseDelegate::closeRequested, this,
            [this](const QString& id) { closeConnections({id}); });

    auto* header = ui->connections->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionResizeMode(ConnectionsTableModel::ColSource, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColDest, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTableModel::ColProcess, QHeaderView::Stretch);
    header->setSectionResizeMode(ConnectionsTableModel::ColProtocol, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColOutbound, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColTraffic, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ConnectionsTableModel::ColSpeed, QHeaderView::ResizeToContents);
    // The close column has no text, so ResizeToContents would collapse it to nothing.
    header->setSectionResizeMode(ConnectionsTableModel::ColClose, QHeaderView::Fixed);
    ui->connections->setColumnWidth(ConnectionsTableModel::ColClose, ConnectionCloseDelegate::ColumnWidth);
    ui->connections->verticalHeader()->hide();

    // Otherwise the five content-sized columns re-measure up to 1000 rows whenever a poll changes the count.
    header->setResizeContentsPrecision(20);
    ui->connections->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->connections->setWordWrap(false);

    ui->connections->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->connections, &QWidget::customContextMenuRequested, this, &MainWindow::showConnectionMenu);

    refreshConnectionCloseIcons();
    restoreConnectionSort();
    setupConnectionSortMenu();
    setupConnectionFilter();

    connect(ui->connections, &QAbstractItemView::clicked, this, [this](const QModelIndex& index)
    {
        if (!index.isValid() || index.column() == ConnectionsTableModel::ColClose) return;
        const auto text = index.data(Qt::DisplayRole).toString();
        if (text.isEmpty()) return;

        QApplication::clipboard()->setText(text);
        const QPoint pos = ui->connections->viewport()->mapToGlobal(ui->connections->visualRect(index).center());
        QToolTip::showText(pos, tr("Copied!"), this);
        auto r = ++toolTipID;
        QTimer::singleShot(1500, this, [=,this] {
            if (r != toolTipID)
            {
                return;
            }
            QToolTip::hideText();
        });
    });
    syncConnectionSourceColumn();
    refreshStatsPanelLabels();
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
    const auto index = ui->connections->indexAt(pos);
    if (!index.isValid()) return;
    const auto* conn = connectionsModel->metaAt(connectionsFilterModel->mapToSource(index).row());
    if (conn == nullptr) return;
    const QString dest = conn->dest;
    const QString domain = conn->domain;
    const QString processPath = conn->processPath;
    const QString outbound = conn->outbound;

    QMenu menu(this);
    auto *verdict = menu.addAction(tr("%1 → %2")
        .arg(domain.isEmpty() ? dest : domain, outbound.isEmpty() ? tr("unknown") : outbound));
    verdict->setEnabled(false);
    menu.addSeparator();

    const auto candidates = candidatesFor(dest, domain, conn->process, processPath);
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
    int stored = settings->connection_sort;
    if (stored < Stats::Default || stored > Stats::BySource) return;
    // The Source header is unreachable while its column is hidden, so that sort would be stuck for good.
    if (stored == Stats::BySource && !LocalNetwork::LanInboundEnabled()) stored = Stats::Default;
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
    btnFilter->setObjectName(QStringLiteral("panelIconButton"));
    btnFilter->setToolTip(tr("Enable Filter"));
    btnFilter->setCheckable(true);
    btnFilter->setCursor(Qt::PointingHandCursor);
    btnFilter->setFocusPolicy(Qt::NoFocus);
    btnFilter->setFixedSize(28, 28);
    btnFilter->setIconSize(QSize(18, 18));
    connect(btnFilter, &QToolButton::toggled, connectionFilterHeader, &ConnectionsFilterHeader::setFiltersVisible);
    connect(connectionFilterHeader, &ConnectionsFilterHeader::closeRequested, btnFilter, [btnFilter] { btnFilter->setChecked(false); });

    connectionCloseAllButton = new QToolButton(this);
    connectionCloseAllButton->setObjectName(QStringLiteral("panelIconButton"));
    connectionCloseAllButton->setToolTip(tr("Close every connection listed below"));
    connectionCloseAllButton->setCursor(Qt::PointingHandCursor);
    connectionCloseAllButton->setFocusPolicy(Qt::NoFocus);
    connectionCloseAllButton->setFixedSize(28, 28);
    connectionCloseAllButton->setIconSize(QSize(18, 18));
    connect(connectionCloseAllButton, &QToolButton::clicked, this, [this] { closeConnections(listedConnectionIds()); });

    const auto retintConnectionTools = [this, btnFilter] {
        const auto colors = themeManager()->Colors();
        btnFilter->setIcon(MaterialIcon::icon(
            MaterialIcon::Glyph::Tune,
            btnFilter->isChecked() ? colors.accent : colors.textMuted, 18));
        if (connectionCloseAllButton != nullptr)
            connectionCloseAllButton->setIcon(MaterialIcon::icon(
                MaterialIcon::Glyph::Block, colors.textMuted, 18));
    };
    retintConnectionTools();
    connect(btnFilter, &QToolButton::toggled, this, retintConnectionTools);
    connect(themeManager(), &ThemeManager::themeChanged, this, retintConnectionTools);

    auto* corner = new QWidget(this);
    corner->setProperty("statsPage", ui->connections_tab->objectName());
    auto* cornerLayout = new QHBoxLayout(corner);
    cornerLayout->setContentsMargins(0, 0, 0, 0);
    cornerLayout->setSpacing(2);
    cornerLayout->addWidget(btnFilter);
    cornerLayout->addWidget(connectionCloseAllButton);
    // The log tools already hold this corner, so join them there; taking it would evict them.
    if (auto* host = ui->stats_widget->cornerWidget(Qt::TopRightCorner);
        host != nullptr && host->layout() != nullptr) {
        host->layout()->addWidget(corner);
    } else {
        ui->stats_widget->setCornerWidget(corner, Qt::TopRightCorner);
    }
    statsPanelTools.append(corner);

    // Only the active page contributes tools to the shared top-right corner.
    // Disabled controls from another page still looked like part of the current
    // toolbar and were especially noisy beside the log menu.
    refreshStatsPanelTools();

    connectionFilterDebounce = new QTimer(this);
    connectionFilterDebounce->setSingleShot(true);
    connectionFilterDebounce->setInterval(50);
    connect(connectionFilterDebounce, &QTimer::timeout, this, [this] { applyConnectionFilters(); });
    connect(connectionFilterHeader, &ConnectionsFilterHeader::filtersChanged, this, [this] { connectionFilterDebounce->start(); });
}

void MainWindow::applyConnectionFilters()
{
    const auto filters = connectionFilterHeader->filters();
    connectionsFilterModel->setFilters(filters.source, filters.dest, filters.process, filters.protocol,
                                       filters.outbound);
    refreshStatsPanelLabels();
}

void MainWindow::syncConnectionSourceColumn()
{
    if (connectionsModel == nullptr) return;
    const bool show = LocalNetwork::LanInboundEnabled();
    // refresh_status() drives this on a 2s tick, so bail out unless the state actually flipped.
    if (ui->connections->isColumnHidden(ConnectionsTableModel::ColSource) == !show) return;

    ui->connections->setColumnHidden(ConnectionsTableModel::ColSource, !show);
    connectionFilterHeader->adjustPositions();
    // Both must be cleared here: a hidden header can be reached by neither the filter field nor a sort click.
    if (!show) {
        connectionFilterHeader->clearFilterFor(ConnectionsTableModel::ColSource);
        if (Stats::connection_lister->getSort() == Stats::BySource) applyConnectionSort(Stats::Default);
    }
    applyConnectionFilters();
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
        const bool isTraffic = columnIndex == ConnectionsTableModel::ColTraffic;
        const bool isSpeed = columnIndex == ConnectionsTableModel::ColSpeed;
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
    // ApplyTheme() fires PaletteChange from the constructor, before setupUi() has built the table.
    if (connectionCloseDelegate == nullptr) return;

    const auto colors = themeManager()->Colors();
    connectionCloseIcon = MaterialIcon::icon(MaterialIcon::Glyph::Close, colors.textMuted,
                                             ConnectionCloseDelegate::IconSize);
    connectionCloseDelegate->setIcon(connectionCloseIcon);
    if (connectionCloseAllButton != nullptr)
        connectionCloseAllButton->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Block,
                                                             colors.textMuted, 18));
    ui->connections->viewport()->update();
}

QStringList MainWindow::listedConnectionIds() const
{
    QStringList ids;
    const int rows = connectionsFilterModel->rowCount();
    ids.reserve(rows);
    for (int row = 0; row < rows; row++)
    {
        const auto id = connectionsFilterModel->index(row, 0).data(ConnectionsTableModel::ConnIdRole).toString();
        if (!id.isEmpty()) ids << id;
    }
    return ids;
}

void MainWindow::closeConnections(const QStringList& ids)
{
    if (ids.isEmpty()) return;
    // Blocks until the core has walked every id, and "close all listed" hands it the whole table.
    runOnNewThread([ids] {
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

void MainWindow::UpdateConnectionList(const QList<Stats::ConnectionMetadata>& connections)
{
    if (connectionsModel == nullptr) return;
    connectionsModel->setConnections(connections);
    refreshStatsPanelLabels();
}

void MainWindow::refreshStatsPanelLabels()
{
    const int tab = ui->stats_widget->indexOf(ui->connections_tab);
    if (tab < 0) return;
    const int count = connectionsFilterModel == nullptr ? 0 : connectionsFilterModel->rowCount();
    const QString countText = count > 99 ? QStringLiteral("100+") : QString::number(count);
    // Keep the label and the number separate. Apart from matching the visual
    // hierarchy, fixed-width badges stop the tabs jumping on every polling tick.
    ui->stats_widget->setTabText(tab, tr("Connections"));
    if (statsConnectionTabCount != nullptr) statsConnectionTabCount->setText(countText);
    // The closed strip has no room for a badge widget, so the number joins the label.
    if (statsConnectionStripCount != nullptr)
        statsConnectionStripCount->setText(countText.isEmpty()
            ? tr("Connections")
            : tr("Connections") + QStringLiteral("   ") + countText);
}
