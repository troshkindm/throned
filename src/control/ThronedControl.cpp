#include "include/control/ThronedControl.h"

#include "include/global/Configs.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/entities/Group.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/RoutesRepo.h"
#include "include/database/SettingsRepo.h"
#include "include/database/entities/Profile.h"
#include "include/database/entities/RouteProfile.h"
#include "include/database/entities/RouteRule.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace ThronedControl {
namespace {

QJsonObject fail(const QString &error) {
    return QJsonObject{{"ok", false}, {"error", error}};
}

QJsonObject ok(const QJsonObject &data = {}) {
    return QJsonObject{{"ok", true}, {"data", data}};
}

QString outboundName(int outboundID) {
    switch (outboundID) {
        case Configs::directID:
            return QStringLiteral("direct");
        case Configs::proxyID:
            return QStringLiteral("proxy");
        case Configs::blockID:
            return QStringLiteral("block");
        case Configs::warpBypassID:
            return QStringLiteral("warp");
        default:
            return QString::number(outboundID);
    }
}

// Returns false for anything that is not one of the four named outbounds, so a
// typo becomes an error instead of silently routing somewhere unintended.
bool outboundFromName(const QString &name, int *out) {
    if (name == QStringLiteral("direct"))
        *out = Configs::directID;
    else if (name == QStringLiteral("proxy"))
        *out = Configs::proxyID;
    else if (name == QStringLiteral("block"))
        *out = Configs::blockID;
    else if (name == QStringLiteral("warp"))
        *out = Configs::warpBypassID;
    else
        return false;
    return true;
}

bool actionFromName(const QString &name, Configs::simpleAction *out) {
    if (name == QStringLiteral("proxy"))
        *out = Configs::proxy;
    else if (name == QStringLiteral("direct"))
        *out = Configs::bypass;
    else if (name == QStringLiteral("block"))
        *out = Configs::block;
    else
        return false;
    return true;
}

std::shared_ptr<Configs::RouteProfile> activeProfile() {
    return Configs::dataManager->routesRepo->GetRouteProfile(
        Configs::dataManager->settingsRepo->current_route_id);
}

// Commands that read or rewrite a whole profile accept an explicit id, so a
// caller can work on one it is not currently routing through.
std::shared_ptr<Configs::RouteProfile> targetProfile(const QJsonObject &request) {
    const QJsonValue id = request.value(QStringLiteral("id"));
    if (id.isDouble()) return Configs::dataManager->routesRepo->GetRouteProfile(id.toInt());
    return activeProfile();
}

QJsonObject describeProfile(const std::shared_ptr<Configs::RouteProfile> &profile) {
    return QJsonObject{
        {"id", profile->id},
        {"name", profile->name},
        {"default_outbound", outboundName(profile->defaultOutboundID)},
        {"rules_enabled", profile->applyProfileRules},
        {"raw", profile->isRaw},
        {"remote", profile->isRemote},
    };
}

// Simple rules are stored with a type prefix. A caller that just has a hostname
// should not have to know that, so a bare entry becomes a suffix match, which is
// what "route this site" nearly always means (it covers subdomains too).
QString withRulePrefix(const QString &entry) {
    static const QStringList prefixes{
        QStringLiteral("domain:"),
        QStringLiteral("suffix:"),
        QStringLiteral("keyword:"),
        QStringLiteral("regex:"),
        QStringLiteral("ruleset:"),
        QStringLiteral("ip:"),
        QStringLiteral("processName:"),
        QStringLiteral("processPath:"),
    };
    for (const QString &prefix: prefixes)
        if (entry.startsWith(prefix)) return entry;
    return QStringLiteral("suffix:") + entry;
}

// A caller naming an application gives either "discord.exe" or a full path.
// The first matches wherever the program runs from, the second pins one binary.
QString asProcessEntry(const QString &app) {
    if (app.startsWith(QStringLiteral("processName:")) || app.startsWith(QStringLiteral("processPath:")))
        return app;
    const bool looksLikePath = app.contains('/') || app.contains('\\');
    return (looksLikePath ? QStringLiteral("processPath:") : QStringLiteral("processName:")) + app;
}

QStringList requestedStrings(const QJsonObject &request, const QString &key) {
    QStringList values;
    for (const QJsonValue &value: request.value(key).toArray()) {
        const QString text = value.toString().trimmed();
        if (!text.isEmpty()) values << text;
    }
    return values;
}

// One description of every command, used to dispatch documentation and to build
// the JSON schema. Keeping it beside the handlers is what stops the reference
// from quietly describing a surface that no longer exists.
struct Argument {
    const char *name;
    const char *type; // "int", "bool", "string", "string[]"
    bool required;
    const char *accepts; // allowed values, empty when free-form
    const char *summary;
};

struct Command {
    const char *name;
    const char *summary;
    QList<Argument> arguments;
    const char *returns;
};

const QList<Command> &commandTable() {
    static const QList<Command> table{
        {"help", "This reference as text.", {}, "text"},
        {"schema", "This reference as JSON.", {}, "commands"},
        {"status", "What is running, where, and through which route.", {}, "running, running_profile_id, running_profile_name, mixed_port, tun_enabled, system_proxy, routing"},
        {"profiles.list", "Every proxy profile.", {}, "profiles[]: id, name, type, group_id"},
        {"profile.start", "Start a proxy profile.", {{"id", "int", true, "", "profile id from profiles.list"}}, "started"},
        {"profile.stop", "Stop the running profile.", {}, ""},
        {"tun.set", "Turn TUN mode on or off.", {{"enabled", "bool", true, "true, false", ""}},
         "tun_enabled. Refuses to turn TUN on when the app is not elevated, because that "
         "path restarts it behind a UAC prompt."},
        {"system_proxy.set", "Turn the system proxy on or off.", {{"enabled", "bool", true, "true, false", ""}}, "system_proxy"},
        {"routing.list", "Every routing profile; \"active\" marks the selected one.", {}, "profiles[]: id, name, default_outbound, rules_enabled, raw, remote, active"},
        {"routing.get", "The active routing profile and its domain lists.", {}, "id, name, default_outbound, rules_enabled, proxy_domains, direct_domains, blocked_domains"},
        {"routing.select", "Make a routing profile active.", {{"id", "int", true, "", "profile id from routing.list"}}, "active"},
        {"routing.set_default", "Where traffic goes when no rule matched.", {{"outbound", "string", true, "direct, proxy, block, warp", ""}}, "default_outbound"},
        {"routing.set_rules_enabled",
         "Apply the profile's own rules, or send everything to the default outbound. "
         "Throned's internal rules and the local-proxy quick option keep applying either way.",
         {{"enabled", "bool", true, "true, false", ""}},
         "rules_enabled"},
        {"routing.add_domains", "Add entries to one of the three routing lists.", {{"action", "string", true, "proxy, direct, block", "which list to edit"}, {"domains", "string[]", true, "",
                                                                                                                                                              "a bare host becomes a suffix match covering subdomains; the typed prefixes "
                                                                                                                                                              "domain:, suffix:, keyword:, regex:, ruleset:, ip:, processName: and processPath: "
                                                                                                                                                              "are kept as given"}},
         "action, domains (the resulting list)"},
        {"routing.paste", "Replace one routing list wholesale with a free-form list.", {{"action", "string", true, "proxy, direct, block", "which list to replace"}, {"lines", "string[]", true, "",
                                                                                                                                                                      "one entry per line in any accepted spelling: the typed prefixes, the sing-box "
                                                                                                                                                                      "ones (domain_suffix, process_name, rule_set, ip_cidr), or a bare value whose "
                                                                                                                                                                      "kind is unambiguous. Comments and list punctuation are ignored"}},
         "action, domains (the resulting list), rejected (lines that could not be placed)"},
        {"routing.remove_domains", "Remove entries from one of the three routing lists.", {{"action", "string", true, "proxy, direct, block", "which list to edit"}, {"domains", "string[]", true, "", "accepts the bare form or the stored one"}}, "action, domains (the resulting list)"},
        {"logs", "Recent log lines, oldest first.", {{"lines", "int", false, "", "how many to return, up to 2000; 200 by default"}, {"contains", "string", false, "", "keep only lines containing this text"}}, "lines[]"},
        {"groups.list", "Server groups and subscriptions.", {}, "groups[]: id, name, url, subscription, archive, profiles (count)"},
        {"subscriptions.update", "Refresh every subscription in the background.", {}, "started. Watch groups.list or logs for the outcome."},
        {"routing.create", "Add a routing profile, optionally as a copy of an existing one.", {{"name", "string", true, "", ""}, {"copy_of", "int", false, "", "routing profile id to copy rules and defaults from"}, {"select", "bool", false, "true, false", "also make it active"}}, "id, name, default_outbound, rules_enabled, raw, remote"},
        {"routing.delete", "Remove a routing profile.", {{"id", "int", true, "", "not the active one, and not the last one left"}}, "deleted"},
        {"routing.apply", "Restart the running profile so pending routing edits take effect.", {}, "applied"},
        // Documented once here rather than on each command: every routing edit
        // takes it, and repeating it fifteen times would bury the rest.
        {"routing.export", "The whole profile as a lossless document: every rule with every field, in order.", {{"id", "int", false, "", "routing profile id; the active one by default"}}, "profile - feed it back to routing.import unchanged, or edited"},
        {"routing.rules", "The ordered rule list as the advanced editor shows it. The first rule that matches wins.", {{"id", "int", false, "", "routing profile id; the active one by default"}}, "id, name, default_outbound, rules[] (or route for a raw profile)"},
        {"routing.import",
         "Replace everything a profile routes. The profile keeps its id, so whatever "
         "points at it keeps working.",
         {{"profile", "object", false, "", "a document from routing.export, edited as you like"},
          {"input", "string", false, "", "the same thing as a throne://route link or base64"},
          {"id", "int", false, "", "routing profile id; the active one by default"},
          {"rename", "bool", false, "true, false", "also take the name from the document; off by default"}},
         "id, name, rules (count), default_outbound, warnings when anything was adjusted"},
        {"routing.add_apps", "Route applications by their process.", {{"action", "string", true, "proxy, direct, block", "which list to edit"}, {"apps", "string[]", true, "",
                                                                                                                                                 "an executable name such as discord.exe, or a full path; a path is matched "
                                                                                                                                                 "exactly, a bare name matches wherever the program runs from"}},
         "action, apps (the resulting process entries)"},
        {"routing.remove_apps", "Stop routing applications by their process.", {{"action", "string", true, "proxy, direct, block", "which list to edit"}, {"apps", "string[]", true, "", "accepts the bare name, a path, or the stored form"}}, "action, apps (the resulting process entries)"},
    };
    return table;
}

// Every routing change is saved at once, but restarting the core to make it
// effective is optional: a caller working through a batch of edits does not want
// the traffic interrupted after each one.
QJsonObject saveAndApply(const std::shared_ptr<Configs::RouteProfile> &profile, QJsonObject data,
                         const QJsonObject &request) {
    Configs::dataManager->routesRepo->Save(profile);
    const bool apply = !request.value(QStringLiteral("apply")).isBool() || request.value(QStringLiteral("apply")).toBool();
    if (apply && hooks.applyRoutingChange) hooks.applyRoutingChange();
    data["applied"] = apply;
    return ok(data);
}

} // namespace

QJsonObject Execute(const QJsonObject &request) {
    const QString cmd = request.value(QStringLiteral("cmd")).toString();
    if (cmd.isEmpty()) return fail(QStringLiteral("missing \"cmd\""));
    if (!Configs::dataManager) return fail(QStringLiteral("data layer is not ready"));

    if (cmd == QStringLiteral("help")) return ok(QJsonObject{{"text", HelpText()}});
    if (cmd == QStringLiteral("schema")) return ok(Schema());

    if (cmd == QStringLiteral("tun.set")) {
        if (!hooks.setTun) return fail(QStringLiteral("the window is not ready yet"));
        if (!request.value(QStringLiteral("enabled")).isBool())
            return fail(QStringLiteral("\"enabled\" must be true or false"));
        const bool enabled = request.value(QStringLiteral("enabled")).toBool();
        // Enabling TUN unelevated makes the app relaunch itself through a UAC
        // prompt, so the answer to this command would never arrive. Say so.
        if (enabled && hooks.isElevated && !hooks.isElevated())
            return fail(QStringLiteral(
                "TUN needs elevated rights; turn it on from the window, "
                "which can ask for them"));
        hooks.setTun(enabled);
        return ok(QJsonObject{{"tun_enabled", Configs::dataManager->settingsRepo->spmode_vpn}});
    }

    if (cmd == QStringLiteral("system_proxy.set")) {
        if (!hooks.setSystemProxy) return fail(QStringLiteral("the window is not ready yet"));
        if (!request.value(QStringLiteral("enabled")).isBool())
            return fail(QStringLiteral("\"enabled\" must be true or false"));
        hooks.setSystemProxy(request.value(QStringLiteral("enabled")).toBool());
        return ok(QJsonObject{{"system_proxy", Configs::dataManager->settingsRepo->spmode_system_proxy}});
    }

    if (cmd == QStringLiteral("status")) {
        const auto profile = activeProfile();
        const int runningId = hooks.runningProfileId ? hooks.runningProfileId() : -1;
        QJsonObject data{
            {"running_profile_id", runningId},
            {"running", runningId >= 0},
            {"mixed_port", Configs::dataManager->settingsRepo->inbound_socks_port},
            {"tun_enabled", Configs::dataManager->settingsRepo->spmode_vpn},
            {"system_proxy", Configs::dataManager->settingsRepo->spmode_system_proxy},
        };
        if (runningId >= 0) {
            if (const auto running = Configs::dataManager->profilesRepo->GetProfile(runningId))
                data["running_profile_name"] = running->name;
        }
        if (profile) data["routing"] = describeProfile(profile);
        return ok(data);
    }

    if (cmd == QStringLiteral("profiles.list")) {
        QJsonArray items;
        for (const int id: Configs::dataManager->profilesRepo->GetAllProfileIds()) {
            const auto profile = Configs::dataManager->profilesRepo->GetProfile(id);
            if (!profile) continue;
            items.append(QJsonObject{
                {"id", profile->id},
                {"name", profile->name},
                {"type", profile->type},
                {"group_id", profile->gid},
            });
        }
        return ok(QJsonObject{{"profiles", items}});
    }

    if (cmd == QStringLiteral("profile.start")) {
        if (!hooks.startProfile) return fail(QStringLiteral("the window is not ready yet"));
        const int id = request.value(QStringLiteral("id")).toInt(-1);
        if (id < 0) return fail(QStringLiteral("\"id\" is required"));
        if (!Configs::dataManager->profilesRepo->GetProfile(id))
            return fail(QStringLiteral("no profile with id %1").arg(id));
        hooks.startProfile(id);
        return ok(QJsonObject{{"started", id}});
    }

    if (cmd == QStringLiteral("profile.stop")) {
        if (!hooks.stopProfile) return fail(QStringLiteral("the window is not ready yet"));
        hooks.stopProfile();
        return ok();
    }

    if (cmd == QStringLiteral("routing.list")) {
        QJsonArray items;
        for (const auto &profile: Configs::dataManager->routesRepo->GetAllRouteProfiles()) {
            QJsonObject item = describeProfile(profile);
            item["active"] = profile->id == Configs::dataManager->settingsRepo->current_route_id;
            items.append(item);
        }
        return ok(QJsonObject{{"profiles", items}});
    }

    if (cmd == QStringLiteral("routing.get")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        QJsonObject data = describeProfile(profile);
        if (!profile->isRaw) {
            data["proxy_domains"] = QJsonArray::fromStringList(
                profile->GetSimpleRules(Configs::proxy).split('\n', Qt::SkipEmptyParts));
            data["direct_domains"] = QJsonArray::fromStringList(
                profile->GetSimpleRules(Configs::bypass).split('\n', Qt::SkipEmptyParts));
            data["blocked_domains"] = QJsonArray::fromStringList(
                profile->GetSimpleRules(Configs::block).split('\n', Qt::SkipEmptyParts));
        }
        return ok(data);
    }

    if (cmd == QStringLiteral("routing.select")) {
        const int id = request.value(QStringLiteral("id")).toInt(-1);
        if (id < 0) return fail(QStringLiteral("\"id\" is required"));
        if (!Configs::dataManager->routesRepo->GetRouteProfile(id))
            return fail(QStringLiteral("no routing profile with id %1").arg(id));
        Configs::dataManager->settingsRepo->current_route_id = id;
        Configs::dataManager->settingsRepo->Save();
        if (hooks.applyRoutingChange) hooks.applyRoutingChange();
        return ok(QJsonObject{{"active", id}});
    }

    if (cmd == QStringLiteral("routing.set_default")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile owns its own final outbound"));
        int outbound = 0;
        if (!outboundFromName(request.value(QStringLiteral("outbound")).toString(), &outbound))
            return fail(QStringLiteral("\"outbound\" must be direct, proxy, block or warp"));
        profile->defaultOutboundID = outbound;
        return saveAndApply(profile, QJsonObject{{"default_outbound", outboundName(outbound)}}, request);
    }

    if (cmd == QStringLiteral("routing.set_rules_enabled")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile has no separable rules"));
        if (!request.value(QStringLiteral("enabled")).isBool())
            return fail(QStringLiteral("\"enabled\" must be true or false"));
        profile->applyProfileRules = request.value(QStringLiteral("enabled")).toBool();
        return saveAndApply(profile, QJsonObject{{"rules_enabled", profile->applyProfileRules}}, request);
    }

    if (cmd == QStringLiteral("routing.apply")) {
        if (!hooks.applyRoutingChange) return fail(QStringLiteral("the window is not ready yet"));
        hooks.applyRoutingChange();
        return ok(QJsonObject{{"applied", true}});
    }

    if (cmd == QStringLiteral("logs")) {
        if (!hooks.recentLogs) return fail(QStringLiteral("the window is not ready yet"));
        const int wanted = request.value(QStringLiteral("lines")).isDouble()
                               ? qBound(1, request.value(QStringLiteral("lines")).toInt(), 2000)
                               : 200;
        QStringList lines = hooks.recentLogs(wanted);
        const QString contains = request.value(QStringLiteral("contains")).toString();
        if (!contains.isEmpty())
            lines = lines.filter(contains, Qt::CaseInsensitive);
        return ok(QJsonObject{{"lines", QJsonArray::fromStringList(lines)}});
    }

    if (cmd == QStringLiteral("groups.list")) {
        QJsonArray items;
        for (const int id: Configs::dataManager->groupsRepo->GetAllGroupIds()) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(id);
            if (!group) continue;
            items.append(QJsonObject{
                {"id", group->id},
                {"name", group->name},
                {"url", group->url},
                {"subscription", !group->url.trimmed().isEmpty()},
                {"archive", group->archive},
                {"profiles", static_cast<int>(group->Profiles().size())},
            });
        }
        return ok(QJsonObject{{"groups", items}});
    }

    if (cmd == QStringLiteral("subscriptions.update")) {
        if (!hooks.updateSubscriptions) return fail(QStringLiteral("the window is not ready yet"));
        // The refresh runs in the background; the caller watches groups.list or
        // the log for the result rather than being blocked here.
        hooks.updateSubscriptions();
        return ok(QJsonObject{{"started", true}});
    }

    if (cmd == QStringLiteral("routing.create")) {
        const QString name = request.value(QStringLiteral("name")).toString().trimmed();
        if (name.isEmpty()) return fail(QStringLiteral("\"name\" is required"));
        auto profile = Configs::RoutesRepo::NewRouteProfile();
        profile->name = name;
        // Copying an existing profile is how an agent experiments without
        // touching the one the user is routing through.
        if (request.value(QStringLiteral("copy_of")).isDouble()) {
            const auto source = Configs::dataManager->routesRepo->GetRouteProfile(
                request.value(QStringLiteral("copy_of")).toInt());
            if (!source) return fail(QStringLiteral("no routing profile to copy from"));
            const Configs::RouteProfile copied(*source);
            profile->Rules = copied.Rules;
            profile->defaultOutboundID = copied.defaultOutboundID;
            profile->applyProfileRules = copied.applyProfileRules;
            profile->isRaw = copied.isRaw;
            profile->rawRoute = copied.rawRoute;
            profile->preventModifications = copied.preventModifications;
        }
        if (!Configs::dataManager->routesRepo->AddRouteProfile(profile))
            return fail(QStringLiteral("could not create the routing profile"));
        if (request.value(QStringLiteral("select")).toBool()) {
            Configs::dataManager->settingsRepo->current_route_id = profile->id;
            Configs::dataManager->settingsRepo->Save();
            if (hooks.applyRoutingChange) hooks.applyRoutingChange();
        }
        return ok(describeProfile(profile));
    }

    if (cmd == QStringLiteral("routing.delete")) {
        const int id = request.value(QStringLiteral("id")).toInt(-1);
        if (id < 0) return fail(QStringLiteral("\"id\" is required"));
        if (!Configs::dataManager->routesRepo->GetRouteProfile(id))
            return fail(QStringLiteral("no routing profile with id %1").arg(id));
        const auto remaining = Configs::dataManager->routesRepo->GetAllRouteProfileIds();
        if (remaining.size() <= 1) return fail(QStringLiteral("the last routing profile cannot be deleted"));
        if (id == Configs::dataManager->settingsRepo->current_route_id)
            return fail(QStringLiteral("select another profile before deleting the active one"));
        Configs::dataManager->routesRepo->DeleteRouteProfile(id);
        return ok(QJsonObject{{"deleted", id}});
    }

    if (cmd == QStringLiteral("routing.export")) {
        const auto profile = targetProfile(request);
        if (!profile) return fail(QStringLiteral("no such routing profile"));
        return ok(QJsonObject{{"profile", profile->ToShareObject()}});
    }

    if (cmd == QStringLiteral("routing.rules")) {
        const auto profile = targetProfile(request);
        if (!profile) return fail(QStringLiteral("no such routing profile"));
        if (profile->isRaw)
            return ok(QJsonObject{{"raw", true}, {"route", QString2QJsonObject(profile->rawRoute)}});
        return ok(QJsonObject{
            {"id", profile->id},
            {"name", profile->name},
            {"default_outbound", outboundName(profile->defaultOutboundID)},
            {"rules", profile->get_route_rules(true)},
        });
    }

    if (cmd == QStringLiteral("routing.import")) {
        const auto profile = targetProfile(request);
        if (!profile) return fail(QStringLiteral("no such routing profile"));

        QString document;
        if (request.value(QStringLiteral("profile")).isObject())
            document = QString::fromUtf8(
                QJsonDocument(request.value(QStringLiteral("profile")).toObject()).toJson(QJsonDocument::Compact));
        else if (request.value(QStringLiteral("input")).isString())
            document = request.value(QStringLiteral("input")).toString();
        else
            return fail(QStringLiteral("give \"profile\" as an object, or \"input\" as a route link or base64"));

        QString fatal, warnings;
        bool wasOldArray = false;
        const auto parsed = Configs::RouteProfile::FromShareInput(document, &fatal, &warnings, &wasOldArray);
        if (!parsed) return fail(fatal.isEmpty() ? QStringLiteral("could not read the routing document") : fatal);
        if (parsed->isRaw != profile->isRaw)
            return fail(QStringLiteral("a raw document cannot replace a structured profile, or the other way round"));

        // The profile keeps its identity: an import replaces what it routes, not
        // which profile the rest of the app is pointing at.
        profile->Rules = parsed->Rules;
        profile->rawRoute = parsed->rawRoute;
        profile->preventModifications = parsed->preventModifications;
        if (!wasOldArray) profile->defaultOutboundID = parsed->defaultOutboundID;
        if (request.value(QStringLiteral("rename")).toBool() && !parsed->name.trimmed().isEmpty())
            profile->name = parsed->name;

        QJsonObject data{
            {"id", profile->id},
            {"name", profile->name},
            {"rules", static_cast<int>(profile->Rules.size())},
            {"default_outbound", outboundName(profile->defaultOutboundID)},
        };
        if (!warnings.trimmed().isEmpty()) data["warnings"] = warnings.trimmed();
        return saveAndApply(profile, data, request);
    }

    if (cmd == QStringLiteral("routing.add_apps") || cmd == QStringLiteral("routing.remove_apps")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile is edited as JSON, not as app lists"));
        Configs::simpleAction action = Configs::proxy;
        if (!actionFromName(request.value(QStringLiteral("action")).toString(), &action))
            return fail(QStringLiteral("\"action\" must be proxy, direct or block"));
        const QStringList apps = requestedStrings(request, QStringLiteral("apps"));
        if (apps.isEmpty()) return fail(QStringLiteral("\"apps\" must be a non-empty array"));

        QStringList current = profile->GetSimpleRules(action).split('\n', Qt::SkipEmptyParts);
        const bool adding = cmd.endsWith(QStringLiteral("add_apps"));
        for (const QString &app: apps) {
            const QString entry = asProcessEntry(app);
            if (adding) {
                if (!current.contains(entry)) current << entry;
            } else {
                current.removeAll(entry);
                current.removeAll(app);
            }
        }
        const QString error = profile->UpdateSimpleRules(current.join('\n'), action);
        if (!error.isEmpty()) return fail(error);
        QStringList processEntries;
        for (const QString &entry: current)
            if (entry.startsWith(QStringLiteral("processName:")) || entry.startsWith(QStringLiteral("processPath:")))
                processEntries << entry;
        return saveAndApply(profile, QJsonObject{
                                         {"action", request.value(QStringLiteral("action")).toString()},
                                         {"apps", QJsonArray::fromStringList(processEntries)},
                                     },
                            request);
    }

    if (cmd == QStringLiteral("routing.paste")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile is edited as JSON, not as domain lists"));
        Configs::simpleAction action = Configs::proxy;
        if (!actionFromName(request.value(QStringLiteral("action")).toString(), &action))
            return fail(QStringLiteral("\"action\" must be proxy, direct or block"));

        // This replaces the list wholesale, so a missing or mistyped "lines" would
        // silently erase every rule of this action. An explicitly empty array still
        // means "clear it" -- only the absent and the malformed are refused.
        const auto linesValue = request.value(QStringLiteral("lines"));
        if (!linesValue.isArray())
            return fail(QStringLiteral("\"lines\" is required and must be an array of strings"));
        for (const auto &line: linesValue.toArray()) {
            if (!line.isString())
                return fail(QStringLiteral("\"lines\" is required and must be an array of strings"));
        }

        QStringList parsed;
        QStringList rejected;
        for (const QString &line: requestedStrings(request, QStringLiteral("lines"))) {
            for (const QString &part: line.split('\n')) {
                const QString clean = part.trimmed();
                if (clean.isEmpty() || clean.startsWith(QLatin1Char('#')) || clean.startsWith(QStringLiteral("//")))
                    continue;
                const QString rule = Configs::NormalizeRuleLine(clean);
                if (rule.isEmpty())
                    rejected << clean;
                else if (!parsed.contains(rule))
                    parsed << rule;
            }
        }

        const QString error = profile->UpdateSimpleRules(parsed.join('\n'), action);
        if (!error.isEmpty()) return fail(error);
        return saveAndApply(profile, QJsonObject{
                                         {"action", request.value(QStringLiteral("action")).toString()},
                                         {"domains", QJsonArray::fromStringList(parsed)},
                                         {"rejected", QJsonArray::fromStringList(rejected)},
                                     },
                            request);
    }

    if (cmd == QStringLiteral("routing.add_domains") || cmd == QStringLiteral("routing.remove_domains")) {
        const auto profile = activeProfile();
        if (!profile) return fail(QStringLiteral("no active routing profile"));
        if (profile->isRaw) return fail(QStringLiteral("a raw profile is edited as JSON, not as domain lists"));
        Configs::simpleAction action = Configs::proxy;
        if (!actionFromName(request.value(QStringLiteral("action")).toString(), &action))
            return fail(QStringLiteral("\"action\" must be proxy, direct or block"));
        const QStringList domains = requestedStrings(request, QStringLiteral("domains"));
        if (domains.isEmpty()) return fail(QStringLiteral("\"domains\" must be a non-empty array"));

        QStringList current = profile->GetSimpleRules(action).split('\n', Qt::SkipEmptyParts);
        const bool adding = cmd.endsWith(QStringLiteral("add_domains"));
        for (const QString &domain: domains) {
            const QString entry = withRulePrefix(domain);
            if (adding) {
                if (!current.contains(entry)) current << entry;
            } else {
                current.removeAll(entry);
                // Tolerate removal by the exact stored form as well, so a value
                // read back from routing.get can be handed straight back.
                current.removeAll(domain);
            }
        }
        const QString error = profile->UpdateSimpleRules(current.join('\n'), action);
        if (!error.isEmpty()) return fail(error);
        return saveAndApply(profile, QJsonObject{
                                         {"action", request.value(QStringLiteral("action")).toString()},
                                         {"domains", QJsonArray::fromStringList(current)},
                                     },
                            request);
    }

    return fail(QStringLiteral("unknown command \"%1\"; send {\"cmd\":\"help\"}").arg(cmd));
}

QString HelpText() {
    QStringList lines;
    lines << QStringLiteral("throned control interface")
          << QString()
          << QStringLiteral("Send one JSON object per invocation to the running instance; one JSON")
          << QStringLiteral("object comes back. Every reply is either")
          << QString()
          << QStringLiteral(R"(  {"ok": true,  "data": { ... }})")
          << QStringLiteral(R"(  {"ok": false, "error": "why it failed"})")
          << QString()
          << QStringLiteral("so a failure is always parseable rather than free-form text.")
          << QString()
          << QStringLiteral("Usage:")
          << QStringLiteral(R"(  throned --cli '{"cmd":"status"}')")
          << QStringLiteral(R"(  throned --cli '{"cmd":"schema"}'   the same reference as JSON)")
          << QString()
          << QStringLiteral("Commands")
          << QString();

    for (const Command &command: commandTable()) {
        QString head = QStringLiteral(R"(  {"cmd":"%1")").arg(QString::fromLatin1(command.name));
        for (const Argument &argument: command.arguments) {
            head += QStringLiteral(",\"%1\":<%2>").arg(QString::fromLatin1(argument.name), QString::fromLatin1(argument.type));
        }
        lines << head + QStringLiteral("}");
        lines << QStringLiteral("      ") + QString::fromLatin1(command.summary);
        for (const Argument &argument: command.arguments) {
            QString detail = QStringLiteral("        %1 (%2%3)")
                                 .arg(QString::fromLatin1(argument.name), QString::fromLatin1(argument.type),
                                      argument.required ? QStringLiteral(", required") : QString());
            if (*argument.accepts) detail += QStringLiteral(" - one of: %1").arg(QString::fromLatin1(argument.accepts));
            if (*argument.summary) detail += QStringLiteral(" - %1").arg(QString::fromLatin1(argument.summary));
            lines << detail;
        }
        if (*command.returns) lines << QStringLiteral("      returns: ") + QString::fromLatin1(command.returns);
        lines << QString();
    }

    lines << QStringLiteral("Routing document")
          << QString()
          << QStringLiteral("  routing.export returns, and routing.import accepts, this shape:")
          << QString()
          << QStringLiteral(R"(    {"kind":"throne-route-profile","v":1,"name":"...",)")
          << QStringLiteral(R"(     "default_outbound":"proxy","rules":[ ... ]})")
          << QString()
          << QStringLiteral("  Rules are evaluated in array order and the first match wins. A rule is")
          << QStringLiteral("  matcher fields plus \"action\"; \"route\" also takes \"outbound\". Every")
          << QStringLiteral("  matcher field and its accepted values are listed under routing_document")
          << QStringLiteral("  in the JSON schema, read straight out of the rule model:")
          << QString()
          << QStringLiteral(R"(    throned --cli '{"cmd":"schema"}')")
          << QString()
          << QStringLiteral("Notes")
          << QString()
          << QStringLiteral("  A routing change is saved at once and restarts the running profile so it")
          << QStringLiteral("  takes effect, which briefly interrupts traffic.")
          << QString()
          << QStringLiteral("  Commands that edit routing refuse to touch raw profiles, because those")
          << QStringLiteral("  carry a verbatim sing-box route object with no domain lists to merge into.");
    return lines.join('\n') + '\n';
}

QJsonObject RuleSchema() {
    QJsonArray fields;
    for (const QString &field: Configs::RouteRule::get_attributes()) {
        const Configs::inputType type = Configs::RouteRule::get_input_type(field);
        QJsonObject described{
            {"name", field},
            {"type", type == Configs::trufalse ? QStringLiteral("bool")
                     : type == Configs::select ? QStringLiteral("string")
                                               : QStringLiteral("string[]")},
        };
        QStringList accepted = Configs::RouteRule::get_values_for_field(field);
        accepted.removeAll(QString());
        if (!accepted.isEmpty()) described["accepts"] = QJsonArray::fromStringList(accepted);
        fields.append(described);
    }
    return QJsonObject{
        {"document", QStringLiteral(
                         R"({"kind":"throne-route-profile","v":1,"name":"...","default_outbound":"proxy|direct|block|warp","rules":[...]})")},
        {"order", QStringLiteral("Rules are evaluated in array order and the first match wins.")},
        {"rule", QStringLiteral(
                     R"(A rule is an object of matcher fields plus "action". "route" also takes )"
                     R"("outbound". "name" is a free label, "type" marks which editor owns the rule )"
                     R"(and may be left as "custom" for anything hand-written.)")},
        {"outbound", QStringLiteral(
                         "proxy, direct, block or warp-bypass, or the name of a server profile.")},
        {"fields", fields},
    };
}

QJsonObject Schema() {
    QJsonArray commands;
    for (const Command &command: commandTable()) {
        QJsonArray arguments;
        for (const Argument &argument: command.arguments) {
            QJsonObject described{
                {"name", QString::fromLatin1(argument.name)},
                {"type", QString::fromLatin1(argument.type)},
                {"required", argument.required},
            };
            if (*argument.accepts) {
                QJsonArray accepted;
                for (const QString &value: QString::fromLatin1(argument.accepts).split(QStringLiteral(", ")))
                    accepted.append(value);
                described["accepts"] = accepted;
            }
            if (*argument.summary) described["summary"] = QString::fromLatin1(argument.summary);
            arguments.append(described);
        }
        QJsonObject described{
            {"name", QString::fromLatin1(command.name)},
            {"summary", QString::fromLatin1(command.summary)},
            {"arguments", arguments},
        };
        if (*command.returns) described["returns"] = QString::fromLatin1(command.returns);
        commands.append(described);
    }
    return QJsonObject{
        {"interface", QStringLiteral("throned-control")},
        {"version", 1},
        {"reply", QStringLiteral(R"({"ok":true,"data":{...}} or {"ok":false,"error":"..."})")},
        {"routing_document", RuleSchema()},
        {"commands", commands},
    };
}

} // namespace ThronedControl
