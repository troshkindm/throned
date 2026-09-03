#include "include/configs/sub/SubscriptionReconcile.hpp"

namespace Subscription {
    ContentIndex::ContentIndex(const QList<OldEntry> &old) {
        for (const auto &entry : old) {
            unclaimed[entry.keys.content].append(entry.id);
            known.insert(entry.keys.content);
        }
    }

    std::optional<int> ContentIndex::claim(const QByteArray &content) {
        const auto it = unclaimed.find(content);
        if (it == unclaimed.end() || it->isEmpty()) return std::nullopt;
        const int id = it->takeFirst();
        claimedIds.insert(id);
        return id;
    }

    ReconcilePlan Reconcile(const QList<OldEntry> &old, const QList<NewEntry> &incoming, const ContentIndex &index) {
        ReconcilePlan plan;

        // Identity pairing only sees what the content pass never saw: leftovers whose key never arrived, arrivals whose key was never known.
        QHash<QByteArray, QList<int>> leftoverByIdentity;
        for (const auto &entry : old) {
            if (index.isClaimed(entry.id) || index.arrived(entry.keys.content)) continue;
            leftoverByIdentity[entry.keys.identity].append(entry.id);
        }

        QHash<int, int> supersededBy;
        QSet<int> matchedOld;
        for (const auto &entry : incoming) {
            if (entry.id < 0 || entry.reused || entry.contentKnown) continue;
            const auto it = leftoverByIdentity.find(entry.keys.identity);
            if (it == leftoverByIdentity.end() || it->isEmpty()) continue;
            const int oldId = it->takeFirst();
            plan.updates.append(qMakePair(oldId, entry.id));
            plan.updated << entry.display;
            supersededBy.insert(entry.id, oldId);
            matchedOld.insert(oldId);
        }

        for (const auto &entry : old) {
            if (index.isClaimed(entry.id) || matchedOld.contains(entry.id)) continue;
            plan.stale << entry.id;
            plan.deleted << entry.display;
        }

        for (const auto &entry : incoming) {
            if (entry.id < 0) continue;
            if (entry.reused) {
                plan.order << entry.id;
                continue;
            }
            if (const auto it = supersededBy.constFind(entry.id); it != supersededBy.constEnd()) {
                plan.order << it.value();
                plan.stale << entry.id;
                continue;
            }
            plan.order << entry.id;
            plan.added << entry.display;
        }
        return plan;
    }
}
