#include "include/api/remote/Server.hpp"

#include <QRegularExpression>

#include "include/global/Configs.hpp"

namespace RemoteApi {
Config ConfigFromSettings() {
    const auto &settings = Configs::dataManager->settingsRepo;
    Config config;
    config.enabled = settings->remote_api_enable;
    config.lan = settings->remote_api_lan;
    if (settings->remote_api_port >= 1 && settings->remote_api_port <= 65535) config.port = quint16(settings->remote_api_port);
    config.key = KeyFromText(settings->remote_api_key);

    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    for (auto entry: settings->remote_api_allow.split(separators, Qt::SkipEmptyParts)) {
        const auto host = entry.section(QLatin1Char('/'), 0, 0);
        // parseSubnet alone takes abbreviated IPv4 such as "10" for 10.0.0.0/8.
        if (!host.contains(QLatin1Char(':')) && host.count(QLatin1Char('.')) != 3) continue;
        if (!entry.contains(QLatin1Char('/'))) entry += host.contains(QLatin1Char(':')) ? QStringLiteral("/128") : QStringLiteral("/32");
        const auto subnet = QHostAddress::parseSubnet(entry);
        if (subnet.second >= 0) config.allow << subnet;
    }
    return config;
}
} // namespace RemoteApi
