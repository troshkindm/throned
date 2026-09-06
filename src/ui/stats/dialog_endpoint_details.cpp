#include "include/ui/stats/dialog_endpoint_details.h"

#include "include/api/RPC.h"
#include "include/database/DatabaseManager.h"
#include "include/database/ProfilesRepo.h"

#include <QBrush>
#include <QClipboard>
#include <QDateTime>
#include <QMutex>
#include <QGuiApplication>
#include <QHeaderView>
#include <QScreen>
#include <QShortcut>
#include <QTableWidget>
#include <QTimer>

#include <algorithm>

namespace {
QStringList endpointDetailsToStringList(const std::vector<std::string> &values) {
    QStringList out;
    out.reserve(static_cast<qsizetype>(values.size()));
    for (const auto &value: values) {
        const auto text = QString::fromStdString(value).trimmed();
        if (!text.isEmpty()) out << text;
    }
    return out;
}

// Pushed suffixes arrive raw: any case, maybe a trailing dot, and "." alone means every domain.
QStringList endpointDetailsNormalizeDomains(const std::vector<std::string> &values) {
    QStringList out;
    for (const auto &value: values) {
        auto text = QString::fromStdString(value).trimmed();
        if (text.isEmpty()) continue;
        if (text != QStringLiteral(".")) {
            while (text.endsWith(QLatin1Char('.'))) text.chop(1);
            text = text.toLower();
        }
        if (text.isEmpty() || out.contains(text)) continue;
        out << text;
    }
    return out;
}
QMutex endpointDetailsProfilesMu;
QMap<QString, int> endpointDetailsProfiles;
} // namespace

namespace Stats {
void SetVpnEndpointProfiles(const QMap<QString, int> &tagToProfileID) {
    QMutexLocker lk(&endpointDetailsProfilesMu);
    endpointDetailsProfiles = tagToProfileID;
}

int VpnEndpointProfileID(const QString &tag) {
    QMutexLocker lk(&endpointDetailsProfilesMu);
    return endpointDetailsProfiles.value(tag, -1);
}

QString VpnEndpointDisplayName(const QString &tag) {
    const int id = VpnEndpointProfileID(tag);
    if (id < 0 || Configs::dataManager == nullptr) return tag;
    const auto ent = Configs::dataManager->profilesRepo->GetProfile(id);
    if (ent == nullptr || ent->outbound == nullptr) return tag;
    return ent->outbound->DisplayName();
}

VpnEndpointView MakeVpnEndpointView(const libcore::VPNEndpointStatus &status) {
    VpnEndpointView view;
    view.tag = QString::fromStdString(status.tag.value());
    view.displayName = VpnEndpointDisplayName(view.tag);
    view.state = QString::fromStdString(status.state.value());
    view.error = QString::fromStdString(status.error.value());
    view.server = QString::fromStdString(status.server.value());
    view.network = QString::fromStdString(status.network.value());
    view.cipher = QString::fromStdString(status.cipher.value());
    view.ipv4 = endpointDetailsToStringList(status.ipv4);
    view.ipv6 = endpointDetailsToStringList(status.ipv6);
    view.dns = endpointDetailsToStringList(status.dns);
    view.routes = endpointDetailsToStringList(status.routes);
    view.excludedRoutes = endpointDetailsToStringList(status.excluded_routes);
    view.domains = endpointDetailsNormalizeDomains(status.search_domains);
    view.mtu = status.mtu.value();
    view.connectedSince = status.connected_since.value();
    view.connected = status.connected.value();
    return view;
}

QString VpnStateText(const QString &state) {
    if (state == QStringLiteral("connected")) return QObject::tr("Connected");
    if (state == QStringLiteral("connecting")) return QObject::tr("Connecting…");
    if (state == QStringLiteral("auth-pending")) return QObject::tr("Waiting for sign-in");
    if (state == QStringLiteral("error")) return QObject::tr("Error");
    if (state.isEmpty()) return QObject::tr("Unknown");
    return state;
}

QColor VpnStateColor(const QString &state) {
    if (state == QStringLiteral("connected")) return kStatsHealthyColor;
    if (state == QStringLiteral("error")) return kStatsProblemColor;
    return kStatsAccentColor;
}

QString HumanizeDuration(qint64 seconds) {
    if (seconds < 0) seconds = 0;
    const qint64 d = seconds / 86400;
    seconds %= 86400;
    const qint64 h = seconds / 3600;
    seconds %= 3600;
    const qint64 m = seconds / 60;
    seconds %= 60;
    QStringList parts;
    if (d > 0) parts << QStringLiteral("%1d").arg(d);
    if (h > 0) parts << QStringLiteral("%1h").arg(h);
    if (m > 0) parts << QStringLiteral("%1m").arg(m);
    if (d == 0 && h == 0) parts << QStringLiteral("%1s").arg(seconds);
    return parts.join(QLatin1Char(' '));
}
} // namespace Stats

DialogEndpointDetails::DialogEndpointDetails(const Stats::VpnEndpointView &view, QWidget *parent)
    : QDialog(parent), ui(new Ui::DialogEndpointDetails) {
    ui->setupUi(this);

    auto *table = ui->table;
    table->setColumnCount(2);
    table->horizontalHeader()->setVisible(false);
    table->verticalHeader()->setVisible(false);
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    // Wrapped rows differ wildly in height, so item-granular scrolling would jump.
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    auto *copy = new QShortcut(QKeySequence::Copy, table);
    copy->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copy, &QShortcut::activated, this, [this]() { copySelection(); });

    connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    applyStatus(view);

    const auto *scr = screen() != nullptr ? screen() : QGuiApplication::primaryScreen();
    if (scr != nullptr) resize(size().expandedTo(sizeHint()).boundedTo(scr->availableGeometry().size()));
}

DialogEndpointDetails::~DialogEndpointDetails() {
    delete ui;
}

void DialogEndpointDetails::setRows(const QList<DetailRow> &rows) {
    auto *table = ui->table;

    bool sameShape = rows.size() == rows_.size();
    for (qsizetype row = 0; sameShape && row < rows.size(); row++) {
        sameShape = rows[row].kind == rows_[row].kind && rows[row].key == rows_[row].key &&
                    rows[row].label == rows_[row].label;
    }

    // Only the shape change rebuilds; a value-only refill would drop the user's scroll position.
    if (!sameShape) {
        table->clearSpans();
        table->setRowCount(static_cast<int>(rows.size()));
        for (int row = 0; row < static_cast<int>(rows.size()); row++) {
            for (int column = 0; column < 2; column++) {
                if (table->item(row, column) == nullptr) table->setItem(row, column, new QTableWidgetItem());
                table->item(row, column)->setTextAlignment(Qt::AlignLeft | Qt::AlignTop);
            }
            auto *label = table->item(row, 0);
            const bool isSection = rows[row].kind == DetailRow::Section;
            label->setText(rows[row].kind == DetailRow::Value ? QString() : rows[row].label);
            auto font = table->font();
            font.setBold(isSection);
            label->setFont(font);
            if (isSection) table->setSpan(row, 0, 1, 2);
        }
    }

    bool valuesChanged = false;
    for (int row = 0; row < static_cast<int>(rows.size()); row++) {
        auto *value = table->item(row, 1);
        if (value->text() != rows[row].value) valuesChanged = true;
        value->setText(rows[row].value);
        value->setForeground(rows[row].color.isValid() ? QBrush(rows[row].color) : QBrush());
    }

    rows_ = rows;
    if (!sameShape) fitRowHeights();
    if (!sameShape || valuesChanged) queueRowFit();
}

void DialogEndpointDetails::fitRowHeights() {
    auto *table = ui->table;

    // Measured off the field labels alone: ResizeToContents would also measure the spanned headings.
    int width = 0;
    for (const auto &row: rows_) {
        if (row.kind != DetailRow::Field) continue;
        width = std::max(width, table->fontMetrics().horizontalAdvance(row.label));
    }
    table->horizontalHeader()->resizeSection(
        0, std::max(80, std::min(width + 24, table->viewport()->width() / 2)));
    table->resizeRowsToContents();
}

// Wrapping depends on the stretched value column, which only settles after the pending layout pass.
void DialogEndpointDetails::queueRowFit() {
    if (rowFitQueued_) return;
    rowFitQueued_ = true;
    QTimer::singleShot(0, this, [this]() {
        rowFitQueued_ = false;
        fitRowHeights();
    });
}

void DialogEndpointDetails::resizeEvent(QResizeEvent *event) {
    QDialog::resizeEvent(event);
    queueRowFit();
}

void DialogEndpointDetails::copySelection() const {
    QStringList lines;
    const auto selected = ui->table->selectedItems();
    for (const auto *item: selected) {
        if (!item->text().isEmpty()) lines << item->text();
    }
    if (!lines.isEmpty()) QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
}

void DialogEndpointDetails::applyStatus(const Stats::VpnEndpointView &view) {
    tag_ = view.tag;
    setWindowTitle(view.displayName.isEmpty() ? tr("Endpoint Details")
                                              : tr("Endpoint — %1").arg(view.displayName));

    QList<DetailRow> rows;
    const auto field = [&rows](const QString &key, const QString &label, const QString &value,
                               const QColor &color = QColor()) {
        if (!value.isEmpty()) rows << DetailRow{DetailRow::Field, key, label, value, color};
    };
    const auto section = [&rows](const QString &title, const QStringList &values) {
        if (values.isEmpty()) return;
        rows << DetailRow{DetailRow::Section, {}, QStringLiteral("%1 (%2)").arg(title).arg(values.size()), {}, {}};
        for (const auto &value: values) rows << DetailRow{DetailRow::Value, {}, {}, value, {}};
    };

    field(QStringLiteral("state"), tr("State"), Stats::VpnStateText(view.state),
          Stats::VpnStateColor(view.state));
    field({}, tr("Error"), view.error, Stats::kStatsProblemColor);
    field({}, tr("Server"), view.server);
    field({}, tr("Transport"), view.network);
    field({}, tr("Cipher"), view.cipher);
    field({}, tr("MTU"), view.mtu > 0 ? QString::number(view.mtu) : QString());
    const bool hasUptime = view.connected && view.connectedSince > 0;
    field(QStringLiteral("uptime"), tr("Connected for"),
          hasUptime ? Stats::HumanizeDuration(QDateTime::currentSecsSinceEpoch() - view.connectedSince)
                    : QString());
    field({}, tr("IPv4 address"), view.ipv4.join(QStringLiteral(", ")));
    field({}, tr("IPv6 address"), view.ipv6.join(QStringLiteral(", ")));

    QStringList domainItems;
    domainItems.reserve(view.domains.size());
    for (const auto &domain: view.domains) {
        domainItems << (domain == QStringLiteral(".")
                            ? tr("All domains — every DNS query goes through this tunnel")
                            : domain);
    }

    section(tr("Routes through this tunnel"), view.routes);
    section(tr("Kept outside this tunnel"), view.excludedRoutes);
    section(tr("DNS servers pushed by the server"), view.dns);
    section(tr("Domains routed to this tunnel"), domainItems);

    setRows(rows);
}

void DialogEndpointDetails::markGone() {
    for (int row = 0; row < static_cast<int>(rows_.size()); row++) {
        auto *value = ui->table->item(row, 1);
        if (value == nullptr) continue;
        if (rows_[row].key == QStringLiteral("state")) {
            value->setText(tr("No longer running"));
            value->setForeground(QBrush(Stats::kStatsProblemColor));
        } else if (rows_[row].key == QStringLiteral("uptime")) {
            value->setText(QStringLiteral("—"));
        }
    }
}
