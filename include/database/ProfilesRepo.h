#pragma once

#include "Database.h"
#include "include/database/entities/Profile.h"
#include <3rdparty/SQLiteCpp/include/SQLiteCpp.h>
#include <memory>
#include <mutex>
#include <map>
#include <QString>
#include <QJsonObject>

namespace Configs {
    class ProfilesRepo {
    private:
        Database& db;
        mutable std::mutex mutex;
        mutable std::map<int, std::weak_ptr<Profile>> identityMap;

        std::shared_ptr<Profile> profileFromJson(const QJsonObject& json) const;
        
        void saveToDatabase(const Profile* profile, int id) const;

        // Must stay column-compatible with saveToDatabase.
        ProfileInsertRow profileToInsertRow(const Profile* profile, int id, int gid) const;

        std::shared_ptr<Profile> loadFromDatabase(int id) const;
        
        // Must stay column-compatible with loadFromDatabase.
        std::shared_ptr<Profile> profileFromRow(SQLite::Statement& stmt) const;

        // Does not touch the identity map.
        std::map<int, std::shared_ptr<Profile>> loadProfilesByIdsChunk(const QList<int>& ids) const;
        
        void createTables() const;

        bool profilesColumnExists(const char* columnName) const;

        int NewProfileID() const;
        // Contiguous block starting at the returned id; atomic in the DB, so no lock is required.
        int NewProfileIDRange(int n) const;

    public:
        explicit ProfilesRepo(Database& database);
        
        // Not saved yet: id stays -1 until AddProfile.
        [[nodiscard]] static std::shared_ptr<Profile> NewProfile(const QString &type);
        
        bool AddProfile(std::shared_ptr<Profile>& profile, int gid = -1);
        
        bool AddProfileBatch(QList<std::shared_ptr<Profile>>& profiles, int gid = -1);
        
        std::shared_ptr<Profile> GetProfile(int id) const;

        QList<std::shared_ptr<Profile>> GetProfileBatch(QList<int> ids);

        QList<std::pair<int, QString> > GetProfileIDNameMappedBatch(QList<int> ids);

        std::shared_ptr<Profile> GetProfileByName(const QString &name);

        QList<std::pair<int, QString> > GetAllProfileIDNameMapped();

        QStringList GetAllProfileNames();
        
        bool BatchDeleteProfiles(QList<int>& ids, bool stopRunningProfile = false);
        
        QList<int> GetAllProfileIds() const;

        // Starred profiles across every group, in group tab order then row order.
        QList<int> GetFavoriteProfileIds() const;

        QList<int> GetProfileIdsByType(const QString& type) const;
        
        // No-op unless id >= 0.
        bool Save(const std::shared_ptr<Profile>& profile);

        // Blind UPDATE of the traffic columns: no existence check.
        bool SaveTraffic(const std::shared_ptr<Profile>& profile);

        void SaveTrafficBatch(const QList<std::shared_ptr<Profile>>& profiles);

        // Runs on a new thread; skips null profiles and id < 0.
        void SaveBatch(const QList<std::shared_ptr<Profile>>& profiles);
    };
}
