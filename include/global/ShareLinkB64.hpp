#pragma once

#include <QByteArray>
#include <QString>

// Deep-link payloads: ours are base64url without padding, while links from other
// clients use the standard alphabet. Returns empty when the input is neither.
QByteArray DecodeShareLinkB64(const QString &input);
