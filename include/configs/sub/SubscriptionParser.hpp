#pragma once

#include "include/database/entities/Profile.h"

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

namespace Subscription {
    struct ParseSink {
        std::function<void(std::shared_ptr<Configs::Profile>)> profile;
        std::function<void(const QString &)> log;
        std::function<void(const QString &, const QString &)> warn;
    };

    // Profiles are handed to the sink in document order; the body is consumed in place.
    // False means parsing aborted after a structural error; callers that reconcile a
    // subscription must then keep the previous contents instead of applying a prefix.
    [[nodiscard]] bool ParseDocument(QByteArray body, const ParseSink &sink);

    [[nodiscard]] bool ParseText(const QString &text, const ParseSink &sink);
}
