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
#include "include/engine/Lifecycle.h"

namespace {
bool showsServices(const Configs::Profile &profile) {
    const auto group = Configs::dataManager->groupsRepo->GetGroup(profile.gid);
    return group == nullptr || group->show_services;
}
} // namespace

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
    return {"application/profile-id"};
}

QMimeData *ProfilesTableModel::mimeData(const QModelIndexList &indexes) const {
    auto *mimeData = new QMimeData;
    QByteArray encodedData;

    QDataStream stream(&encodedData, QIODevice::WriteOnly);

    if (!indexes.isEmpty()) {
        stream << indexes.at(0).data(ProfileIdRole).toInt();
    }

    mimeData->setData("application/profile-id", encodedData);
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

    visual.country = profile->test_country.toUpper();
    visual.exitIp = profile->ip_out;

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

    if (!profile->dl_speed.isEmpty() && profile->dl_speed != QStringLiteral("N/A"))
        visual.speedDown = profile->dl_speed;
    if (!profile->ul_speed.isEmpty() && profile->ul_speed != QStringLiteral("N/A"))
        visual.speedUp = profile->ul_speed;
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
    if (role == ServiceItemsRole) {
        if (m_rowStyle == RowStyle::Comfortable || index.column() != ColExitIP || !showsServices(*profile)) return {};
        return QVariant::fromValue(ServiceCheckHtml::items(*profile));
    }

    const int startedId = Engine::Lifecycle::ResumeTargetId();
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
        if (role == Qt::ToolTipRole && index.column() == ColcPing && !profile->service_checks.isEmpty()) return ServiceCheckHtml::summaryLine(*profile);
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
            case ColLatency:
                return profile->DisplayLatency();
            case ColExitIP:
                return profile->DisplayExitIP();
            case ColSpeed:
                return profile->DisplaySpeed();
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
        if (index.column() == ColType && profile->outbound && profile->outbound->invalid)
            return QCoreApplication::translate("Validation", "Unsupported profile type \"%1\".").arg(profile->type);
        if (index.column() == ColType && Configs::dataManager->settingsRepo->show_config_security && profile->outbound) {
            const auto security = profile->outbound->EffectiveSecurity();
            if (security.compromised) return tr(
                "Certificate checks off: \"Skip TLS certificate authentication\" is "
                "set.");
            if (security.isDangerous()) return tr("This config's traffic is not properly protected.");
        }
        if (index.column() == ColExitIP && !profile->service_checks.isEmpty() && showsServices(*profile)) {
            return ServiceCheckHtml::summaryLine(*profile);
        }
        return {};
    }
    if (role == Qt::TextAlignmentRole) {
        // Numbers line up on their right edge so latencies and traffic totals
        // can be compared down the column instead of being read one by one.
        // A flag alone sits lost at the edge of a column the header keeps wider.
        if (index.column() == ColExitIP) return static_cast<int>(Qt::AlignCenter);
        if (index.column() == ColLatency || index.column() == ColSpeed || index.column() == ColTraffic)
            return static_cast<int>(Qt::AlignRight | Qt::AlignVCenter);
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role == Qt::ForegroundRole) {
        if (index.column() == ColLatency) {
            const QColor latencyColor = profile->DisplayLatencyColor();
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
        if (section == ColExitIP) return static_cast<int>(Qt::AlignCenter);
        const bool numeric = section == ColLatency || section == ColSpeed || section == ColTraffic;
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
            case ColLatency:
                return marked(tr("Latency"));
            case ColExitIP:
                return marked(tr("Exit IP"));
            case ColSpeed:
                return marked(tr("Speed"));
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
    const auto &disabled = Configs::dataManager->settingsRepo->service_check_disabled;
    QList<ProfilesTableModel::ServiceFilterKey> services;
    for (const auto &value: root["checks"].toArray()) {
        const auto check = value.toObject();
        if (disabled.contains(check["id"].toString())) continue;
        const auto category = check["cat"].toString();
        const auto status = check["status"].toString();
        ProfilesTableModel::ServiceFilterKey service;
        service.state = status == "available" || status == "partial" ? 1 : status == "restricted" || status == "blocked" ? -1
                                                                                                                         : 0;
        service.region = check["region"].toString().toLower();
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
    m_serviceNames.clear();
    m_serviceWords.clear();
    for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(m_profileIds)) {
        if (profile) insertFilterKey(profile->id, makeFilterKey(profile));
    }
}

void ProfilesTableModel::insertFilterKey(int profileId, FilterKey key) const {
    for (const auto &service: key.services) {
        for (const auto &name: service.names) m_serviceNames.insert(name);
        for (const auto &word: service.words) m_serviceWords.insert(word);
    }
    m_filterKeys.insert(profileId, std::move(key));
}

const ProfilesTableModel::FilterKey *ProfilesTableModel::filterKeyAt(int row) const {
    if (row < 0 || row >= m_profileIds.size()) return nullptr;
    ensureFilterIndex();
    auto it = m_filterKeys.constFind(m_profileIds[row]);
    return it == m_filterKeys.constEnd() ? nullptr : &it.value();
}

bool ProfilesTableModel::termPicksAnyService(const QString &term) const {
    ensureFilterIndex();
    return termPicksService(m_serviceNames, m_serviceWords, term);
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
            insertFilterKey(profileId, makeFilterKey(profile));
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
        case GroupSortMethod::ByLatency:
        case GroupSortMethod::ByTestResult:
            return ColLatency;
        case GroupSortMethod::ByTraffic:
            return ColTraffic;
        default:
            return -1;
    }
}

int ProfilesTableModel::columnOfSort(const Configs::Group &group) const {
    if (m_rowStyle == RowStyle::Comfortable || group.sort.method != GroupSortMethod::ByTestResult) return columnOfSort(group.sort.method);
    switch (group.test_sort_by) {
        case Configs::testBy::latency:
            return ColLatency;
        case Configs::testBy::ipOut:
            return ColExitIP;
        case Configs::testBy::dlSpeed:
        case Configs::testBy::ulSpeed:
            return ColSpeed;
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
        case ColLatency:
            return GroupSortMethod::ByLatency;
        case ColExitIP:
        case ColSpeed:
            return GroupSortMethod::ByTestResult;
        case ColTraffic:
            return GroupSortMethod::ByTraffic;
        default:
            return GroupSortMethod::Raw;
    }
}

bool ProfilesTableModel::isStartedRow(int sourceRow) const {
    return sourceRow >= 0 && sourceRow < m_profileIds.size() && m_profileIds[sourceRow] == Engine::Lifecycle::ResumeTargetId();
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
