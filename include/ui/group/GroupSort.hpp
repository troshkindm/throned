#pragma once

namespace GroupSortMethod {
enum GroupSortMethod {
    Raw,
    ByType,
    ByAddress,
    ByName,
    ByTestResult,
    ById,
    ByTraffic,
    BySecurity,
};
}

struct GroupSortAction {
    GroupSortMethod::GroupSortMethod method = GroupSortMethod::Raw;
    bool descending = false;
};
