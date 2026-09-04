// Fast, isolated visual harness for the profile list and its lower dock.
// It links the shipped row delegate/theme/icon code but never opens Throned's
// database or starts a core. Every visible value is an RFC 5737/example fake.
//
//   cmake -S tools/row-preview -B build-row
//   cmake --build build-row
//   build-row/row-preview.exe --panel closed --out closed.png
//   build-row/row-preview.exe --panel logs --menu --out logs.png
//
// --show keeps the window open instead of writing a PNG.

#include <QAbstractTableModel>
#include <QApplication>
#include <QClipboard>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QSplitter>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/utils/ProfilesTableModel.h"
#include "include/ui/widget/MaterialIcon.h"

// ThemeManager only reaches for this on legacy stylesheet themes, which this
// harness never applies.
QString ReadFileText(const QString &) { return {}; }

namespace {
using RowVisual = ProfilesTableModel::RowVisual;

QList<RowVisual> sampleRows() {
    return {
        {.name = "Auto (gRPC)", .chip = "VLESS · Xray", .address = "192.0.2.10:443",
         .country = "DE", .exitIp = "192.0.2.10", .latency = "60 ms",
         .udp = "61 ms ±4", .speedDown = "148 Mbps", .speedUp = "36 Mbps",
         .latencyMs = 60},
        {.name = "Auto (hy2)", .chip = "Hysteria", .address = "192.0.2.10:16011",
         .country = "DE", .exitIp = "192.0.2.10", .latency = "57 ms",
         .udp = "58 ms ±3 / 1%", .speedDown = "196 Mbps", .speedUp = "128 Mbps",
         .trafficDown = "33.49 GiB", .trafficUp = "38.08 GiB",
         .latencyMs = 57, .running = true},
        {.name = "Moscow (games, long name that used to elide)", .chip = "Hysteria",
         .address = "192.0.2.10:16012", .latency = "47 ms",
         .udp = "49 ms ±11 / 4%", .trafficDown = "994.18 MiB", .trafficUp = "463.10 MiB",
         .latencyMs = 47, .udpDegraded = true},
        {.name = "Moscow → Helsinki", .chip = "VLESS · Xray", .address = "192.0.2.10:2088",
         .country = "FI", .exitIp = "198.51.100.7", .latency = "32 ms",
         .trafficDown = "1.40 MiB", .trafficUp = "1.09 MiB", .latencyMs = 32},
        {.name = "Warp bypass", .chip = "WireGuard", .address = "203.0.113.90:51820",
         .latency = "78 ms", .udp = "No reply", .trafficDown = "88.10 MiB",
         .trafficUp = "12.40 MiB", .latencyMs = 78, .udpDegraded = true},
    };
}

class SampleModel final : public QAbstractTableModel {
public:
    explicit SampleModel(QObject *parent = nullptr)
        : QAbstractTableModel(parent), rows_(sampleRows()) {}

    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : static_cast<int>(rows_.size());
    }

    int columnCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : ProfilesTableModel::ComfortColumnCount;
    }

    QVariant data(const QModelIndex &index, int role) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
        if (role == ProfilesTableModel::RowVisualRole) return QVariant::fromValue(rows_[index.row()]);
        if (role == Qt::DisplayRole && index.column() == ProfilesTableModel::ColcServer)
            return rows_[index.row()].name;
        if (role == Qt::TextAlignmentRole)
            return static_cast<int>((index.column() == ProfilesTableModel::ColcServer
                                         ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
        return {};
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (orientation != Qt::Horizontal) return {};
        if (role == Qt::TextAlignmentRole)
            return static_cast<int>((section == ProfilesTableModel::ColcServer
                                         ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
        if (role != Qt::DisplayRole) return {};
        switch (section) {
        case ProfilesTableModel::ColcServer: return QStringLiteral("Server");
        case ProfilesTableModel::ColcPing: return QStringLiteral("Ping · UDP");
        case ProfilesTableModel::ColcSpeed: return QStringLiteral("Speed");
        case ProfilesTableModel::ColcTraffic: return QStringLiteral("Traffic");
        default: return {};
        }
    }

    bool runningAt(int row) const {
        return row >= 0 && row < rows_.size() && rows_[row].running;
    }

private:
    QList<RowVisual> rows_;
};

class PreviewIndexHeader final : public QHeaderView {
public:
    PreviewIndexHeader(SampleModel *model, const ThronedThemeColors &colors, QWidget *parent)
        : QHeaderView(Qt::Vertical, parent), model_(model), colors_(colors) {
        setFixedWidth(30);
        setDefaultSectionSize(ProfileRowDelegate::RowHeight);
        setSectionResizeMode(QHeaderView::Fixed);
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int logicalIndex) const override {
        const auto *table = qobject_cast<const QTableView *>(parentWidget());
        const bool selected = table != nullptr && table->selectionModel() != nullptr
            && table->selectionModel()->isRowSelected(logicalIndex, QModelIndex());
        painter->save();
        painter->fillRect(rect, selected ? colors_.selection : colors_.surface);
        painter->setPen(selected ? colors_.selectionBorder : colors_.border);
        painter->drawLine(rect.topRight(), rect.bottomRight());
        painter->drawLine(rect.bottomLeft(), rect.bottomRight());
        if (selected) painter->drawLine(rect.topLeft(), rect.topRight());
        painter->setPen(selected ? colors_.text : colors_.textSubtle);
        painter->drawText(rect, Qt::AlignCenter,
                          model_->runningAt(logicalIndex) ? QStringLiteral("✓")
                                                         : QString::number(logicalIndex + 1));
        painter->restore();
    }

private:
    SampleModel *model_;
    ThronedThemeColors colors_;
};

QString argValue(const QStringList &args, const QString &name, const QString &fallback) {
    const int at = args.indexOf(name);
    return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : fallback;
}

QWidget *makeStatusCell(const QString &caption, const QString &value, QWidget *parent) {
    auto *cell = new QWidget(parent);
    auto *layout = new QVBoxLayout(cell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    auto *captionLabel = new QLabel(caption, cell);
    captionLabel->setObjectName(QStringLiteral("statusCaption"));
    auto *valueLabel = new QLabel(value, cell);
    valueLabel->setObjectName(QStringLiteral("statusValue"));
    layout->addWidget(captionLabel);
    layout->addWidget(valueLabel);
    return cell;
}

class PreviewWindow final : public QWidget {
public:
    PreviewWindow(const QString &panel, const ThronedThemeColors &colors) {
        setObjectName(QStringLiteral("previewWindow"));
        resize(1152, 688);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(15, 9, 15, 0);
        root->setSpacing(7);

        splitter_ = new QSplitter(Qt::Vertical, this);
        splitter_->setChildrenCollapsible(false);
        splitter_->setHandleWidth(7);

        auto *profiles = new QWidget(splitter_);
        auto *profilesLayout = new QVBoxLayout(profiles);
        profilesLayout->setContentsMargins(0, 0, 0, 0);
        profilesLayout->setSpacing(6);

        auto *profileTools = new QWidget(profiles);
        auto *profileToolsLayout = new QHBoxLayout(profileTools);
        profileToolsLayout->setContentsMargins(2, 0, 0, 0);
        profileToolsLayout->setSpacing(7);
        auto *subscription = new QToolButton(profileTools);
        subscription->setObjectName(QStringLiteral("subscriptionPill"));
        subscription->setText(QStringLiteral("Demo subscription"));
        profileToolsLayout->addWidget(subscription);
        profileToolsLayout->addStretch(1);
        auto *tableTools = new QFrame(profileTools);
        tableTools->setObjectName(QStringLiteral("tableTools"));
        auto *tableToolsLayout = new QHBoxLayout(tableTools);
        tableToolsLayout->setContentsMargins(0, 0, 0, 0);
        tableToolsLayout->setSpacing(0);
        auto *filter = new QToolButton(tableTools);
        filter->setObjectName(QStringLiteral("tableFilterButton"));
        filter->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Tune, colors.textMuted, 17));
        filter->setFixedSize(35, 32);
        tableToolsLayout->addWidget(filter);
        auto *search = new QLineEdit(tableTools);
        search->setObjectName(QStringLiteral("serverSearch"));
        search->setPlaceholderText(QStringLiteral("Search servers…"));
        search->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Search, colors.textSubtle, 16),
                          QLineEdit::LeadingPosition);
        search->setFixedSize(236, 32);
        tableToolsLayout->addWidget(search);
        profileToolsLayout->addWidget(tableTools);
        profilesLayout->addWidget(profileTools);

        auto *profileCard = new QFrame(profiles);
        profileCard->setObjectName(QStringLiteral("profileCard"));
        auto *cardLayout = new QVBoxLayout(profileCard);
        cardLayout->setContentsMargins(1, 1, 1, 1);
        cardLayout->setSpacing(0);

        table_ = new QTableView(profileCard);
        auto *model = new SampleModel(table_);
        table_->setModel(model);
        table_->setItemDelegate(new ProfileRowDelegate(table_));
        table_->setVerticalHeader(new PreviewIndexHeader(model, colors, table_));
        table_->setShowGrid(false);
        table_->setAlternatingRowColors(false);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->setCornerButtonEnabled(false);
        table_->verticalHeader()->setDefaultSectionSize(ProfileRowDelegate::RowHeight);
        table_->horizontalHeader()->setSectionResizeMode(ProfilesTableModel::ColcServer, QHeaderView::Stretch);
        for (int column : {ProfilesTableModel::ColcPing, ProfilesTableModel::ColcSpeed,
                           ProfilesTableModel::ColcTraffic}) {
            table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
            table_->horizontalHeader()->resizeSection(
                column, ProfileRowDelegate::metricColumnWidth(column, table_->font()));
        }
        table_->selectRow(1);
        connect(table_->selectionModel(), &QItemSelectionModel::selectionChanged,
                table_->verticalHeader()->viewport(),
                qOverload<>(&QWidget::update));
        cardLayout->addWidget(table_);
        profilesLayout->addWidget(profileCard, 1);

        statsHost_ = new QFrame(splitter_);
        statsHost_->setObjectName(QStringLiteral("statsPanelHost"));
        auto *statsLayout = new QVBoxLayout(statsHost_);
        statsLayout->setContentsMargins(0, 0, 0, 0);
        statsLayout->setSpacing(0);

        tabs_ = new QTabWidget(statsHost_);
        tabs_->setObjectName(QStringLiteral("logsCard"));
        tabs_->setDocumentMode(true);
        tabs_->tabBar()->setUsesScrollButtons(false);

        auto *logs = new QTextBrowser(tabs_);
        logs->setObjectName(QStringLiteral("masterLogBrowser"));
        logs->setPlainText(QStringLiteral(
            "[INF] [2be4] [ui] sing-box: v1.13.20\n"
            "[INF] [2be4] [ui] Xray-core: 26.7.28\n"
            "[INF] [2be4] [ui] Core has successfully connected to Throned\n"
            "[INF] [48809] dns: exchanged A sync.example in 28 ms\n"
            "[INF] [48809] inbound/tun[tun-in]: connection from 192.0.2.190:1900\n"
            "[WRN] [48809] outbound/direct: connection to 198.51.100.7:443 timed out"));
        tabs_->addTab(logs, QStringLiteral("Logs"));

        auto *connections = new QTableWidget(6, 4, tabs_);
        connections->setObjectName(QStringLiteral("connections"));
        connections->verticalHeader()->hide();
        connections->setHorizontalHeaderLabels(
            {QStringLiteral("Destination (Domain)"), QStringLiteral("Process"),
             QStringLiteral("Protocol"), QStringLiteral("Outbound")});
        const QList<QStringList> connectionRows{
            {"203.0.113.10:443 (assistant.example)", "DemoChat.exe", "tcp (tls)", "proxy"},
            {"198.51.100.24:443 (accounts.example.net)", "ExampleBrowser.exe", "tcp (tls)", "proxy"},
            {"192.0.2.53:53 (resolver.example)", "SystemDemo.exe", "udp (dns)", "direct"},
            {"198.51.100.7:443 (code.example.org)", "EditorDemo.exe", "tcp (tls)", "direct"},
            {"203.0.113.88:443 (sync.example.com)", "SyncDemo.exe", "tcp (tls)", "direct"},
            {"192.0.2.190:1900 (discovery.example)", "OverlayDemo.exe", "udp", "direct"},
        };
        for (int row = 0; row < connectionRows.size(); ++row)
            for (int column = 0; column < connectionRows[row].size(); ++column)
                connections->setItem(row, column, new QTableWidgetItem(connectionRows[row][column]));
        connections->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        connections->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        connections->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        connections->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        const int connectionsTab = tabs_->addTab(connections, QStringLiteral("Connections"));
        auto *tabCount = new QLabel(QStringLiteral("6"), tabs_->tabBar());
        tabCount->setObjectName(QStringLiteral("tabCountBadge"));
        tabCount->setAlignment(Qt::AlignCenter);
        tabCount->setFixedWidth(31);
        tabs_->tabBar()->setTabButton(connectionsTab, QTabBar::RightSide, tabCount);

        auto *graph = new QLabel(QStringLiteral("Traffic preview"), tabs_);
        graph->setAlignment(Qt::AlignCenter);
        tabs_->addTab(graph, QStringLiteral("Traffic Graph"));

        auto *corner = new QWidget(tabs_);
        auto *cornerLayout = new QHBoxLayout(corner);
        cornerLayout->setContentsMargins(0, 0, 5, 4);
        cornerLayout->setSpacing(6);
        menuButton_ = new QToolButton(corner);
        menuButton_->setObjectName(QStringLiteral("panelIconButton"));
        menuButton_->setFixedSize(28, 28);
        menuButton_->setIconSize(QSize(18, 18));
        menuButton_->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::More, colors.textMuted, 18));
        menuButton_->setPopupMode(QToolButton::InstantPopup);
        logMenu_ = new QMenu(menuButton_);
        logMenu_->setObjectName(QStringLiteral("logToolsMenu"));
        logMenu_->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Delete, colors.textMuted, 17),
                            QStringLiteral("Clear"));
        logMenu_->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Copy, colors.textMuted, 17),
                            QStringLiteral("Copy all"));
        auto *autoScroll = logMenu_->addAction(
            MaterialIcon::icon(MaterialIcon::Glyph::Check, colors.success, 17),
            QStringLiteral("Auto-scroll"));
        autoScroll->setCheckable(true);
        autoScroll->setChecked(true);
        logMenu_->addSeparator();
        auto *level = logMenu_->addMenu(QStringLiteral("Level        INFO"));
        level->setObjectName(QStringLiteral("logToolsMenu"));
        for (const QString &name : {QStringLiteral("DEBUG"), QStringLiteral("INFO"),
                                    QStringLiteral("WARN"), QStringLiteral("ERROR")})
            level->addAction(name);
        logMenu_->addAction(MaterialIcon::icon(MaterialIcon::Glyph::Tune, colors.textMuted, 17),
                            QStringLiteral("Filter…"));
        menuButton_->setMenu(logMenu_);
        cornerLayout->addWidget(menuButton_);
        connectionFilterButton_ = new QToolButton(corner);
        connectionFilterButton_->setObjectName(QStringLiteral("panelIconButton"));
        connectionFilterButton_->setFixedSize(28, 28);
        connectionFilterButton_->setIconSize(QSize(18, 18));
        connectionFilterButton_->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Tune,
                                                             colors.textMuted, 18));
        cornerLayout->addWidget(connectionFilterButton_);
        connectionCloseAllButton_ = new QToolButton(corner);
        connectionCloseAllButton_->setObjectName(QStringLiteral("panelIconButton"));
        connectionCloseAllButton_->setFixedSize(28, 28);
        connectionCloseAllButton_->setIconSize(QSize(18, 18));
        connectionCloseAllButton_->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::Block,
                                                               colors.textMuted, 18));
        cornerLayout->addWidget(connectionCloseAllButton_);
        auto *collapse = new QToolButton(corner);
        collapse->setObjectName(QStringLiteral("panelIconButton"));
        collapse->setFixedSize(28, 28);
        collapse->setIconSize(QSize(18, 18));
        collapse->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::ChevronDown, colors.textMuted, 18));
        cornerLayout->addWidget(collapse);
        tabs_->setCornerWidget(corner, Qt::TopRightCorner);
        statsLayout->addWidget(tabs_);

        splitter_->addWidget(profiles);
        splitter_->addWidget(statsHost_);
        splitter_->setStretchFactor(0, 3);
        splitter_->setStretchFactor(1, 2);
        root->addWidget(splitter_, 1);

        closedStrip_ = new QFrame(this);
        closedStrip_->setObjectName(QStringLiteral("logsStrip"));
        closedStrip_->setFixedHeight(39);
        auto *stripLayout = new QHBoxLayout(closedStrip_);
        stripLayout->setContentsMargins(9, 4, 7, 4);
        stripLayout->setSpacing(2);
        for (const QString &text : {QStringLiteral("Logs"), QStringLiteral("Connections"),
                                    QStringLiteral("Traffic Graph")}) {
            auto *button = new QToolButton(closedStrip_);
            button->setObjectName(QStringLiteral("stripTab"));
            button->setText(text);
            stripLayout->addWidget(button);
            if (text == QStringLiteral("Connections")) {
                auto *count = new QToolButton(closedStrip_);
                count->setObjectName(QStringLiteral("stripCountBadge"));
                count->setText(QStringLiteral("6"));
                count->setFixedWidth(31);
                stripLayout->addWidget(count);
            }
        }
        stripLayout->addStretch(1);
        auto *hint = new QLabel(QStringLiteral("click a tab to open"), closedStrip_);
        hint->setObjectName(QStringLiteral("stripHint"));
        stripLayout->addWidget(hint);
        stripLayout->addSpacing(7);
        auto *expand = new QToolButton(closedStrip_);
        expand->setObjectName(QStringLiteral("panelIconButton"));
        expand->setFixedSize(28, 28);
        expand->setIconSize(QSize(18, 18));
        expand->setIcon(MaterialIcon::icon(MaterialIcon::Glyph::ChevronUp, colors.textMuted, 18));
        stripLayout->addWidget(expand);
        root->addWidget(closedStrip_);

        auto *status = new QFrame(this);
        status->setObjectName(QStringLiteral("statusCard"));
        status->setFixedHeight(47);
        auto *statusLayout = new QHBoxLayout(status);
        statusLayout->setContentsMargins(5, 5, 5, 5);
        statusLayout->setSpacing(28);
        statusLayout->addWidget(makeStatusCell(QStringLiteral("Connection"), QStringLiteral("Auto (hy2)"), status), 3);
        statusLayout->addWidget(makeStatusCell(QStringLiteral("Inbound"), QStringLiteral("Mixed: 127.0.0.1:2080"), status), 4);
        statusLayout->addWidget(makeStatusCell(QStringLiteral("Proxy"), QStringLiteral("↑ 82.24 KiB ↓ 1.29 MiB"), status), 4);
        statusLayout->addWidget(makeStatusCell(QStringLiteral("Direct"), QStringLiteral("↑ 912.00 B ↓ 4.31 KiB"), status), 4);
        statusLayout->addStretch(1);
        statusLayout->addWidget(makeStatusCell(QStringLiteral("Routing"), QStringLiteral("Default · Proxy"), status), 3);
        root->addWidget(status);

        const QString style = QStringLiteral(R"(
* { font-size: %BASE_FONT_PX%px; color: #F1F3F5; }
QWidget#previewWindow { background: #1B1E23; }
QFrame#profileCard, QFrame#statsPanelHost, QFrame#logsStrip {
    background: #171B21; border: 1px solid #343A42; border-radius: 8px;
}
QToolButton#subscriptionPill {
    background: #222529; border: 1px solid #343A42; border-radius: 5px;
    padding: 6px 11px; color: #F1F3F5; font-weight: 500;
}
QToolButton#subscriptionPill:hover { background: #292D33; border-color: #4A4F57; }
QFrame#tableTools { background: #171B21; border: 1px solid #343A42; border-radius: 7px; }
QToolButton#tableFilterButton {
    background: transparent; border: none; border-right: 1px solid #343A42; border-radius: 6px;
}
QToolButton#tableFilterButton:hover { background: #292D33; }
QLineEdit#serverSearch {
    background: transparent; border: none; border-radius: 6px; padding: 6px 9px 6px 4px;
}
QLineEdit#serverSearch:focus { background: #1B222A; }
QTableView, QTableWidget, QTextBrowser {
    background: transparent; border: none; outline: none;
    selection-color: white; selection-background-color: #143C48;
}
QHeaderView { background: transparent; }
QHeaderView::section {
    background: transparent; color: #C2C7CE; border: none;
    border-right: 1px solid #2F3136; border-bottom: 1px solid #2F3136;
    padding: 5px 8px; font-weight: 500;
}
QTableCornerButton::section {
    background: #171B21; border: none; border-right: 1px solid #2F3136;
    border-bottom: 1px solid #2F3136;
}
QTableView::item, QTableWidget::item { border-bottom: 1px solid #2F3136; padding: 3px 7px; }
QTableView::item:selected, QTableWidget::item:selected {
    color: white; background: #143C48;
    border-top: 1px solid #1D7585; border-bottom: 1px solid #1D7585;
}
QSplitter::handle { background: transparent; height: 7px; }
QTabWidget#logsCard { background: transparent; }
QTabWidget#logsCard::pane { background: transparent; border: none; top: 0px; }
QTabWidget#logsCard::tab-bar { left: 5px; }
QTabWidget#logsCard QTabBar { background: transparent; qproperty-drawBase: 0; }
QTabWidget#logsCard QTabBar::tab {
    background: transparent; border: 1px solid transparent; border-radius: 5px;
    padding: 5px 11px; margin: 4px 2px 4px 0; color: #A4ABB4; font-weight: 500;
}
QTabWidget#logsCard QTabBar::tab:hover { color: #F1F3F5; background: #222529; }
QTabWidget#logsCard QTabBar::tab:selected {
    color: #F1F3F5; background: #292D33; border-color: #343A42;
}
QTabWidget#logsCard QTabBar QLabel#tabCountBadge {
    color: #747C86; background: transparent; border: none; font-weight: 550;
}
QTextBrowser#masterLogBrowser {
    padding: 8px 10px; font-family: "Cascadia Mono", "Consolas", monospace; font-size: 13px;
}
QToolButton#panelIconButton {
    background: transparent; border: 1px solid #2F3136; border-radius: 5px; padding: 3px;
}
QToolButton#panelIconButton:hover { background: #222529; }
QToolButton#panelIconButton::menu-indicator { image: none; width: 0px; }
QFrame#logsStrip QToolButton#stripTab {
    background: transparent; border: none; color: #A4ABB4;
    padding: 4px 11px; border-radius: 5px; font-weight: 500;
}
QFrame#logsStrip QToolButton#stripTab:hover { color: #F1F3F5; background: #222529; }
QFrame#logsStrip QToolButton#stripCountBadge {
    background: transparent; border: none; color: #747C86; padding: 4px 2px;
    border-radius: 5px; font-weight: 550;
}
QFrame#logsStrip QLabel#stripHint { color: #747C86; background: transparent; font-size: 11px; }
QMenu#logToolsMenu {
    background: #22262C; border: 1px solid #3A414A; border-radius: 7px; padding: 6px;
}
QMenu#logToolsMenu::item { padding: 7px 30px 7px 28px; border-radius: 4px; }
QMenu#logToolsMenu::item:selected { background: #2D333B; }
QMenu#logToolsMenu::separator { height: 1px; background: #3A414A; margin: 4px 7px; }
QFrame#statusCard { background: #1B1E23; border: none; border-top: 1px solid #2F3136; }
QFrame#statusCard QLabel#statusCaption { color: #747C86; font-size: 11px; }
QFrame#statusCard QLabel#statusValue { color: #F1F3F5; font-size: 13px; }
QScrollBar:vertical { background: transparent; width: 11px; margin: 3px; }
QScrollBar::handle:vertical { background: #344759; border-radius: 4px; min-height: 34px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
)");
        themeManager()->RegisterStyle(this, style);

        const bool open = panel.compare(QStringLiteral("closed"), Qt::CaseInsensitive) != 0;
        statsHost_->setVisible(open);
        closedStrip_->setVisible(!open);
        const bool logsSelected = panel.compare(QStringLiteral("connections"), Qt::CaseInsensitive) != 0;
        tabs_->setCurrentIndex(logsSelected ? 0 : 1);
        menuButton_->setVisible(logsSelected);
        connectionFilterButton_->setVisible(!logsSelected);
        connectionCloseAllButton_->setVisible(!logsSelected);
        QTimer::singleShot(0, this, [this, open] {
            if (!open) return;
            const int total = splitter_->height() - splitter_->handleWidth();
            splitter_->setSizes({qMax(240, total * 3 / 5), qMax(180, total * 2 / 5)});
        });
    }

    QToolButton *menuButton() const { return menuButton_; }
    QMenu *logMenu() const { return logMenu_; }

private:
    QSplitter *splitter_ = nullptr;
    QTableView *table_ = nullptr;
    QFrame *statsHost_ = nullptr;
    QFrame *closedStrip_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QToolButton *menuButton_ = nullptr;
    QToolButton *connectionFilterButton_ = nullptr;
    QToolButton *connectionCloseAllButton_ = nullptr;
    QMenu *logMenu_ = nullptr;
};
} // namespace

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QStringList args = app.arguments();
    themeManager()->ApplyTheme(QStringLiteral("Throned ")
                             + argValue(args, QStringLiteral("--theme"), QStringLiteral("Midnight")));
    auto *window = new PreviewWindow(
        argValue(args, QStringLiteral("--panel"), QStringLiteral("closed")),
        themeManager()->Colors());
    window->show();

    if (args.contains(QStringLiteral("--show"))) return app.exec();

    const QString output = argValue(args, QStringLiteral("--out"), QStringLiteral("row-preview.png"));
    const bool menuRequested = args.contains(QStringLiteral("--menu"));
    QTimer::singleShot(180, window, [window, output, menuRequested] {
        if (!menuRequested || !window->menuButton()->isVisible()) {
            const bool saved = window->grab().save(output, "PNG");
            qApp->exit(saved ? 0 : 2);
            return;
        }
        QMenu *menu = window->logMenu();
        const QSize hint = menu->sizeHint();
        const QPoint popupAt = window->menuButton()->mapToGlobal(
            QPoint(window->menuButton()->width() - hint.width(), window->menuButton()->height() + 2));
        menu->popup(popupAt);
        QTimer::singleShot(120, window, [window, menu, output] {
            QPixmap result = window->grab();
            QPainter painter(&result);
            painter.drawPixmap(window->mapFromGlobal(menu->mapToGlobal(QPoint(0, 0))), menu->grab());
            painter.end();
            const bool saved = result.save(output, "PNG");
            menu->close();
            qApp->exit(saved ? 0 : 2);
        });
    });
    return app.exec();
}
