#include "include/configs/common/QUICFields.h"

#include <QUrlQuery>

namespace Configs {
bool QUICFields::ParseFromLink(const QString& link) {
    auto url = QUrl(link);
    // Hysteria port-hopping links carry a port range, which QUrl reports as invalid.
    if (!url.isValid() && !url.errorString().startsWith("Invalid port")) return false;
    auto query = QUrlQuery(url.query());

    if (query.hasQueryItem("quic_idle_timeout")) idle_timeout = query.queryItemValue("quic_idle_timeout");
    if (query.hasQueryItem("quic_keep_alive_period")) keep_alive_period = query.queryItemValue("quic_keep_alive_period");
    if (query.hasQueryItem("quic_stream_receive_window")) stream_receive_window = query.queryItemValue("quic_stream_receive_window");
    if (query.hasQueryItem("quic_connection_receive_window")) connection_receive_window = query.queryItemValue("quic_connection_receive_window");
    if (query.hasQueryItem("quic_max_concurrent_streams")) max_concurrent_streams = query.queryItemValue("quic_max_concurrent_streams").toInt();
    if (query.hasQueryItem("quic_initial_packet_size")) initial_packet_size = query.queryItemValue("quic_initial_packet_size").toInt();
    if (query.hasQueryItem("quic_disable_path_mtu_discovery")) {
        disable_path_mtu_discovery = query.queryItemValue("quic_disable_path_mtu_discovery") == "true";
        disable_path_mtu_discovery_unspecified = false;
    } else
        disable_path_mtu_discovery_unspecified = true;
    return true;
}
bool QUICFields::ParseFromJson(const QJsonObject& object) {
    if (object.isEmpty()) return false;

    if (object.contains("idle_timeout")) idle_timeout = object["idle_timeout"].toString();
    if (object.contains("keep_alive_period")) keep_alive_period = object["keep_alive_period"].toString();
    if (object.contains("stream_receive_window")) stream_receive_window = object["stream_receive_window"].toString();
    if (object.contains("connection_receive_window")) connection_receive_window = object["connection_receive_window"].toString();
    if (object.contains("max_concurrent_streams")) max_concurrent_streams = object["max_concurrent_streams"].toInt();
    if (object.contains("initial_packet_size")) initial_packet_size = object["initial_packet_size"].toInt();
    if (object.contains("disable_path_mtu_discovery")) {
        disable_path_mtu_discovery = object["disable_path_mtu_discovery"].toBool();
        disable_path_mtu_discovery_unspecified = false;
    } else
        disable_path_mtu_discovery_unspecified = true;
    return true;
}
QString QUICFields::ExportToLink() {
    QUrlQuery query;
    if (!idle_timeout.isEmpty()) query.addQueryItem("quic_idle_timeout", idle_timeout);
    if (!keep_alive_period.isEmpty()) query.addQueryItem("quic_keep_alive_period", keep_alive_period);
    if (!stream_receive_window.isEmpty()) query.addQueryItem("quic_stream_receive_window", stream_receive_window);
    if (!connection_receive_window.isEmpty()) query.addQueryItem("quic_connection_receive_window", connection_receive_window);
    if (max_concurrent_streams > 0) query.addQueryItem("quic_max_concurrent_streams", QString::number(max_concurrent_streams));
    if (initial_packet_size > 0) query.addQueryItem("quic_initial_packet_size", QString::number(initial_packet_size));
    if (!disable_path_mtu_discovery_unspecified) query.addQueryItem("quic_disable_path_mtu_discovery", disable_path_mtu_discovery ? "true" : "false");
    return query.toString();
}
QJsonObject QUICFields::ExportToJson() {
    QJsonObject object;
    if (!idle_timeout.isEmpty()) object["idle_timeout"] = idle_timeout;
    if (!keep_alive_period.isEmpty()) object["keep_alive_period"] = keep_alive_period;
    if (!stream_receive_window.isEmpty()) object["stream_receive_window"] = stream_receive_window;
    if (!connection_receive_window.isEmpty()) object["connection_receive_window"] = connection_receive_window;
    if (max_concurrent_streams > 0) object["max_concurrent_streams"] = max_concurrent_streams;
    if (initial_packet_size > 0) object["initial_packet_size"] = initial_packet_size;
    // persist On (true) / Off (false) explicitly so an Off override survives a round-trip
    if (!disable_path_mtu_discovery_unspecified) object["disable_path_mtu_discovery"] = disable_path_mtu_discovery;
    return object;
}
BuildResult QUICFields::Build() {
    auto object = ExportToJson();
    const auto& settings = *dataManager->settingsRepo;
    const auto idleTimeout = settings.h2_idle_timeout.trimmed();
    const auto keepAlivePeriod = settings.h2_keep_alive_period.trimmed();
    const auto streamReceiveWindow = settings.h2_stream_receive_window.trimmed();
    const auto connectionReceiveWindow = settings.h2_connection_receive_window.trimmed();
    if (idle_timeout.isEmpty() && !idleTimeout.isEmpty()) object["idle_timeout"] = idleTimeout;
    if (keep_alive_period.isEmpty() && !keepAlivePeriod.isEmpty()) object["keep_alive_period"] = keepAlivePeriod;
    if (stream_receive_window.isEmpty() && !streamReceiveWindow.isEmpty()) object["stream_receive_window"] = streamReceiveWindow;
    if (connection_receive_window.isEmpty() && !connectionReceiveWindow.isEmpty()) object["connection_receive_window"] = connectionReceiveWindow;
    if (max_concurrent_streams <= 0 && settings.h2_max_concurrent_streams > 0) object["max_concurrent_streams"] = settings.h2_max_concurrent_streams;
    if (initial_packet_size <= 0 && settings.quic_initial_packet_size > 0) object["initial_packet_size"] = settings.quic_initial_packet_size;
    if (disable_path_mtu_discovery_unspecified && settings.quic_disable_path_mtu_discovery) object["disable_path_mtu_discovery"] = true;
    return {object, ""};
}
} // namespace Configs
