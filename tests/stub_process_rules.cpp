#include "include/configs/ProcessRules.h"

// RouteRule.cpp asks which spelling the core reported; the rule tests run without the traffic statistics behind it.
namespace Configs::ProcessRules {
QString KnownSpelling(const QString &, const QString &) {
    return {};
}
} // namespace Configs::ProcessRules
