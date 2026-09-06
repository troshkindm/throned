#pragma once
#include <QJsonObject>
#include <QStringList>

namespace Configs {
// Every tag a rule points at has to be declared: a dns rule naming a server
// that no longer gets built resolves nothing, and the failure surfaces far
// from its cause as a plain timeout. Returns one message per dangling tag.
QStringList FindDanglingReferences(const QJsonObject &config);
} // namespace Configs
