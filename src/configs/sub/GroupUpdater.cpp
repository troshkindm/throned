#include "include/configs/sub/GroupUpdater.hpp"

#include "include/configs/sub/SubscriptionParser.hpp"
#include "include/configs/sub/SubscriptionReconcile.hpp"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/global/HTTPRequestHelper.hpp"
#include "include/global/ShareLinkB64.hpp"
#include "include/global/Utils.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QUrl>

#include <algorithm>

namespace Subscription {
    namespace {
        constexpr int kInsertChunk = 500;
        constexpr qint64 kMaxSubscriptionBytes = 64LL * 1024 * 1024;

        QByteArray digest(const QByteArray &data) {
            return QCryptographicHash::hash(data, QCryptographicHash::Sha256);
        }

        QByteArray contentKeyOf(const Configs::Profile &ent) {
            return digest(QJsonDocument(ent.outbound->ExportToJson()).toJson(QJsonDocument::Compact));
        }

        QByteArray identityKeyOf(const Configs::Profile &ent) {
            return digest(ent.type.toUtf8() + '|' + QJsonDocument(ent.outbound->ExportIdentity()).toJson(QJsonDocument::Compact));
        }

        // BatchDeleteProfiles silently drops the running profile from the list it was handed (#1753).
        struct DeleteOutcome {
            bool ok = false;
            QList<int> deleted;
            QList<int> kept;
        };

        DeleteOutcome deleteProfiles(QList<int> ids) {
            DeleteOutcome outcome;
            const QSet<int> requested(ids.begin(), ids.end());
            outcome.ok = Configs::dataManager->profilesRepo->BatchDeleteProfiles(
                ids, Configs::dataManager->settingsRepo->allow_stopping_active_profile);
            const QSet<int> deleted(ids.begin(), ids.end());
            outcome.deleted = std::move(ids);
            for (int id : requested) {
                if (!deleted.contains(id)) outcome.kept << id;
            }
            return outcome;
        }

        // Inserts in chunks; only per-profile digests survive a flush.
        class ImportSink {
        public:
            ImportSink(int gid, ContentIndex *index) : gid(gid), index(index) {}

            void add(std::shared_ptr<Configs::Profile> ent) {
                NewEntry entry;
                entry.display = ent->outbound->DisplayTypeAndName();
                if (index != nullptr) {
                    entry.keys.content = contentKeyOf(*ent);
                    index->noteArrival(entry.keys.content);
                    entry.contentKnown = index->knows(entry.keys.content);
                    if (const auto oldId = index->claim(entry.keys.content)) {
                        entry.id = *oldId;
                        entry.reused = true;
                        entries.append(std::move(entry));
                        return;
                    }
                    if (!entry.contentKnown) entry.keys.identity = identityKeyOf(*ent);
                }
                entryIndex.append(entries.size());
                entries.append(std::move(entry));
                chunk.append(std::move(ent));
                if (chunk.size() >= kInsertChunk) flush();
            }

            void flush() {
                if (chunk.isEmpty()) return;
                if (Configs::dataManager->profilesRepo->AddProfileBatch(chunk, gid)) {
                    for (qsizetype i = 0; i < chunk.size(); ++i) entries[entryIndex[i]].id = chunk[i]->id;
                }
                chunk.clear();
                entryIndex.clear();
            }

            QList<NewEntry> entries;

        private:
            int gid;
            ContentIndex *index;
            QList<std::shared_ptr<Configs::Profile>> chunk;
            QList<qsizetype> entryIndex;
        };

        ParseSink sinkFor(ImportSink &sink) {
            ParseSink parseSink;
            parseSink.profile = [&sink](std::shared_ptr<Configs::Profile> ent) { sink.add(std::move(ent)); };
            parseSink.log = [](const QString &line) { MW_show_log(line); };
            parseSink.warn = [](const QString &title, const QString &text) {
                runOnUiThread([=] { MessageBoxWarning(title, text); });
            };
            return parseSink;
        }

        QString notice(const QStringList &names, const QString &prefix, const QString &action) {
            if (names.size() >= 1000) return QStringLiteral("%1 %2 %3\n").arg(prefix, action).arg(names.size());
            QString result;
            for (const auto &name : names) {
                result += prefix;
                result += ' ';
                result += name;
                result += '\n';
            }
            return result;
        }

        QString groupLabel(int gid) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
            return group == nullptr ? Int2String(gid) : group->name;
        }

        bool groupNameIsAutomatic(const std::shared_ptr<Configs::Group> &group) {
            return group->name.isEmpty() || group->name == group->url || group->name == QUrl(group->url).host();
        }

        QString decodeProviderValue(const QString &raw) {
            const auto value = raw.trimmed();
            if (!value.startsWith("base64:", Qt::CaseInsensitive)) return value;
            const auto decoded = DecodeShareLinkB64(value.mid(7).trimmed());
            return decoded.isEmpty() ? QString() : QString::fromUtf8(decoded).trimmed();
        }

        QString sanitizeProviderUrl(const QString &raw) {
            const QUrl url(decodeProviderValue(raw));
            if (!url.isValid() || url.host().isEmpty()) return {};
            const auto scheme = url.scheme().toLower();
            return scheme == "http" || scheme == "https" ? url.toString() : QString();
        }

        struct ProviderMeta {
            QString title;
            QString announce;
            QString supportUrl;
            QString webPageUrl;
            QString fallbackUrl;
            int updateIntervalHours = 0;

            void read(const QString &key, const QString &value) {
                if (key == "profile-title") { if (title.isEmpty()) title = value; }
                else if (key == "announce") { if (announce.isEmpty()) announce = decodeProviderValue(value); }
                else if (key == "support-url") { if (supportUrl.isEmpty()) supportUrl = sanitizeProviderUrl(value); }
                else if (key == "profile-web-page-url") { if (webPageUrl.isEmpty()) webPageUrl = sanitizeProviderUrl(value); }
                else if (key == "fallback-url") { if (fallbackUrl.isEmpty()) fallbackUrl = sanitizeProviderUrl(value); }
                else if (key == "profile-update-interval" && updateIntervalHours == 0) {
                    bool ok = false;
                    const int hours = value.toInt(&ok);
                    if (ok && hours > 0) updateIntervalHours = qMin(hours, 24 * 30);
                }
            }
        };

        void readProviderBody(const QByteArray &body, ProviderMeta &meta) {
            const auto encoded = QString::fromUtf8(body);
            const auto decoded = DecodeShareLinkB64(encoded);
            const QString text = decoded.isEmpty() ? encoded : QString::fromUtf8(decoded);
            for (const auto &rawLine : text.split(QChar('\n'))) {
                const auto line = rawLine.trimmed();
                if (line.isEmpty()) continue;
                if (!line.startsWith(QChar('#'))) break;
                const auto entry = line.mid(1).trimmed();
                const int separator = entry.indexOf(QChar(':'));
                if (separator <= 0) continue;
                const auto value = entry.mid(separator + 1).trimmed();
                if (!value.isEmpty()) meta.read(entry.left(separator).trimmed().toLower(), value);
            }
        }

        void applyProviderMeta(const ProviderMeta &meta, const std::shared_ptr<Configs::Group> &group) {
            const auto title = decodeProviderValue(meta.title);
            if (!title.isEmpty() && groupNameIsAutomatic(group)) {
                group->name = title;
                MW_show_log(QObject::tr("Subscription named itself \"%1\".").arg(title));
            }

            auto &provider = group->provider;
            provider.announce = meta.announce.left(200);
            provider.supportUrl = meta.supportUrl;
            provider.webPageUrl = meta.webPageUrl;
            provider.fallbackUrl = meta.fallbackUrl;
            if (meta.updateIntervalHours > 0) {
                if (provider.intervalFromProvider || provider.updateIntervalMinutes == 0) {
                    provider.updateIntervalMinutes = meta.updateIntervalHours * 60;
                    provider.intervalFromProvider = true;
                }
            } else if (provider.intervalFromProvider) {
                provider.updateIntervalMinutes = 0;
                provider.intervalFromProvider = false;
            }
        }

        bool subscriptionDue(const std::shared_ptr<Configs::Group> &group) {
            const int global = Configs::dataManager->settingsRepo->sub_auto_update;
            const int minutes = group->provider.updateIntervalMinutes > 0
                                    ? group->provider.updateIntervalMinutes
                                    : global;
            if (minutes <= 0) return false;
            const qint64 elapsed = QDateTime::currentSecsSinceEpoch() - group->sub_last_update;
            return elapsed + 30 >= static_cast<qint64>(minutes) * 60;
        }
    } // namespace

    GroupUpdater *updater() {
        static auto *instance = new GroupUpdater;
        return instance;
    }

    void GroupUpdater::RefreshGroup(int gid, const Finish &finish, bool showDiff) {
        QMutexLocker locker(&mutex);
        if (pending.contains(gid)) {
            locker.unlock();
            MW_show_log(QObject::tr("Subscription update already queued: %1").arg(groupLabel(gid)));
            if (finish != nullptr) finish();
            return;
        }
        pending.insert(gid);
        enqueueLocked({gid, false, [=, this] {
            refresh(gid, showDiff);
            emit asyncUpdateCallback(gid);
            if (finish != nullptr) finish();
        }});
    }

    void GroupUpdater::RefreshAll(bool onlyAllowed) {
        QMutexLocker locker(&mutex);
        if (pendingBatch > 0) {
            locker.unlock();
            MW_show_log("The last subscription update has not exited.");
            return;
        }
        for (const int gid : Configs::dataManager->groupsRepo->GetGroupsTabOrder()) {
            const auto group = Configs::dataManager->groupsRepo->GetGroup(gid);
            if (group == nullptr || group->url.isEmpty() || group->archive ||
                (onlyAllowed && (group->skip_auto_update || !subscriptionDue(group)))) continue;
            if (pending.contains(gid)) continue;
            pending.insert(gid);
            ++pendingBatch;
            enqueueLocked({gid, true, [=, this] {
                refresh(gid, false);
                emit asyncUpdateCallback(gid);
            }});
        }
    }

    void GroupUpdater::SubscribeUrl(const QString &url, const Finish &finish) {
        const auto content = url.trimmed();
        enqueue({-1, false, [=, this] {
            auto group = Configs::GroupsRepo::NewGroup();
            group->name = QUrl(content).host();
            group->url = content;
            Configs::dataManager->groupsRepo->AddGroup(group);
            MW_dialog_message(MwMessage::SubscriptionNewGroup, {});
            refresh(group->id, false);
            emit asyncUpdateCallback(group->id);
            if (finish != nullptr) finish();
        }});
    }

    void GroupUpdater::ImportUrl(const QString &url, const Finish &finish) {
        const auto content = url.trimmed();
        enqueue({-1, false, [=, this] {
            QByteArray body;
            QString userInfo;
            if (fetch(content, QObject::tr("manual URL"), body, userInfo)) importDocuments(-1, {std::move(body)});
            emit asyncUpdateCallback(-1);
            if (finish != nullptr) finish();
        }});
    }

    void GroupUpdater::ImportText(const QString &text, int gid, const Finish &finish) {
        QByteArray body = text.toUtf8();
        enqueue({-1, false, [=, this]() mutable {
            importDocuments(gid, {std::move(body)});
            emit asyncUpdateCallback(gid);
            if (finish != nullptr) finish();
        }});
    }

    void GroupUpdater::ImportBatch(const QStringList &payloads, const Finish &finish) {
        if (payloads.isEmpty()) return;
        QList<QByteArray> documents;
        for (const auto &payload : payloads) documents << payload.trimmed().toUtf8();
        enqueue({-1, false, [=, this]() mutable {
            importDocuments(-1, std::move(documents));
            emit asyncUpdateCallback(-1);
            if (finish != nullptr) finish();
        }});
    }

    void GroupUpdater::enqueue(Job job) {
        QMutexLocker locker(&mutex);
        enqueueLocked(std::move(job));
    }

    void GroupUpdater::enqueueLocked(Job job) {
        queue.append(std::move(job));
        if (running) return;
        running = true;
        runOnNewThread([this] { drain(); });
    }

    void GroupUpdater::drain() {
        for (;;) {
            Job job;
            {
                QMutexLocker locker(&mutex);
                if (queue.isEmpty()) {
                    running = false;
                    return;
                }
                job = queue.takeFirst();
            }
            try {
                job.run();
            } catch (const std::exception &ex) {
                MW_show_log(QString("Subscription task failed: %1").arg(ex.what()));
            } catch (...) {
                MW_show_log("Subscription task failed.");
            }
            QMutexLocker locker(&mutex);
            if (job.gid >= 0) pending.remove(job.gid);
            if (job.batch) --pendingBatch;
        }
    }

    bool GroupUpdater::fetch(const QString &url, const QString &name, QByteArray &body, QString &userInfo,
                             const QString &fallbackUrl,
                             QList<QPair<QByteArray, QByteArray>> *responseHeaders) {
        MW_show_log(">>>>>>>> " + QObject::tr("Requesting subscription: %1").arg(name));
        auto resp = NetworkRequestHelper::HttpGet(url, Configs::dataManager->settingsRepo->sub_send_hwid, false, kMaxSubscriptionBytes);
        if (!resp.error.isEmpty() && !fallbackUrl.isEmpty()) {
            MW_show_log(QObject::tr("Subscription %1 did not answer, trying the fallback address.").arg(name));
            resp = NetworkRequestHelper::HttpGet(fallbackUrl, Configs::dataManager->settingsRepo->sub_send_hwid,
                                                 false, kMaxSubscriptionBytes);
        }
        if (!resp.error.isEmpty()) {
            MW_show_log("<<<<<<<< " + QObject::tr("Requesting subscription %1 error: %2").arg(name, resp.error));
            return false;
        }
        body = std::move(resp.data);
        userInfo = NetworkRequestHelper::GetHeader(resp.header, "Subscription-UserInfo");
        if (responseHeaders != nullptr) *responseHeaders = std::move(resp.header);
        MW_show_log("<<<<<<<< " + QObject::tr("Subscription request fininshed: %1").arg(name));
        return true;
    }

    void GroupUpdater::importDocuments(int gid, QList<QByteArray> documents) {
        auto &settings = Configs::dataManager->settingsRepo;
        settings->imported_count = 0;
        ImportSink sink(gid, nullptr);

        MW_show_log(">>>>>>>> " + QObject::tr("Processing subscription data..."));
        for (auto &document : documents) ParseDocument(std::move(document), sinkFor(sink));
        sink.flush();
        MW_show_log(">>>>>>>> " + QObject::tr("Process complete, applying..."));

        settings->imported_count = sink.entries.size();
        MW_dialog_message(MwMessage::SubscriptionFinished, {});
    }

    void GroupUpdater::refresh(int gid, bool showDiff) {
        auto &settings = Configs::dataManager->settingsRepo;
        auto &profilesRepo = Configs::dataManager->profilesRepo;
        auto &groupsRepo = Configs::dataManager->groupsRepo;

        settings->imported_count = 0;
        auto group = groupsRepo->GetGroup(gid);
        if (group == nullptr || group->archive) return;

        QByteArray body;
        QString userInfo;
        QList<QPair<QByteArray, QByteArray>> responseHeaders;
        if (!fetch(group->url.trimmed(), group->name, body, userInfo,
                   group->provider.fallbackUrl, &responseHeaders)) return;

        ProviderMeta providerMeta;
        for (const auto &name : {"Profile-Title", "Announce", "Support-Url",
                                 "Profile-Web-Page-Url", "Profile-Update-Interval", "Fallback-Url"}) {
            const auto value = NetworkRequestHelper::GetHeader(responseHeaders, name);
            if (!value.isEmpty()) providerMeta.read(QString::fromLatin1(name).toLower(), value);
        }
        readProviderBody(body, providerMeta);

        // Auto selectors are local state, not servers the remote sent: keep them out of the diff.
        const auto selectorIds = profilesRepo->GetProfileIdsByType("autoselector");
        const QSet<int> selectors(selectorIds.begin(), selectorIds.end());
        QList<QPair<int, int>> sticky;
        QSet<int> stickyIDs;
        for (int i = 0; i < group->profiles.size(); i++) {
            if (!selectors.contains(group->profiles[i])) continue;
            sticky << qMakePair(i, group->profiles[i]);
            stickyIDs.insert(group->profiles[i]);
        }
        const auto members = [&] {
            QList<int> ids;
            for (int id : group->profiles) {
                if (!stickyIDs.contains(id)) ids << id;
            }
            return ids;
        };

        // Clear mode used to delete first and only then discover that the response
        // was empty or malformed. Preflight it without retaining the profiles so a
        // bad 200 response cannot wipe a working group.
        if (settings->sub_clear && !members().isEmpty()) {
            bool hasUsableProfile = false;
            ParseSink validation;
            validation.profile = [&hasUsableProfile](std::shared_ptr<Configs::Profile>) { hasUsableProfile = true; };
            validation.log = [](const QString &) {};
            validation.warn = [](const QString &, const QString &) {};
            ParseDocument(body, validation);
            if (!hasUsableProfile) {
                MW_show_log("<<<<<<<< " + QObject::tr(
                    "Subscription \"%1\" returned nothing usable, so the servers already in the "
                    "group were kept. Use Clear servers if it really is empty now.").arg(group->name));
                MW_dialog_message(MwMessage::SubscriptionFinished, {MwArg::Quiet});
                return;
            }
        }

        // Ids a running auto selector can no longer trust: deleted, or same id with new settings.
        QList<int> disturbed;
        bool cleared = false;
        if (settings->sub_clear) {
            MW_show_log(QObject::tr("Clearing servers..."));
            const auto outcome = deleteProfiles(members());
            if (!outcome.ok) {
                runOnUiThread([] { MessageBoxWarning("Internal Error", "DB Error when deleting profiles, Please try again."); });
                return;
            }
            disturbed = outcome.deleted;
            // A survivor still belongs to the subscription: fall through to the diff.
            cleared = outcome.kept.isEmpty();
        }

        QList<OldEntry> old;
        if (!cleared) {
            const auto ids = members();
            for (qsizetype off = 0; off < ids.size(); off += Configs::BATCH_LIMIT_READ) {
                for (const auto &ent : profilesRepo->GetProfileBatch(ids.mid(off, Configs::BATCH_LIMIT_READ))) {
                    if (ent == nullptr) continue;
                    old.append({ent->id, {contentKeyOf(*ent), identityKeyOf(*ent)}, ent->outbound->DisplayTypeAndName()});
                }
            }
        }
        ContentIndex index(old);
        ImportSink sink(gid, cleared ? nullptr : &index);

        MW_show_log(">>>>>>>> " + QObject::tr("Processing subscription data..."));
        ParseDocument(std::move(body), sinkFor(sink));
        sink.flush();
        MW_show_log(">>>>>>>> " + QObject::tr("Process complete, applying..."));

        if (sink.entries.isEmpty() && !old.isEmpty()) {
            MW_show_log("<<<<<<<< " + QObject::tr(
                "Subscription \"%1\" returned nothing usable, so the servers already in the "
                "group were kept. Use Clear servers if it really is empty now.").arg(group->name));
            MW_dialog_message(MwMessage::SubscriptionFinished, {MwArg::Quiet});
            return;
        }

        group->sub_last_update = QDateTime::currentSecsSinceEpoch();
        group->info = userInfo;
        applyProviderMeta(providerMeta, group);
        groupsRepo->Save(group);

        QString change_text;
        if (cleared) {
            if (sink.entries.size() >= 1000) {
                change_text += "[+] " + Int2String(sink.entries.size()) + " profiles\n";
            } else {
                for (const auto &entry : sink.entries) change_text += "[+] " + entry.display + "\n";
            }
        } else {
            const auto plan = Reconcile(old, sink.entries, index);
            for (const auto &[oldId, newId] : plan.updates) {
                auto oldEnt = profilesRepo->GetProfile(oldId);
                const auto newEnt = profilesRepo->GetProfile(newId);
                if (oldEnt != nullptr && newEnt != nullptr) {
                    oldEnt->outbound = newEnt->outbound;
                    oldEnt->name = oldEnt->outbound->name;
                    profilesRepo->Save(oldEnt);
                }
                disturbed << oldId;
            }

            const auto previousOrder = group->profiles;
            group->profiles = plan.order;
            for (const auto &[position, id] : sticky) {
                group->profiles.insert(std::min<qsizetype>(position, group->profiles.size()), id);
            }
            groupsRepo->Save(group);

            const auto outcome = deleteProfiles(plan.stale);
            if (!outcome.ok) {
                runOnUiThread([] { MessageBoxWarning("Internal error", "DB Error when deleting profiles, data may be corrupted"); });
            }
            disturbed << outcome.deleted;

            // Nothing rebuilds group->profiles from the rows: a survivor left out here is orphaned.
            QString notice_kept;
            for (int id : outcome.kept) {
                if (group->HasProfile(id)) continue;
                const auto position = previousOrder.indexOf(id);
                group->profiles.insert(position < 0 ? group->profiles.size()
                                                    : std::min<qsizetype>(position, group->profiles.size()), id);
                if (const auto ent = profilesRepo->GetProfile(id); ent != nullptr) {
                    notice_kept += "[=] " + ent->outbound->DisplayTypeAndName() + "\n";
                }
            }
            if (!outcome.kept.isEmpty()) groupsRepo->Save(group);

            change_text = "\n" + QObject::tr("Added %1 profiles:\n%2\nUpdated %3 profiles:\n%4\nDeleted %5 Profiles:\n%6")
                                     .arg(plan.added.size())
                                     .arg(notice(plan.added, "[+]", "added"))
                                     .arg(plan.updates.size())
                                     .arg(notice(plan.updated, "[~]", "updated"))
                                     .arg(plan.deleted.size())
                                     .arg(notice(plan.deleted, "[-]", "deleted"));
            if (!notice_kept.isEmpty()) {
                change_text += "\n" + QObject::tr("Still in use, so kept instead of deleted:\n%1").arg(notice_kept);
            }
            if (plan.added.isEmpty() && plan.updates.isEmpty() && plan.deleted.isEmpty()) change_text = QObject::tr("Nothing");
        }

        MW_show_log("<<<<<<<< " + QObject::tr("Change of %1:").arg(group->name) + "\n" + change_text);
        if (showDiff && settings->sub_show_change_popup) {
            const auto diffTitle = QObject::tr("Change of %1").arg(group->name);
            auto diffBody = change_text.trimmed();
            if (diffBody.isEmpty()) diffBody = QObject::tr("Nothing");
            runOnUiThread([diffTitle, diffBody] { MessageBoxScrollable(diffTitle, diffBody); });
        }
        // Auto selectors resolve members from the group at build time, so a refresh can invalidate an untouched one.
        QStringList selectorArgs{Int2String(group->id)};
        for (int id : disturbed) selectorArgs << Int2String(id);
        MW_dialog_message(MwMessage::SubscriptionGroupChanged, selectorArgs);
        MW_dialog_message(MwMessage::SubscriptionFinished, {MwArg::Quiet});
    }
} // namespace Subscription
