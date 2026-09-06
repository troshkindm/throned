#pragma once

#include <QString>

namespace Configs {

// The Subscription-UserInfo header most panels send:
// "upload=455727941; download=2016003548; total=274877906944; expire=1948602000"
struct SubInfo {
    bool valid = false; // the header carried a total, so the rest is worth reading
    qint64 upload = 0;
    qint64 download = 0;
    qint64 total = 0;  // 0 is the convention for unlimited
    qint64 expire = 0; // unix seconds, 0 when the plan does not expire

    [[nodiscard]] qint64 used() const { return upload + download; }

    // Share of the allowance spent, 0..1, or -1 when there is no finite allowance to fill.
    [[nodiscard]] double usedFraction() const;

    // Whole days left, or -1 without an expiry date. Negative input days clamp to 0.
    [[nodiscard]] int daysLeft(qint64 nowSeconds) const;
};

SubInfo ParseSubInfo(const QString& info);

} // namespace Configs
