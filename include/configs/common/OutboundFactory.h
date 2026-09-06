#pragma once
#include <QString>

namespace Configs {
class outbound;

// Never null; an unknown type yields a base outbound flagged invalid. Caller takes ownership.
outbound* NewOutboundByType(const QString& type);
} // namespace Configs
