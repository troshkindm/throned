#include "include/control/ControlClient.h"

#include "include/control/ThronedControl.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocalSocket>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace ThronedControl {
namespace {

// Throned is a GUI-subsystem binary on Windows, so it owns no console. When
// it was started from one, borrow the parent's so the JSON reply lands where
// the caller is looking instead of nowhere.
void AttachToParentConsole() {
#ifdef Q_OS_WIN
    // Only borrow a console when the caller left us without a usable stdout.
    // If they redirected it into a pipe or a file, that handle is already
    // valid and attaching would send the answer to a terminal instead.
    const HANDLE existing = GetStdHandle(STD_OUTPUT_HANDLE);
    if (existing == nullptr || existing == INVALID_HANDLE_VALUE) AttachConsole(ATTACH_PARENT_PROCESS);
#endif
}

void PrintLine(const QString &text) {
    const QByteArray bytes = text.toUtf8() + '\n';
#ifdef Q_OS_WIN
    // The CRT's stdout is not wired up in a GUI-subsystem binary, so write to
    // the OS handle, which works for a console and a redirection alike.
    const HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out != nullptr && out != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(out, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
        return;
    }
#endif
    fwrite(bytes.constData(), 1, bytes.size(), stdout);
    fflush(stdout);
}

// Turns a typed-out command line into the JSON the running instance expects.
// Returns false when the words do not name a command, so the caller can say
// so instead of sending something the instance would reject.
bool ParseHumanCommand(const QStringList &words, QJsonObject *request, QString *error) {
    if (words.isEmpty()) return false;
    const QString head = words.first();
    const QStringList rest = words.mid(1);

    const auto needsArgument = [&](const QString &what) {
        *error = QStringLiteral("'%1' needs %2").arg(head, what);
        return false;
    };

    if (head == "status") {
        *request = {{"cmd", "status"}};
        return true;
    }
    if (head == "servers" || head == "profiles") {
        *request = {{"cmd", "profiles.list"}};
        return true;
    }
    if (head == "stop") {
        *request = {{"cmd", "profile.stop"}};
        return true;
    }
    if (head == "start") {
        if (rest.isEmpty()) return needsArgument(QStringLiteral("a server id, as shown by 'servers'"));
        *request = {{"cmd", "profile.start"}, {"id", rest.first().toInt()}};
        return true;
    }
    if (head == "routes") {
        *request = {{"cmd", "routing.list"}};
        return true;
    }
    if (head == "route") {
        if (rest.isEmpty()) {
            *request = {{"cmd", "routing.get"}};
            return true;
        }
        const QString verb = rest.first();
        const QStringList args = rest.mid(1);
        if (verb == "use") {
            if (args.isEmpty()) {
                *error = QStringLiteral("'route use' needs a profile id");
                return false;
            }
            *request = {{"cmd", "routing.select"}, {"id", args.first().toInt()}};
            return true;
        }
        if (verb == "default") {
            if (args.isEmpty()) {
                *error = QStringLiteral("'route default' needs direct, proxy, block or warp");
                return false;
            }
            *request = {{"cmd", "routing.set_default"}, {"outbound", args.first()}};
            return true;
        }
        if (verb == "rules") {
            // "on"/"off" switches the profile's rules; anything else asks to
            // see them, optionally for a profile other than the active one.
            if (!args.isEmpty() && (args.first() == "on" || args.first() == "off")) {
                *request = {{"cmd", "routing.set_rules_enabled"}, {"enabled", args.first() == "on"}};
                return true;
            }
            QJsonObject built{{"cmd", "routing.rules"}};
            if (!args.isEmpty()) built["id"] = args.first().toInt();
            *request = built;
            return true;
        }
        if (verb == "export") {
            QJsonObject built{{"cmd", "routing.export"}};
            if (!args.isEmpty()) built["id"] = args.first().toInt();
            *request = built;
            return true;
        }
        if (verb == "import") {
            if (args.isEmpty()) {
                *error = QStringLiteral("'route import' needs a file, a route link, or - to read stdin");
                return false;
            }
            QString payload = args.first();
            if (payload == QStringLiteral("-")) {
                QFile input;
                if (!input.open(stdin, QIODevice::ReadOnly)) {
                    *error = QStringLiteral("could not read stdin");
                    return false;
                }
                payload = QString::fromUtf8(input.readAll());
            } else if (QFileInfo::exists(payload)) {
                QFile file(payload);
                if (!file.open(QIODevice::ReadOnly)) {
                    *error = QStringLiteral("could not read %1").arg(payload);
                    return false;
                }
                payload = QString::fromUtf8(file.readAll());
            }
            *request = {{"cmd", "routing.import"}, {"input", payload}};
            return true;
        }
        if (verb == "app" || verb == "apps") {
            QString via = QStringLiteral("proxy");
            QJsonArray entries;
            bool removing = false;
            for (int i = 0; i < args.size(); ++i) {
                if (args.at(i) == "--via" || args.at(i) == "-v") {
                    if (i + 1 >= args.size()) {
                        *error = QStringLiteral("--via needs proxy, direct or block");
                        return false;
                    }
                    via = args.at(++i);
                    continue;
                }
                if (entries.isEmpty() && (args.at(i) == "add" || args.at(i) == "remove")) {
                    removing = args.at(i) == "remove";
                    continue;
                }
                entries.append(args.at(i));
            }
            if (entries.isEmpty()) {
                *error = QStringLiteral("'route app' needs add or remove and an executable");
                return false;
            }
            *request = {
                {"cmd", removing ? "routing.remove_apps" : "routing.add_apps"},
                {"action", via},
                {"apps", entries},
            };
            return true;
        }
        if (verb == "add" || verb == "remove") {
            QString via = QStringLiteral("proxy");
            QJsonArray entries;
            for (int i = 0; i < args.size(); ++i) {
                if (args.at(i) == "--via" || args.at(i) == "-v") {
                    if (i + 1 >= args.size()) {
                        *error = QStringLiteral("--via needs proxy, direct or block");
                        return false;
                    }
                    via = args.at(++i);
                    continue;
                }
                entries.append(args.at(i));
            }
            if (entries.isEmpty()) {
                *error = QStringLiteral("'route %1' needs at least one domain").arg(verb);
                return false;
            }
            *request = {
                {"cmd", verb == "add" ? "routing.add_domains" : "routing.remove_domains"},
                {"action", via},
                {"domains", entries},
            };
            return true;
        }
        *error = QStringLiteral("unknown 'route' subcommand '%1'").arg(verb);
        return false;
    }
    *error = QStringLiteral("unknown command '%1'").arg(head);
    return false;
}

QString FormatValue(const QJsonValue &value) {
    if (value.isBool()) return value.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
    if (value.isDouble()) return QString::number(value.toDouble());
    if (value.isArray()) {
        QStringList parts;
        for (const QJsonValue &item: value.toArray()) parts << item.toString();
        return parts.isEmpty() ? QStringLiteral("(none)") : parts.join(QStringLiteral(", "));
    }
    return value.toString();
}

// Renders a reply as lines a person can read. Anything without a tailored
// shape falls back to "key: value", so a new command still prints sensibly.
QString FormatReply(const QString &cmd, const QJsonObject &data) {
    QStringList lines;
    const auto profileLine = [](const QJsonObject &p) {
        QString line = QStringLiteral("  [%1] %2").arg(p.value("id").toInt()).arg(p.value("name").toString());
        if (p.value("active").toBool()) line += QStringLiteral("  (active)");
        line += QStringLiteral("\n        default: %1, rules: %2")
                    .arg(p.value("default_outbound").toString(),
                         p.value("rules_enabled").toBool() ? QStringLiteral("on") : QStringLiteral("off"));
        if (p.value("raw").toBool()) line += QStringLiteral(", raw");
        return line;
    };

    if (cmd == "status") {
        lines << QStringLiteral("proxy:      %1").arg(data.value("running").toBool() ? QStringLiteral("running - %1").arg(data.value("running_profile_name").toString()) : QStringLiteral("stopped"));
        lines << QStringLiteral("inbound:    mixed on port %1").arg(data.value("mixed_port").toInt());
        lines << QStringLiteral("tun:        %1").arg(FormatValue(data.value("tun_enabled")));
        lines << QStringLiteral("system:     %1").arg(FormatValue(data.value("system_proxy")));
        const QJsonObject routing = data.value("routing").toObject();
        if (!routing.isEmpty()) {
            lines << QStringLiteral("routing:    %1 - default %2, rules %3")
                         .arg(routing.value("name").toString(), routing.value("default_outbound").toString(),
                              routing.value("rules_enabled").toBool() ? QStringLiteral("on") : QStringLiteral("off"));
        }
        return lines.join('\n');
    }
    if (cmd == "routing.list") {
        lines << QStringLiteral("routing profiles:");
        for (const QJsonValue &value: data.value("profiles").toArray()) lines << profileLine(value.toObject());
        return lines.join('\n');
    }
    if (cmd == "routing.get") {
        lines << profileLine(data);
        lines << QStringLiteral("  through proxy: %1").arg(FormatValue(data.value("proxy_domains")));
        lines << QStringLiteral("  direct:        %1").arg(FormatValue(data.value("direct_domains")));
        lines << QStringLiteral("  blocked:       %1").arg(FormatValue(data.value("blocked_domains")));
        return lines.join('\n');
    }
    if (cmd == "profiles.list") {
        lines << QStringLiteral("servers:");
        for (const QJsonValue &value: data.value("profiles").toArray()) {
            const QJsonObject p = value.toObject();
            lines << QStringLiteral("  [%1] %2  (%3)").arg(p.value("id").toInt()).arg(p.value("name").toString(), p.value("type").toString());
        }
        return lines.join('\n');
    }
    if (data.isEmpty()) return QStringLiteral("done");
    for (auto it = data.begin(); it != data.end(); ++it)
        lines << QStringLiteral("%1: %2").arg(it.key(), FormatValue(it.value()));
    return lines.join('\n');
}

QString HumanHelpText() {
    return QStringLiteral(R"(Throned command line

  throned --cli <command>

Commands

  status                       what is running, where, and through which route
  servers                      list proxy profiles with their ids
  start <id>                   start a proxy profile
  stop                         stop the running profile

  routes                       list routing profiles
  route                        show the active routing profile and its lists
  route use <id>               make a routing profile active
  route default <where>        traffic that matched no rule: direct, proxy,
                               block or warp
  route rules <on|off>         apply the profile's own rules, or send everything
                               to the default above
  route rules [id]              show the ordered rule list; first match wins
  route add <domain>...        route domains, e.g.
                               route add example.com --via proxy
  route remove <domain>...     drop them again
  route app add <exe>...        route an application, e.g.
                               route app add discord.exe --via proxy
  route app remove <exe>...     stop routing it

  route export [id] --json      the whole profile as a document
  route import <file|link|->    replace what a profile routes

  Add --via proxy|direct|block to choose the list; proxy is the default.
  A bare domain covers its subdomains. Prefixes like ruleset: or processName:
  are passed through untouched.

Options

  --json                       print the raw JSON reply instead of text
  --cli '{"cmd":"..."}'        send a raw command; see --cli '{"cmd":"help"}'
                               for the full machine-facing reference

Routing changes are saved at once and restart the running profile, which
briefly interrupts traffic.
)");
}

} // namespace

int RunClient(const QStringList &serverNames, QStringList words) {
    AttachToParentConsole();

    const bool rawJsonOut = words.removeAll(QStringLiteral("--json")) > 0;
    if (words.isEmpty()) words << QStringLiteral("help");

    QJsonObject request;
    // One argument that looks like an object is a raw command; anything else
    // is read as words a person typed.
    const bool rawJsonIn = words.size() == 1 && words.first().trimmed().startsWith('{');
    if (rawJsonIn) {
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(words.first().toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject()) {
            PrintLine(QStringLiteral(R"({"ok":false,"error":"invalid JSON: %1"})").arg(parseError.errorString()));
            return 2;
        }
        request = parsed.object();
    } else if (words.first() == QStringLiteral("help") || words.first() == QStringLiteral("--help")) {
        // Help is answered locally so the reference is readable without a
        // running instance, which is how anyone discovers the rest.
        PrintLine(rawJsonOut ? ThronedControl::HelpText() : HumanHelpText());
        return 0;
    } else {
        QString error;
        if (!ParseHumanCommand(words, &request, &error)) {
            PrintLine(error + QStringLiteral("\nrun: throned --cli help"));
            return 2;
        }
    }

    if (request.value("cmd").toString() == QStringLiteral("help")) {
        PrintLine(ThronedControl::HelpText());
        return 0;
    }
    // Raw JSON in means raw JSON out, so a scripted caller always gets back
    // the same shape it sent, whether or not it passed --json.
    const bool machineOutput = rawJsonOut || rawJsonIn;
    const auto report = [machineOutput](const QString &json, const QString &text) {
        PrintLine(machineOutput ? json : text);
    };

    // An instance may live under the install folder or under appdata, and
    // which one it picked depends on whether the install folder turned out
    // writable. Trying both beats re-deriving that decision, which would
    // mean creating directories a read-only query has no business creating.
    QLocalSocket socket;
    bool connected = false;
    for (const QString &candidate: serverNames) {
        if (candidate.isEmpty()) continue;
        socket.connectToServer(candidate);
        if (socket.waitForConnected(1000)) {
            connected = true;
            break;
        }
        socket.abort();
    }
    if (!connected) {
        report(QStringLiteral(R"({"ok":false,"error":"Throned is not running in this directory"})"),
               QStringLiteral("throned is not running here; start it first, or run this from its folder"));
        return 3;
    }
    socket.write(QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n');
    socket.flush();
    if (!socket.waitForReadyRead(5000)) {
        report(QStringLiteral(R"({"ok":false,"error":"no answer from Throned"})"),
               QStringLiteral("throned did not answer; it may be an older build without the control interface"));
        return 4;
    }
    QByteArray reply = socket.readAll();
    while (!reply.contains('\n') && socket.waitForReadyRead(1000)) reply += socket.readAll();
    socket.disconnectFromServer();

    const QString line = QString::fromUtf8(reply).trimmed();
    const QJsonObject answer = QJsonDocument::fromJson(line.toUtf8()).object();
    const bool ok = answer.value("ok").toBool();
    if (machineOutput) {
        PrintLine(line);
    } else if (ok) {
        PrintLine(FormatReply(request.value("cmd").toString(), answer.value("data").toObject()));
    } else {
        PrintLine(QStringLiteral("error: %1").arg(answer.value("error").toString()));
    }
    return ok ? 0 : 1;
}

} // namespace ThronedControl
