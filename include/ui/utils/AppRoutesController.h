#pragma once

#include <QObject>
#include <QStringList>

#include <functional>
#include <memory>

namespace Configs {
class Profile;
}
class SimpleRoutesPage;

// What a SimpleRoutesPage cannot know itself: the programs on this computer, the servers an app can go through
// and the site lists its entries name. It belongs to the page and dies with it.
class AppRoutesController : public QObject {
    Q_OBJECT

public:
    explicit AppRoutesController(SimpleRoutesPage *page);
    // Previews must not read the machine they run on, nor the network.
    [[nodiscard]] static bool Preview();
    // One scan per process: a second page gets the first one's result and icons at once.
    void scanApps();
    // The main server, the other servers of its group, and any server the staged entries still name.
    void setTargets(const std::shared_ptr<Configs::Profile> &main);
    void downloadRuleSets(const QStringList &names, const std::function<void(bool)> &then = {});
    // Quietly: a working list stays in place when its refresh fails.
    void refreshStaleRuleSets();
    [[nodiscard]] bool downloading() const { return busy_; }

private:
    SimpleRoutesPage *page_;
    bool busy_ = false;
    QStringList next_;
    std::function<void(bool)> nextThen_;
};
