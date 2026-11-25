#pragma once

#include <QHash>
#include <QIcon>
#include <QSet>
#include <QWidget>

#include "include/database/entities/AppRoutes.h"

class QAbstractButton;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QToolButton;
class QVBoxLayout;

struct SimpleRouteTarget {
    int outbound = 0;
    QString name;
    QString latencyText;
    int latency = 0;
    bool operator==(const SimpleRouteTarget &) const = default;
};

// Simple mode's routing screen: apps found on this computer, the rest of the catalog and the user's own entries.
// Edits stay staged until Apply, because every applied change restarts the connection.
class SimpleRoutesPage : public QWidget {
    Q_OBJECT

public:
    explicit SimpleRoutesPage(QWidget *parent);
    void load(const QList<Configs::AppRoutes::Route> &routes, int restOutbound, int foreignRules, bool editable, bool rulesEnabled = true, bool resetView = false);
    void setDraft(const QList<Configs::AppRoutes::Route> &routes, int restOutbound);
    void markApplied(const QList<Configs::AppRoutes::Route> &routes, int restOutbound);
    [[nodiscard]] const QList<Configs::AppRoutes::Route> &stagedRoutes() const { return routes_; }
    [[nodiscard]] int stagedDefault() const { return rest_; }
    void setTargets(const SimpleRouteTarget &main, const QList<SimpleRouteTarget> &others);
    void setDetected(const QHash<QString, bool> &runningById);
    void setIcon(const QString &id, const QIcon &icon);
    void setWholeComputer(bool wholeComputer);
    void setConnected(bool connected);
    // Inside another window's save: no back button and no Apply, each change goes out through stagedChanged at once.
    void setEmbedded(bool embedded);
    void setProfileName(const QString &name);
    // Off in previews and tests, so every picture and check sees the end state.
    void setAnimated(bool animated) { animated_ = animated; }
    // Lines from the application picker, added to the user's own entry.
    void addEntries(const QStringList &lines);
    [[nodiscard]] bool isDirty() const;
    // Every maintained list the staged entries use; the owner downloads them before Apply needs them.
    [[nodiscard]] QStringList neededRuleSets() const;
    // Progress of those downloads: nothing shown at total 0, an error replaces the progress.
    void setDownload(int done, int total, const QString &error);
    [[nodiscard]] QWidget *scrollArea() const;

signals:
    void back();
    void openAdvanced();
    void wholeComputerRequested();
    void pickApplication();
    void applyRequested(const QList<Configs::AppRoutes::Route> &routes, int restOutbound);
    void stagedChanged(const QList<Configs::AppRoutes::Route> &routes, int restOutbound);
    void ruleSetsNeeded(const QStringList &names);
    void retryDownload();

private:
    // What the list shows, row by row; only a change here rebuilds it, everything else is updated in place.
    struct Plan {
        bool editable = true;
        QString banner;
        QString addRow;
        struct Group {
            QString title;
            QString count;
            QStringList ids;
            bool operator==(const Group &) const = default;
        };
        QList<Group> groups;
        QString more;
        bool custom = false;
        bool restRow = false;
        int foreign = 0;
        bool nothing = false;
        bool operator==(const Plan &) const = default;
    };
    struct RowParts {
        QWidget *row = nullptr;
        QAbstractButton *header = nullptr;
        QAbstractButton *toggle = nullptr;
        class RouteReveal *reveal = nullptr;
        QLineEdit *add = nullptr;
        QString detailKey;
        bool closing = false;
    };
    struct Drafts {
        QHash<QString, QString> text;
        QString focused;
        int cursor = 0;
        int selection = 0;
    };

    void refresh(bool keepDrafts = true);
    [[nodiscard]] Plan plan() const;
    void rebuild(const Plan &next, const QSet<QString> &wasOpen);
    void clearContent();
    void updateRows();
    void updateHeader(const QString &id);
    void updateRestPill();
    [[nodiscard]] Drafts captureDrafts() const;
    void restoreDrafts(const Drafts &drafts);
    void releaseFocus();
    void handOver();
    void renderFooter();
    QWidget *makeSection(const QString &title, const QString &count);
    void makeRow(const QString &id, bool animateOpen);
    QWidget *makeDetail(const QString &id, QWidget *parent);
    void openDetail(const QString &id, bool animate);
    QWidget *makeRestRow();
    Configs::AppRoutes::Route *route(const QString &id);
    [[nodiscard]] const Configs::AppRoutes::Route *routeOf(const QString &id) const;
    const Configs::AppRoutes::CatalogEntry *entry(const QString &id) const;
    [[nodiscard]] int defaultOutbound() const;
    [[nodiscard]] QString targetName(int outbound) const;
    [[nodiscard]] QString detailKey(const QString &id) const;
    void setRouteEnabled(const QString &id, bool enabled);
    void setRest(int outbound);
    bool addValue(const QString &id, const QString &text);
    void removeValue(const QString &id, int kind, const QString &value);
    [[nodiscard]] bool isModified(const QString &id) const;
    [[nodiscard]] QString categoryName(const QString &category) const;
    void updateSticky();

    QList<Configs::AppRoutes::CatalogEntry> catalog_;
    QList<Configs::AppRoutes::Route> loaded_;
    QList<Configs::AppRoutes::Route> routes_;
    QList<Configs::AppRoutes::Route> handed_;
    int loadedRest_ = 0;
    int rest_ = 0;
    int handedRest_ = 0;
    int foreign_ = 0;
    bool editable_ = true;
    bool rulesEnabled_ = true;
    bool wholeComputer_ = true;
    bool connected_ = false;
    bool embedded_ = false;
    bool animated_ = false;
    QHash<QString, bool> detected_;
    QHash<QString, QIcon> icons_;
    SimpleRouteTarget main_;
    QList<SimpleRouteTarget> others_;
    QString open_;
    QString focusAdd_;
    bool showMore_ = false;
    QToolButton *backButton_;
    QLabel *title_;
    QLabel *profileName_;
    QLineEdit *search_;
    QScrollArea *scroll_;
    QWidget *content_;
    QWidget *currentCard_ = nullptr;
    QLabel *sentence_;
    QPushButton *apply_;
    QWidget *download_;
    QLabel *downloadText_;
    class QProgressBar *downloadBar_;
    QPushButton *downloadRetry_;
    QToolButton *restPill_ = nullptr;
    QStringList lastNeeded_;
    bool downloading_ = false;
    bool built_ = false;
    Plan plan_;
    QHash<QString, RowParts> rows_;
    quint64 renderGeneration_ = 0;
    struct Section {
        QWidget *widget;
        QString title;
        QString count;
    };
    QList<Section> sections_;
    class StickyHeader *sticky_ = nullptr;
    class ScrollEdgeFade *fade_ = nullptr;
};
