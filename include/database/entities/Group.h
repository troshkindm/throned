#pragma once
#include <QList>
#include <QMutex>
#include <QString>

#include "include/ui/group/GroupSort.hpp"

namespace Configs
{
    enum class testBy : int {
        latency = 0,
        dlSpeed,
        ulSpeed,
        ipOut
    };

    enum class testShowItems : int {
        all = 0,
        none,
        ipOnly,
        speedOnly
    };

    enum class trafficBy : int {
        total = 0,
        dl,
        ul
    };

    enum class typeBy : int {
        byType = 0,
        bySecurity
    };

    // What the subscription server says about itself, straight from the response
    // headers (or the leading "# key: value" comment lines that mirror them).
    struct SubProvider {
        QString announce;
        // Hash of the announcement the user dismissed; a new text shows again.
        QString announceSeen;
        QString supportUrl;
        QString webPageUrl;
        // Tried once when the subscription URL itself fails to answer.
        QString fallbackUrl;
        // 0 leaves the group on the global auto-update schedule.
        int updateIntervalMinutes = 0;
        bool intervalFromProvider = false;
        // Expiry/quota thresholds already announced for the current billing period.
        int notifiedMask = 0;

        [[nodiscard]] bool hasLinks() const { return !supportUrl.isEmpty() || !webPageUrl.isEmpty(); }
    };

    class Group {
    public:
        QMutex mutex;
        int id = -1;
        bool archive = false;
        bool skip_auto_update = false;
        bool auto_clear_unavailable = false;
        QString name = "";
        QString url = "";
        QString info = "";
        qint64 sub_last_update = 0;
        int front_proxy_id = -1;
        int landing_proxy_id = -1;
        SubProvider provider;

        QList<int> column_width;
        QList<int> calculated_column_width; // memory only, no need to save to db
        QList<int> profiles;
        int scroll_last_profile = -1;
        testBy test_sort_by = testBy::latency;
        trafficBy traffic_sort_by = trafficBy::total;
        typeBy type_sort_by = typeBy::byType;
        testShowItems test_items_to_show = testShowItems::all;
        // Memory only. Pairs of (profileID, row as displayed).
        QList<std::pair<int, int>> selectedProfilesIdIdxPairs;

        Group() = default;

        void clearCalculatedColumnWidth();

        [[nodiscard]] QList<int> Profiles() const;

        bool SortProfiles(GroupSortAction method);

        bool RemoveProfile(int ID);

        bool RemoveProfileBatch(const QList<int>& IDs);

        bool AddProfile(int ID);

        bool AddProfileBatch(const QList<int>& IDs);

        bool SwapProfiles(int idx1, int idx2);

        bool EmplaceProfile(int idx, int newIdx);

        [[nodiscard]] bool HasProfile(int ID) const;
    };
}// namespace Configs
