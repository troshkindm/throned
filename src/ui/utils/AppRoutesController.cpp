#include "include/ui/utils/AppRoutesController.h"

#include <QCoreApplication>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QMutex>
#include <QPointer>
#include <QSet>
#include <QThreadPool>
#include <QTimer>

#include <utility>

#include "include/configs/RuleSetCache.h"
#include "include/database/GroupsRepo.h"
#include "include/database/ProfilesRepo.h"
#include "include/database/entities/AppRoutes.h"
#include "include/database/entities/RouteRule.h"
#include "include/engine/Lifecycle.h"
#include "include/global/Configs.hpp"
#include "include/ui/setting/ApplicationPickerDialog.h"
#include "include/ui/widget/SimpleRoutesPage.h"

namespace {
// The strings predate this file and are translated under MainWindow.
QString latencyLabel(const std::shared_ptr<Configs::Profile> &profile) {
    if (profile->latency == Configs::kLatencyConnectOnly) return QCoreApplication::translate("MainWindow", "Connect OK");
    if (profile->latency < 0) return QCoreApplication::translate("MainWindow", "Unavailable");
    return profile->latency > 0 ? QCoreApplication::translate("MainWindow", "%1 ms").arg(profile->latency) : QString();
}

QList<Configs::AppRoutes::Program> programsOf(const QList<InstalledApplications::Entry> &programs) {
    QList<Configs::AppRoutes::Program> list;
    for (const auto &program: programs) list.append({program.name, program.executable, program.path, program.package, program.running});
    return list;
}

// The scan and the icons are the same for every page, and each costs a walk of the disk or a shell call.
class AppScanCache : public QObject {
public:
    static AppScanCache *instance() {
        static QPointer<AppScanCache> cache;
        if (!cache) cache = new AppScanCache;
        return cache;
    }

    void attach(SimpleRoutesPage *page) {
        pages_.removeIf([](const QPointer<SimpleRoutesPage> &item) { return item.isNull(); });
        if (!pages_.contains(page)) pages_.append(page);
        if (state_ == State::Done) {
            page->setDetected(detected_);
            for (auto it = icons_.cbegin(); it != icons_.cend(); ++it) page->setIcon(it.key(), it.value());
        }
        if (state_ != State::Idle) return;
        state_ = State::Running;
        if (AppRoutesController::Preview()) {
            const QList<InstalledApplications::Entry> fixed{{.name = QStringLiteral("Telegram Desktop"), .executable = QStringLiteral("Telegram.exe"), .running = true},
                                                            {.name = QStringLiteral("Discord"), .executable = QStringLiteral("Discord.exe"), .running = true},
                                                            {.name = QStringLiteral("Steam"), .executable = QStringLiteral("steam.exe")},
                                                            {.name = QStringLiteral("Spotify"), .executable = QStringLiteral("Spotify.exe")}};
            finish(Configs::AppRoutes::Detect(Configs::AppRoutes::Catalog(), programsOf(fixed)), {});
            return;
        }
        const QPointer<AppScanCache> guard(this);
        QThreadPool::globalInstance()->start([guard] {
            const auto programs = programsOf(InstalledApplications::Scan());
            const auto found = Configs::AppRoutes::Detect(Configs::AppRoutes::Catalog(), programs);
            const auto icons = Configs::AppRoutes::IconPaths(Configs::AppRoutes::Catalog(), programs);
            if (!guard) return;
            QMetaObject::invokeMethod(guard.data(), [guard, found, icons] {
                if (guard) guard->finish(found, icons); }, Qt::QueuedConnection);
        });
    }

private:
    enum class State { Idle,
                       Running,
                       Done };

    AppScanCache() : QObject(QCoreApplication::instance()) {
        // Icons hold platform pixmaps, which must go before the GUI does.
        connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
            icons_.clear();
            pending_.clear();
        });
    }

    void finish(const QHash<QString, bool> &found, const QHash<QString, QString> &paths) {
        state_ = State::Done;
        detected_ = found;
        pending_ = paths;
        for (const auto &page: pages_)
            if (page) page->setDetected(found);
        loadNextIcon();
    }

    void loadNextIcon() {
        if (pending_.isEmpty()) return;
        // The shell call behind an icon can stall, so one icon per event-loop turn keeps the screen responsive.
        const QString id = pending_.begin().key();
        const QString path = pending_.take(id);
        const QIcon icon = QFileIconProvider().icon(QFileInfo(path));
        if (!icon.isNull()) {
            icons_.insert(id, icon);
            for (const auto &page: pages_)
                if (page) page->setIcon(id, icon);
        }
        if (!pending_.isEmpty()) QTimer::singleShot(0, this, [this] { loadNextIcon(); });
    }

    State state_ = State::Idle;
    QHash<QString, bool> detected_;
    QHash<QString, QIcon> icons_;
    QHash<QString, QString> pending_;
    QList<QPointer<SimpleRoutesPage>> pages_;
};

// Two pages may fetch the same list; one download at a time keeps them from writing one file together.
QMutex &downloadMutex() {
    static QMutex mutex;
    return mutex;
}
} // namespace

AppRoutesController::AppRoutesController(SimpleRoutesPage *page) : QObject(page), page_(page) {
    connect(page, &SimpleRoutesPage::ruleSetsNeeded, this, [this](const QStringList &names) { downloadRuleSets(names); });
    connect(page, &SimpleRoutesPage::retryDownload, this, [this] { downloadRuleSets(page_->neededRuleSets()); });
}

bool AppRoutesController::Preview() {
    return Configs::dataManager->settingsRepo->argv.contains(QStringLiteral("-ui-preview"));
}

void AppRoutesController::scanApps() { AppScanCache::instance()->attach(page_); }

void AppRoutesController::setTargets(const std::shared_ptr<Configs::Profile> &main) {
    SimpleRouteTarget mainTarget;
    QList<SimpleRouteTarget> others;
    if (main != nullptr && main->outbound != nullptr) {
        mainTarget = {Configs::proxyID, main->outbound->DisplayName(), latencyLabel(main), main->latency};
        if (const auto group = Configs::dataManager->groupsRepo->GetGroup(main->gid)) {
            for (const auto &profile: Configs::dataManager->profilesRepo->GetProfileBatch(group->Profiles())) {
                if (profile == nullptr || profile->id == main->id || profile->outbound == nullptr) continue;
                others.append({profile->id, profile->outbound->DisplayName(), latencyLabel(profile), profile->latency});
            }
        }
    }
    QSet<int> referenced;
    referenced.insert(page_->stagedDefault());
    for (const auto &route: page_->stagedRoutes()) referenced.insert(route.outbound);
    for (const auto &target: others) referenced.remove(target.outbound);
    for (const int id: referenced) {
        if (id < 0) continue;
        const auto target = Configs::dataManager->profilesRepo->GetProfile(id);
        if (target && target->outbound) others.append({id, target->outbound->DisplayName(), latencyLabel(target), target->latency});
    }
    page_->setTargets(mainTarget, others);
}

void AppRoutesController::downloadRuleSets(const QStringList &names, const std::function<void(bool)> &then) {
    const QStringList missing = Preview() ? QStringList() : Configs::RuleSetCache::Missing(names);
    if (missing.isEmpty()) {
        if (!busy_) page_->setDownload(0, 0, {});
        if (then) then(true);
        return;
    }
    if (busy_) {
        // One download at a time; the latest request runs once the current one ends.
        next_ += names;
        next_.removeDuplicates();
        if (then) {
            const auto previous = nextThen_;
            nextThen_ = [previous, then](bool ready) {
                if (previous) previous(ready);
                then(ready);
            };
        }
        return;
    }
    busy_ = true;
    page_->setDownload(0, int(missing.size()), {});
    const bool viaProxy = Engine::Lifecycle::ResumeTargetId() >= 0;
    const QPointer<AppRoutesController> guard(this);
    runOnNewThread([guard, missing, viaProxy, then] {
        QString error;
        int done = 0;
        {
            const QMutexLocker lock(&downloadMutex());
            for (const QString &name: missing) {
                if (Configs::RuleSetCache::Missing({name}).isEmpty()) {
                    ++done;
                    continue;
                }
                error = Configs::RuleSetCache::Download(name, viaProxy);
                if (!error.isEmpty()) break;
                ++done;
                if (!guard) return;
                QMetaObject::invokeMethod(guard.data(), [guard, done, total = int(missing.size())] {
                    if (guard) guard->page_->setDownload(done, total, {}); }, Qt::QueuedConnection);
            }
        }
        if (!guard) return;
        QMetaObject::invokeMethod(guard.data(), [guard, error, done, total = int(missing.size()), then] {
            if (!guard) return;
            guard->busy_ = false;
            guard->page_->setDownload(done, total, error);
            if (error.isEmpty())
                QTimer::singleShot(1500, guard.data(), [guard] {
                    if (guard && !guard->busy_) guard->page_->setDownload(0, 0, {});
                });
            if (then) then(error.isEmpty());
            if (guard && !guard->next_.isEmpty()) {
                const QStringList next = std::exchange(guard->next_, {});
                guard->downloadRuleSets(next, std::exchange(guard->nextThen_, {}));
            } }, Qt::QueuedConnection);
    });
}

void AppRoutesController::refreshStaleRuleSets() {
    if (Preview()) return;
    const QStringList stale = Configs::RuleSetCache::Stale(page_->neededRuleSets());
    if (stale.isEmpty()) return;
    const bool viaProxy = Engine::Lifecycle::ResumeTargetId() >= 0;
    runOnNewThread([stale, viaProxy] {
        const QMutexLocker lock(&downloadMutex());
        for (const QString &name: stale) Configs::RuleSetCache::Download(name, viaProxy);
    });
}
