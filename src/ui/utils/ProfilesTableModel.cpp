#include "include/ui/utils/ProfilesTableModel.h"
#include "include/global/Configs.hpp"
#include "include/global/Utils.hpp"
#include "include/database/entities/Group.h"
#include "include/database/entities/Profile.h"
#include "include/configs/common/Outbound.h"
#include <QApplication>
#include <QMimeData>
#include <QPalette>

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

QMimeData* ProfilesTableModel::mimeData(const QModelIndexList &indexes) const {
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
    if (profile->latency == Configs::kLatencyConnectOnly) visual.latency = tr("Connect OK");
    else if (profile->latency < 0) visual.latency = tr("Unavailable");
    else if (profile->latency > 0) visual.latency = QStringLiteral("%1 ms").arg(profile->latency);

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
    if (!index.isValid() || index.row() < 0 || index.row() >= m_profileIds.size()
        || index.column() < 0 || index.column() >= columnCount()) {
        return {};
    }
    const int profileId = m_profileIds[index.row()];
    if (role == ProfileIdRole) {
        return profileId;
    }
    ensureCached(profileId);
    auto it = m_cache.constFind(profileId);
    if (it == m_cache.constEnd()) return {};
    const std::shared_ptr<Configs::Profile> &profile = it.value();
    if (!profile) return {};

    const int startedId = Configs::dataManager->settingsRepo->started_id;
    const bool isRunning = (profile->id == startedId);
    QColor linkColor = isRunning ? QApplication::palette().link().color() : QColor();

    if (m_rowStyle == RowStyle::Comfortable) {
        if (role == RowVisualRole) return QVariant::fromValue(buildRowVisual(profile, isRunning));
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>((index.column() == ColcServer ? Qt::AlignLeft : Qt::AlignRight)
                                    | Qt::AlignVCenter);
        }
        // The delegate paints; these keep keyboard search and accessibility honest.
        if (role == Qt::DisplayRole) {
            switch (index.column()) {
            case ColcServer: return profile->outbound ? profile->outbound->name : QString();
            case ColcPing: return profile->DisplayTestResult();
            case ColcSpeed: return QString();
            case ColcTraffic: return profile->DisplayTraffic();
            default: return {};
            }
        }
        if (role == Qt::ToolTipRole && index.column() == ColcPing && !profile->udp_error.isEmpty()) {
            return tr("UDP probe error: %1").arg(profile->udp_error);
        }
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
        case ColAddress: return profile->outbound ? profile->outbound->DisplayAddress() : QString();
        case ColName: return profile->outbound ? profile->outbound->name : QString();
        case ColTestResult: return profile->DisplayTestResult();
        case ColTraffic: return profile->DisplayTraffic();
        case ColUDP: return profile->DisplayUDPResult();
        default: return {};
        }
    }
    if (role == Qt::ToolTipRole) {
        if (index.column() == ColUDP && !profile->udp_error.isEmpty()) {
            return tr("UDP probe error: %1").arg(profile->udp_error);
        }
        if (index.column() == ColType && Configs::dataManager->settingsRepo->show_config_security
            && profile->outbound && profile->outbound->GetSecurity().isDangerous()) {
            return tr("This config's traffic is not properly protected.");
        }
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
        if (isRunning && linkColor.isValid()) return linkColor;
        return {};
    }
    return {};
}

QVariant ProfilesTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (m_rowStyle == RowStyle::Comfortable && orientation == Qt::Horizontal) {
        if (role == Qt::TextAlignmentRole)
            return static_cast<int>((section == ColcServer ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter);
        if (role != Qt::DisplayRole) return {};
        switch (section) {
        case ColcServer: return tr("Server");
        case ColcPing: return tr("Ping · UDP");
        case ColcSpeed: return tr("Speed");
        case ColcTraffic: return tr("Traffic");
        default: return {};
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
        case ColType: return tr("Type");
        case ColAddress: return tr("Address");
        case ColName: return tr("Name");
        case ColTestResult: return tr("Test Result");
        case ColTraffic: return tr("Traffic");
        case ColUDP: return tr("UDP");
        default: return {};
        }
    }
    return {};
}

void ProfilesTableModel::setProfileIds(const QList<int> &ids) {
    beginResetModel();
    m_profileIds = ids;
    id2row.clear();
    int idx=0;
    for (const auto &id : ids) {
        id2row.insert(id, idx++);
    }
    m_cache.clear();
    m_lruOrder.clear();
    m_filterKeys.clear();
    m_filterIndexBuilt = false;
    endResetModel();
}

namespace {
    ProfilesTableModel::FilterKey makeFilterKey(const std::shared_ptr<Configs::Profile> &profile) {
        ProfilesTableModel::FilterKey key;
        key.type = profile->type;
        key.country = profile->test_country;
        if (profile->outbound) {
            key.address = profile->outbound->server;
            key.name = profile->outbound->name;
            key.port = profile->outbound->server_port;
        }
        return key;
    }
}

void ProfilesTableModel::ensureFilterIndex() const {
    if (m_filterIndexBuilt) return;
    m_filterIndexBuilt = true;
    m_filterKeys.clear();
    m_filterKeys.reserve(m_profileIds.size());
    for (const auto &profile : Configs::dataManager->profilesRepo->GetProfileBatch(m_profileIds)) {
        if (profile) m_filterKeys.insert(profile->id, makeFilterKey(profile));
    }
}

const ProfilesTableModel::FilterKey *ProfilesTableModel::filterKeyAt(int row) const {
    if (row < 0 || row >= m_profileIds.size()) return nullptr;
    ensureFilterIndex();
    auto it = m_filterKeys.constFind(m_profileIds[row]);
    return it == m_filterKeys.constEnd() ? nullptr : &it.value();
}

void ProfilesTableModel::refreshTable(const QList<int> &ids, bool mayNeedReset) {
    if (m_profileIds.isEmpty() && ids.isEmpty()) return;

    const bool needFullReset = mayNeedReset && (
    ids.size() != m_profileIds.size() ||
    !std::equal(ids.begin(), ids.end(), m_profileIds.begin())
    );

    if (needFullReset) {
        setProfileIds(ids);
    } else {
        // A bulk refresh can rewrite filter fields (clearing tests wipes test_country).
        m_filterKeys.clear();
        m_filterIndexBuilt = false;

        QModelIndex topLeft = index(0, 0);
        QModelIndex bottomRight = index(m_profileIds.count() - 1, columnCount() - 1);

        emit dataChanged(topLeft, bottomRight);
    }
}

void ProfilesTableModel::refreshProfileId(int profileId) {
    if (!id2row.contains(profileId)) return;
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

void ProfilesTableModel::emplaceProfiles(int row1, int row2) {
    if (m_profileIds.size() <= row1 || m_profileIds.size() <= row2) return;
    m_profileIds.insert(row2+1, m_profileIds[row1]);
    if (row1 < row2) m_profileIds.remove(row1);
    else m_profileIds.remove(row1+1);

    // Every row between the two shifted by one; id2row has to follow.
    const int from = std::max(std::min(row1, row2), 0);
    const int to = std::min(std::max(row1, row2), static_cast<int>(m_profileIds.size()) - 1);
    for (int i = from; i <= to; ++i) id2row[m_profileIds[i]] = i;
    for (int i = from; i <= to; ++i) refreshProfileId(m_profileIds[i]);
}

int ProfilesTableModel::indexOfProfile(int id) {
    if (id2row.contains(id)) return id2row.value(id);
    return -1;
}

QString ProfilesTableModel::rowLabel(int sourceRow, int displayRow) const {
    if (sourceRow < 0 || sourceRow >= m_profileIds.size()) return {};
    int id = m_profileIds[sourceRow];
    if (Configs::dataManager->settingsRepo->started_id == id) {
        return QStringLiteral("✓");
    }
    return QString::number(displayRow + 1) + QStringLiteral("  ");
}
