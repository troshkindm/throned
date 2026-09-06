#pragma once

#include <QHash>
#include <QString>

#include "include/global/OTP.hpp"

namespace Configs {
inline constexpr auto kOtpPlaceholder = "{otp}";

// Resolves the current code without changing an HOTP counter.
QString ResolveOtpCode(int otpProfileId);

// One generated config may reference the same VPN profile more than once.
// Cache each OTP so every occurrence gets the same value, then advance each
// HOTP counter at most once after the complete config has built successfully.
class OtpCodeSession {
public:
    QString Resolve(int otpProfileId);
    QString Commit();

private:
    QHash<int, QString> codes;
    QHash<int, OTP::Entry> hotpProfiles;
};

QString SubstituteOtp(const QString &text, const QString &code);
} // namespace Configs
