#pragma once

#include "Database.h"
#include "include/database/SettingsRepo.h"
#include <string>
#include <memory>

namespace Configs {
class RoutesRepo;
class GroupsRepo;
class ProfilesRepo;
class OtpProfilesRepo;
class IpListsRepo;
class IpScansRepo;
class TrafficStatsRepo;
class MarkersRepo;

void initDB(const std::string& dbPath);

class DatabaseManager {
private:
    Database db;
    Database statsDb;
    // Same file as db; transactions are per connection, so scanner writes never interleave with the other repos' BEGIN.
    Database scanDb;

    static void createEntityIdsTable(Database& db);
    static bool entityIdsColumnExists(Database& db, const char* columnName);
    static std::string deriveStatsDbPath(const std::string& dbPath);
    // Quarantines a stats file the previous session flagged, or one that would open read-only or as garbage.
    static std::string prepareStatsDb(const std::string& path);
    static QString statsDbUnusableReason(const std::string& path);
    static void quarantineDbFile(const std::string& path);
    void initializeRepos();
    void applyMigrations();

public:
    std::unique_ptr<ProfilesRepo> profilesRepo;
    std::unique_ptr<GroupsRepo> groupsRepo;
    std::unique_ptr<RoutesRepo> routesRepo;
    std::unique_ptr<OtpProfilesRepo> otpProfilesRepo;
    std::unique_ptr<SettingsRepo> settingsRepo;
    std::unique_ptr<TrafficStatsRepo> trafficStatsRepo;
    std::unique_ptr<MarkersRepo> markersRepo;
    std::unique_ptr<IpListsRepo> ipListsRepo;
    std::unique_ptr<IpScansRepo> ipScansRepo;

    explicit DatabaseManager(const std::string& dbPath);
    ~DatabaseManager() = default;

    // Call once, after the UI is up.
    void RunDeferredMaintenance();

    // So the statistics screen can tell the user how much history is on disk.
    [[nodiscard]] QString StatsDatabasePath() const { return QString::fromStdString(statsDb.Path()); }

    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    Database& getDatabase() { return db; }
    const Database& getDatabase() const { return db; }
};

inline DatabaseManager* dataManager;
} // namespace Configs
