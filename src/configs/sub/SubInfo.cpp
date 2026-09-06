// Parsing of the Subscription-UserInfo header. Kept free of UI and data-layer
// dependencies so it can be tested on its own; formatting lives with the widgets.
#include "include/configs/sub/SubInfo.h"

#include <QRegularExpression>

#include <algorithm>

namespace Configs {

namespace {
// Returns false when the key is absent, so a missing total stays distinguishable
// from the "total=0" a provider sends to mean unlimited.
bool readField(const QString& info, const QString& key, qint64& out) {
    static const QString pattern = QStringLiteral("\\b%1\\s*=\\s*(\\d+)");
    const QRegularExpression re(pattern.arg(key));
    const auto match = re.match(info);
    if (!match.hasMatch()) return false;
    out = match.captured(1).toLongLong();
    return true;
}
} // namespace

double SubInfo::usedFraction() const {
    if (total <= 0) return -1.0;
    return std::clamp(static_cast<double>(used()) / static_cast<double>(total), 0.0, 1.0);
}

int SubInfo::daysLeft(qint64 nowSeconds) const {
    if (expire <= 0) return -1;
    const qint64 seconds = expire - nowSeconds;
    if (seconds <= 0) return 0;
    return static_cast<int>(seconds / 86400);
}

SubInfo ParseSubInfo(const QString& info) {
    SubInfo res;
    if (info.trimmed().isEmpty()) return res;
    res.valid = readField(info, QStringLiteral("total"), res.total);
    if (!res.valid) return res;
    readField(info, QStringLiteral("upload"), res.upload);
    readField(info, QStringLiteral("download"), res.download);
    readField(info, QStringLiteral("expire"), res.expire);
    return res;
}

} // namespace Configs
