#include "include/configs/outbounds/autoselector.h"

#include "include/database/DatabaseManager.h"
#include "include/database/GroupsRepo.h"
#include "include/database/entities/Group.h"

namespace Configs {
QString autoSelector::DisplayAddress() {
    if (gid < 0) return QObject::tr("no group");
    auto group = dataManager->groupsRepo->GetGroup(gid);
    if (group == nullptr) return QObject::tr("missing group");
    return group->name;
}
} // namespace Configs
