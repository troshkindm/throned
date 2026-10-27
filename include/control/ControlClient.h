#pragma once

#include <QString>
#include <QStringList>

namespace ThronedControl {

// `throned --cli ...`: sends one command to the instance listening on the first
// name that answers, prints the reply and returns the process exit code.
int RunClient(const QStringList &serverNames, QStringList words);

} // namespace ThronedControl
