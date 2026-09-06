#pragma once

#include <QString>
#include <QList>
#include <QMutex>

#include <string>

#include "include/database/entities/Profile.h"
#include "include/configs/generate.h"

namespace Stats {
struct TrafficLooperEntry {
    QString tag;
    double downlink_rate = 0;
    double uplink_rate = 0;
};

inline QString Rate(double bytesPerSecond) {
    static const QStringList units{QStringLiteral("B"), QStringLiteral("KiB"), QStringLiteral("MiB"),
                                   QStringLiteral("GiB"), QStringLiteral("TiB")};
    double value = bytesPerSecond;
    int unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    return QString::number(value, 'f', 2) + QChar(' ') + units.at(unit);
}

inline QString DisplaySpeed(const std::shared_ptr<TrafficLooperEntry> &entry) {
    constexpr QChar figureSpace(0x2007);
    return UNICODE_LRO + QString("↑ %1 ↓ %2")
                             .arg(Rate(entry->uplink_rate).leftJustified(11, figureSpace),
                                  Rate(entry->downlink_rate));
}

struct TrafficLooperGroup {
    QString watchTag;
    // watchTag as the core's stats-map key
    std::string watchTagKey;
    QList<std::shared_ptr<Configs::Profile>> profiles;
    long long last_update = 0;
    double uplink_rate = 0;
    double downlink_rate = 0;
    // Set when the group credited a non-zero delta since the last persist.
    bool dirty = false;
};

class TrafficLooper {
public:
    bool loop_enabled = false;
    bool looping = false;
    QMutex loop_mutex;

    std::shared_ptr<TrafficLooperEntry> proxy;
    std::shared_ptr<TrafficLooperEntry> direct;

    void UpdateAll();

    void Loop();

    // Runs synchronously on the caller's thread, in one batched transaction.
    void PersistTraffic();

    void SetChainGroups(const QList<Configs::TrafficChainGroup> &configGroups);

private:
    QList<TrafficLooperGroup> groups;
    long long direct_last_update = 0;
};

extern TrafficLooper *trafficLooper;
} // namespace Stats
