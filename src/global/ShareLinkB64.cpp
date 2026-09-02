// Kept free of the rest of Utils so the decoder can be linked into a test on its own.
#include "include/global/ShareLinkB64.hpp"

namespace {
    QByteArray strictDecode(const QString &input, QByteArray::Base64Options options) {
        const auto result = QByteArray::fromBase64Encoding(
            input.toUtf8(), options | QByteArray::AbortOnBase64DecodingErrors);
        return result ? result.decoded : QByteArray{};
    }
}

QByteArray DecodeShareLinkB64(const QString &input) {
    // A payload valid under both alphabets contains none of + / - _ and decodes the same either way.
    if (auto decoded = strictDecode(input, QByteArray::Base64UrlEncoding); !decoded.isEmpty()) return decoded;
    return strictDecode(input, QByteArray::Base64Encoding);
}
