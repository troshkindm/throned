#pragma once

#include "Database.h"
#include "include/database/entities/Group.h"
#include <memory>
#include <mutex>
#include <map>
#include <QHash>
#include <QJsonObject>
#include <QList>

namespace Configs {
class GroupsRepo {
private:
    Database& db;
    mutable std::mutex mutex;
    mutable std::map<int, std::shared_ptr<Group>> memMap;

    QJsonObject groupToJson(const Group* group) const;

    std::shared_ptr<Group> groupFromJson(const QJsonObject& json) const;

    void saveToDatabase(const Group* group, int id) const;

    std::shared_ptr<Group> loadFromDatabase(int id) const;

    void createTables() const;

    bool groupsColumnExists(const char* columnName) const;

    int NewGroupID() const;

public:
    explicit GroupsRepo(Database& database);

    // Not saved yet: id stays -1 until AddGroup.
    [[nodiscard]] static std::shared_ptr<Group> NewGroup();

    bool AddGroup(std::shared_ptr<Group>& group);

    std::shared_ptr<Group> GetGroup(int id) const;

    // Resolves SettingsRepo::current_group.
    std::shared_ptr<Group> CurrentGroup() const;

    void DeleteGroup(int id);

    QList<int> GetAllGroupIds() const;

    QList<int> GetGroupsTabOrder() const;

    void SetGroupsTabOrder(const QList<int>& order);

    // No-op unless id >= 0.
    bool Save(const std::shared_ptr<Group>& group);
};

class Profile;

// Keep sorted groups sorted from a background queue, one sort at a time; callable from any thread.
// The main window gets GroupOrderChanged for each group whose order moved.

// For changes that can move any row.
void ResortLater(int gid);

// For a few changed rows; only groups whose sort reads `input` are queued.
void RepositionLater(const QHash<int, QList<int>>& changedByGroup, SortInput input);
void RepositionLater(const QList<std::shared_ptr<Profile>>& profiles, SortInput input);
} // namespace Configs
