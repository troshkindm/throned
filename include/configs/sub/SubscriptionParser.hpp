#pragma once

#include "include/database/entities/Profile.h"
#include "include/global/Validation.hpp"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

namespace Subscription {
// One per parsed item that was refused or carries issues. `source` must never be the raw link (it holds secrets).
struct ParseReport {
    QString source;
    bool imported = false;
    Validation::Issues issues;
    // About the whole document (no proxies list, broken JSON), not one profile: counted neither as failed nor as warned.
    bool document = false;
};

struct ParseSink {
    std::function<void(std::shared_ptr<Configs::Profile>)> profile;
    std::function<void(const ParseReport &)> report;
};

// Profiles are handed to the sink in document order; the body is consumed in place.
// False means parsing aborted after a structural error; callers that reconcile a
// subscription must then keep the previous contents instead of applying a prefix.
[[nodiscard]] bool ParseDocument(QByteArray body, const ParseSink &sink);

[[nodiscard]] bool ParseText(const QString &text, const ParseSink &sink);
} // namespace Subscription
