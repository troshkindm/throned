#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QString>

#include <functional>

// The one place every externally driven operation lives. The CLI speaks to the
// running instance through it, and the UI installs the few hooks it cannot
// provide from the config layer alone (starting a profile, restarting the core
// after a routing change).
//
// Keeping the surface here rather than spreading lambdas over MainWindow means
// the operation set is explicit and reviewable, and it is the natural contract
// to port if the backend ever moves out of C++.
namespace ThronedControl {

struct Hooks {
    std::function<void(int id)> startProfile;
    std::function<void()> stopProfile;
    // Persist-and-apply: restart the running profile so a routing edit takes
    // effect, and refresh whatever the window shows about it.
    std::function<void()> applyRoutingChange;
    std::function<int()> runningProfileId;
    std::function<void(bool)> setTun;
    std::function<void(bool)> setSystemProxy;
    std::function<void()> updateSubscriptions;
    // The last log lines the window is showing, newest last.
    std::function<QStringList(int)> recentLogs;
    // True when the process already holds the rights TUN needs. Turning TUN on
    // without them restarts the app behind a UAC prompt, which would leave a
    // control client waiting for an answer that never comes.
    std::function<bool()> isElevated;
};

inline Hooks hooks;

// Runs one command. Must be called on the UI thread. Never throws: failures come
// back as {"ok":false,"error":"..."} so a caller can always parse the answer.
QJsonObject Execute(const QJsonObject &request);

// The command reference, as plain text, generated from the same table the
// commands are dispatched from so it cannot drift out of date.
QString HelpText();

// The same table as JSON: every command, its arguments, their types and which
// values are accepted, plus the shape of a routing rule. Meant for a program
// that wants to discover the surface instead of parsing prose.
QJsonObject Schema();

// What a rule inside a routing document may contain: every matcher field, its
// type and its accepted values. Read out of the rule model itself, so it
// describes what the app really parses rather than what someone wrote down.
QJsonObject RuleSchema();

} // namespace ThronedControl
