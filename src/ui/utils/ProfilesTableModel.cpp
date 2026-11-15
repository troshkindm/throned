#include "include/ui/utils/ProfilesTableModel.h"

#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/database/entities/Group.h"
#include "include/database/entities/Profile.h"
#include "include/configs/common/Outbound.h"
#include "include/ui/setting/ThemeManager.hpp"
#include "include/ui/utils/ServiceCheckHtml.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeData>
#include <QPalette>
#include <QRegularExpression>
#include <algorithm>

#include "include/configs/generate.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"

ProfilesTableModel::ProfilesTableModel(QObject *parent)
    : QAbstractTableModel(parent) {}

int ProfilesTableModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_profileIds.size();
}

int ProfilesTableModel::columnCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    if (m_rowStyle == RowStyle::Comfortable) return ComfortColumnCount;
    // ColUDP is the last column, so leaving it out of the count removes it entirely
    // rather than leaving an empty slot behind.
    return m_udpColumnVisible ? ColumnCount : ColumnCount - 1;
}

void ProfilesTableModel::setRowStyle(RowStyle style) {
    if (m_rowStyle == style) return;
    beginResetModel();
    m_rowStyle = style;
    endResetModel();
}

void ProfilesTableModel::setUdpColumnVisible(bool visible) {
    if (m_udpColumnVisible == visible) return;
    beginResetModel();
    m_udpColumnVisible = visible;
    endResetModel();
}

Qt::ItemFlags ProfilesTableModel::flags(const QModelIndex &index) const {
    Qt::ItemFlags defaultFlags = QAbstractTableModel::flags(index);
    if (index.isValid()) {
        return Qt::ItemIsDragEnabled | defaultFlags;
    }
    return Qt::ItemIsDropEnabled | defaultFlags;
}

Qt::DropActions ProfilesTableModel::supportedDropActions() const {
    return Qt::MoveAction;
}

QStringList ProfilesTableModel::mimeTypes() const {
    return {"application/profile-row-number"};
}

QMimeData *ProfilesTableModel::mimeData(const QModelIndexList &indexes) const {
    auto *mimeData = new QMimeData;
    QByteArray encodedData;

    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    if (!indexes.isEmpty()) {
        stream << indexes.at(0).row();
    }

    mimeData->setData("application/profile-row-number", encodedData);
    return mimeData;
}

void ProfilesTableModel::ensureCached(int profileId) const {
    if (m_cache.contains(profileId)) {
        for (int i = 0; i < m_lruOrder.size(); ++i) {
            if (m_lruOrder[i] == profileId) {
                m_lruOrder.move(i, m_lruOrder.size() - 1);
                break;
            }
        }
        return;
    }

    auto profile = Configs::dataManager->profilesRepo->GetProfile(profileId);
    if (!profile) return;

    while (m_cache.size() >= m_cacheSize && !m_lruOrder.isEmpty()) {
        evictOne();
    }
    m_cache[profileId] = profile;
    m_lruOrder.append(profileId);
}

void ProfilesTableModel::evictOne() const {
    if (m_lruOrder.isEmpty()) return;
    int id = m_lruOrder.takeFirst();
    m_cache.remove(id);
    m_addressCache.remove(id);
}

ProfilesTableModel::RowVisual ProfilesTableModel::buildRowVisual(
    const std::shared_ptr<Configs::Profile> &profile, bool isRunning) const {
    RowVisual visual;
    visual.running = isRunning;
    if (profile->outbound) {
        visual.name = profile->outbound->name;
        visual.chip = profile->outbound->DisplayType();
        visual.address = profile->outbound->DisplayAddress();
    }

    const auto group = Configs::dataManager->groupsRepo->GetGroup(profile->gid);
    const auto shown = group ? group->test_items_to_show : Configs::testShowItems::all;
    const bool showSpeed = shown == Configs::testShowItems::all || shown == Configs::testShowItems::speedOnly;
    const bool showIP = shown == Configs::testShowItems::all || shown == Configs::testShowItems::ipOnly;

    visual.country = profile->test_country.toUpper();
    if (showIP) visual.exitIp = profile->ip_out;

    visual.latencyMs = profile->latency;
    if (profile->latency == Configs::kLatencyConnectOnly)
        visual.latency = tr("Connect OK");
    else if (profile->latency < 0)
        visual.latency = tr("Unavailable");
    else if (profile->latency > 0)
        visual.latency = QStringLiteral("%1 ms").arg(profile->latency);

    visual.favorite = profile->favorite;
    visual.udp = profile->DisplayUDPResult();
    visual.udpDegraded = profile->udp_loss > 0 || profile->udp_jitter >= 10 || profile->udp_avg < 0;

    if (showSpeed) {
        if (!profile->dl_speed.isEmpty() && profile->dl_speed != QStringLiteral("N/A"))
            visual.speedDown = profile->dl_speed;
        if (!profile->ul_speed.isEmpty() && profile->ul_speed != QStringLiteral("N/A"))
            visual.speedUp = profile->ul_speed;
    }
    if (profile->traffic_downlink + profile->traffic_uplink != 0) {
        visual.trafficDown = ReadableSize(profile->traffic_downlink);
        visual.trafficUp = ReadableSize(profile->traffic_uplink);
    }
    return visual;
}

QVariant ProfilesTableModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profileIds.size() || index.column() < 0 || index.column() >= columnCount()) {
        return {};
    }
    const int profileId = m_profileIds[index.row()];
    if (role == ProfileIdRole) {
        return profileId;
    }
    if (role == PinnedRole) {
        return isPinnedRow(index.row());
    }
    ensureCached(profileId);
    auto it = m_cache.constFind(profileId);
    if (it == m_cache.constEnd()) return {};
    const std::shared_ptr<Configs::Profile> &profile = it.value();
    if (!profile) return {};

    const int startedId = Configs::dataManager->settingsRepo->started_id;
    const bool isRunning = (profile->id == startedId);
    const QColor runningColor = isRunning ? themeManager()->Colors().success : QColor();

    if (m_rowStyle == RowStyle::Comfortable) {
        if (role == RowVisualRole) return QVariant::fromValue(buildRowVisual(profile, isRunning));
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>((index.column() == ColcServer ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
        }
        // The delegate paints; these keep keyboard search and accessibility honest.
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
                case ColcServer:
                    return profile->outbound ? profile->outbound->name : QString();
                case ColcPing:
                    return profile->DisplayTestResult();
                case ColcSpeed:
                    return QString();
                case ColcTraffic:
                    return profile->DisplayTraffic();
                default:
                    return {};
            }
        }
        if (role == Qt::ToolTipRole && index.column() == ColcPing && !profile->udp_error.isEmpty()) {
            return tr("UDP probe error: %1").arg(profile->udp_error);
        }
        if (role == Qt::ToolTipRole && index.column() == ColcPing && !profile->service_checks.isEmpty()) return ServiceCheckHtml::details(*profile);
        return {};
    }

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
            case ColType: {
                if (!profile->outbound) return QString();
                auto type = profile->outbound->DisplayType();
                if (Configs::dataManager->settingsRepo->show_config_security) {
                    auto sec = profile->outbound->DisplaySecurity();
                    if (!sec.isEmpty()) type += QStringLiteral(" (%1)").arg(sec);
                }
                return type;
            }
            case ColAddress: {
                auto address = m_addressCache.constFind(profileId);
                if (address == m_addressCache.constEnd()) address = m_addressCache.insert(profileId, Configs::DisplayEffectiveAddress(profile));
                return address.value();
            }
            case ColName:
                return profile->outbound ? profile->outbound->name : QString();
            case ColTestResult:
                return profile->DisplayTestResult();
            case ColTraffic:
                return profile->DisplayTraffic();
            case ColUDP:
                return profile->DisplayUDPResult();
            default:
                return {};
        }
    }
    if (role == Qt::ToolTipRole) {
        if (index.column() == ColUDP && !profile->udp_error.isEmpty()) {
            return tr("UDP probe error: %1").arg(profile->udp_error);
        }
        if (index.column() == ColType && Configs::dataManager->settingsRepo->show_config_security && profile->outbound) {
            const auto security = profile->outbound->EffectiveSecurity();
            if (security.compromised) return tr("Certificate checks are turned off by the \"Skip TLS certificate authentication\" setting.");
            if (security.isDangerous()) return tr("This config's traffic is not properly protected.");
        }
        if (index.column() == ColTestResult && !profile->service_checks.isEmpty()) return ServiceCheckHtml::details(*profile);
        return {};
    }
    if (role == Qt::TextAlignmentRole) {
        // Numbers line up on their right edge so latencies and traffic totals
        // can be compared down the column instead of being read one by one.
        if (index.column() == ColTestResult || index.column() == ColTraffic)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role == Qt::ForegroundRole) {
        if (index.column() == ColTestResult) {
            QColor latencyColor = profile->DisplayLatencyColor();
            if (latencyColor.isValid()) return latencyColor;
        }
        if (isRunning && runningColor.isValid()) return runningColor;
        return {};
    }
    return {};
}

void ProfilesTableModel::setSortIndicator(int column, bool descending) {
    if (m_sortColumn == column && m_sortDescending == descending) return;
    m_sortColumn = column;
    m_sortDescending = descending;
    emit headerDataChanged(Qt::Horizontal, 0, qMax(0, columnCount() - 1));
}

QVariant ProfilesTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    const auto marked = [this, section](const QString &label) {
        if (section != m_sortColumn) return label;
        return label + (m_sortDescending ? QStringLiteral("  ↓") : QStringLiteral("  ↑"));
    };
    if (m_rowStyle == RowStyle::Comfortable && orientation == Qt::Horizontal) {
        if (role == Qt::TextAlignmentRole)
            return static_cast<int>((section == ColcServer ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
        if (role != Qt::DisplayRole) return {};
        switch (section) {
            case ColcServer:
                return marked(tr("Server"));
            case ColcPing:
                return marked(tr("Ping · UDP"));
            case ColcSpeed:
                return tr("Speed");
            case ColcTraffic:
                return marked(tr("Traffic"));
            default:
                return {};
        }
    }
    if (role == Qt::TextAlignmentRole && orientation == Qt::Horizontal) {
        // A header centred over left-aligned text reads as a third alignment;
        // each one now sits over its own column's edge.
        const bool numeric = section == ColTestResult || section == ColTraffic;
        return static_cast<int>((numeric ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter);
    }
    if (role != Qt::DisplayRole) return {};
    if (orientation == Qt::Horizontal) {
        switch (section) {
            case ColType:
                return marked(tr("Type"));
            case ColAddress:
                return marked(tr("Address"));
            case ColName:
                return marked(tr("Name"));
            case ColTestResult:
                return marked(tr("Test Result"));
            case ColTraffic:
                return marked(tr("Traffic"));
            case ColUDP:
                return tr("UDP");
            default:
                return {};
        }
    }
    return {};
}

void ProfilesTableModel::setProfileIds(const QList<int> &ids, int pinnedCount) {
    const int oldPinned = m_pinnedCount;
    beginResetModel();
    m_profileIds = ids;
    m_pinnedCount = std::clamp(pinnedCount, 0, static_cast<int>(ids.size()));
    id2row.clear();
    int idx = 0;
    for (const auto &id: ids) {
        id2row.insert(id, idx++);
    }
    m_cache.clear();
    m_lruOrder.clear();
    m_addressCache.clear();
    m_filterKeys.clear();
    m_filterIndexBuilt = false;
    endResetModel();
    if (m_pinnedCount != oldPinned) emit pinnedCountChanged();
}

void ProfilesTableModel::setPinnedCount(int count) {
    count = std::clamp(count, 0, static_cast<int>(m_profileIds.size()));
    if (count == m_pinnedCount) return;
    m_pinnedCount = count;
    emit pinnedCountChanged();
}

namespace {
QList<ProfilesTableModel::ServiceFilterKey> serviceFilterKeys(const QString &serviceChecks) {
    if (serviceChecks.isEmpty()) return {};
    const auto root = QJsonDocument::fromJson(serviceChecks.toUtf8()).object();
    if (!root["error"].toString().isEmpty()) return {};
    QHash<QString, QString> categoryNames;
    for (const auto &value: root["cats"].toArray()) {
        const auto category = value.toObject();
        categoryNames.insert(category["id"].toString(), category["name"].toString());
    }
    static const QRegularExpression separators(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QList<ProfilesTableModel::ServiceFilterKey> services;
    for (const auto &value: root["checks"].toArray()) {
        const auto check = value.toObject();
        const auto category = check["cat"].toString();
        const auto status = check["status"].toString();
        ProfilesTableModel::ServiceFilterKey service;
        service.state = status == "available" || status == "partial" ? 1 : status == "restricted" || status == "blocked" ? -1
                                                                                                                         : 0;
        for (const auto &name: {check["id"].toString(), check["name"].toString(), category, categoryNames.value(category)}) {
            if (name.isEmpty()) continue;
            service.names << name.toLower();
            service.words << name.toLower().split(separators, Qt::SkipEmptyParts);
        }
        services << service;
    }
    return services;
}

ProfilesTableModel::FilterKey makeFilterKey(const std::shared_ptr<Configs::Profile> &profile) {
    ProfilesTableModel::FilterKey key;
    key.type = profile->type;
    key.country = profile->test_country;
    key.services = serviceFilterKeys(profile->service_checks);
    if (profile->outbound) {
        key.address = profile->outbound->server;
        key.effectiveAddress = Configs::EffectiveEndpointHost(profile);
        key.name = profile->outbound->name;
        key.port = profile->outbound->server_port;
    }
    return key;
}
} // namespace

void ProfilesTableModel::ensureFilterIndex() const {
    if (m_filterIndexBuilt) return;
    m_filterIndexBuilt = true;
    m_filterKeys.clear();
    m_filterKeys.reserve(m_profileIds.size());
    for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(m_profileIds)) {
        if (profile) m_filterKeys.insert(profile->id, makeFilterKey(profile));
    }
}

const ProfilesTableModel::FilterKey *ProfilesTableModel::filterKeyAt(int row) const {
    if (row < 0 || row >= m_profileIds.size()) return nullptr;
    ensureFilterIndex();
    auto it = m_filterKeys.constFind(m_profileIds[row]);
    return it == m_filterKeys.constEnd() ? nullptr : &it.value();
}

void ProfilesTableModel::refreshTable(const QList<int> &ids, int pinnedCount, bool mayNeedReset) {
    if (m_profileIds.isEmpty() && ids.isEmpty()) return;

    const bool sameOrder = ids == m_profileIds;
    if (mayNeedReset && !sameOrder) {
        setProfileIds(ids, pinnedCount);
    } else {
        // A stale order keeps its own count until the reset that replaces it.
        if (sameOrder) setPinnedCount(pinnedCount);
        m_addressCache.clear();
        m_filterKeys.clear();
        m_filterIndexBuilt = false;

        QModelIndex topLeft = index(0, 0);
        QModelIndex bottomRight = index(m_profileIds.count() - 1, columnCount() - 1);

        emit dataChanged(topLeft, bottomRight);
    }
}

void ProfilesTableModel::refreshProfileId(int profileId) {
    if (!id2row.contains(profileId)) return;
    m_addressCache.remove(profileId);
    // Keep the filter key in step before dataChanged makes the proxy re-test the row.
    if (m_filterIndexBuilt) {
        if (auto profile = Configs::dataManager->profilesRepo->GetProfile(profileId)) {
            m_filterKeys.insert(profileId, makeFilterKey(profile));
        }
    }
    auto r = id2row.value(profileId);
    QModelIndex top = index(r, 0);
    QModelIndex bottom = index(r, columnCount() - 1);
    emit dataChanged(top, bottom);
}

void ProfilesTableModel::invalidateAddresses() {
    m_addressCache.clear();
    m_filterKeys.clear();
    m_filterIndexBuilt = false;
    if (m_profileIds.isEmpty()) return;
    emit dataChanged(index(0, ColAddress), index(static_cast<int>(m_profileIds.size()) - 1, ColAddress), {Qt::DisplayRole});
}

void ProfilesTableModel::moveProfile(int from, int to, int pinnedCount) {
    const auto count = static_cast<int>(m_profileIds.size());
    if (from < 0 || to < 0 || from >= count || to >= count) return;
    m_profileIds.move(from, to);
    int first = std::min(from, to);
    int last = std::max(from, to);
    for (int i = first; i <= last; ++i) id2row[m_profileIds[i]] = i;

    const int oldPinned = m_pinnedCount;
    setPinnedCount(pinnedCount);
    if (m_pinnedCount != oldPinned) {
        // The divider under the last pinned row moves with the count.
        first = std::max(std::min({first, oldPinned - 1, m_pinnedCount - 1}), 0);
        last = std::max({last, oldPinned - 1, m_pinnedCount - 1});
    }
    emit dataChanged(index(first, 0), index(last, ColumnCount - 1));
}

int ProfilesTableModel::indexOfProfile(int id) {
    if (id2row.contains(id)) return id2row.value(id);
    return -1;
}

int ProfilesTableModel::columnOfSort(GroupSortMethod::GroupSortMethod method) const {
    if (m_rowStyle == RowStyle::Comfortable) {
        switch (method) {
            case GroupSortMethod::ByName:
                return ColcServer;
            case GroupSortMethod::ByTestResult:
            case GroupSortMethod::ByLatency:
                return ColcPing;
            case GroupSortMethod::ByTraffic:
                return ColcTraffic;
            default:
                return -1;
        }
    }
    switch (method) {
        case GroupSortMethod::ByType:
        case GroupSortMethod::BySecurity:
            return ColType;
        case GroupSortMethod::ByAddress:
            return ColAddress;
        case GroupSortMethod::ByName:
            return ColName;
        case GroupSortMethod::ByTestResult:
        case GroupSortMethod::ByLatency:
            return ColTestResult;
        case GroupSortMethod::ByTraffic:
            return ColTraffic;
        default:
            return -1;
    }
}

GroupSortMethod::GroupSortMethod ProfilesTableModel::sortOfColumn(int column) const {
    if (m_rowStyle == RowStyle::Comfortable) {
        switch (column) {
            case ColcServer:
                return GroupSortMethod::ByName;
            case ColcPing:
                return GroupSortMethod::ByLatency;
            case ColcTraffic:
                return GroupSortMethod::ByTraffic;
            default:
                return GroupSortMethod::Raw;
        }
    }
    switch (column) {
        case ColType:
            return GroupSortMethod::ByType;
        case ColAddress:
            return GroupSortMethod::ByAddress;
        case ColName:
            return GroupSortMethod::ByName;
        case ColTestResult:
            return GroupSortMethod::ByTestResult;
        case ColTraffic:
            return GroupSortMethod::ByTraffic;
        default:
            return GroupSortMethod::Raw;
    }
}

bool ProfilesTableModel::isStartedRow(int sourceRow) const {
    return sourceRow >= 0 && sourceRow < m_profileIds.size() && m_profileIds[sourceRow] == Configs::dataManager->settingsRepo->started_id;
}

bool ProfilesTableModel::endsPinnedBlock(const QModelIndex &index) {
    if (!index.data(PinnedRole).toBool()) return false;
    const QModelIndex next = index.siblingAtRow(index.row() + 1);
    return next.isValid() && !next.data(PinnedRole).toBool();
}

QString ProfilesTableModel::rowLabel(int sourceRow, int displayRow) const {
    if (sourceRow < 0 || sourceRow >= m_profileIds.size()) return {};
    if (isStartedRow(sourceRow)) {
        return QStringLiteral("✓");
    }
    return QString::number(displayRow + 1);
}
