#include "include/global/OtpPlaceholder.hpp"

#include "include/database/DatabaseManager.h"
#include "include/database/OtpProfilesRepo.h"

namespace Configs {
QString ResolveOtpCode(int otpProfileId) {
    if (otpProfileId < 0 || dataManager == nullptr || dataManager->otpProfilesRepo == nullptr) return {};
    auto profile = dataManager->otpProfilesRepo->GetOtpProfile(otpProfileId);
    if (profile == nullptr || !profile->Validate().isEmpty()) return {};
    return profile->CurrentCode();
}

QString OtpCodeSession::Resolve(int otpProfileId) {
    if (codes.contains(otpProfileId)) return codes.value(otpProfileId);
    if (otpProfileId < 0 || dataManager == nullptr || dataManager->otpProfilesRepo == nullptr) return {};

    const auto profile = dataManager->otpProfilesRepo->GetOtpProfile(otpProfileId);
    if (profile == nullptr || !profile->Validate().isEmpty()) return {};
    const auto code = profile->CurrentCode();
    codes.insert(otpProfileId, code);
    if (profile->type == OTP::Type::HOTP)
        hotpProfiles.insert(otpProfileId, static_cast<const OTP::Entry &>(*profile));
    return code;
}

QString OtpCodeSession::Commit() {
    if (hotpProfiles.isEmpty()) return {};
    if (dataManager == nullptr || dataManager->otpProfilesRepo == nullptr)
        return QObject::tr("OTP storage is not available");

    switch (dataManager->otpProfilesRepo->AdvanceHotpCounters(hotpProfiles)) {
        case HotpAdvanceResult::Changed:
            return QObject::tr("HOTP profile changed while the config was being built");
        case HotpAdvanceResult::StorageError:
            return QObject::tr("Could not save the HOTP counter");
        case HotpAdvanceResult::Ok:
            break;
    }
    hotpProfiles.clear();
    return {};
}

QString SubstituteOtp(const QString &text, const QString &code) {
    if (code.isEmpty() || text.isEmpty()) return text;
    return QString(text).replace(QString::fromLatin1(kOtpPlaceholder), code);
}
} // namespace Configs
