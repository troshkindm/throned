// Renders the profile table's comfortable rows on fake data, with no database,
// core process or main window in the way. Building this is seconds; building
// Throned to look at a row is half a minute.
//
//   cmake -S tools/row-preview -B build-row
//   cmake --build build-row
//   build-row/row-preview.exe --theme midnight --out rows.png
//
// --show opens a window instead of writing a PNG.

#include <QAbstractTableModel>
#include <QApplication>
#include <QHeaderView>
#include <QTableView>
#include <QTimer>

#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ProfileRowDelegate.h"
#include "include/ui/utils/ProfilesTableModel.h"

// ThemeManager only reaches for this on the legacy stylesheet themes, which this
// harness never applies.
QString ReadFileText(const QString &) { return {}; }

namespace {
    using RowVisual = ProfilesTableModel::RowVisual;

    // One row per state worth looking at, so a change that fixes one and breaks
    // another is visible in the same picture.
    QList<RowVisual> sampleRows() {
        QList<RowVisual> rows;
        rows.append({.name = "Auto (gRPC)", .chip = "VLESS", .address = "192.0.2.10:443",
                     .country = "DE", .exitIp = "192.0.2.10", .latency = "60 ms",
                     .udp = "61 ms ±4", .speedDown = "148 Mbps", .speedUp = "36 Mbps",
                     .latencyMs = 60});
        rows.append({.name = "Auto (hy2)", .chip = "Hysteria", .address = "192.0.2.10:16011",
                     .country = "DE", .exitIp = "192.0.2.10", .latency = "57 ms",
                     .udp = "58 ms ±3 / 1%", .speedDown = "196 Mbps", .speedUp = "128 Mbps",
                     .trafficDown = "33.49 GiB", .trafficUp = "38.08 GiB",
                     .latencyMs = 57, .running = true});
        rows.append({.name = "Moscow (games, a name long enough to want the whole column)",
                     .chip = "Hysteria", .address = "192.0.2.10:16012", .latency = "47 ms",
                     .udp = "49 ms ±11 / 4%", .trafficDown = "994.18 MiB", .trafficUp = "463.10 MiB",
                     .latencyMs = 47, .udpDegraded = true});
        rows.append({.name = "Moscow → Helsinki", .chip = "VLESS", .address = "192.0.2.10:2088",
                     .country = "FI", .exitIp = "198.51.100.7", .latency = "232 ms",
                     .latencyMs = 232});
        rows.append({.name = "Reserve · CDN", .chip = "VLESS", .address = "203.0.113.88:443",
                     .latency = "Unavailable", .latencyMs = -1});
        rows.append({.name = "Warp bypass", .chip = "WireGuard", .address = "203.0.113.90:51820",
                     .udp = "No reply", .trafficDown = "88.10 MiB", .trafficUp = "12.40 MiB",
                     .udpDegraded = true});
        rows.append({.name = "Never tested", .chip = "Shadowsocks", .address = "203.0.113.71:2087"});
        return rows;
    }

    class SampleModel : public QAbstractTableModel {
    public:
        explicit SampleModel(QObject *parent = nullptr)
            : QAbstractTableModel(parent), m_rows(sampleRows()) {}

        int rowCount(const QModelIndex &parent = {}) const override {
            return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
        }
        int columnCount(const QModelIndex &parent = {}) const override {
            return parent.isValid() ? 0 : ProfilesTableModel::ComfortColumnCount;
        }
        QVariant data(const QModelIndex &index, int role) const override {
            if (!index.isValid() || index.row() >= m_rows.size()) return {};
            if (role == ProfilesTableModel::RowVisualRole) return QVariant::fromValue(m_rows[index.row()]);
            if (role == Qt::DisplayRole && index.column() == ProfilesTableModel::ColcServer)
                return m_rows[index.row()].name;
            return {};
        }
        QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
            if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
            switch (section) {
            case ProfilesTableModel::ColcServer: return QStringLiteral("Server");
            case ProfilesTableModel::ColcPing: return QStringLiteral("Ping · UDP");
            case ProfilesTableModel::ColcSpeed: return QStringLiteral("Speed");
            case ProfilesTableModel::ColcTraffic: return QStringLiteral("Traffic");
            default: return {};
            }
        }

    private:
        QList<RowVisual> m_rows;
    };

    QString argValue(const QStringList &args, const QString &name, const QString &fallback) {
        const int at = args.indexOf(name);
        return at >= 0 && at + 1 < args.size() ? args.at(at + 1) : fallback;
    }
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const QStringList args = app.arguments();

    themeManager->ApplyTheme(QStringLiteral("Throned ") + argValue(args, "--theme", "Midnight"));
    const auto colors = themeManager->Colors();

    QTableView view;
    auto *model = new SampleModel(&view);
    view.setModel(model);
    view.setItemDelegate(new ProfileRowDelegate(&view));
    view.setShowGrid(false);
    view.setSelectionBehavior(QAbstractItemView::SelectRows);
    view.verticalHeader()->hide();
    view.verticalHeader()->setDefaultSectionSize(ProfileRowDelegate::RowHeight);
    view.horizontalHeader()->setSectionResizeMode(ProfilesTableModel::ColcServer, QHeaderView::Stretch);
    for (int column : {ProfilesTableModel::ColcPing, ProfilesTableModel::ColcSpeed,
                       ProfilesTableModel::ColcTraffic}) {
        view.horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
        view.horizontalHeader()->resizeSection(column,
            ProfileRowDelegate::metricColumnWidth(column, view.font()));
    }
    // The same rules the main window's stylesheet gives the table, resolved by hand:
    // this harness has no ThronedPalette-resolved sheet of its own.
    view.setStyleSheet(QStringLiteral(R"(
QTableView { background: %1; border: 1px solid %2; border-radius: 7px; outline: none;
             selection-background-color: %3; }
QHeaderView::section { background: %1; color: %4; border: none; border-right: 1px solid %2;
                       border-bottom: 1px solid %2; padding: 5px 8px; font-weight: 500; }
QHeaderView { background: %1; }
QTableView::item { border-bottom: 1px solid %2; }
QTableView::item:selected { background: %3; border-top: 1px solid %5; border-bottom: 1px solid %5; }
)").arg(colors.surface.name(), colors.border.name(), colors.selection.name(),
        colors.textMuted.name(), colors.selectionBorder.name()));

    view.selectRow(1);
    view.resize(argValue(args, "--width", "1000").toInt(),
                view.horizontalHeader()->height() + ProfileRowDelegate::RowHeight * model->rowCount() + 4);

    if (args.contains(QStringLiteral("--show"))) {
        view.show();
        return app.exec();
    }

    view.show();
    const QString out = argValue(args, "--out", "row-preview.png");
    QTimer::singleShot(120, &view, [&view, out] {
        view.grab().save(out, "PNG");
        qApp->quit();
    });
    return app.exec();
}
